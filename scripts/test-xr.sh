#!/usr/bin/env bash
# Run an app against a real OpenXR runtime (Monado, simulated HMD) inside the
# sfq-monado container -- no headset or GPU needed. Produces:
#   <out.png>              the app's mirror window (left eye, what sfxr submitted)
#   <out>-compositor.png   Monado's compositor window (both eyes, post-distortion)
#   <out>.log / <out>-monado.log
#
#   scripts/test-xr.sh <binary> <out.png> [backend=gl|vk|auto] [settle_seconds=4]
#
# Build the image first: make monado-image
set -euo pipefail
BIN=${1:?binary}; OUT=${2:?out.png}; BACKEND=${3:-auto}; SETTLE=${4:-4}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$(dirname "$OUT")"
OUT_REL=$(realpath --relative-to="$ROOT" "$(readlink -f "$(dirname "$OUT")")/$(basename "$OUT")")
BIN_REL=$(realpath --relative-to="$ROOT" "$(readlink -f "$BIN")")
BASE=${OUT_REL%.png}
# Pass SFXR_* settings through (e.g. SFXR_RECORD=tests/regress/x/input.sfxrec; paths relative to the repo)
ENVARGS=()
while IFS= read -r kv; do ENVARGS+=(-e "$kv"); done < <(env | grep '^SFXR_' | grep -vE '^SFXR_(BACKEND|SHOT|SHOT_FRAME|SHOT_TRIGGER)=' || true)
docker run --rm "${ENVARGS[@]}" -v "$ROOT":/w -w /w sfq-monado bash -c "
  export XDG_RUNTIME_DIR=/tmp/xdg; mkdir -p \$XDG_RUNTIME_DIR; chmod 700 \$XDG_RUNTIME_DIR
  Xvfb :5 -screen 0 1920x1080x24 >/dev/null 2>&1 & sleep 1
  export DISPLAY=:5
  # monado-service epolls stdin, so give it a pipe that never closes
  sleep infinity | SIMULATED_ENABLE=1 XRT_COMPOSITOR_FORCE_XCB=1 monado-service > '$BASE-monado.log' 2>&1 &
  for i in \$(seq 50); do [ -S \$XDG_RUNTIME_DIR/monado_comp_ipc ] && break; sleep 0.1; done
  SFXR_BACKEND=$BACKEND SFXR_SHOT=/w/'$OUT_REL' SFXR_SHOT_FRAME=1000000 SFXR_SHOT_TRIGGER=/tmp/shoot \\
    timeout 120 ./'$BIN_REL' > '$BASE.log' 2>&1 &
  APP=\$!
  # wait for the XR session, let it run a few seconds, grab Monado's window,
  # then ask the app to save its mirror and exit
  for i in \$(seq 300); do grep -q 'session running' '$BASE.log' 2>/dev/null && break; kill -0 \$APP 2>/dev/null || break; sleep 0.1; done
  sleep $SETTLE
  MW=\$(xdotool search --name '^Monado' 2>/dev/null | head -1)
  [ -n \"\$MW\" ] && import -window \"\$MW\" '$BASE-compositor.png' 2>/dev/null || true
  touch /tmp/shoot
  wait \$APP || true
  chown -R $(id -u):$(id -g) /w/shots /w/tests 2>/dev/null || true
"
grep -E 'SFXR' "$ROOT/$BASE.log" | grep -vE 'runtime ext:' | head -30
[ -f "$ROOT/$OUT_REL" ] && echo "mirror:     $ROOT/$OUT_REL" && echo "compositor: $ROOT/$BASE-compositor.png"
