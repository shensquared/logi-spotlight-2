# logi-spotlight-2

Reading a Logitech Spotlight 2 presentation remote (`046D:B506`) from macOS over
HID++, with no Logitech software installed.

The remote's pointer and slide buttons already work as plain HID without any
software. What needs a host is everything Logi Options+ draws or counts, namely
the highlight overlay, the magnifier and the presentation timer.

## Build

```sh
make          # binaries land in bin/
```

Requires clang and the macOS SDK. No third-party dependencies.

## Probes

Each binary does one thing, and all three are read-only.

```sh
bin/receiver           # walk every Logitech HID++ endpoint, list live devices
bin/inspect            # identity, battery, hosts and the 0x1B04 control table
bin/inspect <name>     # same, for any device whose name contains <name>
bin/listen 30          # print every report each collection emits, for 30 s
```

`bin/receiver` finds the remote wherever it is, so no receiver location or
device index has to be passed in.

## Status

The remote enumerates, answers HID++ requests through a Logi Bolt receiver, and
reports its 10 reprogrammable controls. Mapping those controls to the physical
buttons is the next step, and it needs either Input Monitoring or a divert.

`docs/PROTOCOL.md` records what the device has been observed to do, and what is
still unknown.
