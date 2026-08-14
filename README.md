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
bin/probe              # call function 0 on the unnamed features
```

`bin/receiver` finds the remote wherever it is, so no receiver location or
device index has to be passed in.

## Status

The remote enumerates, answers HID++ requests through a Logi Bolt receiver,
reports its 10 reprogrammable controls, and accepts a divert on all of them.

It also transmits nothing. No button press has produced a single input report
on any collection, diverted or not, and none produces any reaction on macOS.
Since the remote answers every request instantly, the radio link is fine and
the remote is choosing not to send. That is the open problem, and everything
else waits on it.

`docs/PROTOCOL.md` records what the device has been observed to do, and what is
still unknown.
