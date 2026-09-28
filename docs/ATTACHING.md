# Attaching things, labels, HUDs and hand menus

How to make one thing ride on another, how to put words in the world, and how to
carry menus and readouts with the player. Everything here is in the toolbox:
the **Attach & label** bench (`examples/toolbox/station_attach.c`) and the **Menus
& HUD** station (`station_menus.c`, `hud.c`).

## Attaching: one rule

A thing that rides on another keeps a **local pose**: where it is *relative to its
parent*. Every frame its world pose is worked out again from the parent's:

```c
child_world = sfxr_pose_mul(parent_world, child_local);
```

That's all attachment is. If the parent moves or turns, the child goes with it,
because the child's pose is never stored in world space.

To attach something **where it is right now** (you picked it up, you set it down on
a moving platform), take its pose relative to the new parent at that moment, then
keep using that:

```c
child_local = sfxr_pose_relative(parent_world, child_world);   // inverse(parent) * child
```

To **detach**, stop recomputing it. Its last world pose is where it stays (then let
gravity or your physics take it from there).

**Chains** work the same way. On the bench, a flag rides on a lever that rides on a
turntable that stands on the bench:

```c
SfxrPose table = vrui_rotary(ID_TABLE, bench_spot, &spinner, &spin).part;   // turns
VruiMech lever = vrui_pivot(ID_LEVER, table, &lever_spec, &arm);           // a mechanism mounted ON it
SfxrPose flag  = sfxr_pose_mul(lever.part, (SfxrPose){ { 0, 0.17f, 0 }, QuaternionIdentity() });
```

A mechanism is happy to have a moving base. The lever above still works while the
table turns, because its base pose is simply different each frame.

### What can be a parent

| Parent | Pose | Used for |
|---|---|---|
| a hand | `sfxr_hand(h)->grip` | held things, a wrist watch, a tablet in your hand |
| the head | `sfxr_head()` | a visor HUD (keep it small and low) |
| the body | `vrui_body()` | a belt, holsters, a readout at your waist |
| the rig | `sfxr_rig_position()` / `sfxr_rig_yaw()` | things that travel with you but don't turn with your head |
| a mechanism | the result's `.part` | things on a turntable, a door's handle, a lever's knob |
| an animated bone | the bone's pose (raylib 6: `model.currentPose[i]`, already in model space) | a sword in a character's hand |

**The body is a guess.** The headset tracks your head and hands, not your torso.
`vrui_body()` stands on the floor under your head, a little behind your eyes, and
faces where your head has been facing. It lags turns under 45 degrees and drifts
round after you in a second or two. Without the lag, a belt would swing every time
you glance sideways. Snap turns and teleports carry it along (its yaw is kept
relative to the rig). For your waist height use a fraction of `vrui_eye_height()`
(about 0.58).

### Grabbing is attaching

`vrui_grabbable` makes the hand the parent while it's held: at the grab it stores
the object's pose relative to the grip, and each frame it sets the pose from the
grip. On release, *you* decide the next parent. The bench's blocks show all four
outcomes:
- over the turntable: attach to it, exactly where you let go
- at your hip: attach to your belt (the slot lights up as you get close, so you
  know it's there)
- anywhere else: no parent, so it falls

## Labels: text in the world

| Kind | Call | Use it for |
|---|---|---|
| floating name | `vrui_text3d(pos, text, height, color)` | a name over a thing; turns to face you (yaw only) |
| printed | `vrui_text_at(pose, text, height, color)` | signs, plaques, words on a control. Lies on the pose, facing its +Z. From behind, nothing is drawn |
| tag | `vrui_tag(pos, text, height, color, plate)` | floating text on a dark plate: readable over any background |
| callout | `vrui_callout(anchor, text, lift, color)` | pointing at something: a dot on it, a leader line up, a tag |
| sign | `vrui_sign(pose, width, title, body, board)` | a board with a title and lines (every station has one) |

**How big.** The Frame shows about 16 pixels per degree at its default render
size. Text reads comfortably at **about 1 degree tall**, which is 1.75 cm for every
meter away. Below about 0.6 degrees it turns to mush. `vrui_text_height(distance,
degrees)` does the arithmetic. A callout does it for you: it keeps about 1.1 degrees as
you step back, up to 5 cm letters, and it hides past 8 m, because a room full of
callouts seen from afar is clutter.

**Fixed or billboard?** Text that belongs to a surface (a label on a button, a
frequency on a radio's face) should be printed on it with `vrui_text_at`. It
stays put when you move, like real print, and you read it by facing it. Text
that names a thing floating in space is better as a billboard, because there's
no "front" to walk round to.

**Labels on moving things** need nothing special: compute the label's position
from the thing's pose each frame, and it follows. The bench's blocks each carry a
callout saying what they're attached to right now.

## HUDs: readouts that go with you

A HUD is an ordinary vrui panel with two options set before `vrui_panel_begin`:
- `vrui_panel_passive()`: a display, not a control. It takes no laser and claims
  no input, so a readout floating in front of you never catches the laser meant
  for what's behind it.
- `vrui_on_top_begin()` / `vrui_on_top_end()` around it: drawn after the world
  without depth testing, so walking up to a wall doesn't swallow it.

Only the pose differs between the styles. The Menus & HUD station switches between
them (`hud.c`):

| Style | Pose from | Verdict |
|---|---|---|
| **Head** | `sfxr_head()` plus an offset | always readable, but it swims with every head movement and you can't turn to look at it. Fine for one or two words at the edge of the view; tiring for more |
| **Follow** | `vrui_follow(id, target, 20 deg, 0.45 s)` | stays where it is while you glance around, and glides back in front of you once you've turned more than 20 degrees away. The comfortable default for something you check often |
| **Body** | `vrui_body()`: waist height, 35 cm out, tilted up | look down to read it. Out of the way until wanted |

**Edge arrows** (`vrui_offscreen_arrow(target, label, color)`) point at things out
of view: toward home, toward a bug behind you. Behind you they only say "left" or
"right", since up or down would point at the sky or your feet.

**Flashes** (`vrui_tint(color, alpha)`) wash the whole view with a color for a
frame. Call it every frame with a falling alpha for a damage flash. Keep it gentle
and brief: a strong full-view flash is unpleasant in a headset.

## Hand menus

Four ways to carry a menu. They can all be on at once because each has its own cue
(`station_menus.c`); every one offers the same list of choices, which the caller
passes in (the toolbox and Daddy Bug Smasher each have their own).

| Menu | Cue | Good for |
|---|---|---|
| **Watch** | turn the back of your less-used wrist toward your face | status, not choices. Read-only |
| **Palm** | turn that hand's palm toward your face (held a quarter second): three buttons float above it; poke one with your other hand's finger | the two or three things you do most. Fast, needs both hands |
| **Tablet** | that hand's menu button (left: View, right: Menu) toggles a panel held in the hand like a clipboard; use it with the other hand's laser | everything. Room for settings |
| **Radial** | hold the secondary button on your main hand (right: B, left: D-pad up), tilt the stick toward a choice, let go | one hand, no aiming. Quick once the positions are learned: you remember a direction, you don't read a list |

Why the cues look like this:
- **"Turn toward your face"** cues (watch, palm) only count within about 35–40
  degrees, and stay until past 55–60. The gap stops them flickering at the edge.
  The palm also has to hold for a quarter second, so a hand swinging past your face
  doesn't open a menu. And it won't open while that hand is holding something.
- **The radial menu claims its hand's stick** while open (`vrui_claim_input`), so
  locomotion doesn't teleport or turn you while you choose. Letting go with the
  stick centered picks nothing: an easy way out.
- **Main hand and less-used hand** come from the Hands-on setup, so a left-handed
  player gets everything mirrored.
