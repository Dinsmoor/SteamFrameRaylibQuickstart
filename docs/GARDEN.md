# The garden: part three of the toolbox

The toolbox's stations show one thing each. The garden puts them together in a small,
real game, the kind of thing you'd build with this quickstart. Walk to the right end of
the row, through the garden gate (or pick "Garden" from a hand menu).

**The game.** The gardener is holding a hammer. Take it from his hand and the bugs
come, crawling at you from all sides of a hilly garden. Swing the hammer at them: it's
the speed of the hammer's *head* that counts, so tapping or resting it on a bug does
nothing. Smash 8 before they bite you 10 times. Green bugs take three hits; their
callouts say how many are left.

## What it's made of, and where each piece is explained

| In the garden | The piece | Read |
|---|---|---|
| the hammer rides the gardener's hand bone, then your hand, then your belt (let go at your right hip), or nothing (drop or throw it: it tumbles) | attaching: `sfxr_pose_mul`, a bone as a parent (`anim_bone_pose`) | [ATTACHING.md](ATTACHING.md) |
| how the hammer follows whatever holds it: Snap, Lag, Spring, Heavy, Steady | the pose smoother | [SMOOTHING.md](SMOOTHING.md) |
| walking on the hills, tree trunks and the tower being solid (your head in one fades the view), teleporting | locomotion: `ground_height`, `solid_depth` | [MOVEMENT.md](MOVEMENT.md) |
| your health and score on the HUD, arrows to bugs behind you, a red flash when bitten | HUDs, `vrui_offscreen_arrow`, `vrui_tint` | [ATTACHING.md](ATTACHING.md) |
| Restart / Recall hammer / Leave / HUD style / Hammer feel on your hands | hand menus (the Menus & HUD station's choices apply) | [ATTACHING.md](ATTACHING.md) |
| a thump per hit, stronger the harder you swung | haptics | [INPUT.md](INPUT.md) |
| the chair you can knock over (walk into it, or hit it) | a box rigid body | `garden_rigidbody.c` |
| winning unlocks an achievement when Steam is there | `sfxr_steam_unlock` | [STEAM.md](STEAM.md) |
| every pick-up, hit, bite and drop is in the event log | `sfxr_event` | [TESTING.md](TESTING.md) |

## The files

- `garden.c`: the game: the round, bugs, the hammer's parents, hits, the board, menus,
  the HUD, the gate.
- `garden_world.c`: the level as a table, terrain heights, the sun shader, colliders,
  loose props.
- `garden_rigidbody.c`: a box rigid body: tips, bounces, settles, sleeps.
- `garden_anim.c`: named animation clips with cross-fades, and a bone's world pose.
- `resources/garden/`: the models (glTF, made in Blender). A package carries them
  (`resources/` is copied next to the app).

The engine parts came from an earlier flat-screen raylib game's 3D mode, along with its
level and models. Its units were about 1.4 to the meter, so everything is scaled by
`GARDEN_SCALE` (0.7) on the way in.

### Things worth knowing, learned porting it

- **raylib 6 keeps bone poses in model space.** `model.currentPose[i]` is already the
  whole chain's result (the skinning matrix is `inverse(bind) * pose`). Walking up the
  parents again, as older code did, applies them twice and the hammer floats off.
- **Blender's front is glTF's -Z.** A character built facing +Y in Blender faces -Z
  in the game, so a character facing the player needs a half turn.
- **Terrain as a height grid.** The terrain mesh is raycast once at load on a 96 x 96
  grid. After that, "how high is the ground here" is a cheap lookup for every bug, the
  teleport arc and your feet. It can't do caves or overhangs, which a garden doesn't need.
- **A custom shader works in stereo** as long as it uses raylib's `mvp` uniform, which
  rlgl sets per eye.
- **Speed from what moved in *your room*.** The hammer's swing speed is measured from
  where its head was last frame in tracking space. A teleport or stick walking isn't
  a swing, and a wrist flick is.

## Tests

`make test T=garden` compiles the toolbox's own garden code (the `tests/garden/toolbox_*.c`
files just `#include` it) and checks:

| Case | Proves | Break switch |
|---|---|---|
| swing-smashes-with-the-head | a swing whose head goes through a bug smashes it, with the hand far away | `garden_hit_at_hand` |
| resting-on-a-bug-does-nothing | it takes a swing, not a touch | `garden_hit_any_speed` |
| bug-bites-once-a-second | bites are paced | `garden_bite_every_frame` |
| hammer-rides-the-belt | the belt goes where you go | `garden_belt_world_space` |
