# Onboarding: learn the player's habits instead of asking

VR has no standard way to hold, grab, poke or point, so every app picks one and players
adapt, often badly. This is "left-handedness for VR": the app should fit the player, not
the other way round. A settings menu is the wrong tool, because players don't know their
own habits in VR terms ("do I grab with the grip or the trigger?").

The toolbox's **hands-on setup** station (`examples/toolbox/onboarding.c`, right behind
you: start it from its panel) does two jobs at once:

1. **It teaches.** A few hands-on tasks: pick up a cube, turn a dial to a mark, press a
   button, click a far target. A hint appears only if the player seems stuck (no success
   after 6 s), so people who already know VR aren't lectured.
2. **It profiles.** While the player does each task **their own way**, it watches how,
   then turns that into settings the player can confirm or change.

It's a **template to copy**, not a library: tasks, observations and mappings are plain
code, because each app's controls differ.

## How it works

- **Accept everything while watching.** During the setup, vrui is set to the most
  permissive style: grab with the grip *or* the trigger, and a light pull
  (`VRUI_GRAB_GRIP_OR_TRIGGER`, `SFXR_PULL_SOFT`). Whatever the player naturally tries
  works, and we see what it was.
- **A task is a row in a table:** `{ update() }`. `update()` draws the task, watches, and
  returns true when done. The instruction panel and the delayed hint are shared helpers.
- **Observations**, per task:

| Task | Watches | Becomes |
|---|---|---|
| Pick up the cube | which hand; grip or trigger; how hard they squeeze while holding | dominant hand; `grab` style; `pull` level |
| Turn the dial | wrist twist vs. dragging around the axis | knob habit (both work; recorded) |
| Press the button | poke or laser; hand shape at the press | `point_to_press`, laser-first |
| Click the far target | how hard they pull the trigger | `pull` level |

- **Inference is simple and explainable** (`infer()`):
  - grabbed with the trigger → grip-or-trigger grabs
  - never squeezed past about half → a light (SOFT) pull
  - more tasks done with the left hand → left-handed

  Then the player sees "what I noticed", can change any of it, and it's saved.
- **Apply** (`onboarding_apply()`): pushes the settings into vrui (`vrui_style()->grab`,
  `->pull`). The app maps the rest; in the toolbox, the wrist panel moves to the
  non-dominant hand.
- **Saved** as `prefs.cfg` next to the app, which on the headset is its folder.
  Deploying keeps it (`frame.sh deploy` doesn't touch it), and `frame.sh pull` fetches
  it. `onboarding_init()` loads and applies it at start; nothing else happens until
  something calls `onboarding_start()` (in the toolbox: the station panel's Start button).

## Replays and tests

The setup never starts on its own (when to run it is the app's choice: first launch, a
settings screen, never), and saved preferences are only used
there when given explicitly (`SFQ_PREFS=path/prefs.cfg`). That keeps replays of a
player's session reproducible: pull their `prefs.cfg` along with the recording.

## Making your own

1. Pick the 3–5 interactions your app depends on most.
2. For each, one task that needs it, a hint, and what to watch.
3. Map observations to settings you already have: vrui style fields, which hand holds
   the tool, snap vs. smooth turning, a comfort vignette...
4. Show the result and let the player change it. Never trap them in the setup: skip is
   always one click away.
