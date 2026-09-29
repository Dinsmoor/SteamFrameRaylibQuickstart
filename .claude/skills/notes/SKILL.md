---
name: notes
description: Act on design notes the developer said inside the VR headset (docs/NOTES.md). Use when a <channel source="notes"> event arrives, or when asked to check for feedback, notes, or what was said in the headset.
---

A design note is a short spoken instruction recorded in the headset by holding
VIEW, with the moment it was said in: what each hand pointed at, what the eyes
or head looked at, where the player stood, and a screenshot. `notesd` (a user
service on this machine) transcribes each one and appends a JSON line to
`local-data/notes/<app>.jsonl`; the `notes` channel pushes new lines into this
session as they arrive.

## Read the note

1. **From a channel event:** the `id` and `app` attributes name it. Find its
   line: `grep '"id":"<id>"' local-data/notes/<app>.jsonl`. **Asked to check:**
   the unseen lines are those after the count in `local-data/notes/<app>.seen`
   (create it with 0 if missing). Handle them in order; write the new count
   back when done.
2. Each line has: `text` (the transcript), `context` (what the app said you
   meant: `place`, `pointing.left/right`, `looking`, `looking_by`), `head`
   (position and forward), `hands` (aim and grip poses), `screenshot` (a PNG
   path, both eyes side by side on the headset; look at it with Read),
   `seconds`, `clock`, `dir` (where the wav/json/png are).
3. Transcripts are Whisper's: read them the way you'd read a colleague across
   the room. "This" and "that" mean the pointed-at or looked-at thing. A blank
   `text` means nothing was heard; say so and move on.

## Resolve what was meant

- Registry names are `<group>.<label>` from the widget registry (vrui.h §12):
  the group table is in `examples/toolbox/main.c` (`GROUPS`), labels are the
  widgets' labels, marks are `vrui_mark()` calls. `garden.bug 3` is
  `GD.bugs[3]` in `examples/toolbox/garden.c`; `voice.bug 2` is the Voice
  station's third minion; `table.sky` the workbench's SKY lever.
- `place` names the toolbox area (station names in `examples/toolbox/notes.c`).
- If nothing was pointed at, use `looking`, then `place`, then the screenshot.

## Do it

- An **asset** change ("make a variant of this bug that flies", "give it a
  hat", "a darker red"): the `/asset` skill. Sources are
  `examples/<app>/assets/<name>.py`; a variant is a new file or a parameter.
- A **behavior** change ("this one should hover and spit", "the lever's too
  stiff"): edit the C, keep the toolbox's rules (CLAUDE.md), add or extend a
  test when a promise changes (docs/TESTING.md).
- **Build and check before deploying:** `make`, then the tests that cover what
  changed (`make test T=<suite>`, or `make test`), and a screenshot
  (`make shot EX=toolbox` or `make view-shot MODEL=...`) that you look at.
- **Deploy** with the `/deploy` skill when the change is done, or when the
  note asks to see something. The developer is wearing the headset: prefer a
  sensible default over a question, and say what you assumed.
- Reply in the session briefly: what you understood, what you changed, and
  what to look at. There is no channel back into the headset yet.
