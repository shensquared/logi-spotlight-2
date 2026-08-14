// Read-only dump of one device's identity, battery and 0x1B04 control table.
// Finds the Spotlight by name, so no location or device index has to be passed.
// Every request here is a getter; nothing changes device state.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define VID_LOGITECH 0x046D
#define TIMEOUT 1.2

static IOHIDDeviceRef gDev = NULL;
static uint8_t gResp[64];
static int gHave = 0;
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

// Feature index for a feature ID, through the root feature. 0 when absent.
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

// Capability flags from 0x1B04 getCidInfo. Bits above 0x40 are left to the
// caller to print raw, since no press seen here has exercised them.
static void printFlags(uint8_t f) {
    printf("%s%s%s%s%s%s",
           f & 0x01 ? "mouse " : "", f & 0x02 ? "fkey " : "",
           f & 0x04 ? "hotkey " : "", f & 0x10 ? "reprog " : "",
           f & 0x20 ? "divertable " : "", f & 0x40 ? "persist " : "");
}

int main(int argc, char **argv) {
    const char *match = argc > 1 ? argv[1] : "Spotlight";

    IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    IOHIDManagerSetDeviceMatching(mgr, NULL);
    IOHIDManagerOpen(mgr, kIOHIDOptionsTypeNone);
    CFSetRef set = IOHIDManagerCopyDevices(mgr);
    if (!set) { printf("no HID devices\n"); return 1; }
    CFIndex n = CFSetGetCount(set);
    IOHIDDeviceRef *devs = malloc(sizeof(IOHIDDeviceRef) * n);
    CFSetGetValues(set, (const void **)devs);

    uint8_t devIdx = 0;
    char name[64] = {0};
    for (CFIndex i = 0; i < n && !devIdx; i++) {
        if (propInt(devs[i], CFSTR(kIOHIDVendorIDKey)) != VID_LOGITECH) continue;
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
            if (strstr(name, match)) { devIdx = (uint8_t)d; break; }
        }
        if (devIdx) {
            printf("\"%s\" at location 0x%08x, device index 0x%02x\n\n",
                   name, propInt(devs[i], CFSTR(kIOHIDLocationIDKey)), devIdx);
            break;
        }
        IOHIDDeviceUnscheduleFromRunLoop(gDev, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        IOHIDDeviceClose(gDev, kIOHIDOptionsTypeNone);
    }
    if (!devIdx) { printf("no device matching \"%s\" is awake\n", match); return 1; }

    uint8_t f = findFeat(devIdx, 0x0003);
    if (f && hidpp(devIdx, f, 0x00, 0, 0, 0) == 1)
        printf("0x0003  unitId %02x%02x%02x%02x  modelId %02x%02x  transport %02x%02x\n",
               gResp[5], gResp[6], gResp[7], gResp[8], gResp[11], gResp[12],
               gResp[9], gResp[10]);

    f = findFeat(devIdx, 0x1004);
    if (f && hidpp(devIdx, f, 0x01, 0, 0, 0) == 1)
        printf("0x1004  battery %d%%, level 0x%02x, charging 0x%02x\n",
               gResp[4], gResp[5], gResp[6]);

    f = findFeat(devIdx, 0x1815);
    if (f && hidpp(devIdx, f, 0x00, 0, 0, 0) == 1)
        printf("0x1815  %d host slots, currently on host %d\n", gResp[5], gResp[6]);

    f = findFeat(devIdx, 0x1b04);
    if (!f || hidpp(devIdx, f, 0x00, 0, 0, 0) != 1) return 0;
    int count = gResp[4];
    printf("\n0x1b04 at index 0x%02x, %d controls\n\n", f, count);
    printf("  i   CID     TID     flags xflag divert  capability\n");
    printf("  --  ------  ------  ----- ----- ------  ----------\n");
    for (int i = 0; i < count; i++) {
        if (hidpp(devIdx, f, 0x01, (uint8_t)i, 0, 0) != 1) continue;
        uint16_t cid = (uint16_t)((gResp[4] << 8) | gResp[5]);
        uint16_t tid = (uint16_t)((gResp[6] << 8) | gResp[7]);
        uint8_t flags = gResp[8], xflags = gResp[12];
        // Live divert state comes from function 2, not from the capability byte.
        const char *divert = "?";
        if (hidpp(devIdx, f, 0x02, (uint8_t)(cid >> 8), (uint8_t)cid, 0) == 1)
            divert = gResp[6] & 0x01 ? "ON " : "off";
        printf("  %2d  0x%04x  0x%04x  0x%02x  0x%02x  %s     ", i, cid, tid,
               flags, xflags, divert);
        printFlags(flags);
        if (xflags & 0x01) printf("rawXY ");
        if (xflags & 0x02) printf("forceRawXY ");
        printf("\n");
    }
    return 0;
}
