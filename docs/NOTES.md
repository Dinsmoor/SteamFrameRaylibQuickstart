# Design notes: talking to your tools from inside the headset

The loop this quickstart is built for: you stand in the game, look at a thing,
hold a button, and say what you want changed. "Make a variant of this bug that
flies and spits arcs of slop at you while it stops to hover." A few seconds
later the sentence, what you were pointing at, where you stood and a
screenshot land in a Claude Code session on the build machine, which edits the
code or the asset, rebuilds and deploys. You never take the headset off.

The pieces, in order:

```
headset: hold VIEW, talk, let go        sfxr_note_begin / sfxr_note_end   (sfxr_voice.h)
   -> notes/<id>.wav .json .png         next to the app
build machine: notesd (user service)    tools/notes/notesd.py
   -> frame.sh notes <app>              pulls new notes over the paired key every 2 s
   -> whisper-server (user service)     whisper.cpp, large-v3-turbo, on the GPU: text in ~0.3 s
   -> local-data/notes/<app>.jsonl      one line per note: text + context + paths
Claude Code: the "notes" channel        tools/notes/channel.ts (.mcp.json)
   -> <channel source="notes" ...>      the note, pushed into the live session
   -> the /notes skill                  reads, resolves "this", edits, builds, deploys
```

Nobody has done this loop for native C on a headset before as far as we
found; the parts that exist elsewhere are voice-into-a-terminal (VoidCode VR)
and editor MCP servers (Unity, Unreal, Godot). What's new here is the
**context packet**: the note carries what you meant by "this".

## In the headset

Hold **VIEW** (the small button on the left controller; in the simulator, key
`3` with the left hand active) and talk. A blue tag over the hand counts the
seconds. Let go: a green tag names the note. The clip includes the 0.3 s
before the press, because people start talking as they press (tested:
`voice/note-keeps-the-words-before-the-button`).

What gets saved (`notes/` next to the app; `SFXR_NOTES_DIR` moves it):

| file | what |
|---|---|
| `<id>.wav` | 16 kHz mono PCM, the whole note (up to 90 s) |
| `<id>.json` | `time`, `frame`, `clock`, `seconds`; `head` (position, forward); `hands.left/right` (aim and grip poses, or null); `gaze` when the headset tracks eyes; `context`: whatever the app passed |
| `<id>.png` | a screenshot of that frame: both eyes side by side on the headset, the window in the simulator |

The **toolbox's context** (`examples/toolbox/notes.c`) is `{"app", "place",
"pointing": {"left", "right"}, "looking", "looking_by"}`: the registry name of
the widget or mark nearest each hand's aim ray (within a few degrees) and the
gaze or head ray, and the station or area you're in. Registry names are
`<group>.<label>` (vrui.h §12): `garden.bug 3`, `table.sky`, `voice.bug 2`. The
garden marks every live bug so a note can name one. Your own app decides what
"this" means and passes any JSON.

Voice commands (`sfxr_voice_listen_*`, the bumper) and notes share the
microphone and don't interfere; a note needs no recognizer on the headset.

## On the build machine

Once: `scripts/notes.sh build` (whisper.cpp's server with CUDA if there's an
NVIDIA GPU, and the `large-v3-turbo` model, about 870 MB) and
`scripts/notes.sh install [app]`, which writes and enables two `systemd --user`
services: **sfq-whisper** (the transcriber on `127.0.0.1:9878`, about 1.6 GB of
GPU memory) and **sfq-notesd** (pull, transcribe, append). `scripts/notes.sh
status|log|stop|start`. Without a headset, notesd also watches `notes/` in the
repo, which is where the simulator writes them, so the loop can be tried on
the desk.

Why a big model here and the tiny one on the headset: commands are a few
known words, and tiny.en on four phone cores handles them; a note is a
sentence with the game's own nouns in it, and the turbo model on the build
machine's GPU gets those right in a fraction of a second while the headset
spends nothing. The words to expect are handed to the transcriber as a prompt
(`SFQ_NOTES_PROMPT`; edit it in `scripts/notes.sh` when your game's vocabulary
grows).

`scripts/notes.sh once [app] [--no-frame]` does one round in the foreground.

## Into Claude Code

`.mcp.json` names two servers: **notes** (the channel) and **blender** (the
asset side, docs/ASSETS.md). A channel is an MCP server that pushes events into
the session (the contract: code.claude.com/docs/en/channels-reference). Ours
tails `local-data/notes/*.jsonl` and sends each new line:

```
<channel source="notes" app="toolbox" id="note-20260929-013012" place="garden (Daddy Bug Smasher)"
         pointing_right="garden.bug 3" looking="garden.bug 3" looking_by="head"
         json="local-data/notes/toolbox/note-20260929-013012.json" screenshot="...png">
"make a variant of this bug that flies and spits arcs of slop at you while it stops to hover"
context: {"app":"toolbox","place":"garden (Daddy Bug Smasher)","pointing":{"left":null,"right":"garden.bug 3"},...}
head at [0.1,1.6,-0.2] looking [0,-0.2,-1]; note note-20260929-013012 in toolbox; screenshot ...
</channel>
```

Channels are a research preview, so the session starts with a flag:
`scripts/notes.sh claude` runs `claude --dangerously-load-development-channels
server:notes` (and brings the Blender bridge up). The first time, Claude Code
asks to trust the project's `.mcp.json`. Events that arrive while Claude is
busy queue up and are handled together. The `/notes` skill is what Claude
does with one: resolve "this" from the context, change code or assets, build,
test, deploy (`/deploy`). Asked "check for notes", it reads the lines after
the count in `local-data/notes/<app>.seen`.

Work from the headset or a laptop: run Claude Code on the build machine and
drive it with Remote Control (`claude remote-control`) from anywhere, or just
talk to it through notes.

## Trying it without a headset

`make sim EX=toolbox`, hold `3` with the left hand active (`Tab` switches),
talk (the simulator opens the machine's default microphone), let go: `notes/`
in the repo gets the files. `scripts/notes.sh once toolbox --no-frame`
transcribes them. Or drop any WAV plus a JSON line into `notes/` by hand; the
tests do the equivalent through `sfxr_voice_feed`.

## What's next

- A way back: a line of text or a chime in the headset when Claude has
  deployed something ("your flying bug is in wave 2").
- Hot reload, so a deploy doesn't restart the app: first resources (models,
  textures, shaders reloaded when their files change), then the game code as
  a shared library the host reloads.
- The tiny recognizer could give a first guess on the headset for an instant
  "heard: ..." tag, with the build machine's transcript replacing it.
