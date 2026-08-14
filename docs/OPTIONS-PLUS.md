# What Options+ exposes

Read off the Logi Options+ configuration screens for a Spotlight 2, and off the
vendor's own [product page][product] and [support hub][hub]. This is the
vendor's account of the device, and it sets the target for what a host has to
reproduce. `PROTOCOL.md` covers the wire format.

[product]: https://www.logitech.com/en-us/shop/p/spotlight-2-presenter-remote
[hub]: https://hub.sync.logitech.com/spotlight-2

## Buttons

Four buttons. Action is the big round button on the top face, Highlight is on
the right edge, and the two arrows sit below Action. The mapping from these
names to CIDs is in `PROTOCOL.md`.

| Button | Gesture | Action |
|---|---|---|
| Highlight | soft press | digital pointer |
| Highlight | hard press | spotlight |
| Action | single click | start or exit presentation mode |
| Action | press and hold | laser pointer |
| Action | double click | breathing experience |
| Back | single click | previous slide |
| Back | press and hold | fast backward |
| Next | single click | next slide |

The Highlight button is force-sensitive with two thresholds, and a sensitivity
setting picks between soft, medium and hard for where the boundary sits. That
is why ten `0x1B04` controls exist for four physical buttons: a single button
reports as more than one control depending on how hard it is pressed and how
long it is held.

Press-and-hold on Back can be reassigned to fast backward, fast forward, volume
control, scroll, or nothing. Single click on Back and Next chooses between next
slide and previous slide only.

## Effects

Every visual effect is drawn by the host. The remote contributes a button state
and gyro motion, and the software puts pixels on the screen.

| Effect | Settings |
|---|---|
| Digital pointer | pointer colour, size, or cursor-only instead |
| Spotlight | contrast, size, freeze on release, or squarelight instead |
| Laser pointer | held on the Action button |

Two settings confirm the host does the drawing. Freeze Spotlight keeps the
effect on screen after the button is released, and Recenter Pointer Effects
resets the pointer to the middle of the screen on a slide change. Neither is
something a device could do to a host it cannot see.

A separate Cursor Control toggle moves the real system cursor so links and
videos stay clickable during a presentation. With it off, the effect draws
without disturbing the cursor, which is what raw XY reporting is for.

Pointer speed is a host-side scale factor, set to 50 percent by default.

## Other panels

| Panel | State |
|---|---|
| Timer | disabled by default |
| Haptic feedback | present, so the vibration motor is exposed to software |
| Focus | present |

The product page says the haptic buzz confirms a press of the Highlight button,
so the motor fires on a button the host already sees as `0x00fb` and `0x00fc`.
It also lists Magnify and Annotate as highlighting modes alongside Spotlight and
Squarelight, and describes the Focus panel as a breathing exercise before
presenting, which is the breathing experience the Action button double click
triggers.

The timer and the haptic motor are the two features that need something sent to
the device rather than drawn on screen. Everything else is host-side rendering
over a stream of button and motion events.
