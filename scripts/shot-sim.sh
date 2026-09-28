#!/usr/bin/env bash
# Headless screenshot of an app running in the desktop simulator, optionally
# driving it with scripted mouse/keyboard input first.
#
#   scripts/shot-sim.sh <binary> <out.png> [action ...]
#
# Actions (run in order while the app is live; coordinates are window pixels):
#   sleep S | move X Y | down BTN | up BTN | click BTN | key K | keydown K | keyup K
#   (BTN: 1 left = trigger, 2 middle = grip, 3 right = look; K: xdotool keysym)
# Env: SHOT_FRAME (default 90, or 1200 when actions are given), SHOT_SIZE (1280x720),
#      plus anything sfxr reads (SFXR_SIM_LOOK, SFXR_SIM_POS, ...).
#
# Press and release on DIFFERENT frames: put a `sleep 0.2` between down and up.
set -euo pipefail
BIN=${1:?binary}; OUT=${2:?out.png}; shift 2
SIZE=${SHOT_SIZE:-1280x720}
if [ $# -gt 0 ]; then FRAME=${SHOT_FRAME:-100000}; else FRAME=${SHOT_FRAME:-90}; fi
mkdir -p "$(dirname "$OUT")"
OUT_ABS=$(readlink -f "$(dirname "$OUT")")/$(basename "$OUT")
LOG=${OUT_ABS%.png}.log
for d in $(seq 90 120); do [ -e /tmp/.X11-unix/X$d ] || { DISP=:$d; break; }; done
Xvfb "$DISP" -screen 0 "${SIZE}x24" >/dev/null 2>&1 &
XPID=$!
trap 'kill $XPID 2>/dev/null || true' EXIT
sleep 0.5
export DISPLAY=$DISP
rm -f "$OUT_ABS"
TRIGGER=$(mktemp -u)
LIBGL_ALWAYS_SOFTWARE=1 SFXR_BACKEND=sim SFXR_SHOT="$OUT_ABS" SFXR_SHOT_FRAME="$FRAME" \
  SFXR_SHOT_TRIGGER="$TRIGGER" timeout 180 "$BIN" > "$LOG" 2>&1 &
APP=$!
if [ $# -gt 0 ]; then
  for i in $(seq 100); do xdotool search --name . >/dev/null 2>&1 && break; sleep 0.1; done
  sleep 2
  WID=$(xdotool search --name . | head -1)
  xdotool windowactivate "$WID" 2>/dev/null || xdotool windowfocus "$WID" 2>/dev/null || true
  for a in "$@"; do
    set -- $a
    case "$1" in
      sleep)   sleep "$2" ;;
      move)    xdotool mousemove "$2" "$3" ;;
      down)    xdotool mousedown "$2" ;;
      up)      xdotool mouseup "$2" ;;
      click)   xdotool mousedown "$2"; sleep 0.2; xdotool mouseup "$2" ;;
      key)     xdotool keydown "$2"; sleep 0.15; xdotool keyup "$2" ;;
      keydown) xdotool keydown "$2" ;;
      keyup)   xdotool keyup "$2" ;;
      *) echo "unknown action: $a" >&2 ;;
    esac
  done
  sleep 0.3
  touch "$TRIGGER"          # ask sfxr to take the shot now
fi
wait $APP || true
rm -f "$TRIGGER"
if [ -f "$OUT_ABS" ]; then echo "screenshot: $OUT_ABS"; else echo "no screenshot; see $LOG"; tail -20 "$LOG"; exit 1; fi
