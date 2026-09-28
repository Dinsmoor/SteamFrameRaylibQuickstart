# Testing VR apps: the spec

> **Status: partly built.** Working today:
> - input recording and deterministic replay, with golden-image comparison
>   (`make regress`)
> - the C test harness (`sfxt/`), with break switches, an automatic red leg and an audit
>   (`make test`, `make test-audit`)
> - 59 cases (`make test`):
>   - `tests/mech`: the reference mechanisms
>   - `tests/input`: trigger input and hand shapes
>   - `tests/move`: movement and climbing
>   - `tests/hands`: bare hands
>   - `tests/steam`: Steamworks
> - the event log (`SFXR_EVENTS`)
>
> The scenario language and widget registry are still planned. The "Staging"
> section at the end tracks what exists.

This document explains **how** apps built on this quickstart get tested, and **why** it's
done this way. The quickstart's own `toolbox` example is tested exactly like this, so it
works as a worked example you can copy for your own app.

---

## 1. The problem

A VR app's input is a person: a head moving in 3D, two hands, a dozen buttons and
analog axes, eye gaze, and a headset that can be taken off at any moment. The output is
two images, 72–144 times a second. Two naive approaches both fail:

- **Compare screenshots.** This tells you *that* something changed, never *what*. It
  breaks when you move a table 10 cm, and it can't express "the lever works".
- **Replay a recorded person.** This is faithful, but the recording holds absolute
  positions. Move the lever and the recorded hand grabs empty air. The test didn't catch
  a bug, it just broke.

So we test **what the app understood**, not coordinates or pixels. "The right hand took
hold of the lever, pushed it forward, the lever's value went from 0 to about 1, and the
sky turned to dusk" is the thing we care about. Tests should say exactly that.

## 2. Principles (the doctrine)

1. **Test meaning, not mechanics.** Assert on **events** (grab, press, value changed,
   teleported) and **state** (lever value, the app's sky value). Screenshots are an
   optional extra layer, never the only check.
2. **Real code, no pretend versions.** Tests drive the real app, the real vrui and the
   real sfxr through an input backend. Only the *person* is simulated. There is no mock
   lever.
3. **Every test must be seen failing.** A check that has never failed proves nothing. Each
   case names a **break switch** (section 9) that deliberately breaks the behavior it
   protects, and the runner automatically checks that the case fails with it on. A case
   that still passes when broken is reported as **UNTRUSTED** and fails the run.
4. **Simulated time only.** Tests run on a fixed clock (every frame exactly 1/72 s or
   1/90 s), as fast as the machine allows, and reproduce exactly. Apps use `sfxr_dt()`,
   `sfxr_time()` and `sfxr_frame_index()`, never the wall clock.
5. **Say where, relative to what.** Positions in tests are relative to named things (the
   lever's handle, the panel's "Reset" button) and in real units (meters, degrees).
   Tolerances are explicit.
6. **Say when, as a window.** Timing expectations are windows ("within 3 frames"), and
   the window size is itself a requirement: it states how much delay is acceptable.
7. **Real hands shake.** Scripted input carries realistic noise by default (hand jitter,
   trigger noise, tracking dropouts), so "happy path" tests exercise the debouncing too.
8. **Headless by default, watchable on demand.** Everything runs without a monitor
   (Xvfb, software GL) and in parallel. Any failure can be watched in a window afterwards.
9. **A failure explains itself.** A failed test leaves: the failed assertion, the event
   log around it, a screenshot of that frame, and a recording that replays the failing run.

## 3. Layers: what gets tested where

| Layer | Question it answers | Written as | Example |
|---|---|---|---|
| **1. Unit** | Is this function right? | C, `CHECK()` | pose math; projection from FOV |
| **2. Input interpretation** (sfxr) | Do raw controller readings become the right buttons at the right frame? | C, `CHECK()` | trigger ramp → exactly one press; noise at 0.65 → no press |
| **3. Interaction** (vrui) | Does each widget behave? | scenario file | grab the lever and push it → value ≥ 0.9 |
| **4. App scenario** | Does the app react correctly? | scenario file | push the SKY lever → the app reports sky ≥ 0.9 |
| **5. Visual snapshot** | Did the look change unexpectedly? | `snapshot` in a scenario | approved image of the workbench after the push |
| **6. Device** | Does it work on the real headset? | `scripts/frame.sh` + recordings | session reaches FOCUSED; 72 fps; recorded session replays with the same events |

Most tests belong in layers 3–4. Layers 1–2 pin down the low-level details once, so
higher tests don't have to.

## 4. The event log

sfxr and vrui write every meaningful thing as one line of text when `SFXR_EVENTS=<file>`
is set. `scripts/test.sh` sets it for every case, and every headset launch writes one next
to its session recording (`recordings/session-<time>.events`). Here is a real one from
the toolbox:

```
# sfxr events: seconds, frame, kind, text
    0.000 f1       hand       L controller
    3.773 f71      grab       SIZE R laser+grip
    4.566 f86      release    SIZE R
    4.566 f86      value      SIZE 0.750
    5.188 f98      press      SPAWN (full pull) by laser
```

**Why plain text, not JSON:** the first reader is a person going through a playtest
("what did I touch when it felt wrong?"). The first three fields are fixed (seconds,
frame, kind), so a script can still split lines on whitespace.

| Source | Events |
|---|---|
| sfxr | `session` (state), `headset` (put on / taken off), `hand` (controller, bare hand, lost), `floor` (floor-guard correction) |
| vrui | `grab` / `release` (hand, laser+trigger, laser+grip), `value` (a control's value when let go), `press` (poke or laser), `flip`, `click`, `toggle` |
| vrui locomotion | `teleport` (to a point or a pad), `turn`, `climb` (which hand leads, let go), `mantle`, `land` |
| your app | `sfxr_event("kind", "printf format", ...)`, wherever something meaningful happens |

Widgets are named by their label (a mechanism's `spec.label`, a button's text), which is
one more reason to give them one. `mech/knob-events-name-it` proves it.

Not logged yet: hover, and haptics.

## 5. The widget registry

Tests refer to widgets by name. Every frame, each vrui widget reports
`{name, kind, pose, value, parts}`. The name comes from its ID group and label
(`table.lever`), and parts are named sub-poses such as `handle` or `cap`. Scenario
commands like `hand R to table.lever/handle` look the pose up **each frame**, so moving
the workbench doesn't break tests, and a widget that moves mid-test (a panel being
dragged) is followed correctly.

## 6. Two kinds of test input

| | **Scripts** (intent) | **Recordings** (capture) |
|---|---|---|
| Positions | relative to named widgets, converted each frame | absolute, as the person moved |
| Survives layout changes | yes | no |
| Good for | specifying behavior; debouncing; edge cases | reproducing a real session or bug; device vs. simulator comparisons; crash hunting |
| Source | hand-written `.sfxt` file | `make record`, `scripts/frame.sh record` |

Both feed the same machinery. Under the hood a script is converted to per-frame raw
inputs, exactly like a recording. A script can also `play` part of a recording.

**Frames of reference.** Recordings store tracking space, what the body physically did,
so a replay goes through the app's own locomotion logic. Scripts give hand targets in
world space. The harness converts them using the player's current position and facing,
so scripted hands still reach the lever after a teleport or snap turn.

## 7. Time and debouncing

- Fixed rate per file (`rate 72`). `over 0.4s` means the motion is spread over
  0.4 s × 72 = 29 frames with smooth easing.
- vrui decides what each hand is hovering at the end of a frame, so hovering takes
  effect **one frame later** by design. Grabbing therefore needs hover to have been
  established first. Windows such as `within 3f` state the acceptable delay; tightening
  them is how a latency regression gets caught.
- **Thresholds:** the trigger and grip have three pull levels, each with its own press
  and release points (`SfxrPull`, docs/MECHANISMS.md "Pulling the trigger"). FIRM
  (0.55 / 0.35) is the default; the Frame's hardware click only counts toward FULL.
  Layer-2 tests pin these down (`tests/input`, built):
  - ramps 0 → 1 → 0 over 50, 100 and 300 ms → **exactly one** press and release per level
  - resting at 0.55 ± 0.03 noise for 2 s → at most one press, **no** release
    (proven by `sfxr_no_hysteresis`)
  - a pull shaped like a measured Frame pull → FIRM within 2 frames
  - still planned: a 1-frame tracking dropout while grabbing → the grab **holds**
- **Default noise** in scenarios: hand position ±1 mm, rotation ±0.2°, trigger ±0.02.
  Files can override it (`noise off` for exact geometry tests).

## 8. What a scenario looks like

One file per topic, many small named cases (in the spirit of SQLite's test suite).
Each case starts from a fresh app state.

```
# tests/scenarios/lever.sfxt
app   toolbox
rate  72
noise hand=1mm trigger=0.02

case push-lever-by-hand
  fails-if vrui_lever_grab                              # red leg: see section 9
  look    at table.lever
  hand    R to table.lever/handle over 0.4s
  grip    R 1.0
  expect  grab table.lever by R within 3f
  hand    R move 0,0,-0.20 in table.lever over 0.6s     # along the lever's own axes
  grip    R 0
  expect  release table.lever by R within 2f
  expect  value table.lever >= 0.9
  expect  haptic R count >= 3                         # detent ticks
  expect  app sky >= 0.9                              # the app reacted
  snapshot lever-pushed                               # optional visual check

case push-lever-by-laser
  fails-if vrui_lever_grab
  hand    R point at table.lever/handle
  trigger R 1.0
  expect  grab table.lever by R via ray within 3f
  hand    R aim move 0,0.15,-0.30 in table.lever over 0.6s
  trigger R 0
  expect  value table.lever >= 0.5

case grip-out-of-reach-does-nothing
  hand    R to table.lever/handle offset 0,0.15,0
  grip    R 1.0
  expect  no grab table.lever for 30f
```

**Command set (first version):**

| Command | Meaning |
|---|---|
| `app NAME`, `rate HZ`, `noise ...` | file header |
| `case NAME` | new case; the app restarts from initial state |
| `look at TARGET` / `look dir YAW,PITCH` | head orientation (default head at 1.6 m) |
| `hand H to TARGET [offset x,y,z] [over T]` | move the grip to a widget part |
| `hand H move x,y,z in TARGET [over T]` | relative move in that widget's axes |
| `hand H point at TARGET` / `hand H aim move ...` | aim the laser |
| `trigger H V`, `grip H V`, `button H primary|secondary|menu down|up`, `stick H x,y` | controls |
| `gaze at TARGET` | eye gaze |
| `session visible|focused|lost`, `controller H lost Nf` | runtime conditions |
| `wait T` / `wait Nf` | let time pass |
| `play FILE [from A to B]` | insert a recorded segment |
| `expect EVENT ... [within T] / expect no EVENT ... for T` | event assertions |
| `expect value NAME op V` / `expect app KEY op V` | state assertions (`op` = `>= <= == ~=`) |
| `snapshot NAME` | compare a screenshot with its approved image |

**Low-level tests (layers 1–2)** are plain C and link the real code:
```c
// tests/unit/test_trigger_threshold.c
SFXT_FAILS_IF(sfxr_no_hysteresis);
static void ramp(float ms) {
    feed_trigger_ramp(0.0f, 1.0f, ms);
    CHECK(count_edges(PRESS) == 1, "exactly one press on a clean ramp");
    CHECK(frames_after_crossing(0.75f) <= 1, "press within 1 frame of 0.75");
}
```

## 9. Break switches: proving tests can fail

A break switch deliberately breaks one named behavior so we can watch the tests that
protect it turn red. It exists **only** for that. Test conditions such as jitter,
dropouts or the headset being removed are inputs and belong in scenarios, not here.

**In code.** One macro that exists only in test builds:
```c
if (!SFXR_BREAK(vrui_lever_grab))
    handle = handle_update(id, it, ray, prox);
```
In normal and headset builds `SFXR_BREAK(x)` is the constant `0`: no code, no strings,
nothing anyone can switch on. `make test` compiles a separate build with
`-DSFXR_TESTING`, where switches can be turned on for a run.

**One registry per module.** Each switch is declared once, with one sentence:
```c
// vrui/src/vrui_breaks.def
BREAK(vrui_lever_grab,    "lever ignores grab attempts (hand and laser)")
BREAK(vrui_hover_latency, "hover resolves 3 frames late instead of 1")
// sfxr/src/sfxr_breaks.def
BREAK(sfxr_no_hysteresis, "trigger/grip use one threshold (no debounce)")
// examples/toolbox/app_breaks.def   (apps get their own)
BREAK(toolbox_sky_unwired, "the SKY lever no longer changes the sky")
```
Switch names are identifiers, not strings, so a typo is a compile error.

**Tests name their switch** with the directive `fails-if NAME` (scenarios) or
`SFXT_FAILS_IF(NAME);` (C). The runner runs each such case twice:

| Normal run | With the break on | Result |
|---|---|---|
| passes | fails | **PASS** |
| fails | (any) | **FAIL** |
| passes | still passes | **UNTRUSTED**: the test doesn't detect what it claims. Fails the run. |

**`make test-audit`** fails if a switch exists that no test uses, or a test names a switch
that doesn't exist. It also lists cases without any `fails-if` as *unproven*, so the
gaps stay visible.

This is the "red leg / green leg" discipline made mechanical: the runner enforces it
instead of people remembering it.

## 10. The test language

**Two levels, one engine.**

**Scenario files (`.sfxt`)** cover interaction and app behavior, which is most tests:
- One command per line, `#` comments, `case NAME` blocks.
- **Declarative on purpose**: no variables, loops, conditions or expressions. If a test
  needs logic, write it in C (below).
- Units are always explicit: `0.4s`, `250ms`, `3f` (frames); `20cm`, `5mm`, `15deg`; a
  plain number means meters. Targets are `group.label[/part]` from the widget registry.
  Comparisons are `>= <= == ~=`.
- **Time moves only on these lines:** `over T`, `wait T`,
  `expect ... within T` (advances until the event appears, or fails at the end of the
  window) and `expect no ... for T`. Everything else happens in the current frame, so
  the timeline is explicit.
- Parsed and run **by sfxr itself** (C, test builds only). No Python, Lua or TCL to
  install.
- **One process per case.** The runner starts the app with
  `SFXR_TEST=file.sfxt SFXR_TEST_CASE=name`. Every case starts from a genuinely fresh
  app, cases run in parallel, and apps need no restructuring.

**C tests** cover low-level logic and anything needing loops (sweeps, exhaustive checks):
- The same harness and vocabulary: every scenario command is one C function
  (`hand R to table.lever/handle over 0.4s` is
  `sfxt_hand_to(SFXR_RIGHT, "table.lever/handle", 0.4f)`), so nothing is DSL-only.
- plain `CHECK(cond, "message")` checks, plus `SFXT_FAILS_IF(...)`, the same runner and
  the same audit.

**Why this shape:**
- *All C* buries choreography ("move here over 0.4 s, grip, push") in boilerplate.
- *Embedded Python/Lua* adds a runtime and invites tests that need their own tests.
- *Bash + JSON* works for daemons but gets unreadable for dense spatial choreography.
- SQLite's TCL style (many tiny named cases, each with an explicit expectation) is the
  model, without the TCL.

**Output:** one line per case for tools (`PASS lever/push-lever-by-hand 2.1s`, `FAIL ...`,
`UNTRUSTED ...`) and a summary table for people.

## 11. Running tests and reading failures

```
make test                         # every layer, in parallel, headless
make test T=lever                 # one scenario file (or T=lever/push-lever-by-hand)
make test-watch T=lever/push-lever-by-hand   # same, in a slowed-down window
make test-bless T=lever           # approve snapshots
```
The output is a PASS/FAIL table. For each failure the runner prints the failed checks,
and keeps in `build/host-test/test-artifacts/<case>/`:
- the case's output
- its event log, meaning every grab, release, press and teleport it made

Still planned: the frame where it failed, as a picture, and the exact inputs as a
`run.sfxrec` to replay.

## 12. The device layer

**Tests never add entries to the headset's Steam library.** Each app has exactly one
entry ("Devkit Game: toolbox"). Device-side checks reuse it and pass per-run settings
through `launch.env` (`scripts/frame.sh run|record|shot`). All scenario and C tests run
on the build machine. `scripts/frame.sh deploy` refuses anything that isn't an app
folder, and `scripts/frame.sh list` / `delete` clean up stray entries.

Scenario scripts can't move real controllers, so on the headset the tests are:
- **Smoke:** the session reaches FOCUSED, the frame rate holds, the Frame profile is
  bound, and there are no errors in the log (`scripts/frame.sh run` + heartbeat).
- **Record and compare:** record a real session with its event log on the headset
  (`scripts/frame.sh record`), replay it on the build machine, and require **the same
  event stream**. Differences point at device vs. simulator behavior, which is exactly
  what we want to learn about.
- **Snapshot:** `scripts/frame.sh shot` for eyeballing real output.

## 13. Using this pattern in your own app

1. Use sfxr time (`sfxr_dt()`, `sfxr_time()`, `sfxr_frame_index()`) everywhere.
2. Give widgets stable IDs **and** meaningful labels; the registry names come from them.
3. Report your app's meaningful moments: `sfxr_event("door_opened", 1)`.
4. Write one scenario file per feature, a few small cases each. Declare a break switch
   for each behavior you protect (`app_breaks.def`) and name it in `fails-if`. The runner
   proves the test can fail.
5. Record a real session on the headset now and then, and keep it as a replay test.

## 14. Staging

| Piece | Status |
|---|---|
| Input recording (`SFXR_RECORD`), replay backend (`SFXR_REPLAY`), multi-frame shots | **done** |
| Golden-image regression runner (`scripts/regress.sh`), 2 example tests | **done** (becomes the `snapshot` layer) |
| Auto-recording of every headset launch; `frame.sh sessions / keep / push-recording` | **done** |
| `tools/sfxrec_dump` (recording → CSV for analysis) | **done** |
| Replaying a recorded session on the headset's own GPU (no one wearing it) | **done** (used to reproduce a device-only rendering bug) |
| Script backend (`SFXR_BACKEND_SCRIPT`) and the C harness `sfxt/` (`sfxt_hand_to/path`, `sfxt_grip/trigger/button`, `sfxt_wait/frames`, `CHECK`, `CHECK_NEAR`, default noise) | **done** |
| Break switches (`SFXR_BREAK`, `sfxr/src/sfxr_breaks.def`, `vrui/src/vrui_breaks.def`), automatic red leg, `make test-audit` | **done** (20 switches, all proven) |
| Mechanism tests (30 cases, `tests/mech`) and input tests (5 cases: pull levels, hand shapes, `tests/input`) | **done** |
| Parallel one-process-per-case runner, `make test` (headless, private Xvfb) | **done**; failing cases keep their output and event log (screenshots and `run.sfxrec` still planned) |
| Event log (`SFXR_EVENTS`), `sfxr_event()` | **done** (text lines; headset sessions and failing tests keep one) |
| Widget registry (names, parts, poses, values); string targets like `"table.lever/handle"` | planned (harness positions are world coordinates today) |
| `.sfxt` scenario files (a syntax over the `sfxt_*` calls) | planned |
| Gaze, session and dropout commands, `play` | planned |
| Split sessions into per-vrui-item clips (from the event log) to review and keep as tests | planned |
| Device event-stream comparison | planned |
