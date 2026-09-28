# Moving the player: teleport, pads, surfaces, climbing

How an app built on this quickstart moves the player through its world without
making them sick, and which parts are ready to use. All of it goes through one
function, `vrui_locomotion(&cfg)`, the only thing that moves the player's **rig**
(the origin of their tracked space). The toolbox's **Movement yard**, straight ahead
past the workbench (`examples/toolbox/yard.c`), has one of each. `tests/move/`
proves the promises below.

## Why these choices

People get sick in VR when their eyes see motion their inner ear doesn't feel.
Three kinds of movement don't cause that:
- **Motion the player makes themselves**, such as walking in their room or pulling on a
  handhold. Their body did it, so it feels right.
- **Instant jumps with a short blink**, such as a teleport or a snap turn. There's no motion to see.
- **Motion that is small and expected**, such as stepping up a stair.

Anything else is optional and off by default: smooth walking, smooth turning, and falling
under real gravity. Some players are fine with them and many aren't. The toolbox panel has
toggles so you can feel the difference.

## Teleport (stick forward)

Push either stick forward to aim an arc and let go to jump. Pushing a stick sideways snap turns.
While a hand is using a panel, holding something, or has been claimed
(`vrui_claim_input`), its stick is left alone (see [INPUT.md](INPUT.md)).

| Config | Default | What it does |
|---|---|---|
| `teleport_max_dist` | 8 m | how far the arc may land |
| `valid_target(target, user)` | anywhere | your rule for where landing is allowed, such as not in water or not inside a building |
| `floor_y` | 0 | the floor height, when there's no `ground_height` |
| `fade_seconds` | 0.08 | the blink on every jump and turn |

## Teleport pads: location-based teleport

"Teleport anywhere" is the wrong fit for many games: puzzles where position matters, tight
interiors, set pieces, or a seat in a vehicle. **Pads** are fixed destinations:

```c
static const VruiTeleportPad PADS[] = {
    { { 0, 0, -4.6f }, 0.45f, true, 0 },     // center, radius, face?, yaw (0 = -Z, 90 = +X)
    { { -2, 2.6f, -9.6f }, 0.45f, true, 180 },
};
loco.pads = PADS;
loco.npads = 2;
loco.pads_only = false;   // true: pads are the only valid targets
```

- **The arc snaps.** Landing within a pad's `radius` (and within 0.5 m of its height)
  puts the player on the pad's **center**. They don't have to aim precisely: "near the
  pad" counts.
- **Facing.** With `face`, the player is also turned to look along `yaw_deg`. Use it
  when the view matters on arrival, such as looking at a puzzle or over a balcony. Leave it off when
  it doesn't: being turned can be disorienting.
- **While aiming**, every pad is outlined and the one you'd land on lights up, so players
  can see their options. Draw the pads yourself too, with an arrow for the facing, so they're
  visible before anyone aims (the yard does this).
- **Pads-only areas.** Combine pads with `valid_target`. In the yard, `valid_target` says no
  inside the yard's rectangle. Pads are checked first and are always valid, so inside the yard
  only pads work and outside it you can land anywhere. Pads are the level designer's decision,
  so `valid_target` never overrides them.

## Surfaces: platforms, stairs, lifts, edges

By default the ground is flat at `floor_y`. Give vrui a `ground_height` function and the
world gets levels:

```c
// The top of the highest walkable surface at or below p.
static float ground(Vector3 p, void *user)
{
    float g = 0;
    for (int i = 0; i < nblocks; i++)
        if (inside_xz(&blocks[i], p) && blocks[i].top <= p.y && blocks[i].top > g) g = blocks[i].top;
    return g;
}
loco.ground_height = ground;
```

This works with a list of boxes, a heightmap, or raycasts into your level. vrui asks it three
questions, and "at or below this point" is what makes all three work:

| When | vrui asks about | So that |
|---|---|---|
| the teleport arc, each step | the point the arc just left | the arc comes down **on top of** a platform instead of through it |
| standing, every frame | your head's position, `step_height` above your feet | you **step up** a stair (20 cm) or ride a lift up, but walking into a table (too tall to be a step) leaves you on the floor, since it's something you bumped into, not something you climbed |
| after letting go of a handhold | your **head** | pulling your head over a ledge and letting go puts you **on** the ledge |

Coming down:
- A drop of up to `step_height` is a step down. You follow it with no blink: stairs, or a
  lift lowering.
- Anything bigger is a **fall**. `VRUI_FALL_BLINK` (the default) is a short fade and you're
  down. `VRUI_FALL_DROP` falls for real under `gravity`, with a landing thump in both
  controllers that's stronger for harder landings.

The toolbox's LIFT platform is in its ground function. Stand on it and move the LIFT slider
at the workbench with your laser, and it carries you.

**What this is not:** a physics engine. The player isn't pushed out of walls: nothing can
stop a real body walking in a real room. What the eyes see can be handled, though; see Walls.

## Walls: your head inside things

```c
loco.solid_depth = my_solid;   // how deep a point is inside something solid (0 = in the open)
```

With `solid_depth` set:
- **Head in a wall: the view fades.** From 30% the moment your head is inside, to fully
  dark 10 cm in. Seeing the inside of a wall is disorienting and lets players peek
  through walls; the fade says "not this way", and backing out brings the view back.
- **Stick walking stops at walls, and slides along them.** A move that would take the head
  deeper into a wall is refused; if only one direction is blocked, the other part still
  happens, so walking diagonally into a wall glides along it.
- **Teleports never land you inside one.** The arc turns red when your head would end up in
  a wall at the target.

In the yard, the climbing wall and the platforms are solid (`yard_solid` in `yard.c`): walk
into the wall to see the fade. In Daddy Bug Smasher's garden, tree trunks, rocks and the Bugmaster's tower are.
`vrui_faded()` says how dark the view is this frame, if your app wants to pause or mute
while the player can't see.

## Climbing: handholds

```c
vrui_handhold(id, a, b, radius, color);   // a..b: a bar (rung, monkey bar, ledge edge); a == b: one hold
...
vrui_locomotion(&loco);                   // AFTER the frame's handholds
```

Take hold with the hand, using the same grab style as everything else (grip, closing the hand,
or either button; see [INPUT.md](INPUT.md)). The laser never climbs.
While you hold on, **the hand stays where it grabbed, and the world moves instead**:
- pull the hand down and you rise
- push it away and you move back
- pull it toward you and you move forward

Every centimeter of movement is one your arm made, which is why climbing is comfortable even
for players who can't stand smooth walking.

The promises:
- **Exact.** Pulling down 40 cm lifts you 40 cm, and the hand stays on the hold.
- **Bars can be taken anywhere along their length.** You don't have to find their middle.
- **Two hands: the newer grab leads.** When it lets go, the other hand takes over from
  **where it is now**, not from where it first grabbed, because the world has moved since.
  Otherwise every hand-over-hand move would yank you by the distance you just climbed.
- **Let go and you come down** to whatever is under your head (see Surfaces), and a
  ledge you pulled yourself over is where you land.
- **No sinking.** Pushing down on a low hold stops at the floor.
- **No stick while climbing.** Turning would swing you around your hand.

Patterns you can build from these:

| Pattern | How | In the yard |
|---|---|---|
| Climbing wall | point holds on a wall face, plus a bar along the top edge to pull over | the left part of the wall |
| Ladder | a bar per rung, 30 cm apart | the right part of the wall |
| Monkey bars | bars overhead, 40 to 45 cm apart, across a gap. Swing forward by pulling the hand toward you, reach the next bar, let go of the last one | between the two grey platforms |
| Ledge grab / mantle | a bar along the edge. Pull down, then push the hand away past your head, and let go | the wall's top edge |

`vrui_climbing()` says a hand is on a handhold. Use it to pause gravity, a stamina bar, or
footstep sounds. `vrui_airborne()` is true while coming down.

## Tuning for your game

- **Hold size.** A hold can be taken within 6 cm of its surface, the same reach as grabbable
  objects, so even a 2 cm rung is easy to find without looking. Draw it the size it
  really is.
- **Spacing.** Rungs 30 cm apart and monkey bars 40 to 45 cm apart match a comfortable reach.
  Wider spacing turns climbing into a stretch, which can be what you want.
- **Where the player lands.** Put a pad at the foot and the top of every climb. Players who
  can't or won't climb can still get there. That's an accessibility question, not a cheat.
- **Real falls** are fine for short drops in action games. Keep the blink for anything
  over a couple of meters.

## Tests

`make test T=move`: every promise above has a case in `tests/move/main.c`, and each names the
break switch that must make it fail (see [TESTING.md](TESTING.md)):

| Case | Proves | Break switch |
|---|---|---|
| climb-pull-down-rises | the world moves by exactly what the hand did | `vrui_climb_fixed_world` |
| climb-grab-bar-anywhere | bars take hold along their length | `vrui_handhold_point_only` |
| climb-switch-hands-no-jump | the other hand takes over from where it is | `vrui_climb_no_reanchor` |
| climb-let-go-falls | letting go brings you down | `vrui_loco_no_fall` |
| climb-over-ledge-lands-on-top | mantling | `vrui_loco_no_mantle` |
| head-in-wall-fades | a head inside a wall darkens the view | `vrui_loco_no_wall_fade` |
| stick-walk-stops-at-wall | stick walking doesn't pass through walls | `vrui_loco_walk_through_walls` |
| monkey-bars-hand-over-hand | hand over hand along bars | `vrui_climb_fixed_world` |
| steps-yes-tables-no | step height | `vrui_loco_step_any_height` |
| walk-off-edge-falls | edges | `vrui_loco_no_fall` |
| teleport-lands-on-platform | the arc lands on surfaces | `vrui_loco_arc_floor_only` |
| teleport-pad-snaps-and-faces | pad center and facing | `vrui_loco_no_pad_snap` |
| teleport-pads-only-rejects-elsewhere | `pads_only` | `vrui_loco_pads_only_ignored` |
