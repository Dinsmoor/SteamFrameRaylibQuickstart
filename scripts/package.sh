#!/usr/bin/env bash
# Stage a Steam Frame build into dist/<app>/ -- the folder you point the
# SteamOS Devkit Client at (target: "Steam Linux Runtime 3.0 ARM64"), rsync to
# the headset, or later upload as a Steam depot.
#
#   scripts/package.sh <app> [build_dir=build/frame-release]
#
# dist/<app>/
#   <app>                 the binary (aarch64, built in the sniper SDK)
#   launch.sh             cd's next to the binary and runs it (use as launch command)
#   vrpreferences.json    Steam Frame default video settings (see CLAUDE.md)
#   xr_probe              runtime capability dump; run it first on a new device
#   resources/            copied from examples|apps/<app>/resources if present
#   libsteam_api.so       only with STEAMWORKS_SDK=<unpacked Steamworks SDK> (docs/STEAM.md),
#   steam_appid.txt       plus the app id: SFQ_STEAM_APPID, default 480 (Valve's test app)
set -euo pipefail
APP=${1:?app name}; BUILD=${2:-build/frame-release}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
BIN="$BUILD/bin/$APP"
[ -x "$BIN" ] || { echo "missing $BIN -- run 'make frame' first"; exit 1; }
file "$BIN" | grep -q aarch64 || echo "warning: $BIN is not aarch64"
SRC=""
for d in "apps/$APP" "examples/$APP"; do [ -d "$d" ] && SRC=$d; done
OUT="dist/$APP"
rm -rf "$OUT"; mkdir -p "$OUT"
cp "$BIN" "$OUT/"
[ -x "$BUILD/bin/xr_probe" ] && cp "$BUILD/bin/xr_probe" "$OUT/"
if [ -n "$SRC" ] && [ -f "$SRC/vrpreferences.json" ]; then cp "$SRC/vrpreferences.json" "$OUT/";
else cp templates/vrpreferences.json "$OUT/"; fi
[ -n "$SRC" ] && [ -d "$SRC/resources" ] && cp -r "$SRC/resources" "$OUT/"
# Steamworks (optional). The SDK never goes in the repo: its license lets you
# ship the redistributable library with your game, nothing more.
if [ -n "${STEAMWORKS_SDK:-}" ]; then
  arch=linux64; file "$BIN" | grep -q aarch64 && arch=linuxarm64
  lib="$STEAMWORKS_SDK/redistributable_bin/$arch/libsteam_api.so"
  [ -f "$lib" ] || { echo "no $lib (the Frame needs SDK 1.64 or newer for linuxarm64)"; exit 1; }
  cp "$lib" "$OUT/"
  # Needed when the game isn't launched by Steam under its own app id (devkit
  # launches, your desk). Leave it out of a real Steam release.
  echo "${SFQ_STEAM_APPID:-480}" > "$OUT/steam_appid.txt"
fi
cat > "$OUT/launch.sh" <<'LAUNCH'
#!/bin/sh
# Launch command registered with Steam ("Devkit Game") and used by scripts/frame.sh.
#   * runs from its own directory, so relative resource paths work
#   * logs/diag.txt : environment + xr_probe as seen from inside Steam's launch
#   * logs/run.log  : the app's stdout/stderr (previous run kept as prev-run.log)
#   * launch.env    : optional one-shot settings (SFXR_* etc.) written by frame.sh
#   * recordings/   : every launch's inputs, newest 5 kept (for turning into tests)
cd "$(dirname "$0")" || exit 1
APP=__APP__
mkdir -p logs shots
[ -f logs/run.log ] && mv -f logs/run.log logs/prev-run.log
{
  echo "== $(date) launching $APP (pid $$)"
  echo "== id: $(id)"
  echo "== env"; env | sort
  echo "== xr_probe"; ./xr_probe 2>&1
} > logs/diag.txt 2>&1
# One-shot per-run settings from scripts/frame.sh: used for THIS launch only,
# so a later launch from the Steam library starts clean.
if [ -f launch.env ]; then
  . ./launch.env
  mv -f launch.env logs/last-launch.env
fi
# Every launch records its inputs (SFQ_AUTORECORD=0 to disable), keeping the
# newest 5 sessions: anything interesting you do can become a test afterwards
# (scripts/frame.sh sessions / keep).
if [ -z "${SFXR_RECORD:-}" ] && [ "${SFQ_AUTORECORD:-1}" != 0 ]; then
  mkdir -p recordings
  export SFXR_RECORD="recordings/session-$(date +%Y%m%d-%H%M%S).sfxrec"
  ls -1t recordings/session-*.sfxrec 2>/dev/null | tail -n +5 | xargs -r rm -f
fi
# In the headset the desktop mirror window is useless (and may show up as a
# flat window); keep the GL context's window hidden unless asked otherwise.
export SFXR_MIRROR="${SFXR_MIRROR:-0}"
exec "$PWD/$APP" "$@" > logs/run.log 2>&1
LAUNCH
sed -i "s/__APP__/$APP/" "$OUT/launch.sh"
chmod +x "$OUT/launch.sh"
echo "packaged $OUT:"; ls -la "$OUT"
