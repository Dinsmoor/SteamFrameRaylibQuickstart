#!/usr/bin/env bash
# Golden-image regression tests from recorded input sessions.
#
#   scripts/regress.sh [--bless] [test-name ...]      (default: every test)
#
# A test is a folder tests/regress/<name>/ containing:
#   input.sfxrec   recorded inputs (make record ..., or scripts/frame.sh record ...)
#   app            the program to replay it with, e.g. "toolbox"
#   frames         (optional) frame numbers to compare, e.g. "120 240 360"
#                  (written automatically on first --bless: 6 evenly spaced frames)
#   golden/        approved screenshots (created by --bless)
#
# Each run replays the recording offscreen (Mesa software rendering under Xvfb,
# so results are identical on any machine), saves the listed frames to
# shots/regress/<name>/ and compares them to golden/ with a small tolerance.
# Differences are written as *-diff.png next to the outputs.
#
#   --bless   accept the current output as the new golden images
# Env: REGRESS_SCALE (eye resolution scale for replays, default 0.5),
#      REGRESS_TOLERANCE (fraction of pixels allowed to differ, default 0.002)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
BLESS=0
[ "${1:-}" = "--bless" ] && { BLESS=1; shift; }
BIN_DIR=${BIN_DIR:-build/host-debug/bin}
SCALE=${REGRESS_SCALE:-0.5}
TOL=${REGRESS_TOLERANCE:-0.002}
command -v Xvfb >/dev/null || { echo "needs Xvfb"; exit 2; }
command -v compare >/dev/null || { echo "needs ImageMagick (compare)"; exit 2; }

tests=("$@")
if [ ${#tests[@]} -eq 0 ]; then
  for d in tests/regress/*/; do [ -f "$d/input.sfxrec" ] && tests+=("$(basename "$d")"); done
fi
[ ${#tests[@]} -gt 0 ] || { echo "no tests in tests/regress/ (record one: make record EX=toolbox NAME=my-test)"; exit 0; }

for d in $(seq 90 120); do [ -e /tmp/.X11-unix/X$d ] || { DISP=:$d; break; }; done
Xvfb "$DISP" -screen 0 1280x720x24 >/dev/null 2>&1 &
XPID=$!
trap 'kill $XPID 2>/dev/null || true' EXIT
sleep 0.5

frame_count() {  # number of frames in a recording (reads the header sizes)
  python3 - "$1" <<'PY'
import os, struct, sys
p = sys.argv[1]
with open(p, 'rb') as f:
    magic, ver, hsize, fsize = struct.unpack('<8sIII', f.read(20))
print((os.path.getsize(p) - hsize) // fsize)
PY
}

fail=0
for t in "${tests[@]}"; do
  dir=tests/regress/$t
  app=$(tr -d ' \n' < "$dir/app")
  bin=$BIN_DIR/$app
  [ -x "$bin" ] || { echo "FAIL $t: $bin not built (make)"; fail=1; continue; }
  if [ ! -f "$dir/frames" ]; then
    n=$(frame_count "$dir/input.sfxrec")
    python3 -c "n=$n; print(' '.join(str(max(1, n*k//7)) for k in range(1,7)))" > "$dir/frames"
  fi
  frames=$(cat "$dir/frames")
  out=shots/regress/$t
  rm -rf "$out"; mkdir -p "$out"
  DISPLAY=$DISP LIBGL_ALWAYS_SOFTWARE=1 SFXR_REPLAY="$dir/input.sfxrec" SFXR_REPLAY_SCALE="$SCALE" \
    SFXR_SHOT="$out/f%06d.png" SFXR_SHOT_FRAMES="$(echo $frames | tr ' ' ',')" \
    timeout 600 "$bin" > "$out/replay.log" 2>&1 || true
  if [ $BLESS = 1 ]; then
    rm -rf "$dir/golden"; mkdir -p "$dir/golden"
    cp "$out"/f*.png "$dir/golden/" 2>/dev/null || { echo "FAIL $t: replay produced no images (see $out/replay.log)"; fail=1; continue; }
    echo "BLESSED $t ($(ls "$dir/golden" | wc -l) images, frames: $frames)"
    continue
  fi
  status=PASS; detail=""
  for f in $frames; do
    name=$(printf 'f%06d.png' "$f")
    if [ ! -f "$out/$name" ]; then status=FAIL; detail+=" [frame $f: no output]"; continue; fi
    if [ ! -f "$dir/golden/$name" ]; then status=FAIL; detail+=" [frame $f: no golden; run --bless]"; continue; fi
    px=$(identify -format '%[fx:w*h]' "$out/$name")
    ae=$(compare -metric AE -fuzz 3% "$dir/golden/$name" "$out/$name" "$out/${name%.png}-diff.png" 2>&1 >/dev/null | awk '{print int($1)}') || true
    ae=${ae:-999999999}
    if python3 -c "import sys; sys.exit(0 if $ae <= $px*$TOL else 1)"; then :; else
      status=FAIL; detail+=" [frame $f: $ae px differ]"
    fi
  done
  echo "$status $t$detail"
  [ $status = PASS ] || fail=1
done
exit $fail
