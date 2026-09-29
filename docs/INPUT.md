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
| **Hand shape** | an open palm, a point, a fist... | the Frame's touch sensors, or hand joints | an open hand shoves blocks, a fast fist knocks them |

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

With bare hands, the curls are measured from the hand joints instead. Holding the
controllers, SteamVR also reports joints (a skeleton inferred from the same sensors), but
**not for shapes**: in the recordings its index finger stays curled round the trigger
(0.97 of fully curled) even when the finger is lifted off it, so a point could never show.
The touch sensors decide while you hold controllers. A shape must hold for 3 frames before
it changes, so it doesn't flicker.

**Don't gate controls on a shape the Frame guesses.** An earlier toolbox had a button that
only a pointing finger could press. On the headset it wasn't dependable: the index sensor
reads "off the trigger" late or not at all, so a deliberate point often didn't count. Shapes
are good for things that tolerate a miss (an open hand shoving, a fist knocking). Buttons,
menus and anything the player must be able to do on the first try belong on the tip and the
buttons. With bare hands the shape comes from the joints, and is steadier.

**Where a poke lands.** Holding a controller, the poke point is the controller's tip (where
the laser starts). SteamVR's own poke pose for the Frame controllers is 12.5 cm *below* the
grip and 4.6 cm to the side (every recorded session), so the first headset sessions had
no poke presses at all: every button was pressed by laser. Bare hands poke with the index
fingertip, as reported.

`palm` means one thing for controllers and bare hands: -Y comes out of the palm, -Z
runs along the fingers (the hand-joint convention). A runtime's controller palm pose
(`palm_ext`) has the grip's axes instead, with the palm along +-X and -Y down the handle,
so sfxr turns it. Used as given, a "palm to your face" menu opened only with the wrist
held against the head.

```c
const SfxrHand *h = sfxr_hand(SFXR_RIGHT);
if (h->shape == SFXR_SHAPE_OPEN) shove_with_palm(h->palm);
float index_bend = h->curl[SFXR_FINGER_INDEX];
```

Use shapes to make physical interaction **conditional**:
- pushing with an open hand
- grabbing by closing the hand

**Tests:** `input/shapes-from-touch-sensors`,
`mech/grab-by-closing-hand`; as SteamVR reports the Frame (`SFXT_FRAME_SKELETON`):
`input/frame-point-from-touch` (break switch `sfxr_shapes_from_controller_skeleton`) and
`input/frame-poke-at-the-tip` (`sfxr_poke_from_runtime`).


**The thumb on the controller skeleton isn't figured out yet.** Holding the Frame
controllers, SteamVR also reports a full hand skeleton built from the touch sensors (the
joints' data source is "controller"). Where its thumb should go is still an open question:
- The first version drew the joints as reported, and the thumb sat exactly where you'd
  expect, except that left looked like right and right like left.
- A reflection meant to fix that didn't.
- sfxr now poses the thumb itself from the touch sensors (`sfxr__thumb_spot`,
  `sfxr__thumb_reach` in `sfxr_input.c`: the tip on the touched control, hovering over the
  face otherwise). In the headset that doesn't look right either: it isn't even
  transformed properly.

The first version's result suggests the real problem is a left/right swap between the
hands (or their frames), not SteamVR's thumb placement. That's the lead to follow. The
palm and fingers are left as reported. The test `hands/frame-thumb-on-what-it-touches`
(break switch `sfxr_thumb_as_reported`) checks the current posing, not that it's right.

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

## Attention: when the player leaves

A VR player leaves without quitting in two ways: they **take the headset off**, or they
**open the SteamVR dashboard** (the system button). The app keeps running either way,
and nothing it shows is being watched. `sfxr_attention()` says which:

| `sfxr_attention()` | Means | What a game does |
|---|---|---|
| `SFXR_HERE` | playing | carry on |
| `SFXR_AWAY_HEADSET_OFF` | the headset is off (the presence sensor) | pause, hush, save |
| `SFXR_AWAY_DASHBOARD` | the dashboard (or another overlay) has focus | the same |

The pattern (the toolbox's `main.c` and Daddy Bug Smasher's `garden.c` both follow it):
- **Freeze** the simulation: step it with `dt = 0`. Nothing should bite, fall or time
  out while nobody is looking.
- **Hush** it: `sfxr_audio_pause(true)` holds every sound and the music where it is.
- **Save** at once (`sfxr_store_save`). The system may close an app whose player has
  walked away, and the player expects their progress to still be there.
- **Coming back, show a pause menu**, placed in front of wherever they now face, with
  Resume, Restart and Quit. Don't drop them back into the fight they left. On Resume a
  short 3-2-1 gives them time to find their footing and their weapon.

Every change is in the event log ("attention"), recordings keep it (a replay pauses where
the session did), tests drive it (`headset off` / `dashboard open` in a scenario,
`sfxt_headset_worn` / `sfxt_dashboard` in C), and in the simulator F2 takes the headset
off and F3 opens the dashboard.

## Fitting the player

Players differ in which hand they lead with, whether they grab with the grip or the
trigger, how hard they pull, and whether they poke or point. The toolbox's **hands-on
setup** station learns this by watching a few hands-on tasks, then applies it. See
[ONBOARDING.md](ONBOARDING.md).
