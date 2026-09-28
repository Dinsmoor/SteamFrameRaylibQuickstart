#!/usr/bin/env bash
# clips.sh - split a recorded session into clips, one per interaction, using
# its event log; look at them; keep the good ones as regression tests.
#
#   scripts/clips.sh SESSION.sfxrec                     list the clips
#   scripts/clips.sh SESSION.sfxrec --shots DIR         + pictures of each (start, middle, end)
#   scripts/clips.sh SESSION.sfxrec --keep N NAME       keep clip N as tests/regress/NAME
#
# Options: --events FILE (default: SESSION.events next to the recording),
#          --app NAME (default: the app named in the recording, else toolbox).
#
# A clip is one interaction: a grab until its release (a lever pulled, a block
# thrown), or a moment with a second either side (a button press, a click, a
# teleport). Recordings store what the hands did in the room, not where the
# player was in the world, so a clip can't start mid-session on its own: it
# is a window into the session. Keeping one writes the session up to the
# clip's end as the test's input and checks three frames inside the clip (a
# replay runs from the start, fast, without rendering the frames it doesn't
# save). Headset sessions (scripts/frame.sh pull) and failing tests
# (build/host-test/test-artifacts/<case>/run.sfxrec + its events) both work.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

REC=${1:?usage: scripts/clips.sh SESSION.sfxrec [--events FILE] [--shots DIR | --keep N NAME] [--app NAME]}
shift
EVENTS=${REC%.sfxrec}.events
SHOTS=""; KEEP=""; KEEP_NAME=""; APP=""
while [ $# -gt 0 ]; do
  case $1 in
    --events) EVENTS=$2; shift 2 ;;
    --shots)  SHOTS=$2; shift 2 ;;
    --keep)   KEEP=$2; KEEP_NAME=${3:?--keep N NAME}; shift 3 ;;
    --app)    APP=$2; shift 2 ;;
    *) echo "unknown option $1"; exit 2 ;;
  esac
done
[ -f "$REC" ] || { echo "no recording $REC"; exit 2; }
if [ ! -f "$EVENTS" ]; then
  # a failing test keeps its events as <suite>_<case>.events beside run.sfxrec
  alt=$(ls "$(dirname "$REC")"/*.events 2>/dev/null | head -1 || true)
  [ -n "$alt" ] && EVENTS=$alt || { echo "no event log $EVENTS (--events FILE)"; exit 2; }
fi

# The clips, as lines: "n kind label hand first_index last_index note"
CLIPS=$(python3 - "$REC" "$EVENTS" <<'PY'
import struct, sys
rec, evpath = sys.argv[1], sys.argv[2]
with open(rec, 'rb') as f:
    magic, ver, hsize, fsize = struct.unpack('<8sIII', f.read(20))
    f.seek(0, 2); n = (f.tell() - hsize) // fsize
    index = {}
    for i in range(n):
        f.seek(hsize + i * fsize)
        index[struct.unpack('<Q', f.read(8))[0]] = i   # the app's frame number -> recording index
lo, hi = min(index), max(index)
def at(frame):   # the recording index of an app frame (nearest recorded one)
    frame = max(lo, min(hi, frame))
    while frame not in index and frame < hi: frame += 1
    return index.get(frame, n - 1)
events = []
for line in open(evpath):
    if line.startswith('#') or not line.strip(): continue
    parts = line.split(None, 3)
    if len(parts) < 3: continue
    t, fr, kind = float(parts[0]), int(parts[1][1:]), parts[2]
    events.append((t, fr, kind, parts[3].strip() if len(parts) > 3 else ''))
def split_hand(words):   # "SPAWN (full pull) R hand" -> ("SPAWN (full pull)", "R")
    for i in range(len(words) - 1, -1, -1):
        if words[i] in ('L', 'R'): return ' '.join(words[:i]), words[i]
    return ' '.join(words), '-'
clips, open_ = [], {}
margin = 36   # half a second at 72 Hz
for t, fr, kind, text in events:
    if fr < lo or fr > hi: continue
    words = text.split()
    if kind == 'grab':
        label, hand = split_hand(words)
        open_[(label, hand)] = len(clips); clips.append(['grab', label, hand, fr, None, ''])
    elif kind == 'release':
        label, hand = split_hand(words)
        if (label, hand) in open_: clips[open_.pop((label, hand))][4] = fr
    elif kind == 'value':
        label = ' '.join(words[:-1])
        for c in reversed(clips):
            if c[1] == label: c[5] = 'value ' + words[-1]; break
    elif kind in ('press', 'click', 'toggle', 'flip', 'teleport', 'fire', 'insert', 'turn', 'menu', 'smash', 'hit', 'bite'):
        if kind == 'press': label, note = text.split(' by ')[0], 'by ' + text.split(' by ')[-1]
        elif kind in ('toggle', 'flip'): label, note = ' '.join(words[:-1]), words[-1] if words else ''
        elif kind == 'turn' and '->' in text: label, note = text.split('->')[0].strip(), '-> ' + text.split('->')[1].strip()
        elif kind == 'teleport': label, note = '-', text
        else: label, note = text[:40], ''
        clips.append([kind, label, '-', fr, fr, note[:40]])
for i, (kind, label, hand, a, b, note) in enumerate(clips, 1):
    b = b if b is not None else a
    first, last = at(a - margin), at(b + margin)
    print(i, kind, (label or '-').replace(' ', '_'), hand, first, last, note.replace(' ', '_') or '-')
PY
)
[ -n "$CLIPS" ] || { echo "no interactions in $EVENTS (within the recording)"; exit 0; }

printf "%3s  %-8s %-24s %-4s %-14s %s\n" "#" "what" "widget" "hand" "frames" "note"
while read -r n kind label hand a b note; do
  printf "%3s  %-8s %-24s %-4s %-14s %s\n" "$n" "$kind" "${label//_/ }" "$hand" "$a-$b" "${note//_/ }"
done <<<"$CLIPS"

APP_NAME=${APP:-toolbox}
BIN=${BIN_DIR:-build/host-debug/bin}/$APP_NAME

frames_of() {   # three frames inside a clip: start, middle, end (1-based replay frames)
  python3 -c "a,b=$1,$2; print(' '.join(str(x+1) for x in (a, (a+b)//2, b)))"
}

replay() {   # recording out_pattern frames...
  local rec=$1 out=$2; shift 2
  command -v Xvfb >/dev/null || { echo "needs Xvfb"; exit 2; }
  [ -x "$BIN" ] || { echo "$BIN not built (make)"; exit 2; }
  local d; for d in $(seq 90 120); do [ -e /tmp/.X11-unix/X$d ] || { DISP=:$d; break; }; done
  Xvfb "$DISP" -screen 0 1280x720x24 >/dev/null 2>&1 & local x=$!
  sleep 0.5
  DISPLAY=$DISP LIBGL_ALWAYS_SOFTWARE=1 SFXR_REPLAY="$rec" SFXR_REPLAY_SCALE=0.5 SFXR_SHOT="$out" \
    SFXR_SHOT_FRAMES="$(echo "$*" | tr ' ' ',')" timeout 600 "$BIN" >/dev/null 2>&1 || true
  kill $x 2>/dev/null || true
}

if [ -n "$SHOTS" ]; then
  mkdir -p "$SHOTS"
  all=""
  while read -r n kind label hand a b note; do all+=" $(frames_of "$a" "$b")"; done <<<"$CLIPS"
  replay "$REC" "$SHOTS/f%06d.png" $all
  while read -r n kind label hand a b note; do
    k=0
    for f in $(frames_of "$a" "$b"); do
      k=$((k + 1))
      [ -f "$SHOTS/$(printf 'f%06d.png' "$f")" ] && cp "$SHOTS/$(printf 'f%06d.png' "$f")" "$SHOTS/$(printf 'clip%02d-%d-%s.png' "$n" "$k" "$(sed 's/[^A-Za-z0-9]/_/g' <<<"$label")")"
    done
  done <<<"$CLIPS"
  rm -f "$SHOTS"/f??????.png
  echo "pictures: $SHOTS/clipNN-{1,2,3}-<widget>.png (start, middle, end of each clip)"
fi

if [ -n "$KEEP" ]; then
  line=$(awk -v n="$KEEP" '$1 == n' <<<"$CLIPS")
  [ -n "$line" ] || { echo "no clip $KEEP"; exit 2; }
  read -r n kind label hand a b note <<<"$line"
  dir=tests/regress/$KEEP_NAME
  [ -e "$dir" ] && { echo "$dir exists"; exit 2; }
  mkdir -p "$dir"
  # the session up to the clip's end (a replay needs everything before it)
  python3 - "$REC" "$dir/input.sfxrec" "$b" <<'PY'
import struct, sys
src, dst, last = sys.argv[1], sys.argv[2], int(sys.argv[3])
d = open(src, 'rb').read()
magic, ver, hsize, fsize = struct.unpack('<8sIII', d[:20])
open(dst, 'wb').write(d[:hsize + (last + 1) * fsize])
PY
  echo "$APP_NAME" > "$dir/app"
  frames_of "$a" "$b" > "$dir/frames"
  echo "# kept from $(basename "$REC"): clip $n, $kind $label ($hand)" > "$dir/README"
  scripts/regress.sh --bless "$KEEP_NAME"
  echo "kept: $dir (make regress checks it from now on)"
fi
