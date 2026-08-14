# Logitech Spotlight 2 protocol notes (macOS)

Everything below was read off the device with `bin/receiver` and `bin/inspect`.
Anything not yet confirmed sits under [Open questions](#open-questions).

## Identity

| | |
|---|---|
| Name, feature `0x0005` | `Spotlight 2` |
| Device type | `0x06` |
| Model ID, feature `0x0003` | `0xB506` |
| Unit ID | `xxxxxxxx` |
| Transport word | `0x0002` |

## Addressing

The remote reaches the Mac through a Logi Bolt receiver, USB `046D:C548`. HID++
requests go to the receiver's vendor collection and carry the remote's device
index in byte 1.

| | |
|---|---|
| Receiver vendor collection | usage page `0xFF00`, usage `0x0001` |
| Device index | `0x04` |
| Request report | `0x11`, 20 bytes |

The device index is a slot number on that particular receiver, not a property of
the remote, so it has to be discovered rather than assumed. `bin/receiver` pings
indices 1 to 6 on every Logitech vendor collection it finds. An empty slot
answers with a HID++ error; the live slot answers with data.

Two Bolt receivers plugged into the same Mac share the product ID `0xC548`, so
matching on vendor and product alone picks an arbitrary one. Match on the
LocationID too, or scan every handle.

A direct Bluetooth connection would use vendor page `0xFF43` and device index
`0xFF` instead, the way the MX Creative Dialpad does. That path is untested
here.

### Timing

A request through the receiver takes longer than a direct USB one. At a 0.35
second timeout the live slot reads as silent while the empty slots still answer
promptly, which inverts the result. 1.2 seconds is enough.

### Matching replies to requests

A reply carries the feature index in byte 2 and the function-and-software-id
byte in byte 3. An error reply tags byte 2 with `0x8F` for HID++ 1.0 or `0xFF`
for 2.0 and pushes both bytes one to the right. Taking the first inbound report
as the answer reads unrelated traffic as data, because the receiver interleaves
notifications with replies.

## HID collections

The receiver exposes four collections while the remote is connected.

| Usage page | Usage | Max input |
|---|---|---|
| `0x0001` Generic Desktop | `0x0002` Mouse | 9 B |
| `0x0001` Generic Desktop | `0x0006` Keyboard | 8 B |
| `0x000D` Digitizer | `0x0005` | 29 B |
| `0xFF00` Vendor | `0x0001` | 20 B |

The digitizer collection appears only on the receiver the remote is paired to,
so it belongs to the remote rather than to the receiver.

## Feature table

36 features, read through `IFeatureSet` (`0x0001`).

| idx | ID | Name |
|---|---|---|
| `0x00` | `0x0000` | Root |
| `0x01` | `0x0001` | IFeatureSet |
| `0x02` | `0x0003` | DeviceInformation |
| `0x03` | `0x0005` | DeviceNameAndType |
| `0x04` | `0x1d4b` | |
| `0x05` | `0x0020` | |
| `0x06` | `0x0021` | |
| `0x07` | `0x0007` | DeviceFriendlyName |
| `0x08` | `0x0011` | |
| `0x09` | `0x1004` | UnifiedBattery |
| `0x0a` | `0x1701` | |
| `0x0b` | `0x1b04` | ReprogControlsV4 |
| `0x0c` | `0x1814` | ChangeHost |
| `0x0d` | `0x1815` | HostsInfo |
| `0x0e` | `0x2250` | |
| `0x0f` | `0x19b0` | |
| `0x10` | `0x19c0` | |
| `0x11` | `0x1a01` | |
| `0x12` | `0x2205` | PointerMotionScaling |
| `0x13` | `0x00c3` | |
| `0x14` | `0x1802` | DeviceReset |
| `0x15` | `0x1803` | |
| `0x16` | `0x1807` | |
| `0x17` | `0x1816` | BLEProPrepairing |
| `0x18` | `0x1805` | OOBState |
| `0x19` | `0x1830` | |
| `0x1a` | `0x1891` | |
| `0x1b` | `0x18a1` | |
| `0x1c` | `0x1e00` | EnableHiddenFeatures |
| `0x1d` | `0x1e02` | |
| `0x1e` | `0x1e30` | |
| `0x1f` | `0x1602` | |
| `0x20` | `0x1eb0` | |
| `0x21` | `0x1861` | |
| `0x22` | `0x9401` | |
| `0x23` | `0x9402` | |
| `0x24` | `0x18b1` | |

No `0x8100` OnboardProfiles, so button assignments do not live on the device.
Whatever Options+ configures, it configures on the host.

## Buttons

`0x1B04` sits at feature index `0x0b` and reports 10 controls.

| # | CID | TID | Flags | xFlags | Capability |
|---|---|---|---|---|---|
| 0 | `0x0050` | `0x0038` | `0x31` | `0x04` | mouse, reprog, divertable |
| 1 | `0x00d8` | `0x00b7` | `0x31` | `0x07` | mouse, reprog, divertable, raw XY |
| 2 | `0x01a8` | `0x00bb` | `0x31` | `0x07` | mouse, reprog, divertable, raw XY |
| 3 | `0x00d9` | `0x00b6` | `0x30` | `0x04` | reprog, divertable |
| 4 | `0x00da` | `0x00bc` | `0x30` | `0x07` | reprog, divertable, raw XY |
| 5 | `0x00db` | `0x00b8` | `0x30` | `0x04` | reprog, divertable |
| 6 | `0x00dc` | `0x00bd` | `0x30` | `0x07` | reprog, divertable, raw XY |
| 7 | `0x00fb` | `0x00ce` | `0x30` | `0x04` | reprog, divertable |
| 8 | `0x00fc` | `0x0062` | `0x20` | `0x04` | divertable |
| 9 | `0x01b0` | `0x0116` | `0x30` | `0x04` | reprog, divertable |

Flags byte, from `getCidInfo` function 1: `0x01` mouse button, `0x02` F-key,
`0x04` hotkey, `0x10` reprogrammable, `0x20` divertable, `0x40` persistently
divertable. None of the ten carries `0x40`, so a divert is volatile and a host
that wants these buttons sets it again after every power cycle.

The additional-flags byte carries `0x01` raw XY and `0x02` force raw XY. Four
controls have both. Raw XY is the mechanism a host uses to receive pointer
motion as HID++ notifications instead of cursor movement, which is how a
highlight overlay gets gyro deltas while the system cursor stays put.

Live divert state comes from `getCidReporting` function 2, where bit 0 of the
flags byte is the divert bit.

### Diverting

`setCidReporting` function 3 takes the CID in the first two parameter bytes and
a flags byte third. Each setting is a value bit paired with a validity bit, so
`0x03` is divert-on and `0x02` hands the control back.

All ten controls accept the divert. Reading them back with function 2 returns
`0x01` for every one. Writing the recorded original state back restores them
exactly, `0x0050` included.

CID `0x0050` reads as diverted before anything writes to the device, and stays
that way across a restore that puts back what it found.

## Feature probes

Function 0 on the features with no published name, which by HID++ 2.0
convention is `getCapabilities` or `getInfo`.

| Feature | idx | Function 0 reply |
|---|---|---|
| `0x0011` | `0x08` | error `0x02` |
| `0x0020` | `0x05` | all zero |
| `0x0021` | `0x06` | `xx xx xx xx xx xx xx xx xx xx xx xx xx xx xx xx` |
| `0x00c3` | `0x13` | `00 00 1e 07 d9 db` |
| `0x1701` | `0x0a` | `01 00 0a` |
| `0x19b0` | `0x0f` | `00 1f 00 3c 00 0f ff ff` |
| `0x19c0` | `0x10` | `01` |
| `0x1a01` | `0x11` | all zero |
| `0x1d4b` | `0x04` | error `0x07` |
| `0x2250` | `0x0e` | `00 01` |

`0x0020` is ConfigChange, whose function 0 reads a configuration cookie that
vendor software writes when it sets a device up. On this remote the cookie is
zero, and the remote has never been onboarded through Options+ on any machine.

## Silence

The remote answers every request instantly and initiates nothing. Across
repeated tests it emitted no input report on any of its four collections, and
none as a `0x1B04` notification with all ten controls diverted. Pressing its
buttons produces no reaction on macOS and lights no LED.

The receiver's own 15-second heartbeat is the only traffic on that receiver, so
the listener and the callback are working. The MX Creative Keypad has the same
signature and resolves it with the `0x0008` keepalive, but this remote has no
`0x0008`.

Whether the zero config cookie is the cause or merely another symptom of a
never-configured device is untested. Writing a cookie with function 1 would
test it, at the cost of a persistent write.

## Power and hosts

| Feature | Reading |
|---|---|
| `0x1004` UnifiedBattery | percent, level, charging state |
| `0x1815` HostsInfo | 8 host slots, currently on host 2 |

## Open questions

1. Why the remote transmits nothing. This blocks everything else, since no
   button can be mapped until some press reaches the host. See
   [Silence](#silence).
2. Whether writing a configuration cookie through `0x0020` function 1 changes
   that, and whether onboarding the remote once through Options+ on any machine
   does.
3. Which CID belongs to which physical button. The remote has three buttons on
   its face and reports ten controls, so most are unreachable combinations or
   long-press variants.
4. What `0x19b0`, `0x19c0`, `0x1a01`, `0x2250` and `0x1701` do. `0x19b0`
   answers `00 1f 00 3c 00 0f ff ff`, whose `0x3c` and `0x0f` read like
   durations, which would suit the presentation timer.
5. Why `getCidReporting` reports CID `0x0050` as already diverted when no
   software has set it.
6. Whether raw XY delivers usable gyro deltas, and on which report.
7. What the 29-byte digitizer collection carries.
