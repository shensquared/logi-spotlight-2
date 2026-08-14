# logi-spotlight-2

Driving a Logitech Spotlight 2 presentation remote from macOS over HID++,
with no Logitech software in the loop.

This file is guidance for anyone, human or agent, working on the device
protocol. `README.md` is the entry point for using it.

## Rules

**Do not write to the device without explicit approval in the current session.**
Reading, enumerating features, and listening for input reports are fine.

These features change persistent state and are never called:

| Feature | Why it stays untouched |
|---|---|
| `0x1802` | DeviceReset |
| `0x1805` | OOBState, returns the remote to out-of-box state |
| `0x1814` | ChangeHost, moves the remote to another paired host |
| `0x1816` | BLEProPrepairing, rewrites pairing |
| `0x1602` | password |
| `0x1e00` | EnableHiddenFeatures |

The write this project needs is `0x1B04` function 3 `setCidReporting`, which
sets the divert and raw-XY bits. Both are volatile, and a power cycle clears
them, so a mistake there costs one battery pull. It still needs approval.

No Logitech software is installed, so nothing competes for the device.

## Layout

- `docs/PROTOCOL.md` is the wire-format reference. Read it before touching bytes.
- `docs/OPTIONS-PLUS.md` is the vendor's own account of the buttons and effects,
  which sets the target for what a host has to reproduce.
- `src/*.c` are dependency-free IOKit probes, one concern each. Each file is
  self-contained, so the HID++ request and reply-matching code repeats across
  them rather than living in a shared header.

| File | Purpose |
|---|---|
| `receiver.c` | walk every Logitech HID++ endpoint, list live devices and their features |
| `inspect.c` | one device's identity, battery, hosts and `0x1B04` control table |
| `listen.c` | read-only report listener across every collection, never writes |
| `divert.c` | divert the `0x1B04` controls, print presses, restore on exit |
| `probe.c` | call function 0 on the features with no published name |

## Build

`make` puts binaries in `bin/`. Frameworks are IOKit and CoreFoundation. No
third-party dependencies, and no network access is needed.

## Gotchas

A request through a Bolt receiver needs a timeout of about 1.2 seconds. At 0.35
seconds the live device slot reads as silent while empty slots still answer, so
a live device looks absent.

Two Bolt receivers on the same Mac share product ID `0xC548`. Match on
LocationID as well, or scan every handle.

The mouse, keyboard and digitizer collections need Input Monitoring. The vendor
collection does not, so feature reads work from an unprivileged terminal while
native button reports stay invisible.

## Related work

`~/code/mx-creative-console` drives the MX Creative Keypad and Dialpad over the
same HID++ stack. Its `docs/PROTOCOL.md` covers reply-matching traps and the
`0x1B04` divert flow in more detail, and its `helper.c` is the model for a
long-lived Hammerspoon bridge.
