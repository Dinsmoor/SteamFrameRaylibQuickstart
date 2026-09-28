# Mechanisms: reference physical controls for VR

The toolbox's **Mechanisms bench** (ahead right in `examples/toolbox`) has one of every
physical control vrui provides: knob, selector, crank, lever, sprung lever, joystick,
slider, plunger, button, latching button, rocker switch. Each one is meant to be copied
into your own app. The idea: *"here is a toggle switch that already feels right. Give it
your own graphics and collider, and keep the behavior."*

This page describes what each control does, why, what players expect of it, its default
settings, and which test proves each promise.

## Why this is harder than it looks

On a desk, a knob is easy: the mouse moves in 2D and you map it to an angle. In VR the
hand moves in 3D, and **it never moves along the control's axis cleanly**. People:
- press down on a knob while turning it, or lean on it sideways
- pull a lever slightly toward themselves while pushing it forward
- lift a slider handle a little as they slide it
- let their finger slide across a button as they press
- tremble: physiological tremor plus tracking noise is about a millimeter, all the time

Many VR controls treat this as an error. They let go when the hand leaves a tight
collider, jump when grabbed off-center, or turn the sideways push into motion. Players
experience that as "fiddly" and "broken", and they're right.

## The rules every mechanism follows

1. **Only motion along the control's own freedom counts.** A knob turns, a lever swings,
   a slider slides, a joystick tilts. Everything else the hand does is *projected away*.
   It isn't treated as an error.
2. **Motion is relative to where you took hold.** Grabbing a slider handle 1.5 cm
   off-center doesn't make it jump 1.5 cm.
3. **A small break-in at grab time** (`spec.slop`), so the act of grabbing, with a
   trembling hand, doesn't nudge the value. The break-in is dropped, not added later, so
   nothing jumps once motion starts.
4. **Holding ends only when you let go** of the trigger or grip (at the pull level in
   effect). How far the hand has drifted from the control never ends a hold. A real
   hand can't slip off a real knob it's gripping, so a virtual one shouldn't either.
5. **Near and far both work.** Grip it with the hand, or point the laser and hold the
   trigger (or grip). Every mechanism does both.
6. **Resistance you can feel.** The controllers can't push back, so resistance is a
   *speed limit* and a *weight*:
   - The control follows your hand, but no faster than `max_speed`, or `far_max_speed`
     by laser, where a tiny wrist flick is a huge sweep.
   - It takes `weight` seconds to catch up.
   - If it falls more than `slip` behind, your grip slips: the extra motion is dropped,
     so it never keeps turning after you stop.
   - While it lags you feel a rough **strain hum**.
7. **You can feel everything else too.** Haptics carry it all (see
   [INPUT.md](INPUT.md) "Haptics"):
   - detent ticks
   - end-stop bumps
   - grab and press clicks
   - on sprung controls, a **tension hum** that gets stronger, higher and wobbles faster
     the further you pull from rest

## How a mechanism is built

Each mechanism is three separable parts:

| Part | What it is | Where |
|---|---|---|
| **Behavior** | how it moves and feels: range, travel, detents, spring, break-in, haptics | the spec: `vrui_knob_spec()` etc. **Keep this.** |
| **Collider** | what hands and lasers can take hold of | the spec's `size` (and `reach` for near grabs). **Match it to your model.** |
| **Look** | how it's drawn | vrui draws a plain default; set `spec.draw = false` and draw your own at the result's `part` pose |

```c
// A volume knob with your own model:
VruiMechSpec spec = vrui_knob_spec();   // the tested defaults
spec.size = 0.03f;                      // collider radius: match your mesh
spec.draw = false;                      // you draw it
VruiMech k = vrui_rotary(ID_VOLUME, knob_pose, &spec, &volume);
// ... later, inside sfxr_draw_begin/end:
sfxr_push_pose(k.part);                 // the knob cap, already rotated to the value
    DrawModel(knob_model, (Vector3){0}, 1.0f, WHITE);
sfxr_pop_pose();
```

The result (`VruiMech`) also reports `hovered`, `grabbed`, `held`, `released`,
`changed`, the `hand`, whether it's held `via_ray`, the nearest `detent`, and the
`position` of the moving part (an angle or an offset). Use them for your own sounds,
tooltips or highlight effects.

**Naming:** full forms are named after the *motion* and take a spec (`vrui_rotary`,
`vrui_pivot`, `vrui_linear`, `vrui_tilt`, `vrui_press`, `vrui_rocker`). Short forms are
named after the *thing* and use the default spec (`vrui_knob`, `vrui_lever`,
`vrui_slider3d`, `vrui_joystick`, `vrui_push_button`, `vrui_switch`).

## Direction conventions

- **Rotary** values increase **clockwise** when you look down the axis, like a volume
  knob seen from above.
- A **lever**'s value increases as the handle is pushed toward the base's **-Z** (away from
  a player facing -Z).
- **Linear** controls increase toward the base's **+X**.
- A **joystick** reports x toward +X and y toward -Z.

## The mechanisms

### Knob (`vrui_knob_spec`, `vrui_rotary`)
**Players expect:** to grab it and turn it, either by twisting the wrist or by dragging
the hand around it like a lazy susan. They don't expect to crank their wrist 180°, or to
have to hold their hand perfectly still over the axis.

**Behavior:**
- **Orbit.** The hand's angle around the axis drives it (a lazy susan). Only the hand's
  position in the plane of rotation counts, so pressing down along the axis or leaning
  in or out changes nothing.
- **Twist.** The wrist's rotation about the axis also drives it. Wrist tilt is ignored.
- **How they combine.** Orbit is trusted in proportion to the distance from the axis,
  because near the center a millimeter is a huge angle. When orbit and twist agree, the
  bigger one wins. Grabbing the rim and twisting moves the hand a little and the wrist a
  lot; dragging it around moves the hand a lot. Otherwise the two blend.
- **Laser.** The laser spot on the knob's top face orbits the same way.
- **Stick.** While pointing at it, the thumbstick fine-adjusts.
- **End stops.** They hold. If you over-turn past the end and come back, it responds
  immediately, with no unwinding.

**Defaults:**

| Setting | Default |
|---|---|
| Travel | 3/4 turn (270°) end to end, so no regripping |
| Radius | 3.5 cm |
| Break-in | 2°, or 3 mm of hand travel at the grab radius, whichever is larger |
| Speed limit | 3 turns/s by hand, **1 turn/s by laser** (circling the laser can't spin it) |
| Weight | 0.03 s; slip after 45° of lag |
| Haptics | stop bump 0.4, strain 0.35 |

**Tests** (`tests/mech`):
- `knob-orbit-quarter-turn`
- `knob-twist-quarter-turn`
- `knob-press-down-while-turning`
- `knob-side-push-while-twisting`
- `knob-grab-does-not-nudge`
- `knob-grab-off-axis-no-jump`
- `knob-hold-survives-drift`
- `knob-end-stop-holds`
- `knob-laser-orbit`
- `knob-laser-fast-spin-is-limited`

### Selector (`vrui_selector_spec(n)`, `vrui_rotary`)
**Players expect:** a rotary switch that clicks between labelled positions and never
rests between them.

**Behavior:**
- It is a knob with `n` positions, 30° apart.
- The value only ever sits on a position.
- It moves to the next position only 65% of the way there (hysteresis). A trembling
  hand resting at the halfway point can't make it flicker.
- There's a haptic tick per position.

**Tests:** `selector-turn-snaps`, `selector-no-chatter-at-boundary`.

### Crank / handwheel (`vrui_crank_spec`, `vrui_rotary`)
**Players expect:** to grab the rim or handle and wind it round and round, with the arm.

**Behavior:**
- It's endless (no stops), and the value counts turns.
- Orbit only: you crank with your arm, not your wrist.
- There's a ratchet click every 30°.

- It's heavy: up to 2 turns/s (0.75 by laser), with 0.08 s of weight.

**Test:** `crank-two-turns`.

### Spinner / wheel of fortune (`vrui_spinner_spec(pegs)`, `vrui_rotary`)
**Players expect:** to flick it and watch it spin down, clicking past its pegs, and to
see where it lands.

**Behavior:**
- An endless rotary with `coast`: on release it keeps the speed you gave it and slows by
  friction.
- The result's `position` tells you which wedge is under the pointer (the toolbox's
  linkage bench shows how).

### Dial (`vrui_dial_spec`, `vrui_rotary`)
**Players expect:** a tuning dial, like a radio's. It turns most of a turn, you feel it
tick past the marks, and it stays wherever you leave it.

**Behavior:**
- A knob from 0 to 100 over 300°.
- 21 ticks (every 5) that it **doesn't** snap to. Compare the selector, which only ever
  rests on a position.

In the toolbox, it's the radio's TUNE dial in *Hinges & cords*.

### Lever (`vrui_lever_spec`, `vrui_pivot`)
**Players expect:** to grab the handle and push or pull it along its arc, like a
throttle.

**Behavior:**
- It's a rotary joint grabbed far from its axis, so it runs on pure orbit about the
  pivot.
- Pushing sideways (along the pivot axis) is ignored.
- With `spring = true` and `rest = 0.5` it becomes a dead-man lever that returns to
  center when released (the yellow one on the bench).
- **Set point:** `rest` can change every frame, so another control can set it. On the
  linkage bench, a PIN (a short lever with 9 snapping holes) sets where the sprung lever
  returns to: `spec.rest = pin / 8.0f;`.
- While held away from rest, the tension hum tells you how far.

**Defaults:** 80° of swing, 18 cm long, 7 cm near-grab reach.

**Tests:** `lever-push-forward`, `lever-sideways-push-ignored`, `lever-grab-no-jump`,
`sprung-lever-returns-to-set-point`.

### Joystick (`vrui_joystick_spec`, `vrui_tilt`; short form `vrui_joystick`)
**Players expect:** to grab the ball, push it around, and have it spring back to center.

**Behavior:**
- The tilt follows the hand's motion across the stick, relative to where you grabbed.
- Pushing down on the stick is ignored.
- There's a radial dead zone (`slop`, 4 mm of hand motion).
- 6 cm of hand motion gives full deflection, and there's a bump at the rim.

**Tests:** `joystick-tilt-and-return`, `joystick-push-down-ignored`.

### Slider (`vrui_slider_spec`, `vrui_linear`; short form `vrui_slider3d`)
**Players expect:** to slide it along its track.

**Behavior:**
- Only motion along the track counts. Lifting the handle or leaning across the track
  is ignored.
- It's relative to where you grabbed.
- With the laser, the spot is taken on the plane that contains the track and faces the
  laser.

**Defaults:** 25 cm, 4 mm break-in.

**Tests:** `slider-off-center-grab`, `slider-lift-and-lean-ignored`, `slider-laser`.

### Plunger / pull handle (`vrui_plunger_spec`, `vrui_linear`)
A sprung slider: pull it out, let go, and it glides home (most of the way in about a
sixth of a second). While you hold it out, the tension hum grows with the distance.

**Tests:** `plunger-pull-and-return`, `plunger-tension-hum`.

### Door and lid (`vrui_hinge_spec(width)`, `vrui_hinge`; short form `vrui_door`)
**Players expect:**
- to take the handle and walk it round
- to pull a door toward themselves, and lift a lid
- the door to feel heavy
- leaning on the handle not to open anything

**Behavior:**
- A pivot turned so its axis is the hinge. It is grabbed by the handle, `width` from the
  hinge.
- Only motion **around** the hinge counts. Pushing along the hinge (leaning down on a
  door handle) or toward the hinge does nothing.
- It opens 100°, from closed to a little past square, with a bump at each end.
- It's heavy: 0.15 s of weight, 150°/s at most (60°/s by laser). A fast yank makes it
  trail and strain.
- The result's `part` is the door itself, posed on the hinge with +X toward the handle.
  Draw your own door there.

**A lid is a door on its side.** Turn the hinge pose so its +Y runs along the back edge of
the chest and its +Z points up. *Hinges & cords* shows both.

**Tests:** `door-pull-open`, `door-lean-on-handle-ignored`.

### Pull cord (`vrui_pull_cord_spec`, short form `vrui_pull_cord`)
**Players expect:** to pull a cord all the way down and have it do its thing **once**:
ring a bell, start an engine, flush.

**Behavior:**
- A sprung handle, 25 cm of travel, hanging from an anchor.
- It fires when pulled past 90%, with a clunk in the hand.
- It re-arms only after coming back up past **half**. A hand jiggling at the bottom can't
  fire it twice. Real hands do jiggle, and a re-arm right below the firing point
  double-fires.
- Let go and it springs back up.

**Test:** `cord-fires-once-per-pull`.

### Valve: the two-handed wheel (`vrui_valve_spec`, `vrui_valve`)
**Players expect:** a big stuck valve to need both hands, the way a real one does, and to
turn like a steering wheel once both are on it.

**Behavior:**
- Grab the rim anywhere, with each hand, independently. Near only: a laser can't put two
  hands on a rim.
- It turns by the **average** of the two hands' swings round the axis. Both hands going
  the same way turn it with them. One pushing and one pulling still turns it, like a
  steering wheel. And one hand slipping can't spin it on its own.
- Only the swing round the axis counts: pushing, pulling or lifting the rim does nothing.
- With one hand (fewer than `hands`) it **won't budge** (`one_hand = 0`). It strains in
  your hand, harder the harder you try, and a tag by the wheel says "use both hands". Set
  `one_hand` to a fraction for a valve that's merely stiff with one hand.
- 3 turns from shut to open, a tick every quarter turn, a heavy speed limit (half a turn a
  second: more slips, with a strain hum), a hard bump at each end.
- The result's `holders` says how many hands are on it.
- **It looks two-handed before anyone tries.** Two hand-sized grip pads sit on opposite
  sides of the rim, amber until a hand holds that side, then green, and "BOTH HANDS" is
  printed across the wheel (showing "1 of 2" while only one hand is on it). A player who
  can see it needs two hands never has to find out by failing.

**Tests:** `valve-two-hands-turn-it`, `valve-one-hand-wont-budge`.

### Key switch (`vrui_key_spec(positions)`, `vrui_key_switch`)
**Players expect:** to pick up a key, push it into its slot, turn it, and pull it out
again, all without letting go, like a real key.

**Behavior:**
- The key is a loose object with a pose you keep (`SfxrPose key`). vrui moves it while
  it's held or in the slot, and leaves it where you let go otherwise.
- **Going in:** bring the tip within 3 cm of the slot, lined up within 30 degrees, and it's
  drawn in with a click. Line it up roughly, not exactly: the last few centimeters are a
  magnet. Held on its side, it doesn't go in.
- **Turning:** twist your wrist. Only the twist round the slot's axis counts, so moving
  your arm doesn't turn it. It turns through `positions` stops 45 degrees apart,
  clockwise. Let go and it settles on the nearest stop. With `spring_last`, the last stop
  springs back to the one before, like an ignition's START.
- **Coming out:** at the first stop, pull straight back 4 cm. Turned past it, the key is
  locked in. Just after coming out, the key only goes in again once it has been well
  clear of the slot, so it doesn't pop straight back in.
- It's **one hold throughout**. A version that ended the hold on insertion ("now grab it
  again to turn it") was the tempting shortcut, and players hate it; there's a break
  switch for that bug.

**Tests:** `key-insert-then-turn`, `key-sideways-does-not-go-in`,
`key-pull-out-only-at-off`, `key-start-springs-back`.

### Push button (`vrui_press_spec`, `vrui_press`)
**Players expect:**
- to poke it with a fingertip (or the controller tip) and feel one click
- a finger brushing past it on the way to something else must *not* press it
- a press that drifts sideways must not "let go" and re-press

**Behavior:**
- A hand is **armed** only when its tip arrives over the cap from above. Only an armed
  tip presses, so side brushes do nothing.
- Once pressed, the tip may wander `slide` radii (2×) from the center before the press
  lets go.
- Press and release happen at different depths (60% / 30% of travel).
- Laser plus trigger presses it from afar.
- With `latching = true` it's push-on/push-off, and it sits lower while latched.
- With `require_point = true`, only a pointing index finger presses it (the hand shape
  comes from the Frame's touch sensors), so a fist or palm bumping past does nothing.

**Tests:** `button-press-from-above`, `button-side-brush-does-nothing`,
`button-press-then-slide`, `button-point-to-press`.

### Rocker switch (`vrui_rocker_spec`, `vrui_rocker`)
**Players expect:** poke it and it flips, once.

**Behavior:**
- Entering the switch with the tip flips it.
- It can only flip again after the tip has backed out by `exit_margin` (1.5 cm), so a
  fingertip trembling on its edge can't flip it back and forth.
- Laser plus trigger flips it too.

**Test:** `switch-poke-flips-once`.

## Displays: showing values mechanically

Controls are only half of a machine. vrui's displays are read-only (no laser, no input):
- `vrui_gauge(id, pose, value, min, max, label)`: a needle dial whose needle has a little
  mass. It swings, overshoots slightly and settles.
- `vrui_odometer(id, pose, value, digits, label)`: rolling number drums like a car's
  mileage counter. The last drum turns continuously, and each drum to its left turns
  only while the one to its right rolls from 9 to 0.
- `vrui_lamp(pose, on, color, label)`: an indicator light.

The toolbox's **Linkage bench** wires controls to displays in plain code:

| Control | Drives |
|---|---|
| crank | a speed gauge and a turn counter |
| knob | a gauge |
| selector | which lamp is lit |
| ARM | whether FIRE counts, shown on a shot counter |
| PIN | the sprung lever's set point |
| spinner | a wedge readout |

Reading a value and feeding it somewhere is just C: the controls write through their
`float *value` every frame, and anything can read it.

## Pulling the trigger (`SfxrPull`)

Every "hold the trigger / grip" in vrui uses a **pull level**:

| Level | Press / release | Use for |
|---|---|---|
| `SFXR_PULL_SOFT` | 0.25 / 0.15 | delicate things, a light "hold to drag" |
| `SFXR_PULL_FIRM` | 0.55 / 0.35 | **the default**: grabbing, clicking |
| `SFXR_PULL_FULL` | 0.97 / 0.85, or the hardware click | deliberate or destructive actions |

**Why not the hardware click:** measured on the Steam Frame, the trigger rests at
exactly 0 and passes half travel about 15 ms into a normal pull. Its click fires only
fully bottomed out, 40–100 ms later, and sometimes not at all. Players expect about a
3/4 pull to grab.

- Set the default for all widgets with `vrui_style()->pull`, and per item with
  `vrui_push_pull(level)` … `vrui_pop_pull()`.
- Change the thresholds with `sfxr_set_pull_threshold()` (for accessibility).

**Tests** (`tests/input`): `firm-press-once-on-ramps`, `no-chatter-near-threshold`,
`levels-in-order`, `real-pull-registers-fast`.

## Making your own mechanism

Build it from the same core (`vrui/src/vrui_mech.c`):
1. **A collider** feeds `vrui__handle_update()`, which gives you grab, hold and release.
   The hold ends only on letting go.
2. **A drive** turns the hand into motion along your mechanism's freedom:
   `rotary_drive` (orbit plus twist around an axis) or `linear_drive` (projection onto a
   line). Project everything else away.
3. **`break_in()`** for the slop; **`model_drive()`** applies resistance, slip, strain
   and tension for you.
4. **The value model** (`model_grab` / `model_drive` / `model_free`) for range, detents,
   snapping, springs, stops and haptics.
5. **A default look** that can be switched off, plus a `part` pose.
6. **Tests** in `tests/mech`: at least one "does what it's for" case, and one per rule
   above that your mechanism could plausibly break. Each case names a break switch that
   makes it fail (`vrui/src/vrui_breaks.def`, see `docs/TESTING.md` §9). `make test`
   proves every case can fail; `make test-audit` checks that no switch is left unproven.
