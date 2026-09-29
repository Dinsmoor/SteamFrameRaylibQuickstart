# Assets: models made by scripts, in Blender, for raylib

Models here are **generated**: each is a small Python file that builds the
thing in an empty Blender scene, and `make assets` turns it into a GLB that
raylib loads. The script is the source (diffable, regenerable, something an
agent can edit from a sentence); the GLB is committed too, so a checkout
without Docker still has every model.

```
examples/toolbox/assets/bug.py        the generator (build() with sfq_assets helpers)
tools/assets/sfq_assets.py            the helpers: primitives, materials, rigs, actions
tools/assets/build.py                 runs a generator in Blender, exports, checks the file
docker/blender.Dockerfile             Blender 5.2 headless (Alpine's aarch64 package) + the MCP add-on
scripts/blender.sh                    runs Blender in it; `mcp` starts the bridge
examples/toolbox/resources/models/    the output: bug.glb
examples/viewer/                      look at a GLB the way the game draws it
```

Why headless scripts and not a modeling session: practitioners who generate
assets with an agent converge on "MCP to look, scripts to build". A live
Blender session drifts from what's on disk and can't be re-run; a script can,
and a change to it is a diff. The Blender MCP server (Blender Lab's own) is
wired in for inspection and API lookup, not as the source of truth.

## Making one

```
make assets                        every examples|apps/<app>/assets/*.py that changed
make view MODEL=<glb>              the viewer: turntable, animations, a 1 m bar, lit
make view-shot MODEL=<glb> ...     the same, headless, to shots/viewer-sim.png
make blender-image                 (re)build the container; make assets does it on first use
```

A generator (see `bug.py`, the worked example: a rigged, walking bug built
from a 2D sprite):

```python
from sfq_assets import Action, armature, group, join, material, rgb, skin, sphere

def build():
    pink = material("pink", rgb(247, 166, 242))          # raylib's 0..255 values, as they are
    body = group(sphere("body", 0.36, at=(0, 0, 0.42), scale=(1, 1.35, 0.85), mat=pink), "body")
    ...
    thing = join("thing", [body, head, legs...])         # one mesh, a vertex group per bone
    arm = armature("rig", [("body", (0, -0.35, 0.42), (0, 0.3, 0.42), None), ...])
    skin(thing, arm)
    with Action(arm, "walk", 24) as a:                    # 30 fps; keys at 1 .. 25 for a loop
        a.turn("leg_fl", 1, 'Z', 22); a.turn("leg_fl", 13, 'Z', -22); a.turn("leg_fl", 25, 'Z', 22)
    return {"tris": 2500, "bones": 16}                    # the budget build.py checks
```

Conventions (the helpers' docstring has the details):
- Meters. Model Z-up with the ground at z = 0; the exporter makes it Y-up.
  Face +Y in Blender: that's -Z, "forward", in sfxr's world.
- **Colors are not linearized.** raylib reads glTF's base color factor straight
  into a `Color` and draws gamma-space colors raw (CLAUDE.md, "Color"), so a
  linearized color would come out dark. `rgb(r, g, b)` stores what you'd hand
  raylib.
- One armature, one skin (parts are joined into one mesh with a vertex group
  per bone), deform bones only, at most 4 influences a vertex, no shape keys.
- Each `Action` goes on its own NLA track; that's how the exporter finds them
  all (`export_animation_mode='ACTIONS'`). Loops repeat the first key at
  `length + 1`; `Action.turn()` rotates about a **world** axis so you don't
  have to think in bone space.

`build.py` exports with the settings raylib wants (Y-up, applied modifiers,
sampled animations, no Draco, no morphs) and then reads the GLB back and
checks: one skin, triangles only, no morph targets, no required extensions,
the bone and triangle budgets. It prints one line:

```
ASSET {"ok": true, "tris": 1208, "meshes": 1, "primitives": 5, "materials": 5, "bones": 8, "animations": ["idle", "walk"], ...}
```

## What raylib 6.0 loads (rmodels.c)

- `.glb`/`.gltf` with embedded or external textures; PBR metallic-roughness
  (base color, metallic-roughness, normal, occlusion, emissive); vertex colors
  (`COLOR_0`); every primitive becomes a `Mesh`, node transforms baked in.
- Animation: **one skin per file** (extras are skipped), TRS channels only
  (morph target weights ignored), every sampler resampled to **60 keyframes a
  second**, `JOINTS_0`/`WEIGHTS_0` only (4 influences). Many animations per
  file are fine: `LoadModelAnimations()`, `anim.name`.
- No Draco, no `KHR_texture_transform`.
- CPU skinning by default: `UpdateModelAnimation(model, anim, frame)` moves
  the vertices and re-uploads them, and any shader draws the result (the
  toolbox's world shader too). GPU skinning needs raylib built with
  `SUPPORT_GPU_SKINNING` and a skinning shader (`boneMatrices[128]`); do that
  when there are dozens of animated things on screen, not before.
- In code: `frame += dt * 60`, wrap at `keyframeCount`; the model faces -Z, so
  `DrawModelEx(model, pos, (Vector3){0,1,0}, yaw_deg, scale, WHITE)`.

## The viewer

`examples/viewer`: the model on a turntable 2 m ahead, a simple sun so shapes
read, its animations played in turn and named over it, a 1 m bar for scale.
Trigger: next animation; grip: stop the turntable; stick: size. For
screenshots: `SFQ_ANIM=<n>`, `SFQ_TURN=0`, `SFQ_YAW=<deg>` (180 faces you),
`SHOT_FRAME=<n>` picks the moment, and the simulator's `SFXR_SIM_POS`/`_LOOK`
place the camera. Look at the PNG; that's the whole point of it.

## Blender in a container

Blender publishes no Linux ARM64 build and the build machine is ARM64, so
`docker/blender.Dockerfile` takes Alpine edge's `blender` package (the
current release for aarch64) plus the Python modules it forgets (numpy,
requests, cattrs) and Blender Lab's **MCP add-on**, enabled with online access
so its bridge can run. `scripts/blender.sh` runs Blender in it as you, with
the repo at `/w`. `scripts/blender.sh mcp` keeps a background Blender up with
the bridge on `127.0.0.1:9876`.

The MCP side, `blender-mcp` (Blender Lab; `uv tool install
'git+https://projects.blender.org/lab/blender_mcp.git#subdirectory=mcp'`),
is what `.mcp.json` starts (`scripts/blender-mcp.sh`, which also starts the
bridge). Its tools: `execute_blender_code`, scene and object summaries,
`search_api_docs` / `get_python_api_docs` / `search_manual_docs` (bundled,
current docs: Blender 5 changed enough API that an agent writing bpy from
memory gets it wrong), renders to a path. The bridge runs headless, so window
screenshots don't apply; look at models with the viewer instead. Note the
add-on's own warning: it runs whatever Python it's sent, without guards.

## Generators that don't run here

AI mesh generators (TRELLIS, Hunyuan3D) produce dense scans that need
decimation and rebaking before a mobile GPU should see them; for the plain,
low-poly look this quickstart wants, procedural modeling in a script beats
generate-then-retopo. Tileable textures from a diffusion model (ComfyUI) fit
well; put the PNG next to the generator and reference it from a material.
