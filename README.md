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

`receiver`, `inspect` and `listen` are read-only. `divert` writes, and restores
what it found on exit.

```sh
bin/receiver           # walk every Logitech HID++ endpoint, list live devices
bin/inspect            # identity, battery, hosts and the 0x1B04 control table
bin/inspect <name>     # same, for any device whose name contains <name>
bin/listen 30          # print every report each collection emits, for 30 s
bin/divert 45          # divert the 0x1B04 controls, print presses, then restore
```

`bin/receiver` finds the remote wherever it is, so no receiver location or
device index has to be passed in.

## Status

The remote enumerates, answers HID++ requests through a Logi Bolt receiver, and
reports its 10 reprogrammable controls. All ten accept a divert, and the state
they were found in is restored on exit.

Mapping those controls to the physical buttons is the next step. No press has
been captured yet, so run `bin/divert` from a terminal and watch it live.

`docs/PROTOCOL.md` records what the device has been observed to do, and what is
still unknown.
