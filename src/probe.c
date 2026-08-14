// Calls function 0 on the features whose purpose is not yet known, and prints
// the reply. Function 0 is getCapabilities or getInfo by HID++ 2.0 convention.
//
// The list is explicit rather than "everything not recognised", so that the
// features named in CLAUDE.md as never-call cannot be reached by accident.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define VID_LOGITECH 0x046D
#define TIMEOUT 1.2

static const struct { uint16_t id; const char *note; } kProbe[] = {
    { 0x0011, "" },
    { 0x0020, "ConfigChange, cookie a host writes at setup" },
    { 0x0021, "unique identifier" },
    { 0x00c3, "answers function 0 only on the MX Keypad" },
    { 0x1701, "" },
    { 0x19b0, "" },
    { 0x19c0, "" },
    { 0x1a01, "" },
    { 0x1d4b, "wireless device status" },
    { 0x2250, "" },
};

static IOHIDDeviceRef gDev = NULL;
static uint8_t gResp[64];
static int gHave = 0;
static uint8_t gErrCode = 0;
static uint8_t gWantIdx, gWantFeat, gWantFn;

static void inputCB(void *ctx, IOReturn res, void *sender, IOHIDReportType type,
                    uint32_t reportID, uint8_t *report, CFIndex len) {
    (void)ctx; (void)res; (void)sender; (void)type; (void)reportID;
    if (len < 6 || report[1] != gWantIdx) return;
    int err = (report[2] == 0x8F || report[2] == 0xFF) &&
              report[3] == gWantFeat && report[4] == gWantFn;
    int ok = report[2] == gWantFeat && report[3] == gWantFn;
    if (!err && !ok) return;
    memset(gResp, 0, sizeof gResp);
    memcpy(gResp, report, len > (CFIndex)sizeof gResp ? (CFIndex)sizeof gResp : len);
    gErrCode = err ? report[5] : 0;
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
    gHave = 0; gErrCode = 0;
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

int main(void) {
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

    uint8_t devIdx = 0;
    char name[64] = {0};
    for (CFIndex i = 0; i < n && !devIdx; i++) {
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
            if (strstr(name, "Spotlight")) { devIdx = (uint8_t)d; break; }
        }
        if (!devIdx) {
            IOHIDDeviceUnscheduleFromRunLoop(gDev, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
            IOHIDDeviceClose(gDev, kIOHIDOptionsTypeNone);
        }
    }
    if (!devIdx) { printf("Spotlight not found or asleep\n"); return 1; }
    printf("\"%s\" at device index 0x%02x\n\n", name, devIdx);

    for (size_t i = 0; i < sizeof kProbe / sizeof kProbe[0]; i++) {
        uint16_t fid = kProbe[i].id;
        uint8_t f = findFeat(devIdx, fid);
        if (!f) { printf("  0x%04x  absent\n", fid); continue; }
        int r = hidpp(devIdx, f, 0x00, 0, 0, 0);
        printf("  0x%04x  idx 0x%02x  ", fid, f);
        if (r == 1) {
            printf("|");
            for (int b = 4; b < 20; b++) printf(" %02x", gResp[b]);
        } else if (r == -1) {
            printf("error 0x%02x", gErrCode);
        } else {
            printf("no answer");
        }
        if (kProbe[i].note[0]) printf("   %s", kProbe[i].note);
        printf("\n");
    }
    return 0;
}
