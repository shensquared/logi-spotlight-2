// Diverts the Spotlight's 0x1B04 controls so presses arrive as HID++
// notifications, listens, then puts every control back the way it was found.
//
// This is the one tool here that writes. It touches 0x1B04 function 3 only,
// whose divert bit is volatile: a power cycle clears it even if this exits
// badly. See CLAUDE.md for the features that are never called.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>

#define VID_LOGITECH 0x046D
#define TIMEOUT 1.2
#define MAX_CIDS 32

// Prompts printed during the listen, so a run is self-contained rather than
// needing a script kept somewhere else. Edit these as the open questions in
// docs/PROTOCOL.md change. Right now they chase the Highlight button, the only
// one whose CIDs are unresolved, and whether raw XY streams motion.
static const struct { int at; const char *say; } kScript[] = {
    {  0, "HIGHLIGHT: gentle press, hold 2 s, three times" },
    { 15, "hands off" },
    { 20, "HIGHLIGHT: firm press, hold 2 s, three times" },
    { 35, "hands off" },
    { 40, "hold HIGHLIGHT gently and wave the remote around" },
    { 55, "hands off until it exits" },
};

static IOHIDDeviceRef gDev = NULL;
static uint8_t gResp[64];
static int gHave = 0;
static uint8_t gWantIdx, gWantFeat, gWantFn;

static uint8_t gDevIdx = 0, gFeat1b04 = 0;
static uint16_t gCid[MAX_CIDS];
static uint8_t gWasDiverted[MAX_CIDS];
static uint8_t gRawCapable[MAX_CIDS];
// Discovered raw-XY encoding, zero until a candidate is confirmed by read-back.
static uint8_t gRawOn = 0, gRawOff = 0;
static int gCidCount = 0;
static volatile sig_atomic_t gStop = 0;
static CFAbsoluteTime gListenStart = 0;

static void onSig(int s) { (void)s; gStop = 1; CFRunLoopStop(CFRunLoopGetCurrent()); }

static void inputCB(void *ctx, IOReturn res, void *sender, IOHIDReportType type,
                    uint32_t reportID, uint8_t *report, CFIndex len) {
    (void)ctx; (void)res; (void)sender; (void)type; (void)reportID;
    if (len < 6 || report[1] != gWantIdx) return;

    // Byte 3 carries the function-and-software-id the host sent, so a frame
    // with 0x00 there is a device notification and anything else is our own
    // ack echoing back. Reading an ack as a press invents CIDs from the
    // request bytes.
    if (report[2] == gFeat1b04 && report[3] == 0x00) {
        uint16_t cid = (uint16_t)((report[4] << 8) | report[5]);
        printf("  %6.2fs  %s 0x%04x  |", CFAbsoluteTimeGetCurrent() - gListenStart,
               cid ? "cid    " : "release", cid);
        for (CFIndex i = 0; i < 10 && i < len; i++) printf(" %02x", report[i]);
        printf("\n");
        fflush(stdout);
        return;
    }

    int err = (report[2] == 0x8F || report[2] == 0xFF) &&
              report[3] == gWantFeat && report[4] == gWantFn;
    int ok = report[2] == gWantFeat && report[3] == gWantFn;
    if (!err && !ok) return;
    memset(gResp, 0, sizeof gResp);
    memcpy(gResp, report, len > (CFIndex)sizeof gResp ? (CFIndex)sizeof gResp : len);
    gHave = err ? -1 : 1;
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

static int32_t propInt(IOHIDDeviceRef d, CFStringRef key) {
    CFTypeRef v = IOHIDDeviceGetProperty(d, key);
    int32_t out = -1;
    if (v && CFGetTypeID(v) == CFNumberGetTypeID())
        CFNumberGetValue((CFNumberRef)v, kCFNumberSInt32Type, &out);
    return out;
}

// Flags byte of setCidReporting: bit 0 is the divert value, bit 1 marks it
// valid. 0x03 diverts, 0x02 hands the control back.
static void setDivert(uint16_t cid, int on) {
    hidpp(gDevIdx, gFeat1b04, 0x03, (uint8_t)(cid >> 8), (uint8_t)cid,
          on ? 0x03 : 0x02);
}

static void restoreAll(void) {
    printf("\nrestoring %d controls\n", gCidCount);
    for (int i = 0; i < gCidCount; i++) {
        if (gRawOff && gRawCapable[i])
            hidpp(gDevIdx, gFeat1b04, 0x03, (uint8_t)(gCid[i] >> 8),
                  (uint8_t)gCid[i], gRawOff);
        setDivert(gCid[i], gWasDiverted[i]);
    }
}

// The divert bit is bit 0 with bit 1 as its validity flag. Raw XY sits
// elsewhere in the same byte and the position is not established here, so try
// each candidate and let the read-back say which one the device took.
static void findRawXYEncoding(uint16_t cid) {
    static const struct { uint8_t on, off; const char *where; } kCand[] = {
        { 0x33, 0x23, "value bit 4, validity bit 5" },
        { 0xC3, 0x83, "value bit 6, validity bit 7" },
    };
    uint8_t hi = (uint8_t)(cid >> 8), lo = (uint8_t)cid;
    if (hidpp(gDevIdx, gFeat1b04, 0x02, hi, lo, 0) != 1) return;
    uint8_t base = gResp[6];
    printf("\nprobing raw XY on CID 0x%04x, divert-only reads 0x%02x\n", cid, base);

    for (size_t c = 0; c < sizeof kCand / sizeof kCand[0]; c++) {
        hidpp(gDevIdx, gFeat1b04, 0x03, hi, lo, kCand[c].on);
        if (hidpp(gDevIdx, gFeat1b04, 0x02, hi, lo, 0) != 1) continue;
        printf("  flags 0x%02x (%s) reads 0x%02x%s\n", kCand[c].on, kCand[c].where,
               gResp[6], gResp[6] != base ? "   <- took" : "");
        if (gResp[6] != base) {
            gRawOn = kCand[c].on;
            gRawOff = kCand[c].off;
            return;
        }
        hidpp(gDevIdx, gFeat1b04, 0x03, hi, lo, kCand[c].off);
    }
    printf("  no candidate changed the read-back\n");
}

int main(int argc, char **argv) {
    int secs = argc > 1 ? atoi(argv[1]) : 45;
    int rawxy = argc > 2 && strcmp(argv[2], "rawxy") == 0;
    signal(SIGINT, onSig);

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
    if (!set) { printf("no Logitech HID device\n"); return 1; }
    CFIndex n = CFSetGetCount(set);
    IOHIDDeviceRef *devs = malloc(sizeof(IOHIDDeviceRef) * n);
    CFSetGetValues(set, (const void **)devs);

    // The remote drops off its receiver when idle and comes back on a press,
    // so a single scan finds nothing more often than not. Keep scanning.
    char name[64] = {0};
    for (int attempt = 0; attempt < 15 && !gDevIdx && !gStop; attempt++) {
        if (attempt == 1) printf("asleep, press any button on the remote to wake it\n");
        for (CFIndex i = 0; i < n && !gDevIdx; i++) {
            int32_t page = propInt(devs[i], CFSTR(kIOHIDPrimaryUsagePageKey));
            if (page != 0xFF00 && page != 0xFF43) continue;
            gDev = devs[i];
            IOHIDDeviceOpen(gDev, kIOHIDOptionsTypeNone);
            static uint8_t rbuf[64];
            IOHIDDeviceRegisterInputReportCallback(gDev, rbuf, sizeof rbuf, inputCB, NULL);
            IOHIDDeviceScheduleWithRunLoop(gDev, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
            int lo = page == 0xFF43 ? 0xFF : 1, hi = page == 0xFF43 ? 0xFF : 6;
            for (int d = lo; d <= hi; d++) {
                if (hidpp((uint8_t)d, 0x00, 0x00, 0x00, 0x01, 0x00) != 1) continue;
                readName((uint8_t)d, name, sizeof name);
                if (strstr(name, "Spotlight")) { gDevIdx = (uint8_t)d; break; }
            }
            if (!gDevIdx) {
                IOHIDDeviceUnscheduleFromRunLoop(gDev, CFRunLoopGetCurrent(),
                                                 kCFRunLoopDefaultMode);
                IOHIDDeviceClose(gDev, kIOHIDOptionsTypeNone);
            }
        }
        if (!gDevIdx) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 2.0, false);
    }
    if (!gDevIdx) { printf("Spotlight never woke\n"); return 1; }
    printf("\"%s\" at device index 0x%02x\n", name, gDevIdx);

    gFeat1b04 = findFeat(gDevIdx, 0x1b04);
    if (!gFeat1b04 || hidpp(gDevIdx, gFeat1b04, 0x00, 0, 0, 0) != 1) {
        printf("no 0x1b04\n");
        return 1;
    }
    int count = gResp[4];
    if (count > MAX_CIDS) count = MAX_CIDS;

    // Record the state each control is in before touching it, so the restore
    // puts it back rather than blanket-clearing.
    for (int i = 0; i < count; i++) {
        if (hidpp(gDevIdx, gFeat1b04, 0x01, (uint8_t)i, 0, 0) != 1) continue;
        uint16_t cid = (uint16_t)((gResp[4] << 8) | gResp[5]);
        uint8_t raw = gResp[12] & 0x01;   // read before function 2 overwrites it
        uint8_t was = 0;
        if (hidpp(gDevIdx, gFeat1b04, 0x02, (uint8_t)(cid >> 8), (uint8_t)cid, 0) == 1)
            was = gResp[6] & 0x01;
        gCid[gCidCount] = cid;
        gWasDiverted[gCidCount] = was;
        gRawCapable[gCidCount] = raw;
        gCidCount++;
    }

    printf("diverting %d controls\n", gCidCount);
    for (int i = 0; i < gCidCount; i++) setDivert(gCid[i], 1);

    printf("\nread back after divert\n  CID     flags\n  ------  -----\n");
    for (int i = 0; i < gCidCount; i++) {
        if (hidpp(gDevIdx, gFeat1b04, 0x02, (uint8_t)(gCid[i] >> 8),
                  (uint8_t)gCid[i], 0) == 1)
            printf("  0x%04x  0x%02x\n", gCid[i], gResp[6]);
    }

    if (rawxy) {
        int first = -1;
        for (int i = 0; i < gCidCount && first < 0; i++)
            if (gRawCapable[i]) first = i;
        if (first < 0) printf("\nno control advertises raw XY\n");
        else {
            findRawXYEncoding(gCid[first]);
            if (gRawOn) {
                printf("\nsetting raw XY on every capable control\n");
                for (int i = 0; i < gCidCount; i++) {
                    if (!gRawCapable[i]) continue;
                    hidpp(gDevIdx, gFeat1b04, 0x03, (uint8_t)(gCid[i] >> 8),
                          (uint8_t)gCid[i], gRawOn);
                    if (hidpp(gDevIdx, gFeat1b04, 0x02, (uint8_t)(gCid[i] >> 8),
                              (uint8_t)gCid[i], 0) == 1)
                        printf("  0x%04x reads 0x%02x\n", gCid[i], gResp[6]);
                }
            }
        }
    }

    printf("\nlistening %d s. Press each button in turn.\n"
           "Only frames with 0x00 in byte 3 are presses; acks are filtered.\n\n", secs);
    gListenStart = CFAbsoluteTimeGetCurrent();
    for (int t = 0; t < secs && !gStop; t++) {
        for (size_t i = 0; i < sizeof kScript / sizeof kScript[0]; i++)
            if (kScript[i].at == t)
                printf("\n>>> %2ds  %s\n\n", t, kScript[i].say);
        if (t % 5 == 0) { printf("    [%2ds]\n", t); }
        fflush(stdout);
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, false);
    }

    restoreAll();
    IOHIDDeviceClose(gDev, kIOHIDOptionsTypeNone);
    return 0;
}
