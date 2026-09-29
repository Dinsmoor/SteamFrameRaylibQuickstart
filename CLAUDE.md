# CLAUDE.md — Steam Frame + raylib + C quickstart

**Start here.** This repo is a **quickstart**, not a game: a public, portable starting
point that teaches how to build native **Steam Frame** VR apps in **C with raylib 6.0**,
and hands you the pieces to do it.

- **`sfxr/`**: the headset layer. OpenXR session, swapchains, input, player rig,
  desktop simulator, input record/replay.
- **`vrui/`**: an immediate-mode VR interaction toolkit (panels with widgets, physical
  props, locomotion).
- **`examples/`**: `hello` (minimal) and `toolbox`. The toolbox is the **reference
  implementation**: every vrui item, tested the way `docs/TESTING.md` prescribes, so it
  doubles as the worked example people copy.
- **Tooling**:
  - host builds, and Frame builds inside Valve's Steam Runtime 4.0 arm64 SDK
  - headset control over devkit pairing (`scripts/frame.sh`)
  - the desktop simulator
  - the Monado and "fake Frame" containers
  - record/replay regression tests

**Long-term purpose (keep in mind with every change):**
- **Teach.** Docs are plain speech and explain *why* as well as *how*. Every hard-won
  lesson goes into Gotchas.
- **Be reusable.** sfxr and vrui are libraries for other people's apps, and the testing
  approach is a pattern they can adopt (`docs/TESTING.md` §13).
- **Stay public-safe.** No personal IPs, hostnames or private project names.
- **Keep the headset loop remote-first.** Everything goes through `scripts/frame.sh` and
  devkit pairing; nobody should need to take the headset on and off repeatedly.

**Where to read next:** Part 1 below is the platform research. Part 2 is how the repo
works. The last section, **"Current state and next steps"**, is the handoff: what's done,
what's broken, and what's next.

---

## Part 1: Steam Frame platform notes (researched Sept 2026)

### Hardware / OS
- Snapdragon 8 Gen 3 (ARM64), **Adreno 750** GPU, 16 GB LPDDR5X. The GPU is about
  25–30% faster than a Quest 3's, and Valve does not underclock it.
- 2160×2160 LCD per eye, pancake lenses, 72–144 Hz, about 110° FOV. Four mono tracking
  cameras and two eye-tracking cameras.
- **SteamOS** (Arch-based Linux). Graphics come from Mesa: the **Turnip** Vulkan driver,
  with OpenGL through Freedreno or Zink.
- Execution models:
  - native **Linux ARM64** (what we build)
  - **Android APKs** (the "Lepton" container)
  - Windows via Proton
  - x86 via the **FEX** translator
- Valve recommends custom engines target **Linux ARM64 or Android 10**, using **OpenXR**.

### Developer workflow (Valve docs)
- On the headset, go to Steam Settings → System → **Enable Developer Mode**. This enables
  SSH, ADB and RDP. (Valve's docs also mention a user password; this repo doesn't use
  it, since everything goes through devkit pairing.) Optionally
  a Hostname.
- **Access rule: talk to the headset ONLY through the devkit pairing** (`make frame-pair`,
  then `scripts/frame.sh ...`, run on the build machine that holds the paired key
  `~/.ssh/sfq_frame`). Never use raw `ssh steamos@...` or passwords; a stray interactive
  ssh pops a password prompt on the user's desktop. For one-off diagnostics use
  `scripts/frame.sh exec '<cmd>'`. The login user is `steamos`. Logs are in
  `~/.local/share/Steam/logs` (alias `cdl`); for crashes use `coredumpctl debug <pid>`.
- **SteamOS Devkit Client** "Title Upload" deploys builds. Valve's docs page says to pick
  "Steam Linux Runtime 3.0 ARM64 (Sniper)". However, the client's own code (v0.20260925)
  only offers **"Steam Linux Runtime 4.0 ARM64"** (`compat_tool=SteamLinuxRuntime_4-arm64`)
  for ARM. We follow the code: `make frame` builds in the **steamrt4 arm64 SDK**.
  `make frame FRAME_SDK=registry.gitlab.steamos.cloud/steamrt/sniper/sdk/arm64:latest`
  plus `FRAME_RUNTIME=SteamLinuxRuntime_sniper` switch back.
- How the Devkit Client works under the hood (from its MIT source):
  1. It rsyncs `devkit-utils/` to `~/devkit-utils`.
  2. `steamos-prepare-upload --gameid X` returns `~/devkit-game/X`, and the game is
     rsynced there.
  3. `steam-client-create-shortcut --parms <json>` records argv/env/settings in
     `~/devkit-game/X-*.json` and asks the running Steam client, via `~/.steam/steam.pipe`
     (`devkit-1 steam://devkit-1/<token>/create-shortcut?...`), to add
     "Devkit Game: X" to the library.
  4. `steam-devkit-rpc run-game gameid=X` launches it the same way.

  `scripts/frame.sh` replays exactly this. The Steam client must be running.
- **Pairing protocol** (`frame.sh pair`, from the client's `register()`):
  - The headset runs a devkit HTTP service on **:32000**. `GET /properties.json` returns
    `{"login":"steamos","devkit1":["devkit-1"],...}`.
  - While the headset shows "Pairing..." (Developer → **Pair devkit**), `POST /register`
    with body `"<ssh pubkey> 900b919520e4cf601998a71eec318fec\n"` makes the wearer confirm.
    The reply is `Registered` and the key is authorized.
  - **The key must be RSA-2048 with comment `devkit-client:<user>@<host>`, like Valve's.**
    An ed25519 key got `{"error": "Failed to write the ssh key"}`. Our key is
    `~/.ssh/sfq_frame`.
  - The "Pairing..." screen stays up after success; that's cosmetic.
  - mDNS (`_steamos-devkit._tcp`) was **not** seen from our LAN, so `discover()` falls back
    to scanning the /24 for port 32000.
- Steamworks SDK ≥ 1.63 ships Linux ARM64 libraries. Steam API integration is not
  wired into this repo yet.
- Verified bar for VR: **≥ 72 fps at 1728×1728 per eye** in normal play. UI text must be
  legible on the built-in display, and the default controller config must work with
  Frame controllers.
- SteamVR has a performance overlay: Dashboard → Advanced Settings: Show →
  Developer → **Draw Performance Assessment in Headset**.
- **`vrpreferences.json`** goes at the package root. Root key `steam_frame`; keys
  `preferResolution` (eye width), `preferMinRefreshRate`, `preferHalfFramerate`,
  `preferMotionSmoothingMode` ("off" | "on" | "alwaysOn"). No trailing commas.
  Template: `templates/vrpreferences.json`. `package.sh` copies it.

### Rendering / graphics API: the key constraint
- **raylib 6.0 is OpenGL-only upstream** (GL 1.1/2.1/3.3/4.3, ES2/ES3, plus the new
  `rlsw` software renderer). A Vulkan backend (`rlvk.h`, by rygo6) was announced in
  2026 but is **not in upstream raylib**, and the fork is no longer public. Treat Vulkan
  raylib as "maybe later". sfxr isolates the graphics binding so it can be swapped in.
- OpenXR needs frames through a graphics binding. **Verified on a real Frame
  (SteamVR/OpenXR 2.17.10): `XR_KHR_opengl_enable` is available and works.** The Frame's
  GL is Mesa **Zink over Turnip** (GL 4.3 core, "zink Vulkan 1.4 (Turnip Adreno 750)").
  The Frame-specific rendering extensions (`XR_FB_foveation_vulkan`,
  `XR_META_vulkan_swapchain_create_info`) are Vulkan-only. sfxr supports both:
  1. **`gl`**: `XR_KHR_opengl_enable` (GLX). raylib draws straight into the swapchain.
  2. **`vk`**: `XR_KHR_vulkan_enable2`. raylib draws into a GL texture that shares
     Vulkan memory (`GL_EXT_memory_object_fd`); each frame it is blitted and flipped into
     the runtime's Vulkan swapchain. Requires GL and Vulkan on the same GPU (checked by
     UUID). On the Frame both are Mesa on the Adreno.
  Both paths are verified under Monado. **`gl` is verified on the real Frame:** hello ran
  at a locked 72 fps (1728×1728 per eye, MSAA 4x), session FOCUSED, with the Frame
  controller profile bound. `vk` hasn't been tried on hardware yet
  (`scripts/frame.sh run hello 20 SFXR_BACKEND=vk`).
- **Runtime extensions the Frame reported** (`make frame-probe`; 48 total):
  - `XR_KHR_opengl_enable`, `opengl_es_enable`, `vulkan_enable(2)`, `XR_MNDX_egl_enable`,
    `XR_MND_headless`
  - `XR_VALVE_frame_controller_interaction`, `XR_EXT_eye_gaze_interaction`
  - `XR_EXT_hand_tracking` (plus `hand_interaction`, `palm_pose`)
  - `XR_EXT_user_presence`, `XR_FB_display_refresh_rate`, `XR_EXT_local_floor`
  - `XR_KHR_composition_layer_depth`, `XR_KHR_visibility_mask`
  - `XR_FB_foveation(_vulkan)`, `XR_META_foveation_eye_tracked`, `XR_FB_space_warp`,
    `XR_EXT_frame_synthesis`, `XR_META_performance_metrics`
  - Blend modes: opaque and alpha_blend. Recommended eye size 1728×1728.
- **Game environment on the Frame:**
  - Apps launched through Steam run under gamescope with an X11 display (`DISPLAY=:1`
    for the app, `:0` for Steam) and `XDG_RUNTIME_DIR=/run/user/1000`.
  - The OpenXR active runtime is `/opt/steamvr/steamxr_linuxarm64.json`.
  - **Not worn → `SYNCHRONIZED`, `should_render=0`.** The loop keeps running at 72 Hz but
    nothing is drawn, so screenshots need someone wearing the headset (or its presence
    sensor covered). Worn → `VISIBLE`/`FOCUSED`, `should_render=1`.
- Foveated rendering requires Vulkan swapchains, so it is not available to raylib/GL today.
  **Foveated streaming** (PC VR over Wi-Fi) is automatic and needs nothing from us.

### Input
- The Frame controllers are a split gamepad plus VR. **Verified input list** (the
  runtime's own answer, `scripts/frame.sh probe --paths`, 2026-09-28):
  - both hands: trigger, grip ("squeeze"), thumbstick, bumper (also reachable as
    `shoulder`). **Every one of them has click and capacitive touch**; trigger and grip
    also have an analog value, and the stick has x/y.
  - **right hand**: A, B, X, Y and Menu (click + touch each)
  - **left hand**: D-pad up/down/left/right and View (click + touch each)
  - `system` (click + touch) exists too, but it's Steam's dashboard button. Don't bind it.
  - poses: `grip`, `aim`, plus `poke_ext` (controller tip), `pinch_ext` and `palm_ext`
    (enable `XR_EXT_hand_interaction` and `XR_EXT_palm_pose`)
  - haptics: `output/haptic` (amplitude, duration, frequency)
  - **no** finger curl or force values on the controller profile
- OpenXR profile `/interaction_profiles/valve/frame_controller_valve`, which needs
  extension **`XR_VALVE_frame_controller_interaction`**. Apps that don't bind it see the
  controllers as **Oculus Touch**, and SteamVR shows an emulation notice. **On a Frame,
  sfxr offers only the Frame profile, the bare-hand profile and eye gaze.** Touch, Index
  and simple-controller bindings are offered only on other runtimes (Monado in tests),
  or with `SFXR_FALLBACK_BINDINGS=1`.
- **Bare hands:** `/interaction_profiles/ext/hand_interaction_ext` is accepted, with
  `pinch_ext/value`, `grasp_ext/value`, `aim_activate_ext/value` (each with `ready_ext`)
  and all five poses. sfxr maps pinch → trigger and grasp → grip, and sets
  `SfxrHand.source = SFXR_SOURCE_HAND`. vrui then works with bare hands unchanged.
  **Not yet tried on the headset** (put the controllers down during a session).
  `XR_EXT_hand_tracking` (joints) is present, but sfxr doesn't use it yet.
- **Headset and system signals sfxr exposes** (all confirmed present on the Frame,
  2026-09-28; the API is in `sfxr.h` under "Hand joints" and "Headset and system
  signals"):

  | Signal | Extension | API |
  |---|---|---|
  | worn / not worn | `XR_EXT_user_presence` | `sfxr_user_present()`, `sfxr_user_presence_known()`. The event arrives at session start, so it's known at once. |
  | refresh rate | `XR_FB_display_refresh_rate` | `sfxr_refresh_rate()`, `sfxr_refresh_rates()`, `sfxr_set_refresh_rate()`. Unworn on 2026-09-28, SteamVR offered **only 72 Hz**; check again when worn. |
  | alpha-blend compositing (passthrough if the headset shows it) | blend modes | `sfxr_set_blend_mode(SFXR_BLEND_ALPHA)`, then clear with alpha 0 |
  | hand joints (26 per hand), from cameras or inferred while holding a controller | `XR_EXT_hand_tracking` + `_data_source` | `sfxr_hand_joints(hand)` |
  | controller batteries | `XR_EXT_interaction_profile_battery_state_display` | `sfxr_battery(hand)`, polled once a second |
  | runtime performance counters | `XR_META_performance_metrics` | `sfxr_perf_enable()`, `sfxr_perf_count/name/value()` |
  | the runtime's controller 3D models | `XR_EXT_render_model` + `XR_EXT_interaction_render_model` (+ `XR_EXT_uuid`) | `sfxr_controller_model(hand, &pose)`. Loaded via a temporary `.glb` because raylib loads models from files. Static: buttons don't animate. |

  Presence, refresh rate, batteries and joints are recorded (format v3), so replays see
  them.
- **Still unused:**
  - `XR_KHR_visibility_mask`, a rendering optimization
  - `XR_EXT_hand_joints_motion_range`
  - `XR_VALVE_analog_threshold` (runtime-side thresholds; sfxr does its own)
  - `XR_FB_space_warp`, `XR_EXT_frame_synthesis` and foveation, which need Vulkan
    swapchains
  - audio (speakers and mic) is plain Linux audio: raylib's audio module plays sound;
    raylib has no mic capture
- **Eye tracking** via `XR_EXT_eye_gaze_interaction`, exposed as `sfxr_gaze()`. Use it
  for gaze-highlight and NPC reactions, but never as the only way to select something.
- Passthrough is low-resolution greyscale, so mixed-reality designs are a poor fit.
- Tracking is reported solid, including the upper cameras covering behind the head.

### VR design: what works and what doesn't
**Comfort is the first priority.**
- **Hold the frame rate.** 72 Hz leaves 13.9 ms for both eyes; aim for 90 Hz headroom.
  Dropped frames cause sickness faster than anything else.
- **Never move the camera without the player's input.** No head bob, camera shake, or
  forced camera animations.
- Locomotion comfort, best to worst:
  - teleport
  - grab-the-world motion (climbing, rowing)
  - smooth movement with a vignette
  - smooth turning (worst)
  Defaults in `vrui_loco_default()` are teleport + 30° snap turn + a short blink.
  Offer smooth options as settings.
- Reach full speed instantly (acceleration is sickening). Keep the horizon stable. Use
  ramps, not stairs, and slow elevators.
- Use the tracked real height (stage/floor space). Offer a seated mode or height offset.

**Interaction:**
- Physical grab-and-manipulate is the core appeal of VR. Prefer levers, knobs and objects
  over abstract button presses.
- Attach held things to the **grip** pose, and point and shoot with the **aim** pose.
- Haptics are cheap and effective: tick on hover, pulse on click, detents on dials.
  vrui does this by default.
- Hands passing through walls feels bad. Stop the visual hand or show a ghost hand; never
  push the player's body.
- **Lasers** work for distant UI and **poking** for near panels. vrui supports both
  everywhere.
- Throwing needs hand velocity plus the tangential velocity from wrist rotation. Scale it
  up slightly; `vrui_grab_region` returns it.

**UI and text:**
- **Avoid head-locked HUDs.** Put UI in the world, on the wrist, or on panels about 0.5–2 m
  away. A readout that goes with you should lag behind your head (lazy follow) or sit on
  your body (`docs/ATTACHING.md`).
- Text must be much larger than on flat screens: roughly ≥ 2 cm tall at 1 m (a little over
  1 degree; `vrui_text_height()`). vrui panels default to 1 px/mm with 20 px text. Keep
  key content in the center of the lens.

**Scale / presence:** 1 unit = 1 m (players notice wrong scale instantly). Spatial audio
matters. Sessions of 15–30 minutes suit VR.

**Performance (mobile GPU):**
- Few draw calls (≤ a few hundred). Use `DrawMeshInstanced`.
- 4× MSAA (sfxr default). No post-processing anti-aliasing and no full-screen effects
  (bloom, SSAO).
- Bake lighting. Avoid large transparent areas covering the view.
- Stylized low-poly art is the sweet spot.
- sfxr renders both eyes in **one pass** using rlgl stereo mode: each batch is drawn twice
  with different view/projection, halving CPU cost.

### Sources
- Steamworks docs: [Steam Frame](https://partner.steamgames.com/doc/steamframe)
  - [custom engines](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom)
  - [input](https://partner.steamgames.com/doc/steamhardware/steamframe/input)
  - [compatibility](https://partner.steamgames.com/doc/steamhardware/steamframe/compatibility)
  - [perf criteria](https://partner.steamgames.com/doc/steamhardware/steamframe/compat/perf_criteria)
  - [vrpreferences](https://partner.steamgames.com/doc/steamhardware/steamframe/vrpreferences)
- [Factorio FFF #446](https://factorio.com/blog/post/fff-446): porting a custom C++
  engine to ARM64 Linux for the Frame (Clang cross-compile, sysroot, Steam Runtime, glibc
  pitfalls; native ran 11% faster than FEX).
- [Road to VR review](https://roadtovr.com/valve-steam-frame-review/),
  [VRcompare specs](https://vr-compare.com/headset/steamframe),
  [Igalia: Turnip retrospective](https://blogs.igalia.com/dpiliaiev/turnip-my-5y-retrospective/)
- [raylib 6.0 release](https://github.com/raysan5/raylib/releases/tag/6.0) (no Vulkan).
  rlvk announcement: [raysan5 on X](https://x.com/raysan5/status/2074463189253681309),
  tests at [rygo6/raylib_tests](https://github.com/rygo6/raylib_tests).

---

## Part 2: How this repo works

### Layout
```
external/            raylib-6.0-src.tar.gz, OpenXR-SDK-release-1.1.63.tar.gz (vendored, extracted on first make)
sfxt/                test harness (sfxt.h: scripted hands, CHECK, break switches); tests/<suite>/main.c use it
patches/             our fixes to raylib, applied after extraction (each explained in patches/README.md)
sfxr/include/sfxr.h  public XR API. sfxr/src/:
                       sfxr.c (init, frame loop, stereo draw, mirror, shots)  sfxr_pose.c (poses, rig)
                       sfxr_input.c (buttons, pull levels, hand shapes)       sfxr_signals.c (presence, refresh, ... accessors)
                       sfxr_xr.c (OpenXR instance/session/frames)  sfxr_xr_input.c (bindings, sampling, haptics)
                       sfxr_xr_signals.c (presence, refresh, joints, battery, counters, models)  sfxr_xr_internal.h
                       sfxr_xr_gl.c / sfxr_xr_vk.c (swapchains)  sfxr_sim.c  sfxr_replay.c + sfxr_rec.h  sfxr_script.c (tests)
                       sfxr_audio.c  sfxr_voice.c  sfxr_steam.c  sfxr_store.c (saving)
vrui/include/vrui.h  interaction toolkit (starts with a table of contents). vrui/src/:
                       vrui.c (context, arbitration, claims, draw)  vrui_panel.c  vrui_grab.c (grab machinery, grabbables)
                       vrui_mech.c (rotary/pivot/linear/tilt)  vrui_press.c (press/rocker)  vrui_display.c
                       vrui_haptics.c (mixer)  vrui_loco.c  vrui_label.c (world text)  vrui_attach.c (body,
                       follow, edge arrows)  vrui_menu.c (radial)  vrui_smooth.c (damping, springs, easing,
                       pose smoother)  vrui_wheel.c (two-handed valve)  vrui_key.c (key switch)  vrui_wield.c
                       (held by the handle)  vrui_collide.c (ray / sphere cast / ground). Per-widget state: typed structs in VruiItem.state
                       (VRUI_STATE / VRUI_STATE_FITS in vrui_internal.h), never generic slots.
examples/<name>/     each dir with main.c -> bin/<name>   (hello, toolbox: one file per station in a row, see toolbox.h)
apps/<name>/         your projects; same rule (make new-app NAME=foo)
tools/xr_probe.c     runtime capability dump; run it first on real hardware
tools/sfxrec_dump.c  recording -> CSV (inspect what a player actually did)
tests/regress/<t>/   recorded tests: input.sfxrec, app, frames, golden/ (scripts/regress.sh)
docs/TESTING.md      the testing doctrine and plan (read before writing tests)
docs/MECHANISMS.md   what each reference mechanism promises players, its defaults, how to reskin it
docs/INPUT.md        input methods (laser/grab/poke/hand shape), input ownership, 2D vs 3D, the haptic vocabulary
docs/ONBOARDING.md   the hands-on setup station that learns the player's habits (template: examples/toolbox/onboarding.c)
docs/MOVEMENT.md     teleport, pads, surfaces, falling, climbing (examples/toolbox/yard.c)
docs/ATTACHING.md    attaching things to things, world labels, HUDs, hand menus
docs/AUDIO.md        positional sound, sounds made in code, the mic, push-to-talk voice commands (Whisper)
docs/SMOOTHING.md    how held things follow (snap/lag/spring/heavy/steady), springs, easing
docs/DADDY_BUG_SMASHER.md       the toolbox's part three: a small game (examples/toolbox/garden*.c, tests/garden)
docs/WIELDING.md, STEAM.md, PERFORMANCE.md   held things with weight; Steamworks; frame rate and foveation
tests/<suite>/       C suites: mech input move hands attach voice wield steam collide garden; tests/toolbox/*.sfxt
                     scenarios against the real toolbox (make test); tests/regress/ golden replays (make regress)
scripts/             frame.sh (headset remote control), shot-sim.sh, test-xr.sh, frame-build.sh, package.sh, new-app.sh
tools/devkit-utils/  Valve's device-side devkit helper scripts (MIT, pinned copy; see VERSION)
docker/              monado.Dockerfile (OpenXR test runtime), fake-frame.* (rehearsal "headset")
templates/           vrpreferences.json
```

### Frame structure (the one rule that matters)
```c
while (sfxr_frame_begin()) {              // wait for runtime, sample poses and input
    vrui_begin();
        /* all logic + vrui widget calls here: OUTSIDE the draw block */
    vrui_end();
    if (sfxr_draw_begin(clear)) {         // binds the stereo target
        /* raylib 3D draw calls, world space, meters */
        vrui_draw();
        sfxr_draw_end();
    }
    /* optional 2D DrawText: mirror window only */
    sfxr_frame_end();                     // submit to the runtime
}
```
- **Draw once.** Inside the draw block rlgl is in stereo mode; every draw call renders
  both eyes. **Do not call `BeginMode3D`/`EndMode3D` or `BeginTextureMode` inside it.**
  They reset the matrices or rebind the target. Use `sfxr_push_pose()` or
  `rlPushMatrix()` for transforms.
- vrui panels render their 2D content into textures **during logic** (they call
  `BeginTextureMode`). That's why widget calls must stay outside the draw block.
- `sfxr_should_render()` can be false (headset off the face, dashboard open). Keep
  updating, skip drawing.
- Billboards and anything else needing a `Camera3D`: use `sfxr_head_camera()`.
- Custom shaders work if they use raylib's `mvp` uniform. rlgl sets it per eye. If a shader
  needs the camera position, pass `sfxr_head().position` yourself.

### Coordinates, rig, poses
- Right-handed, +Y up, −Z forward, meters, origin on the floor (STAGE or LOCAL_FLOOR
  space; falls back to LOCAL −1.6 m).
- `SfxrPose` = position + quaternion. Helpers: `sfxr_pose_mul/inverse/apply/...`.
- **The rig** maps tracking space to world space. All public poses are world space. Move
  the player with `sfxr_rig_move`, `sfxr_rig_turn` (pivots on the head) and
  `sfxr_rig_teleport`.
- vrui conventions: a prop's `base` pose has local **+Y pointing out of the surface**.
  Panels face their local **+Z**; use `vrui_facing(pos, viewer)`.

### Input mapping (`SfxrHand`)
- **Every Frame control** is in `hand->button[SfxrControl]` (`down/pressed/released/touched`,
  where touched is the capacitive sensor). Named copies: `stick_btn`, `bumper`,
  `a b x y menu` (right), `view dpad_up/down/left/right` (left). `primary`/`secondary` =
  the two buttons under the thumb (right A/B, left D-pad down/up).
  `sfxr_control_name()` and `sfxr_control_on_hand()` describe them.
- `trigger`/`squeeze` are floats (0..1). Each is also turned into buttons at three **pull
  levels** (`SfxrPull`), each with its own press/release points (hysteresis):
  - `SFXR_PULL_SOFT`: 0.25 / 0.15, a light touch
  - `SFXR_PULL_FIRM`: 0.55 / 0.35, the default. `trigger_btn`/`squeeze_btn` are this level.
  - `SFXR_PULL_FULL`: 0.97 / 0.85, or the hardware click if it comes first
  - Read any level as `hand->trigger_at[level]` / `squeeze_at[level]`. Change the
    thresholds with `sfxr_set_pull_threshold()` (accessibility).
  - **Why not the hardware click:** measured on the Frame, the trigger rests at exactly 0
    and passes 0.55 about 15 ms into a normal pull. Its click fires only when bottomed
    out, 40–100 ms later, and 2 of 17 full pulls never clicked. Players expect about a
    3/4 pull to grab.
- Poses: `grip`, `aim`, `poke` (press things with this: the controller tip or index
  fingertip), `pinch`, `palm`. Poses a runtime doesn't give are derived from grip/aim.
- `source`: controller, bare hand or none.
- `sfxr_haptic(hand, amp, seconds, hz)`, `sfxr_gaze(&pose)`, `sfxr_interaction_profile(hand)`.
- **Recordings** store every control's click and touch bits, all five poses (v2), and the
  headset signals: presence, refresh rate, batteries and hand joints (v3, about 3.4 KB
  per frame). v1 and v2 recordings are upgraded on load (`sfxr_rec_read()`), so old
  sessions and tests keep working.

### vrui (immediate-mode, desktop-UI style)
- Immediate mode: each call handles input **and** queues its drawing, and returns what
  happened. IDs are stable nonzero `VruiId`s. Convention: `VRUI_ID2(group, index)`, one
  group per screen or area. Panel-internal widget IDs only need to be unique within the
  panel.
- Targeting: widgets *offer* hits (laser distance or hand proximity). The closest offer
  per hand becomes hot on the next frame. Pressing captures (`ray_active`/`grab_active`)
  until release. Touching beats pointing, so the laser hides when your hand is on
  something.
- `vrui_hand_busy(hand)` tells you when the user is using UI with that hand, so your
  own trigger actions can be suppressed.
- Panels: `vrui_panel_begin(id, &pose, w_m, h_m, title)`, then a layout
  (`vrui_layout_begin`, `vrui_row`, `vrui_row_cols`), widgets (`button`, `toggle`,
  `slider`, `segmented`, `list`, `progress`, `label`), then `vrui_panel_end()`. Drag a
  panel by its title with trigger, or anywhere on it with grip. Lists scroll by dragging
  (content or scrollbar) or with the stick.
- Props: `vrui_grabbable`/`vrui_grab_region` (near grab with grip, or laser force-grab;
  release velocities for throwing).
- **Mechanisms** (`vrui/src/vrui_mech.c`, **`docs/MECHANISMS.md`**): reference physical
  controls built on one core, each with a behavior spec with tested defaults, a collider
  and a switchable default look, plus the moving part's pose for custom models:
  - full forms, named after the motion: `vrui_rotary` (knob / selector / crank /
    spinner specs), `vrui_pivot` (lever), `vrui_linear` (slider / plunger), `vrui_tilt`
    (joystick), `vrui_press` (momentary or latching button), `vrui_rocker` (switch); also
    `vrui_hinge` (door, lid), `vrui_pull_cord`, `vrui_valve` (two-handed wheel) and
    `vrui_key_switch` (insert, turn, pull out)
  - short forms, named after the thing: `vrui_knob`, `vrui_lever`, `vrui_slider3d`,
    `vrui_joystick`, `vrui_push_button`, `vrui_switch`

  The rules:
  - only motion along the control's freedom counts; off-axis pushes are projected away
  - relative to the grab point
  - a break-in slop
  - holds end only on letting go
  - near and far
  - haptic detents and stops

  **Changing a mechanism's behavior means updating `docs/MECHANISMS.md` and
  `tests/mech` (one file per family around `scene.h`).**
- **Pull levels:** widgets use `vrui_style()->pull` (default FIRM). Wrap widgets in
  `vrui_push_pull(level)` ... `vrui_pop_pull()` to override per item. A panel uses the
  level in effect at `vrui_panel_begin`. The toolbox shows both: a "Pull to use" selector
  sets the style default, and SPAWN always wants FULL. The controller tip turns pale
  yellow / yellow / orange at soft / firm / full.
- **Input ownership** (`docs/INPUT.md`):
  - `vrui_claim_input(hand)` / `vrui_input_claimed(hand)`
  - panels claim by `VRUI_CAPTURE_POINT` (default), `LOOK` (both hands while it's in
    front of you; the Controllers panel uses this) or `MODAL`
  - locomotion only uses unclaimed hands; app code should check before acting on raw
    buttons
- **Haptics** go through the mixer: `vrui_haptic_pulse()` / `vrui_haptic_hum()`, never
  `sfxr_haptic()` directly, or ticks and hums cut each other off.
  `vrui_style()->haptic_scale` scales all of it.
- **Hand shape** (`sfxr_hand()->shape`, `->curl[5]`, from touch sensors or joints),
  **grab styles** (`vrui_style()->grab`: grip / closing hand / grip-or-trigger). Don't
  gate controls on the Frame's shape guess: point-to-press was tried and removed (the index
  sensor wasn't dependable enough, sixth session).
- **Displays:** `vrui_gauge`, `vrui_odometer`, `vrui_lamp` (read-only; passive panels
  take no input).
- Locomotion: `vrui_locomotion(&cfg)` is the only thing that moves the rig. It leaves a
  hand's stick alone while that hand points at UI or is claimed. It also handles teleport
  pads, surfaces (`ground_height`: platforms, stairs, lifts, falling) and climbing
  (`vrui_handhold`, called before it). See `docs/MOVEMENT.md`; tests in `tests/move/`.
- **World text** (vrui.h section 9): `vrui_text3d` (billboard), `vrui_text_at` (printed on a
  pose, one-sided), `vrui_tag` (on a plate), `vrui_callout` (points at a spot, keeps its
  apparent size, hides past 8 m), `vrui_sign`. **Attaching** is `sfxr_pose_mul(parent,
  local)` every frame and `sfxr_pose_relative(parent, world)` when attaching; parents can be
  hands, the head, `vrui_body()` (estimated torso), the rig, a mechanism's `part`, a bone.
  **HUDs**: a passive panel (`vrui_panel_passive`) drawn on top (`vrui_on_top_begin/end`),
  posed by `vrui_follow` (lazy) or the body; `vrui_offscreen_arrow`, `vrui_tint`.
  **Hand menus**: watch, palm buttons, a tablet, `vrui_radial_menu`. All in
  `docs/ATTACHING.md`, shown at the toolbox's Attach & label bench and Menus & HUD station.
- **Smoothing** (vrui.h section 11, `docs/SMOOTHING.md`): frame-rate independent
  `vrui_damp*`, `vrui_spring*`, `vrui_move_toward3`/`vrui_turn_toward`, `vrui_ease`, and
  `vrui_smooth_pose` with modes Snap / Lag / Spring / Heavy / Steady (a 1-euro jitter
  filter); `with_player` keeps held things riding the rig. The toolbox's Smoothing station
  tunes them; Daddy Bug Smasher's hammer uses them (hits come from the smoothed pose).
- **Daddy Bug Smasher** (`docs/DADDY_BUG_SMASHER.md`): the toolbox's part three, a small game through the
  gate at the right end of the row. It's a separate scene: `garden_update` replaces the
  stations while you're in it. Models in `examples/toolbox/resources/garden/`.
- **Sound and voice** (`docs/AUDIO.md`): `sfxr_audio.h` (positional sounds re-panned from the
  head every frame, `sfxr_sound_synth`, the microphone through raylib's miniaudio) and
  `sfxr_voice.h` (push-to-talk, Whisper tiny.en through a dlopen'd shim built by
  `scripts/get-speech.sh`, recognition on a background thread, `sfxr_voice_match`). vrui asks
  for sounds through `vrui_style()->sound`; it never plays audio itself.
- **Adding a widget:** follow the existing ones. Compute hit distance/proximity, call
  `vrui__ray_offer`/`vrui__grab_offer`, use `handle_update()` for grabbable handles, keep
  per-widget state in `vrui__item(id)`, queue drawing with `vrui_box`/`vrui__cylinder`/...,
  and add haptics. Then add it to `examples/toolbox` and verify it with a scripted
  screenshot.

### Building and testing (verified on an aarch64 Ubuntu 24.04 host, DGX Spark)
| Command | What it does |
|---|---|
| `make` | host debug build: raylib, the OpenXR loader (static, via cmake), libs, examples, tools |
| `make run EX=toolbox` / `make sim EX=toolbox` | run it (AUTO backend / forced simulator) |
| `make shot EX=toolbox` | headless simulator screenshot → `shots/toolbox-sim.png` |
| `scripts/shot-sim.sh BIN out.png "move X Y" "down 1" "sleep 0.3" "up 1" ...` | scripted mouse/keys, then screenshot |
| `make monado-image` then `make test-xr EX=toolbox BACKEND=gl\|vk` | real OpenXR run under Monado (simulated HMD, software GL/VK); saves the app mirror and **Monado's compositor window** (`*-compositor.png`) |
| `make frame` | release build inside `registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk/arm64` → `build/frame-release/bin` |
| `make package EX=toolbox` | stage `dist/toolbox/` (binary, launch.sh, vrpreferences.json, xr_probe) |
| `make frame-pair [FRAME_HOST=<ip>]` | one time: headset shows "Pairing..." (Developer → Pair devkit); sends a key, you confirm on the headset |
| `make frame-go EX=toolbox` | build + upload + register + launch through Steam + follow `logs/run.log` |
| `make frame-shot / frame-logs / frame-stop / frame-status / frame-probe` | see `scripts/frame.sh` |
| `make fake-frame` | local container that behaves like a Frame in Developer Mode (rehearsal) |
| `make new-app NAME=foo` | `apps/foo/` from the hello template |
| `make test` / `make test T=mech` / `make test-audit` | C test suites (`tests/<suite>/main.c`) headless, one process per case, each re-run with its break switch on (red leg); audit checks every switch is proven |
| `make regress` / `make regress-bless` | golden-image replays of recorded sessions (`tests/regress/`) |

- **Simulator keys:** RMB-drag look · WASD/QE move · mouse aims the active hand (the laser
  runs exactly along the cursor) · wheel sets hand distance · Shift+wheel twists · Tab
  switches hand · LMB trigger · F/MMB grip · G latches grip · 1–5 the face buttons under
  the active hand (right: A/B/menu/X/Y; left: D-pad down/up/view/left/right) · 6 bumper ·
  arrows stick · F1 help.
- **Env vars:**
  - `SFXR_BACKEND=gl|vk|sim|auto`
  - `SFXR_MIRROR=0`
  - `SFXR_LOG=1` (lists runtime extensions)
  - `SFXR_SHOT=file.png` with `SFXR_SHOT_FRAME=N` or `SFXR_SHOT_TRIGGER=file` (save the
    window, then exit)
  - `SFXR_SIM_LOOK="yaw,pitch"` and `SFXR_SIM_POS="x,y,z"` (simulator start pose)
  - `SFXR_RIG="x,y,z,yaw_deg"` (start rig offset in any backend; used to aim remote
    headset screenshots, e.g. `frame.sh shot toolbox 300 SFXR_RIG=0,0.3,1.2,20`)
- **Verification loop for agents:** build, then `make shot`, or `shot-sim.sh` with actions
  to exercise an interaction, then **look at the PNG**. For XR-path changes, also run
  `make test-xr BACKEND=gl` and `BACKEND=vk` and look at `*-compositor.png`. Mouse
  press and release must land on different frames: put `sleep ≥0.2` between `down` and `up`.
- The Frame binary needs only glibc ≥ 2.38, libGL, libX11 and libstdc++ (all in the Steam
  Runtime). Check with `readelf -d` and `objdump -T` after changing link flags.

### Testing spec
**`docs/TESTING.md` is the testing doctrine and plan.** Follow it when adding tests:
- test events and state, not pixels
- drive real code; only the person is simulated
- every test must be seen failing
- simulated time only
- positions relative to named widgets, timing as windows

The quickstart's toolbox is the reference implementation of it.

**What exists (C harness):**
- `sfxt/include/sfxt.h`: scripted hands against the app's real frame loop, using the
  script backend. Time is exactly 1/72 s per frame, and there's default tremor noise.
- Break switches: `SFXR_BREAK(name)`, declared in `sfxr/src/sfxr_breaks.def` and
  `vrui/src/vrui_breaks.def`. They're compiled only into `make test` builds
  (`build/host-test`, `-DSFXR_TESTING`).
- `scripts/test.sh` runs each case in its own process under a private Xvfb, then again
  with the case's `fails_if` switch on. PASS, FAIL or UNTRUSTED.
- To add a case: a function plus a `{ "suite/name", fn, "break_switch" }` row in
  `tests/<suite>/main.c`. If no plausible bug would make it fail, use `NULL`; it will be
  listed as unproven.

### Record / replay regression tests
- `SFXR_RECORD=f.sfxrec` works in any backend. After the backend samples input each frame,
  sfxr appends the **raw inputs** (`SfxrRawHand` ×2, head/eye poses, FOVs, gaze, dt,
  should_render, eye size) as fixed-size binary records (`sfxr_replay.c`). The header
  carries a version and struct sizes; incompatible builds refuse the file.
- `SFXR_REPLAY=f.sfxrec` selects the **replay backend**. It feeds the records back and
  renders offscreen (stereo for headset recordings, mono for simulator ones;
  `SFXR_REPLAY_SCALE` shrinks the eye resolution) and exits at the end. Haptics are no-ops.
- **Determinism:** with Mesa software GL (`LIBGL_ALWAYS_SOFTWARE=1`, as `regress.sh` uses),
  replays are pixel-identical run to run (verified: AE = 0). Apps must use
  `sfxr_dt()/sfxr_time()/sfxr_frame_index()`, never raylib's wall clock.
- `SFXR_SHOT_FRAMES="a,b,c"` with `SFXR_SHOT=path-%06d.png` takes several shots in one run
  (target readback for non-simulator backends), then quits after the last.
- `scripts/regress.sh [--bless] [names]` runs `tests/regress/<name>/{input.sfxrec,app,frames,golden/}`:
  it replays under Xvfb and compares with ImageMagick (`-fuzz 3%`, tolerance 0.2% of
  pixels). Make targets: `record`, `replay`, `regress`, `regress-bless`, `frame-record`.
  `scripts/test-xr.sh` forwards `SFXR_*` env, so `SFXR_RECORD=... make test-xr` records
  a Monado (stereo) session.
- Included tests: `toolbox-sim-basics` (scripted: lever push, grab and throw) and
  `toolbox-monado-stereo`.

### Remote testing on the headset (no need to wear it for most steps)
- The packaged `launch.sh` writes, next to the binary on the device:
  - `logs/diag.txt`: the environment and `xr_probe` output **from inside Steam's launch**.
    This answers which display, which runtime and which bindings games actually get.
  - `logs/run.log`: timestamped app log, with a sfxr **heartbeat** every 5 s showing loop
    and render fps, session state, `should_render`, controllers and profile.
  - It also sources `launch.env`, which `frame.sh run <app> [secs] KEY=VAL...` writes, so
    per-run settings need no re-registration.
- **Every launch records its inputs automatically** into `recordings/` on the headset
  (the newest 5 are kept; `SFQ_AUTORECORD=0` disables it).
  - Recording starts when someone puts the headset on, and frames while it's off are
    skipped. A launch nobody wears leaves no file.
  - `frame.sh sessions <app>` lists them.
  - `frame.sh keep <app> <test> [session]` copies one into `tests/regress/`.
  - `frame.sh pull <app>` copies every session, the logs and `prefs.cfg` into
    `local-data/` for review.
  - Deploys never touch `recordings/`, `logs/`, `shots/` or `prefs.cfg`.
- `launch.env` is **one-shot**: `launch.sh` sources it and moves it to
  `logs/last-launch.env`, so later library launches start clean.
- **Debugging the Frame's GPU without anyone wearing it:** the replay backend needs no VR
  runtime. `frame.sh push-recording <app> <file>`, then
  `frame.sh run <app> 60 SFXR_REPLAY=recordings/<file> SFXR_SHOT=shots/x-%05d.png SFXR_SHOT_FRAMES=...`
  renders a real session on the headset's own GPU. Pull the PNGs with `scp` using the
  paired key.
- `tools/sfxrec_dump file.sfxrec [--summary]` turns a recording into CSV (per frame and
  hand: poses, head yaw/pitch, trigger/grip value and click, stick, buttons). This is how
  the "grab needs a full pull" problem was proven.
- `frame.sh shot` runs with `SFXR_SHOT`. In XR mode sfxr reads back the **submitted
  swapchain image** (both eyes), so it works with the mirror window hidden.
- Reading the heartbeat: `session=FOCUSED should_render=1` means frames reach the display.
  `VISIBLE/SYNCHRONIZED` or `should_render=0` usually means the headset isn't being worn
  or the dashboard is up. Nothing after `session state -> IDLE` means the runtime never
  started the session.

### Gotchas (each learned the hard way here)
- **Static libraries and deleted files:** `ar rcs` never drops members, so after
  renaming or deleting a source file the old object lingered in `libvrui.a` and caused
  duplicate symbols. The Makefile now recreates archives (`rm -f` first). If you see
  "multiple definition" after a rename, delete `build/*/lib*.a`.
- **X11 `Font` vs raylib `Font`:** any file including Xlib must include it *before* raylib
  with `#define Font X11Font` (see `sfxr_xr_gl.c`).
- **Color:**
  - Swapchains are sRGB, and raylib writes gamma-space colors raw
    (`GL_FRAMEBUFFER_SRGB` stays off), so the headset matches the desktop.
  - The mirror samples those textures with `GL_SKIP_DECODE_EXT`.
  - If you enable `GL_FRAMEBUFFER_SRGB` or move to GLES (where sRGB writes always
    encode), colors will wash out.
- **GL render targets are bottom-up:** draw them with a negative source height. The VK path
  flips during its blit.
- **Monado in Docker:** `monado-service` epolls stdin, so pipe `sleep infinity` into it.
  The Vulkan ICD is `lvp_icd.json`. The compositor window is 640×360 at (0,0).
- **The `vk` backend** needs `GL_EXT_memory_object_fd` and a GL/VK device UUID match. It
  syncs with `glFinish()` plus a fence (simple and correct). Upgrade to
  `GL_EXT_semaphore_fd` if the profiler says so.
- **Zink reorders buffer uploads (blank panels).** On the Frame, GL is Zink on
  Vulkan. Zink moved raylib's `glBufferSubData` batch uploads ahead of earlier draws,
  so render-to-texture draws (vrui panels) read vertices from later in the frame and
  came out blank. It was fixed by `patches/raylib-6.0-orphan-batch-buffers.patch`
  (buffer orphaning); see `patches/README.md`. Lesson: a GPU bug that only shows on the
  device can be debugged without wearing the headset. Replay a recorded session on it,
  then try `ZINK_DEBUG=noreorder` (or other `ZINK_DEBUG` flags) with
  `frame.sh shot <app> <frame> KEY=VAL`.
- **ESC does not quit** (`SetExitKey(KEY_NULL)`), because VR users can't see the keyboard.
  Close the window or let the runtime end the session.
- **raylib shapes that send no normals:** `DrawCylinderEx`, `DrawCylinder`,
  `DrawTriangle3D`, `DrawCubeV` (only `DrawCube`, `DrawSphereEx`, `DrawPlane` do). Under a
  lighting shader they take whatever normal was set last. vrui draws its own cylinders
  and sets triangle normals; set `rlNormal3f` before the others.
- **A shader must read `vertexTexCoord`:** sampling `texture0` at a fixed point turned every
  textured quad (the Bugmaster) invisible.
- **Batched shapes arrive pre-transformed:** rlgl transforms DrawCube's corners on the CPU,
  so the world shader takes `vertexPosition` as world space; models need `matModel`
  (hence two programs, `useModelMatrix`). Flush the batch (`rlDrawRenderBatchActive`)
  before changing a uniform, or the half-full batch draws with the new value.
- **raylib's image noise doesn't tile** (`GenImagePerlinNoise`, `GenImageCellular`): seams
  at every repeat; `graphics.c` has a tiling value noise. **raylib seeds its random numbers
  from the clock:** anything a replay or golden image sees (textures, particles) uses a
  fixed seed or its own generator.
- **The scripted test backend renders nothing:** sfxt draws every frame into a tiny
  target itself, so draw-time work (batch builds, culling) runs in tests.
- **`scripts/regress.sh` runs `build/host-debug/bin/toolbox` as it is:** `make` first, or
  it tests a stale binary (it once passed goldens on a build from before the change).

### Current state and next steps (handoff, updated 2026-09-28, end of the sixth session)

**The goal (from the user):** a quickstart for the Steam Frame, **not a game engine**.
Every bit of the hardware exposed with sane defaults; reference patterns a game would use
anyway (take "a good toggle switch", reskin it, keep its documented, tested behavior);
the toolbox is the reference and testing baseline. Hands never move along a perfect
axis, so off-axis force must never prevent use. Add a primitive when the hardware makes
it intrinsic (attention, pause) or nearly every game needs it; otherwise let a real game
pull it in. **Next: a real but small game on the quickstart** (probably Revenge of the
Bugmaster: pointing plus voice orders; see "Next" below).

**Working on it**
- The repo is public (`github.com/Dinsmoor/SteamFrameRaylibQuickstart`, MIT). Claude
  commits and pushes (spark's git identity, the attribution lines, the message written to
  a file and `git commit -F`: an apostrophe inside `ssh '...'` once truncated one). Never
  force-push.
- **Build everything first (`make`, `make CONFIG=test test-bins`, `make frame`), then run
  tests, and only on spark**, never on the local machine (the user asked).
- Visual checks without a headset: a throwaway `tests/toolbox/zz_look.sfxt` whose cases
  end in a failing `expect`; `SFXT_SNAPSHOT_SIZE=1920x1080` gives headset-like sharpness;
  `fail.png` lands in `build/host-test/test-artifacts/<file>/<case>/` (test.sh wipes them
  each run). README pictures: the same with the HUD default set to `HUD_OFF` temporarily.
- The user's taste: plain, readable, "nicer to be in"; not straight-up Minecraft. The
  visual ceiling now is the artwork, not the rendering.

**What exists (details in the docs and the headers)**
- **sfxr:** OpenXR GL (verified on the Frame at 72 Hz, 1728² per eye) and VK paths, the
  simulator, record/replay (every headset launch records; replays run on the headset's GPU
  too), scripted backend for tests. Every Frame control's click and touch, pull levels,
  hand shapes from the touch sensors, the thumb posed from them, bare-hand gestures,
  presence, focus and **attention** (`sfxr_attention`: headset off / dashboard; recorded),
  refresh rate, batteries, performance counters, controller models, passthrough, depth
  submission, floor guard, screenshots, the event log, `sfxr_report`, **`sfxr_store`**
  (saving), `sfxr_in_view` (culling), positional audio mixed by sfxr (ears: level, time,
  tone), emitters, `sfxr_audio_pause`, the mic, push-to-talk and hands-free Whisper,
  optional Steamworks.
- **vrui:** panels (Atkinson Hyperlegible, wrapping), the reference mechanisms, grab and
  wield (Blade & Sorcery-style), locomotion (teleport, pads, surfaces, climbing), world
  labels and callouts, hand menus and HUDs, smoothing, haptics mixer, the widget registry,
  a lighting-shader hook and culling for its shapes, **collision queries**
  (`vrui_collide.c`: ray, sphere cast, ground).
- **The toolbox** (README "What's in the toolbox" lists every station): the workbench with
  the look switches (LIGHT SHINE FOG TEXTURES SHADOWS CULL BATCH) and a cost plaque, the
  controls diagram, the stations in a row from Particles (x -23) to Hinges & cords, the
  Movement yard, and **Daddy Bug Smasher** (pauses when you leave, best time saved).
  `graphics.c` (one commented world shader in `resources/shaders/`, textures, static
  batching, blob shadows), `particles.c`, `impacts.c`.
- **Tests:** 136 cases (28 unproven: no break switch fits them), audit clean, two golden
  replays. `make test`, `scripts/test.sh [file]`, `scripts/regress.sh [--bless]`.

**What the Frame taught us (keep: each cost a session to find)**
- **B's click never reaches the app** (touch yes, click 0 in every recording). Nothing
  essential on B. `docs/INPUT.md` "Buttons that don't arrive".
- **SteamVR's poke pose is 12.5 cm below the grip**, and its controller skeleton **never
  straightens the index** (curl 0.97): poke from the tip, shapes from the touch sensors.
  The index sensor isn't dependable enough to gate a control on (point-to-press removed).
- **SteamVR's thumb is placed on the wrong control** (on A for the stick, on the stick for
  A, the far side when lifted): sfxr poses it from the touch sensors, spots measured from
  the driver's render model (`/opt/steamvr/drivers/frame_controller/resources/rendermodels/`,
  grip = its `openxr_grip` component).
- **A controller's palm pose (`palm_ext`) has the grip's axes** (palm along ∓X, -Y down
  the handle; the Frame's is the grip tipped 42° about X): sfxr turns it to the joint
  convention.
- **Only 72 Hz offered**, even worn and with `preferMinRefreshRate: 90`.
- **One launch failed on GL** (`xrCreateReferenceSpace` gave `HANDLE_INVALID`) and fell
  back to Vulkan (so VK works on hardware). Cause unknown; watch the logs.
- **SteamVR's floor settles** (LOCAL_FLOOR 0.64 m off STAGE for ~12 s once): the floor
  guard corrects it (`SFXR_FLOOR=local` turns it off).
- **Zink reorders buffer uploads** (blank panels): the raylib patch; see Gotchas.
- Foveation: no GL route; Valve's `fdm_injection` layer is loaded into our process and
  might foveate Zink's passes. Unmeasured (`docs/PERFORMANCE.md`, `scripts/frame-perf.sh`).
- The radial menu lives on the **main hand's bumper** (A and the stick share a thumb).

**Next headset session: check these (the newest first)**
1. Daddy Bug Smasher: take the headset off mid-round, then open the dashboard: paused,
   silent, a menu in front of you on return, 3-2-1 on Resume? The garden lit, the
   Bugmaster on his tower.
2. Blob shadows under your hands; throw blocks at other tables and posts; strike a table
   with the sword (knock, dust, buzz).
3. The Particles bench and the stream: read well in stereo? The plaques' costs.
4. The workbench switches with the Performance overlay on: what does each cost on the
   Frame (CPU ms on the plaque)? Fog and light at day and dusk (SKY lever).
5. The palm buttons at a normal distance; the controls diagram's real models and callouts.
6. Thumbs with "Joints while holding" on (Headset panel).
7. Still open from earlier sessions: left/right sound at the Sound station; B on the
   Controllers panel; bare hands (put the controllers down; `SFXR_HANDS=joints`); the
   Movement yard; `scripts/frame-perf.sh toolbox` while worn; the mic's name
   (`frame.sh exec 'pactl list sources'`) and Whisper's time on the headset.
8. `scripts/frame.sh pull toolbox` afterwards: recordings, `.events`, `shots/`.

**Next (for the small game; build a primitive when the game needs it)**
- Enemy movement: steering, separation, following the terrain, waves.
- Animated characters (glTF animation; the world shader needs a skinning path).
- A comfort vignette, if the game uses smooth movement.
- Instancing, once there are hundreds of copies of something.
- The garden's terrain isn't a collider yet (shadows and impacts skip the garden).

**Still to build / later**
- Animate the controller models' buttons (`xrGetRenderModelStateEXT`).
- Scenario commands still planned: `snapshot`, `play`, gaze, controller dropout.
- The `vk` backend and 90/120 Hz on hardware; eye gaze on the device; depth for the VK
  path; motion vectors (`XR_EXT_frame_synthesis`).
- Steamworks on the Frame (devkit launches + app 480); depots.
- A virtual keyboard widget and a `vrui_scroll_panel`.
- An Android APK target (raylib's Android backend + `XR_KHR_opengl_es_enable`) as a
  fallback; a native Vulkan renderer if foveation can't reach GL.
- raylib stays on the 6.0 release; pin a development commit only if a feature needs it.
