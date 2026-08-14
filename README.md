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
bin/divert 60 rawxy    # same, plus find the raw-XY bit and switch it on
bin/probe              # call function 0 on the unnamed features
bin/helper             # long-lived bridge, streams presses to a unix socket
```

`bin/receiver` finds the remote wherever it is, so no receiver location or
device index has to be passed in.

## Status

The remote enumerates, answers HID++ requests through a Logi Bolt receiver,
reports its 10 reprogrammable controls, and accepts a divert on all of them.

Presses arrive as `0x1B04` notifications and gyro motion arrives on the mouse
collection, both without Logitech software. The pointer already moves the system
cursor unaided.

What remains is mapping the ten controls to the four physical buttons, and
finding the vibration motor and the timer among the features that answer but
have no published name.

## Spotlight overlay

`bin/helper` holds the remote and streams button presses over
`/tmp/logi-spotlight.sock`. `hammerspoon/logi_spotlight.lua` connects to it and
dims the screen around a bright circle while the trigger button is held.

```sh
ln -s "$PWD/hammerspoon/logi_spotlight.lua" ~/.hammerspoon/modules/
./bin/helper &
```

Then from `init.lua`:

```lua
require("modules.logi_spotlight").start()
```

The circle follows the system cursor rather than gyro deltas, because the remote
already moves the cursor while a pointing control is held. `TRIGGER_CID` at the
top of the module picks which button shows it.

`M.preview(3)` shows the overlay for three seconds with no device attached, to
check the drawing on its own.

`docs/PROTOCOL.md` records what the device has been observed to do, and what is
still unknown.
