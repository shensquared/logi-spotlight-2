// Read-only walk of every Logitech HID++ endpoint on this Mac.
// Receiver handles (vendor page 0xFF00) are polled at device indices 1-6;
// a directly connected device (vendor page 0xFF43) is polled at 0xFF.
// Live endpoints get their name and feature table printed.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define VID_LOGITECH 0x046D

// A wireless round trip through a Bolt receiver takes well over the 0.35 s the
// direct-attach probes in the sibling MX repo allow, and a device that is awake
// but slow reads as absent at that timeout.
#define TIMEOUT 1.2

static IOHIDDeviceRef gDev = NULL;
static uint8_t gResp[64];
static int gHave = 0;
static uint8_t gErr = 0;
static uint8_t gWantIdx, gWantFeat, gWantFn;

static void inputCB(void *ctx, IOReturn res, void *sender, IOHIDReportType type,
                    uint32_t reportID, uint8_t *report, CFIndex len) {
    (void)ctx; (void)res; (void)sender; (void)type; (void)reportID;
    if (len < 6 || report[1] != gWantIdx) return;
    // A reply carries the feature index and the function-and-software-id byte
    // it was asked with. An error reply pushes both one byte right behind an
    // 0x8F (HID++ 1.0) or 0xFF (2.0) tag. Anything else is unrelated traffic.
    int err = (report[2] == 0x8F || report[2] == 0xFF) &&
              report[3] == gWantFeat && report[4] == gWantFn;
    int ok = report[2] == gWantFeat && report[3] == gWantFn;
    if (!err && !ok) return;
    memset(gResp, 0, sizeof gResp);
    memcpy(gResp, report, len > (CFIndex)sizeof gResp ? (CFIndex)sizeof gResp : len);
    gErr = err ? report[5] : 0;
    gHave = err ? -1 : 1;
    CFRunLoopStop(CFRunLoopGetCurrent());
}

// One HID++ 2.0 request. 1 on an answer, -1 on an error reply, 0 on silence.
static int hidpp(uint8_t devIdx, uint8_t featIdx, uint8_t func,
                 uint8_t p0, uint8_t p1, uint8_t p2) {
    uint8_t buf[20];
    memset(buf, 0, sizeof buf);
    buf[0] = 0x11; buf[1] = devIdx; buf[2] = featIdx;
    buf[3] = (uint8_t)((func << 4) | 0x01);
    buf[4] = p0; buf[5] = p1; buf[6] = p2;
    gWantIdx = devIdx; gWantFeat = featIdx; gWantFn = buf[3];
    gHave = 0; gErr = 0;
    if (IOHIDDeviceSetReport(gDev, kIOHIDReportTypeOutput, 0x11, buf, sizeof buf)
        != kIOReturnSuccess)
        return 0;
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, TIMEOUT, false);
    return gHave;
}

static const char *featName(uint16_t id) {
    switch (id) {
    case 0x0000: return "Root";
    case 0x0001: return "IFeatureSet";
    case 0x0003: return "DeviceInformation";
    case 0x0005: return "DeviceNameAndType";
    case 0x0007: return "DeviceFriendlyName";
    case 0x0008: return "KeepAlive";
    case 0x1000: return "BatteryLevelStatus";
    case 0x1004: return "UnifiedBattery";
    case 0x1802: return "DeviceReset";
    case 0x1805: return "OOBState";
    case 0x1814: return "ChangeHost";
    case 0x1815: return "HostsInfo";
    case 0x1816: return "BLEProPrepairing";
    case 0x1b04: return "ReprogControlsV4";
    case 0x1e00: return "EnableHiddenFeatures";
    case 0x2201: return "AdjustableDPI";
    case 0x2205: return "PointerMotionScaling";
    case 0x4610: return "MultiRoller";
    case 0x6501: return "Gesture";
    default: return "";
    }
}

static int32_t propInt(IOHIDDeviceRef d, CFStringRef key) {
    CFTypeRef v = IOHIDDeviceGetProperty(d, key);
    int32_t out = -1;
    if (v && CFGetTypeID(v) == CFNumberGetTypeID())
        CFNumberGetValue((CFNumberRef)v, kCFNumberSInt32Type, &out);
    return out;
}

// Name and device type through 0x0005. The name arrives in 15-byte chunks.
static void readIdentity(uint8_t devIdx, char *name, size_t n, uint8_t *type) {
    name[0] = 0;
    *type = 0xFF;
    if (hidpp(devIdx, 0x00, 0x00, 0x00, 0x05, 0x00) != 1 || !gResp[4]) return;
    uint8_t nameIdx = gResp[4];
    if (hidpp(devIdx, nameIdx, 0x00, 0, 0, 0) != 1) return;
    size_t total = gResp[4];
    if (total > n - 1) total = n - 1;
    for (size_t off = 0; off < total; off += 15) {
        if (hidpp(devIdx, nameIdx, 0x01, (uint8_t)off, 0, 0) != 1) break;
        for (size_t k = 0; k < 15 && off + k < total; k++) name[off + k] = (char)gResp[4 + k];
    }
    name[total] = 0;
    if (hidpp(devIdx, nameIdx, 0x02, 0, 0, 0) == 1) *type = gResp[4];
}

static void enumerate(uint8_t devIdx) {
    if (hidpp(devIdx, 0x00, 0x00, 0x00, 0x01, 0x00) != 1) return;
    uint8_t fsIdx = gResp[4];

    char name[64];
    uint8_t type;
    readIdentity(devIdx, name, sizeof name, &type);
    printf("\n  index 0x%02x: \"%s\", device type 0x%02x\n", devIdx, name, type);

    if (hidpp(devIdx, fsIdx, 0x00, 0, 0, 0) != 1) return;
    int count = gResp[4];
    printf("  %d features\n\n    idx   featureID  flags  name\n"
           "    ----  ---------  -----  ----\n", count);
    for (int i = 0; i <= count; i++) {
        if (hidpp(devIdx, fsIdx, 0x01, (uint8_t)i, 0, 0) != 1) continue;
        uint16_t fid = (uint16_t)((gResp[4] << 8) | gResp[5]);
        printf("    0x%02x  0x%04x     0x%02x   %s\n", i, fid, gResp[6], featName(fid));
    }
}

int main(void) {
    IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    IOHIDManagerSetDeviceMatching(mgr, NULL);
    IOHIDManagerOpen(mgr, kIOHIDOptionsTypeNone);
    CFSetRef set = IOHIDManagerCopyDevices(mgr);
    if (!set) { printf("no HID devices\n"); return 1; }
    CFIndex n = CFSetGetCount(set);
    IOHIDDeviceRef *devs = malloc(sizeof(IOHIDDeviceRef) * n);
    CFSetGetValues(set, (const void **)devs);

    int found = 0;
    for (CFIndex i = 0; i < n; i++) {
        if (propInt(devs[i], CFSTR(kIOHIDVendorIDKey)) != VID_LOGITECH) continue;
        int32_t page = propInt(devs[i], CFSTR(kIOHIDPrimaryUsagePageKey));
        if (page != 0xFF00 && page != 0xFF43) continue;

        found = 1;
        gDev = devs[i];
        int32_t pid = propInt(devs[i], CFSTR(kIOHIDProductIDKey));
        int32_t loc = propInt(devs[i], CFSTR(kIOHIDLocationIDKey));
        printf("\n=== %04x at location 0x%08x, vendor page 0x%04x ===\n",
               pid, loc, page);

        IOHIDDeviceOpen(gDev, kIOHIDOptionsTypeNone);
        static uint8_t rbuf[64];
        IOHIDDeviceRegisterInputReportCallback(gDev, rbuf, sizeof rbuf, inputCB, NULL);
        IOHIDDeviceScheduleWithRunLoop(gDev, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

        if (page == 0xFF43) {
            enumerate(0xFF);
        } else {
            for (int d = 1; d <= 6; d++) {
                int r = hidpp((uint8_t)d, 0x00, 0x00, 0x00, 0x01, 0x00);
                if (r == 1) enumerate((uint8_t)d);
                else printf("  index 0x%02x: %s\n", d,
                            r == -1 ? "empty slot" : "no answer");
            }
        }
        IOHIDDeviceUnscheduleFromRunLoop(gDev, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        IOHIDDeviceClose(gDev, kIOHIDOptionsTypeNone);
    }
    if (!found) printf("no Logitech HID++ endpoint found\n");
    return 0;
}
