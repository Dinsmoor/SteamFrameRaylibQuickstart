---
name: asset
description: Make or change a 3D model (mesh, materials, rig, animations) for a Steam Frame app with the Blender asset pipeline (docs/ASSETS.md): a Python generator per asset, built headless into a GLB that raylib loads. Use for "make a model of", "a variant of", "animate", "retexture".
---

An asset is `examples|apps/<app>/assets/<name>.py` with a `build()` that makes
the thing in an empty Blender scene using `tools/assets/sfq_assets.py`, and
`make assets` turns it into `<app>/resources/models/<name>.glb` (Blender runs
headless in a container: `scripts/blender.sh`; the first run builds the image).
The `.py` is the source and the `.glb` is committed too. Read
`examples/toolbox/assets/bug.py` first: it's the worked example (a rigged,
walking bug from a 2D sprite).

## Write the generator

- **Helpers** (`sfq_assets`): `material(name, rgb(r,g,b))`, `sphere`,
  `cylinder(a, b, r)`, `box`, `tube(points, r)`, `arc(...)`, `join`,
  `group(obj, bone)`, `armature(name, [(bone, head, tail, parent)])`,
  `skin(obj, arm)`, `Action(arm, name, frames)` with `.turn(bone, frame, axis,
  deg)`, `.rot`, `.move`. Read the file's docstring for the conventions.
- **Units and axes:** meters; model Z-up with the ground at z = 0 and the front
  facing +Y (that's -Z, "forward", in the game). Origin on the ground under it.
- **Colors** are raylib's 0..255 values (`rgb(247,166,242)`), not linearized.
- **Rig:** every part is `group()`ed to a bone, then all parts `join()`ed into
  one mesh, then `skin()`ed: one armature, one skin, ≤128 bones, ≤4 influences,
  no shape keys (raylib's loader). Animations are `Action`s (each lands on its
  own NLA track so the exporter finds them all); loops repeat their first key
  at frame `length + 1`.
- **Budget:** low-poly and stylized (the Frame is a mobile GPU). Return
  `{"tris": N, "bones": N}` to set the budget the build checks.
- A **variant** of an existing asset: a new file that imports the original's
  module-level parts, or a parameter on the original with a second output
  file. Keep the original's look unless told otherwise.

## Build and look

1. `make assets` prints one `ASSET {...}` line per file (tris, bones, animation
   names). Failures point at `build/assets/<name>.py.log`.
2. `make view-shot MODEL=<glb> SFQ_ANIM=<n> SFQ_YAW=150 SFXR_SIM_POS="0.4,1.3,0.2" SFXR_SIM_LOOK="0,-12" SHOT_FRAME=52`
   renders it lit in the simulator to `shots/viewer-sim.png`: **look at the
   PNG** (Read). `SFQ_YAW=180` faces you, `0` faces away; `SHOT_FRAME` picks a
   moment of the animation (60 frames a second). Take two angles for anything
   with a rig.
3. In the app: `LoadModel`, `LoadModelAnimations`, `UpdateModelAnimation(model,
   anim, frame)` with `frame += dt * 60` (raylib resamples to 60 keys/s); give
   its materials `gfx_model_shader()` in the toolbox so it's lit like the world.
   Draw with `DrawModelEx`; the model faces -Z, so yaw it toward its heading.
4. Blender MCP (the `blender` server: `execute_blender_code`,
   `search_api_docs`, `get_python_api_docs`) is for checking the bpy API or
   inspecting a scene; the bridge runs headless, so no viewport screenshots.
   The generator script stays the source of truth: never leave a change only
   in Blender's memory.
