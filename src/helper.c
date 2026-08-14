// Long-lived bridge for the Spotlight 2. Diverts the 0x1B04 controls so button
// presses reach a host, re-diverts whenever the remote wakes, and streams the
// presses to a unix socket client. Restores the controls it changed on exit.
//
// Events go to stdout and to a connected socket client:
//   ready
//   down <cid>    e.g. down 00d8
//   up
//   ping          every 10 s, socket client only
//
// The socket is /tmp/logi-spotlight.sock or $LOGI_SPOTLIGHT_SOCKET. Hammerspoon
// has to use the socket rather than stdin, because hs.task:setInput only
// delivers what was queued before start().
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

#define VID_LOGITECH 0x046D
#define TIMEOUT 1.2
#define MAX_CIDS 32
#define REDIVERT_BACKSTOP 30.0   // seconds

static IOHIDDeviceRef gDev = NULL;
static uint8_t gResp[64];
static int gHave = 0;
static uint8_t gWantIdx, gWantFeat, gWantFn;

static uint8_t gDevIdx = 0, gFeat1b04 = 0;
static uint16_t gCid[MAX_CIDS];
static uint8_t gWasDiverted[MAX_CIDS];
static int gCidCount = 0;

static int gClient = -1;
static volatile sig_atomic_t gStop = 0;
static int gNeedRedivert = 0;
static uint16_t gHeld = 0;
static CFAbsoluteTime gLastDivert = 0;

static void onSig(int s) { (void)s; gStop = 1; CFRunLoopStop(CFRunLoopGetCurrent()); }

// Events go to stdout and to the socket client, so a shell session and a
// Hammerspoon client both work.
static void emit(const char *fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n > (int)sizeof line - 1) n = (int)sizeof line - 1;
    fputs(line, stdout);
    fflush(stdout);
    if (gClient >= 0 && write(gClient, line, (size_t)n) < 0 && errno != EAGAIN) {
        close(gClient);
        gClient = -1;
    }
}

static int32_t propInt(IOHIDDeviceRef d, CFStringRef key) {
    CFTypeRef v = IOHIDDeviceGetProperty(d, key);
    int32_t out = -1;
    if (v && CFGetTypeID(v) == CFNumberGetTypeID())
        CFNumberGetValue((CFNumberRef)v, kCFNumberSInt32Type, &out);
    return out;
}

static void inputCB(void *ctx, IOReturn res, void *sender, IOHIDReportType type,
                    uint32_t reportID, uint8_t *report, CFIndex len) {
    (void)ctx; (void)res; (void)sender; (void)type; (void)reportID;
    if (len < 6) return;

    // Subid 0x41 is the receiver announcing a device is back. The divert bits
    // are volatile and the remote sleeps within seconds, so every wake needs
    // them set again. Re-diverting here would nest run loops inside a callback,
    // so the timer does it.
    if (report[2] == 0x41 && report[1] == gDevIdx) {
        gNeedRedivert = 1;
        return;
    }

    if (report[1] != gDevIdx) return;

    // Byte 3 carries the function-and-software-id the host sent, so only 0x00
    // is a device notification. Anything else is an ack of our own write, and
    // reading one as a press invents a CID out of the request bytes.
    if (report[2] == gFeat1b04 && report[3] == 0x00) {
        uint16_t cid = (uint16_t)((report[4] << 8) | report[5]);
        if (cid && cid != gHeld) emit("down %04x\n", cid);
        else if (!cid && gHeld) emit("up\n");
        gHeld = cid;
        return;
    }

    if (report[2] != gWantFeat || report[3] != gWantFn) {
        // An error reply tags byte 2 and pushes the request one byte right.
        if (!((report[2] == 0x8F || report[2] == 0xFF) &&
              report[3] == gWantFeat && report[4] == gWantFn))
            return;
        gHave = -1;
    } else {
        gHave = 1;
    }
    memset(gResp, 0, sizeof gResp);
    memcpy(gResp, report, len > (CFIndex)sizeof gResp ? (CFIndex)sizeof gResp : len);
    CFRunLoopStop(CFRunLoopGetCurrent());
}

static int hidpp(uint8_t devIdx, uint8_t featIdx, uint8_t func,
                 uint8_t p0, uint8_t p1, uint8_t p2) {
    uint8_t buf[20];
    memset(buf, 0, sizeof buf);
    buf[0] = 0x11; buf[1] = devIdx; buf[2] = featIdx;
    buf[3] = (uint8_t)((func << 4) | 0x01);
    buf[4] = p0; buf[5] = p1; buf[6] = p2;
    gWantIdx = devIdx; gWantFeat = featIdx; gWantFn = buf[3];
    gHave = 0;
    if (!gDev) return 0;
    if (IOHIDDeviceSetReport(gDev, kIOHIDReportTypeOutput, 0x11, buf, sizeof buf)
        != kIOReturnSuccess)
        return 0;
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, TIMEOUT, false);
    return gHave;
}

static uint8_t findFeat(uint8_t devIdx, uint16_t fid) {
    if (hidpp(devIdx, 0x00, 0x00, (uint8_t)(fid >> 8), (uint8_t)fid, 0x00) != 1) return 0;
    return gResp[4];
}

static void readName(uint8_t devIdx, char *out, size_t n) {
    out[0] = 0;
    uint8_t f = findFeat(devIdx, 0x0005);
    if (!f || hidpp(devIdx, f, 0x00, 0, 0, 0) != 1) return;
    size_t total = gResp[4];
    if (total > n - 1) total = n - 1;
    for (size_t off = 0; off < total; off += 15) {
        if (hidpp(devIdx, f, 0x01, (uint8_t)off, 0, 0) != 1) break;
        for (size_t k = 0; k < 15 && off + k < total; k++) out[off + k] = (char)gResp[4 + k];
    }
    out[total] = 0;
}

// Bit 0 is the divert value and bit 1 marks it valid, so 0x03 diverts and
// 0x02 hands the control back.
static void setDivert(uint16_t cid, int on) {
    hidpp(gDevIdx, gFeat1b04, 0x03, (uint8_t)(cid >> 8), (uint8_t)cid,
          on ? 0x03 : 0x02);
}

static void divertAll(void) {
    for (int i = 0; i < gCidCount; i++) setDivert(gCid[i], 1);
    gLastDivert = CFAbsoluteTimeGetCurrent();
    gNeedRedivert = 0;
}

static void restoreAll(void) {
    for (int i = 0; i < gCidCount; i++) setDivert(gCid[i], gWasDiverted[i]);
}

// Reads the control table and records the state each control is in, so the
// restore puts back what was found rather than blanket-clearing. CID 0x0050
// already carries a divert bit on an untouched device.
static int readControls(void) {
    gFeat1b04 = findFeat(gDevIdx, 0x1b04);
    if (!gFeat1b04 || hidpp(gDevIdx, gFeat1b04, 0x00, 0, 0, 0) != 1) return 0;
    int count = gResp[4];
    if (count > MAX_CIDS) count = MAX_CIDS;
    gCidCount = 0;
    for (int i = 0; i < count; i++) {
        if (hidpp(gDevIdx, gFeat1b04, 0x01, (uint8_t)i, 0, 0) != 1) continue;
        uint16_t cid = (uint16_t)((gResp[4] << 8) | gResp[5]);
        uint8_t was = 0;
        if (hidpp(gDevIdx, gFeat1b04, 0x02, (uint8_t)(cid >> 8), (uint8_t)cid, 0) == 1)
            was = gResp[6] & 0x01;
        gCid[gCidCount] = cid;
        gWasDiverted[gCidCount] = was;
        gCidCount++;
    }
    return gCidCount;
}

// Finds the remote on any Logitech vendor collection. The device index is a
// slot on one receiver, not a property of the remote, so it is discovered.
static int discover(IOHIDDeviceRef *devs, CFIndex n) {
    static uint8_t rbuf[64];
    char name[64];
    for (CFIndex i = 0; i < n; i++) {
        int32_t page = propInt(devs[i], CFSTR(kIOHIDPrimaryUsagePageKey));
        if (page != 0xFF00 && page != 0xFF43) continue;
        gDev = devs[i];
        IOHIDDeviceOpen(gDev, kIOHIDOptionsTypeNone);
        IOHIDDeviceRegisterInputReportCallback(gDev, rbuf, sizeof rbuf, inputCB, NULL);
        IOHIDDeviceScheduleWithRunLoop(gDev, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        int lo = page == 0xFF43 ? 0xFF : 1, hi = page == 0xFF43 ? 0xFF : 6;
        for (int d = lo; d <= hi; d++) {
            gDevIdx = (uint8_t)d;
            if (hidpp((uint8_t)d, 0x00, 0x00, 0x00, 0x01, 0x00) != 1) continue;
            readName((uint8_t)d, name, sizeof name);
            if (strstr(name, "Spotlight")) return 1;
        }
        gDevIdx = 0;
        IOHIDDeviceUnscheduleFromRunLoop(gDev, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        IOHIDDeviceClose(gDev, kIOHIDOptionsTypeNone);
        gDev = NULL;
    }
    return 0;
}

static void acceptCB(CFSocketRef s, CFSocketCallBackType type, CFDataRef addr,
                     const void *data, void *info) {
    (void)s; (void)addr; (void)info;
    if (type != kCFSocketAcceptCallBack) return;
    CFSocketNativeHandle fd = *(const CFSocketNativeHandle *)data;
    // One client at a time. A Hammerspoon reload reconnects without the old
    // connection needing to have closed cleanly.
    if (gClient >= 0) close(gClient);
    gClient = fd;
    fcntl(gClient, F_SETFL, O_NONBLOCK);
    emit("ready\n");
}

static void timerCB(CFRunLoopTimerRef t, void *info) {
    (void)t; (void)info;
    static int ticks = 0;
    ticks++;

    if (gNeedRedivert ||
        CFAbsoluteTimeGetCurrent() - gLastDivert > REDIVERT_BACKSTOP)
        divertAll();

    // The heartbeat is how a client tells a dead socket from an idle remote.
    if (ticks % 10 == 0) emit("ping\n");
}

int main(void) {
    signal(SIGINT, onSig);
    signal(SIGTERM, onSig);
    signal(SIGPIPE, SIG_IGN);

    IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    int32_t matchVid = VID_LOGITECH;
    CFNumberRef num = CFNumberCreate(NULL, kCFNumberSInt32Type, &matchVid);
    const void *k[] = { CFSTR(kIOHIDVendorIDKey) };
    const void *v[] = { num };
    CFDictionaryRef match = CFDictionaryCreate(NULL, k, v, 1,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
    IOHIDManagerSetDeviceMatching(mgr, match);
    CFRelease(match); CFRelease(num);
    IOHIDManagerOpen(mgr, kIOHIDOptionsTypeNone);
    CFSetRef set = IOHIDManagerCopyDevices(mgr);
    if (!set) { fprintf(stderr, "no Logitech HID device\n"); return 1; }
    CFIndex n = CFSetGetCount(set);
    IOHIDDeviceRef *devs = malloc(sizeof(IOHIDDeviceRef) * n);
    CFSetGetValues(set, (const void **)devs);

    for (int attempt = 0; attempt < 15 && !gDevIdx && !gStop; attempt++) {
        if (discover(devs, n)) break;
        if (attempt == 0)
            fprintf(stderr, "asleep, press any button on the remote to wake it\n");
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 2.0, false);
    }
    if (!gDevIdx) { fprintf(stderr, "Spotlight never woke\n"); return 1; }
    if (!readControls()) { fprintf(stderr, "no 0x1b04 control table\n"); return 1; }
    fprintf(stderr, "Spotlight 2 at device index 0x%02x, %d controls\n",
            gDevIdx, gCidCount);
    divertAll();

    const char *path = getenv("LOGI_SPOTLIGHT_SOCKET");
    if (!path) path = "/tmp/logi-spotlight.sock";
    unlink(path);
    int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un sa;
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, path, sizeof sa.sun_path - 1);
    if (bind(lfd, (struct sockaddr *)&sa, sizeof sa) < 0 || listen(lfd, 4) < 0) {
        fprintf(stderr, "socket %s failed: %s\n", path, strerror(errno));
        restoreAll();
        return 1;
    }
    CFSocketRef ls = CFSocketCreateWithNative(NULL, lfd, kCFSocketAcceptCallBack,
                                              acceptCB, NULL);
    CFRunLoopSourceRef src = CFSocketCreateRunLoopSource(NULL, ls, 0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), src, kCFRunLoopDefaultMode);
    fprintf(stderr, "listening on %s\n", path);

    CFRunLoopTimerRef timer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 1.0,
                                                   1.0, 0, 0, timerCB, NULL);
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), timer, kCFRunLoopDefaultMode);

    emit("ready\n");
    while (!gStop) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, false);

    fprintf(stderr, "\nrestoring %d controls\n", gCidCount);
    restoreAll();
    unlink(path);
    return 0;
}
