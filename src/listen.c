// Read-only listener. Opens every Logitech HID collection and prints each
// report it emits, tagged with the collection it arrived on. Never writes.
//
// The keyboard, mouse and digitizer collections need Input Monitoring; without
// it they stay silent while the vendor collection still reports.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define VID_LOGITECH 0x046D
#define MAX_HANDLES 24

static int32_t gPage[MAX_HANDLES], gUsage[MAX_HANDLES];
static int gCount = 0;

static int32_t propInt(IOHIDDeviceRef d, CFStringRef key) {
    CFTypeRef v = IOHIDDeviceGetProperty(d, key);
    int32_t out = -1;
    if (v && CFGetTypeID(v) == CFNumberGetTypeID())
        CFNumberGetValue((CFNumberRef)v, kCFNumberSInt32Type, &out);
    return out;
}

static void inputCB(void *ctx, IOReturn res, void *sender, IOHIDReportType type,
                    uint32_t reportID, uint8_t *report, CFIndex len) {
    (void)res; (void)sender; (void)type;
    int slot = (int)(intptr_t)ctx;
    printf("[%04x:%04x] id=%02x len=%2ld |", gPage[slot], gUsage[slot],
           reportID, (long)len);
    for (CFIndex i = 0; i < len; i++) printf(" %02x", report[i]);
    printf("\n");
    fflush(stdout);
}

int main(int argc, char **argv) {
    int secs = argc > 1 ? atoi(argv[1]) : 30;

    IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    IOHIDManagerSetDeviceMatching(mgr, NULL);
    IOHIDManagerOpen(mgr, kIOHIDOptionsTypeNone);
    CFSetRef set = IOHIDManagerCopyDevices(mgr);
    if (!set) { printf("no HID devices\n"); return 1; }
    CFIndex n = CFSetGetCount(set);
    IOHIDDeviceRef *devs = malloc(sizeof(IOHIDDeviceRef) * n);
    CFSetGetValues(set, (const void **)devs);

    static uint8_t bufs[MAX_HANDLES][4096];
    for (CFIndex i = 0; i < n && gCount < MAX_HANDLES; i++) {
        if (propInt(devs[i], CFSTR(kIOHIDVendorIDKey)) != VID_LOGITECH) continue;
        int slot = gCount++;
        gPage[slot] = propInt(devs[i], CFSTR(kIOHIDPrimaryUsagePageKey));
        gUsage[slot] = propInt(devs[i], CFSTR(kIOHIDPrimaryUsageKey));
        printf("open %04x at 0x%08x, usage %04x:%04x, maxIn %d\n",
               propInt(devs[i], CFSTR(kIOHIDProductIDKey)),
               propInt(devs[i], CFSTR(kIOHIDLocationIDKey)),
               gPage[slot], gUsage[slot],
               propInt(devs[i], CFSTR(kIOHIDMaxInputReportSizeKey)));
        IOHIDDeviceOpen(devs[i], kIOHIDOptionsTypeNone);
        IOHIDDeviceRegisterInputReportCallback(devs[i], bufs[slot], sizeof bufs[slot],
                                               inputCB, (void *)(intptr_t)slot);
        IOHIDDeviceScheduleWithRunLoop(devs[i], CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    }
    if (!gCount) { printf("no Logitech HID device found\n"); return 1; }

    printf("\nlistening %d s\n", secs);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, secs, false);
    printf("done\n");
    return 0;
}
