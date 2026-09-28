#!/usr/bin/env bash
# Copy dist/<app>/ to the Steam Frame over SSH and (optionally) start it.
#
#   scripts/deploy.sh <app> <user@frame-host> [--run]
#
# Prerequisites on the headset (Steam Settings > System):
#   * Enable Developer Mode (turns on SSH)
#   * Developer section > Set User Password; the username is shown there
#   * Optionally set a Hostname so <name>.local resolves
#
# --run starts the app in the headset's graphical session. SteamVR must be
# running (it is whenever you are in the Frame's VR home). This relies on the
# session's DISPLAY / XDG_RUNTIME_DIR -- UNVERIFIED on real hardware; if it
# fails, launch through the SteamOS Devkit Client instead (see README).
set -euo pipefail
APP=${1:?app}; HOST=${2:?user@host (set FRAME_HOST=... for make deploy)}; RUN=${3:-}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC="$ROOT/dist/$APP"
[ -d "$SRC" ] || { echo "missing $SRC -- run 'make package EX=$APP'"; exit 1; }
DEST="sfq/$APP"
ssh "$HOST" "mkdir -p $DEST"
rsync -az --delete "$SRC/" "$HOST:$DEST/"
echo "deployed to $HOST:~/$DEST"
if [ "$RUN" = "--run" ]; then
  ssh -t "$HOST" "cd $DEST && export DISPLAY=\${DISPLAY:-:0} XDG_RUNTIME_DIR=\${XDG_RUNTIME_DIR:-/run/user/\$(id -u)} && ./xr_probe; ./launch.sh"
else
  echo "run it:  ssh -t $HOST 'cd $DEST && DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/\$(id -u) ./launch.sh'"
fi
