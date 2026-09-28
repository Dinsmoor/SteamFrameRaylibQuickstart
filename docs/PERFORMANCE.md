# Performance on the Steam Frame: frame rate, foveation, reprojection

What makes a Frame app fast enough, what this quickstart does about it today, and what
is still unknown and how to find out. Short version: hold the frame rate, send the
compositor depth, and measure. Foveated rendering is the big prize, and for an OpenGL
app like ours it's still an open question, not a switch.

## The budget

The Frame renders 1728 x 1728 per eye by default (the panels are 2160 x 2160), at
**72 Hz**. That's 13.9 ms for everything, both eyes. It can also run at 90, 120 and 144 Hz.
A missed frame is felt before it's seen, so the frame rate comes first and resolution second.

- **Where we stand:** the toolbox runs at 72 fps on the Frame through the GL path, with room to
  spare (the heartbeat line in the log). In our sessions SteamVR offered **only 72 Hz**,
  even with the headset on and `preferMinRefreshRate: 90` in `vrpreferences.json`. Finding out why is on the
  list below.
- **Valve's bar:** the SteamVR **Performance Assessment** overlay grades an app against the
  frame time for its refresh rate, over a rolling 32-frame window. Turn it on in the headset under Dashboard →
  Advanced Settings → Developer → "Draw Performance Assessment in Headset".

## What this quickstart does today

| Feature | Status | Why |
|---|---|---|
| **Depth submission** (`XR_KHR_composition_layer_depth`) | **on** in the GL path. `SFXR_DEPTH=0` turns it off | the compositor reprojects late or missed frames using real per-pixel depth, not a flat image at one distance. Near objects (hands, controls) stop swimming. Valve recommends it. It costs nothing to render: raylib's depth test already writes a depth buffer, and we write it into the runtime's image instead of a private one. Verified under Monado. On the Frame, the log says "depth submission on" |
| `vrpreferences.json` | shipped with every package (`templates/vrpreferences.json`) | per-app defaults the player can override: `preferResolution`, `preferMinRefreshRate`, `preferHalfFramerate`, `preferMotionSmoothingMode`. There is no foveation key |
| Refresh-rate switch | on the Headset panel | try 90 and 120 Hz yourself |
| Measuring | the heartbeat line (fps, **CPU ms per frame**) plus `SFXR_PERF_LOG=1` (the runtime's own counters) | see below |

The Vulkan interop path (`SFXR_BACKEND=vk`) doesn't submit depth yet. It copies color
into Vulkan images and would need the same copy for depth.

## Foveated rendering: what's known

Foveation renders the edges of the view, where the lens blurs anyway and the eye isn't
looking, at lower resolution. On a tile-based GPU like the Frame's Adreno 750, done in the
driver as a **fragment density map** (FDM), it can save a large share of the GPU's pixel work.
With eye tracking, the sharp region follows your gaze.

**The documented way is Vulkan only.** The Frame's runtime offers
`XR_FB_foveation` and `XR_META_foveation_eye_tracked`. The app asks for a foveation profile,
and the runtime hands back a density-map image for each swapchain image. The app then attaches
it to the Vulkan render pass that draws that image. Valve wrote Turnip's FDM support and a
stereo extension for it, `VK_VALVE_fragment_density_map_layered`. Every step is a Vulkan
object, so there is no OpenGL entry point to any of it.

**OpenGL has no way in.** raylib 6.0 is OpenGL only. On the Frame, OpenGL is **Zink** (Mesa's
GL-on-Vulkan) running on **Turnip** (Mesa's Vulkan driver for Adreno). No GL extension
exposes Vulkan's density maps. Qualcomm's own GL foveation extensions (`QCOM_*_foveated`)
live in their proprietary driver, not in Mesa.

**But something is already happening underneath us.** On the headset, SteamVR launches
every app with:

```
VK_INSTANCE_LAYERS=VK_LAYER_VALVE_rpo:VK_LAYER_VALVE_fdm_injection
VRCOMPOSITOR_TU_DEBUG=sysmem,preempt
```

Our own diagnostic log prints `fdm_injection: XR layer loaded` when the OpenXR loader starts.
So Valve's `fdm_injection` is **both a Vulkan layer and an OpenXR layer, loaded into our
process**. Zink turns our GL into real Vulkan render passes, and a Vulkan layer only sees
Vulkan calls. It can't know the calls came from GL. So it's plausible that Valve already
injects a density map into the passes that draw our swapchain images, and that our GL app
is foveated without doing anything. **No public document describes these layers.** This
is inference until measured.

What that means in practice:
- If the injection works through Zink, foveation is already on and the job is to
  measure it and keep out of its way. For example, render straight into the swapchain image,
  which we do, and don't add offscreen passes the layer can't see.
- If it doesn't work through Zink, real foveation needs a native Vulkan renderer. raylib 6.0
  has none, but the swapchain and interop plumbing in `sfxr_xr_vk.c` is the start of one.

## How to find out: the experiments

`scripts/frame-perf.sh toolbox` runs these on the headset, one launch each. **Wear the headset
while it runs.** It prints a table and saves the full logs to `local-data/perf-<time>/`:

| Variant | Setting | What it answers |
|---|---|---|
| `base` | as shipped | the baseline: fps, CPU ms, the runtime's GPU time |
| `nodepth` | `SFXR_DEPTH=0` | what depth submission costs, if anything |
| `nolayers` | `VK_INSTANCE_LAYERS=` (empty) | **the key one:** without Valve's layers, does GPU time go up? If it does, the injection was foveating us |
| `tufdm` | `TU_DEBUG=fdm` | Turnip's FDM debug option. It's listed in Mesa's docs without a description, so this finds out what it changes |
| `vk` | `SFXR_BACKEND=vk` | the Vulkan interop path's cost against plain GL |

It also saves `layers.txt`, the headset's implicit Vulkan and OpenXR layer manifests. They
say what `fdm_injection` and `rpo` are (library, description) and which environment variable
disables each. That is the clean way to switch one off, rather than emptying
`VK_INSTANCE_LAYERS`.

Beyond timing:
- **Look for it.** Foveation is visible if you look for it. Put fine detail (a text panel) at the edge of your view,
  then compare `base` and `nolayers`. A blur toward the edges that comes and goes with the layers means the injection is live.
- **Capture a frame.** `ENABLE_VULKAN_RENDERDOC_CAPTURE=1` (Valve's Frame debugging docs)
  and RenderDoc show whether the render pass that writes our swapchain image has a density-map
  attachment. That's the definitive answer.
- **Read the runtime's logs.** The SteamVR logs on the headset (`frame.sh logs toolbox`): grep for `fdm`,
  `foveat`, `density`, `rpo`, and for "Loading app-provided preferences" (it confirms
  `vrpreferences.json` was read).

## Next steps, in order

1. Run `scripts/frame-perf.sh toolbox` in a headset session and read `layers.txt`.
2. Find out why only 72 Hz is offered. Check `vrpreferences.json` loaded (the log line
   above), and try the Headset panel's refresh-rate switch while wearing it.
3. **Motion vectors** (`XR_EXT_frame_synthesis`), if the runtime offers them, so the
   compositor can synthesize frames, not just reproject them. They need a velocity pass in raylib's
   shaders, which is a bigger job than depth.
4. If foveation turns out not to reach GL, work out the Vulkan renderer route: foveation
   via `XR_FB_foveation`, with the density map on the pass that draws each eye.
