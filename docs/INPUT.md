# Input on the Steam Frame: methods, ownership and feedback

How a player's hands reach things in an app built on this quickstart, who gets a
controller's input at any moment, and how the app talks back through haptics. For the
individual controls (knobs, levers, buttons...), see [MECHANISMS.md](MECHANISMS.md).

## The input methods

With controllers (no whole-body tracking), there are four ways to act on the world. vrui
supports all of them, and the toolbox demonstrates each.

| Method | What the player does | How it's read | In the toolbox |
|---|---|---|---|
| **Laser** | points and pulls the trigger | aim pose + trigger at a pull level | 2D panels; any 3D control from afar |
| **Grab** | reaches and takes hold | grip within reach: the grip button, or **closing the hand** (`VRUI_GRAB_CLOSE`), or either button | blocks, knobs, levers, joystick |
| **Poke** | touches with a fingertip | the poke pose (controller tip, or index fingertip with bare hands) pushing in | push buttons, switches |
| **Hand shape** | an open palm, a point, a fist... | the Frame's touch sensors, or hand joints | an open hand shoves blocks, a fast fist knocks them, "point to press" buttons |

### Buttons that don't arrive
Check the recordings before trusting a button. Every headset session so far recorded
touches on the right controller's **B** (over 300 frames across sessions) but **not one
click**, while A, X, Y, the bumpers and the D-pad clicks all came through. So the toolbox's
radial menu, first bound to B, never opened on the Frame; it's on **A** now. To check a
button yourself, watch it on the *Controllers panel*, or count it in a recording:
`sfxrec_dump session.sfxrec --summary` lists clicked/touched frames per control.

### Hand shapes from the touch sensors
Every Frame control reports **touch** as well as press, so sfxr knows where each finger
rests:
- **index:** on the trigger
- **middle, ring and little:** on the grip
- **thumb:** on the stick or any face button

From that it estimates a **curl** per finger and a **shape**:

| Shape | Fingers |
|---|---|
| open | nothing touched |
| point | grip held, index off the trigger |
| fist | everything closed |
| thumbs-up | fist with the thumb lifted |
| pinch | index and thumb, others open |

When the runtime reports hand joints, whether from bare hands or inferred while holding
the controllers, the curls are measured from the joints instead. A shape must hold for
3 frames before it changes, so it doesn't flicker.

```c
const SfxrHand *h = sfxr_hand(SFXR_RIGHT);
if (h->shape == SFXR_SHAPE_OPEN) shove_with_palm(h->palm);
float index_bend = h->curl[SFXR_FINGER_INDEX];
```

Use shapes to make physical interaction **conditional**:
- a button that only a pointing finger presses (`VruiPressSpec.require_point`), so a
  fist or palm bumping past does nothing
- pushing with an open hand
- grabbing by closing the hand

**Tests:** `input/shapes-from-touch-sensors`, `mech/button-point-to-press`,
`mech/grab-by-closing-hand`.

## Bare hands

Put the controllers down and the Frame's cameras track your hands. Everything above still
works, because a bare hand is presented as a controller:

| Controller | Bare hand |
|---|---|
| trigger | **pinch** (thumb tip to index tip): its strength is the trigger value, so pull levels work |
| grip | **closing the hand** (middle, ring and little finger curl) |
| poke point | the **index fingertip** |
| aim (laser) | a ray from the hand |

`sfxr_hand(h)->source` says which one you have. Buttons and the stick don't exist on a
bare hand, so anything that needs them (menus on B, teleport on the stick) needs a
hand-friendly alternative. Offer a pinch or a palm gesture, or say "pick up a controller".

**Where it comes from.** A runtime with a *hand-interaction profile* (the Frame's SteamVR
accepts one) supplies pinch, grasp and the poses itself. A runtime that reports only the
joints would otherwise leave bare hands dead. sfxr then builds the whole hand from the
joints, using the gestures below. `SFXR_HANDS=joints` forces that path even when the
profile is there, so you can compare the two in the headset.

### Gestures (`sfxr_hand_gestures`)

Measured from the joints every frame, for bare hands or joints inferred while holding a
controller:

| Field | What it is | Why |
|---|---|---|
| `pinch`, `pinch_strength`, `pinch_dist[4]` | thumb tip to each fingertip | the index pinch **closes under 2 cm and opens over 3.5 cm**. A hand hovering at the edge doesn't flicker between the two |
| `middle_pinch` | thumb to middle finger | a second "button" that's hard to hit by accident |
| `grasp`, `grasp_strength` | the last three fingers curled | closes at 0.65 curl, opens under 0.45 |
| `palm_up`, `palm_to_head`, `palm_normal` | which way the palm faces | a wrist menu that opens when you look at your palm. On within 40°, off past 55° |
| `ray` | from an estimated shoulder through the index knuckle | a laser that **doesn't dip when you pinch**. A ray from the fingertips moves as they close, so the target slips off just as you click |

A **pinch** shape is also recognized by distance: thumb and index touching, the other
fingers open, even when the index is nearly straight. A light fingertip pinch barely
curls anything, so curl alone misses it.

### Trying it without a headset

In the simulator, **H** switches to bare hands. **LMB** pinches, **F** or **MMB** makes a fist,
and **P** points. The skeleton is the same procedural hand the tests use.

In tests, `sfxt_hand_kind(h, SFXT_BARE)` (runtime with a hand profile) or
`SFXT_BARE_JOINTS_ONLY` (joints only), then `sfxt_shape`, `sfxt_fingers` and `sfxt_pinch`.
The harness builds all 26 joints from the grip pose.

**Tests (`tests/hands`):**
- `shapes-from-joints`
- `light-pinch-is-a-pinch`
- `pinch-no-flicker-at-the-edge`
- `palm-up-both-hands`: the hands are mirror images, and a sign error shows up here
- `joint-only-hand-pinch-clicks`, `joint-only-fingertip-pokes`, `joint-only-fist-grabs`

**Not yet tried on the Frame:** the tests run on the procedural hand, not on real camera
tracking. How the Frame's own pinch values and aim pose compare with these is the first
thing to check in a headset session: look at the Controllers panel's gesture line and the
thin ray it draws from each bare hand.

## Who owns the controller right now?

A controller is shared by everything: locomotion wants the stick, a panel wants the
stick to scroll, the app wants the A button. Without a rule, a player testing buttons on
a panel teleports across the room. vrui's rule is **input ownership**:

- Whatever the player is using **claims** that hand for the frame (`vrui_claim_input`).
  Everything else checks `vrui_input_claimed(hand)` before acting.
- **Locomotion** (teleport, turning) only uses a hand nobody has claimed.
- **Panels** claim in one of three ways (`vrui_panel_capture(...)` before
  `vrui_panel_begin`):

| Mode | Claims | Use for |
|---|---|---|
| `VRUI_CAPTURE_POINT` (default) | a hand whose laser is on the panel | ordinary menus |
| `VRUI_CAPTURE_LOOK` | both hands while the panel is in front of your face (within 30°, 3 m) | panels whose job *is* the controls: the toolbox's Controllers panel, a controller-remapping screen |
| `VRUI_CAPTURE_MODAL` | both hands for as long as the panel is shown | dialogs that must be answered |

A capturing panel draws an accent border and says so in its title bar ("controls held:
look away to release"), so the player understands why the stick doesn't move them.

**In your own code**, the template is:
```c
// act on raw buttons only if nothing else owns the hand
if (!vrui_input_claimed(SFXR_RIGHT) && sfxr_hand(SFXR_RIGHT)->a.pressed) jump();

// claim a hand for your own context (a held tool that uses the stick, a radial menu)
if (holding_spray_can) vrui_claim_input(SFXR_RIGHT);
```

Claims made this frame or last frame count, so call order doesn't matter.

## 2D and 3D, kept apart

2D panels and 3D physical things are different kinds of interfaces, and players should
always know which one they're using:
- **Look:** the laser is **blue** on a 2D panel and **amber** on a 3D thing.
- **Input:** a panel under your laser claims that hand (see above), so the grip or stick
  can't also reach a 3D thing behind it.
- **Scrolling** in panels works by dragging the content or its scrollbar with the
  trigger held, like a phone, as well as with the stick. A "drag to scroll" hint appears
  on first hover.
- **Place them apart:** in the toolbox, panels stand to the left and the physical benches
  to the right and front.

## Haptics: the only resistance there is

The controllers can't push back, so haptics carry everything physical. vrui mixes one
channel per hand (`vrui/src/vrui_haptics.c`). A new vibration normally replaces the
current one; the mixer lets a hum and short ticks coexist. Use `vrui_haptic_pulse()` and
`vrui_haptic_hum()`, not `sfxr_haptic()`. The vocabulary is small and each sensation
means one thing:

| Sensation | Means | Where |
|---|---|---|
| short **tick** | passing a detent or peg | selectors, knobs with detents, cranks, the spinner |
| firmer **bump** | an end stop | every bounded control |
| **click** | took hold, let go, pressed | grabs, buttons |
| light **tick** on hover | "this one" | anything the laser or hand is over |
| **tension hum**: stronger, higher and wobbling faster the further from rest | a spring wants to go back | sprung lever, plunger, joystick |
| rough **strain hum**, stronger the more it lags | you're moving it faster than it goes | every control with resistance |
| a dull low **buzz** | "that did nothing" | the FIRE button while not armed |

`vrui_style()->haptic_scale` scales all of it, and 0 turns it off (comfort,
accessibility). The Controllers panel has a haptics tester for strength, length and
frequency.

## Fitting the player

Players differ in which hand they lead with, whether they grab with the grip or the
trigger, how hard they pull, and whether they poke or point. The toolbox's **hands-on
setup** station learns this by watching a few hands-on tasks, then applies it. See
[ONBOARDING.md](ONBOARDING.md).
