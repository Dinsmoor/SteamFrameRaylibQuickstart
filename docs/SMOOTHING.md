# Smoothing: how things follow

Anything that follows something else, such as a weapon in your hand, a tool on a
character's bone, a HUD or a camera, needs a decision: does it stick exactly, or
does it trail, swing or resist? That decision is most of how the thing *feels*.
vrui has a small kit for it (`vrui.h` section 11, `vrui/src/vrui_smooth.c`). The
toolbox's **Smoothing** station lets you try every mode by hand, and the garden's
hammer uses whichever you pick.

## Frame-rate independence

The usual `x = lerp(x, target, 0.1)` every frame is tied to the frame rate: at 144 Hz
it catches up twice as fast as at 72 Hz, and the Frame runs at 72, 90, 120 or 144.
Every tool here takes the frame's `dt` and is written so the same settings look the
same at any rate.

## The primitives

| Tool | Call | Use it for |
|---|---|---|
| exponential damping | `vrui_damp`, `vrui_damp3`, `vrui_dampq` (float, vector, rotation) | "follow, a little behind". `halflife`: seconds to close half the gap |
| spring | `vrui_spring`, `vrui_spring3`, `vrui_springq` | things that overshoot and settle. `frequency` (Hz): how fast it swings. `damping`: 1 settles fastest without overshoot, below 1 wobbles |
| speed limits | `vrui_move_toward3`, `vrui_turn_toward` | a maximum distance or angle per frame: heavy things, turrets |
| easing curves | `vrui_ease(VRUI_EASE_..., t)` | tweens: a door swinging shut, a menu popping in. linear, smooth, in, out, in-out, back (overshoots), elastic, bounce |

## The pose smoother: five ways to follow

```c
static VruiSmooth sword_smooth;                              // one per thing, kept between frames
VruiSmoothSpec feel = vrui_smooth_spec(VRUI_SMOOTH_HEAVY);   // tested defaults; tweak any field
SfxrPose shown = vrui_smooth_pose(&sword_smooth, hand->grip, &feel);
// draw the sword at `shown`, and do its hits from `shown` too
```

| Mode | What it does | Feels like | Setting (default) |
|---|---|---|---|
| **Snap** | exactly on the target | precise and weightless: a tool, a pointer | |
| **Lag** | exponential damping of position and rotation | a touch of weight, softened jitter | `halflife` (0.05 s) |
| **Spring** | a spring on position and rotation | floppy, cartoony, a flail | `frequency` (3 Hz), `damping` (0.35) |
| **Heavy** | a firm, critically damped pull with a top speed and turn rate | a real weight: a flick can't whip it round, a committed swing carries it | `max_speed` (3 m/s), `max_turn_deg` (300 deg/s) |
| **Steady** | a jitter filter (the "1 euro filter"): smooths hard when nearly still, not at all when moving fast | steady aim with no felt lag: pointing, drawing, a rifle's sight | `min_cutoff` (1 Hz), `beta` (6) |

**Hits come from what you see.** A smoothed weapon should do its hits (and its
haptics) from the smoothed pose, not the hand. Otherwise players hit things the blade
visibly missed. The garden does that: in Heavy mode a lazy flick doesn't smash a
bug, a real swing does.

**Riding the player.** A held thing is attached to your hand, and your hand moves when
the *rig* moves: a teleport, stick walking, a snap turn. If the smoother only saw world
positions, every teleport would make the sword lag across the world after you.
`with_player` (the default) keeps the smoother's memory in the rig's space, so only
your hand's own motion is smoothed. Turn it off for things attached to the *world*,
such as a character's hand bone, which don't move when you do.

**Changing parents.** Keep one `VruiSmooth` per thing, not per parent. When the
hammer in Daddy Bug Smasher goes from its stump to your hand, the smoother glides it over
instead of popping. Call `vrui_smooth_reset` when you *want* a jump.

## Where to see it

- **Smoothing station** (on the row, between Attach & label and Hinges & cords): pick
  up the sword and wave it. Five ghost swords copy it, one per mode. The panel tunes each
  mode's setting, and the rails behind show the easing curves.
- **Daddy Bug Smasher**: the board by the spawn point (or the hand menus' "Hammer feel")
  picks how the hammer follows. It uses the Smoothing station's settings.
- **Tests** (`tests/attach`): `smooth-lag-halflife`, `smooth-spring-overshoots`,
  `smooth-heavy-speed-limited`, `smooth-steady-quiets-tremble`, and
  `smooth-rides-the-rig` (break switch `vrui_smooth_world_space`).
