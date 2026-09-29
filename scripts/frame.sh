#!/usr/bin/env bash
# frame.sh - drive a Steam Frame (Developer Mode) from your build machine over SSH.
#
# This does exactly what Valve's SteamOS Devkit Client does for a "Title
# Upload" (same device-side helper scripts, vendored in tools/devkit-utils),
# but from the command line, so the headset can sit on the desk while you
# iterate: upload -> launch -> read logs -> pull screenshots.
#
#   scripts/frame.sh pair [<frame-ip-or-name>]          one time: press "Pair devkit" in the
#                    headset's Developer settings, run this, approve on the headset
#   scripts/frame.sh status                             Steam/SteamVR/session environment
#   scripts/frame.sh probe [--paths]                    xr_probe over plain SSH (--paths: which
#                    controller inputs the Frame really has)
#   scripts/frame.sh deploy <app>                       package + upload + register in Steam
#   scripts/frame.sh run <app> [secs] [KEY=VAL ...]     launch via Steam, follow its log
#   scripts/frame.sh shot <app> [frame] [KEY=VAL ...]   run until frame N, pull both-eye PNG
#                    (e.g. SFXR_RIG=x,y,z,yaw to aim the view while the headset sits on a desk)
#   scripts/frame.sh record <app> <test-name> [secs]    record a headset session (default 60 s)
#                    into tests/regress/<test-name>/ for replay tests (scripts/regress.sh)
#   scripts/frame.sh sessions <app>                     list auto-recorded sessions (every launch records)
#   scripts/frame.sh keep <app> <test-name> [session]   copy a session (default newest) into tests/regress/
#   scripts/frame.sh push-recording <app> <file>        copy a recording to the headset (replay on its GPU)
#   scripts/frame.sh pull <app> [dest]                  copy all recorded sessions, screenshots taken in the
#                    headset + app/SteamVR logs to local-data/<app>-<time>/ (git-ignored) for review
#   scripts/frame.sh logs <app>                         pull app + Steam logs to shots/frame/
#   scripts/frame.sh stop <app>                         kill a running instance
#   scripts/frame.sh exec '<command>'                  run one command on the headset
#   scripts/frame.sh list | delete <app> | shell
#
# Everything talks to the headset ONLY through the key registered by 'pair'
# (Valve's devkit pairing). There is deliberately no password login path.
#
# The target is remembered in .frame-host (gitignored). Override with FRAME_HOST.
# Runtime (compat tool) defaults to Steam Linux Runtime 4.0 ARM64, the Linux
# ARM runtime Valve's devkit client offers for the Frame; override with
# FRAME_RUNTIME=SteamLinuxRuntime_sniper etc.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
HOSTFILE=${FRAME_HOSTFILE:-$ROOT/.frame-host}   # which paired headset (override for a 2nd device / the fake Frame)
RUNTIME=${FRAME_RUNTIME:-SteamLinuxRuntime_4-arm64}
BUILD=${FRAME_BUILD:-build/frame-release}
DEVKIT_PORT=32000
KEY=${FRAME_KEY:-$HOME/.ssh/sfq_frame}      # key registered by 'pair'
SSH_OPTS=(-o ConnectTimeout=8 -o ServerAliveInterval=10 -o StrictHostKeyChecking=accept-new)
[ -f "$KEY" ] && SSH_OPTS+=(-i "$KEY")

die()  { echo "frame.sh: $*" >&2; exit 1; }
host() {
    if [ -n "${FRAME_HOST:-}" ]; then echo "$FRAME_HOST"; return; fi
    [ -f "$HOSTFILE" ] || die "not paired yet: press 'Pair devkit' on the headset, then run 'scripts/frame.sh pair'"
    cat "$HOSTFILE"
}
# BatchMode: never fall back to password prompts -- only the paired key is used.
rsh()  { [ -f "$KEY" ] || die "no paired key ($KEY): run 'scripts/frame.sh pair'"; ssh "${SSH_OPTS[@]}" -o BatchMode=yes -o IdentitiesOnly=yes "$(host)" "$@"; }
jsonq(){ python3 -c 'import json,sys; print(json.dumps(sys.argv[1]))' "$1"; }

# Run a devkit-utils helper that talks to the Steam client and needs the
# session's environment; the helpers only need HOME, so plain SSH is fine.
dk()   { rsh "python3 ~/devkit-utils/$*"; }

push_utils() {
    rsync -az --delete -e "ssh ${SSH_OPTS[*]}" "$ROOT/tools/devkit-utils/" "$(host):devkit-utils/"
}

# --- pairing (same protocol as Valve's SteamOS Devkit Client "Register") ---
# The headset runs a small devkit HTTP service on :32000. In pairing mode it
# accepts a POSTed SSH public key (with the client's fixed marker phrase) and
# asks the wearer to approve it; the key then lands in ~/.ssh/authorized_keys.
DEVKIT_MAGIC=900b919520e4cf601998a71eec318fec

discover() {
    # 1) mDNS service advertised by devkit-enabled SteamOS devices
    if command -v avahi-browse >/dev/null; then
        local a
        a=$(timeout 6 avahi-browse -rpt _steamos-devkit._tcp 2>/dev/null | awk -F';' '$1=="=" && $3=="IPv4" {print $8; exit}')
        [ -n "$a" ] && { echo "$a"; return; }
    fi
    # 2) scan our IPv4 /24 subnets for the devkit service port
    local net
    for net in $(ip -4 -o addr show scope global | awk '{print $4}' | grep -vE '^(172\.1[6-9]|172\.2|172\.3)' | sed 's#\.[0-9]*/24$##'); do
        for i in $(seq 1 254); do
            ( timeout 0.5 bash -c "</dev/tcp/$net.$i/$DEVKIT_PORT" 2>/dev/null && echo "$net.$i" ) &
        done
        wait
    done | head -1
}

cmd_pair() {
    local addr=${1:-}
    addr=${addr#*@}
    if [ -z "$addr" ]; then
        echo "== looking for a Steam Frame in developer mode on the local network..."
        addr=$(discover)
        [ -n "$addr" ] || die "none found. Is Developer Mode on and the headset on this network? Or pass its IP: frame.sh pair <ip>"
    fi
    local props login
    props=$(curl -s -m 5 "http://$addr:$DEVKIT_PORT/properties.json") || die "no devkit service at $addr:$DEVKIT_PORT (Developer Mode on?)"
    login=$(python3 -c 'import json,sys; print(json.load(sys.stdin).get("login","steamos"))' <<<"$props")
    echo "== found devkit at $addr (login: $login)"
    if [ ! -f "$KEY" ]; then
        # Must match Valve's client: RSA 2048 with a "devkit-client:user@host"
        # comment -- the headset's approve script rejects other keys
        # ("Failed to write the ssh key").
        ssh-keygen -q -t rsa -b 2048 -N '' -C "devkit-client:$(id -un)@$(hostname)" -f "$KEY"
        echo "== created key $KEY"
        SSH_OPTS+=(-i "$KEY")
    fi
    echo "$login@$addr" > "$HOSTFILE"
    if rsh true 2>/dev/null; then
        echo "== already paired"
    else
        echo "== sending pairing request -- put the headset on and APPROVE it (waiting up to 90s)"
        echo "   (if the headset isn't showing 'Pairing...', press 'Pair devkit' in Developer settings first)"
        local resp
        resp=$(printf '%s %s\n' "$(tr -d '\n' < "$KEY.pub")" "$DEVKIT_MAGIC" |
               curl -s -m 90 -X POST -H 'Content-Type: text/plain' --data-binary @- "http://$addr:$DEVKIT_PORT/register") || true
        echo "== headset replied: ${resp:-(nothing / timed out)}"
        for _ in 1 2 3 4 5; do rsh true 2>/dev/null && break; sleep 2; done
        rsh true 2>/dev/null || die "the paired key doesn't work yet; make sure the headset shows 'Pairing...' and confirm the prompt, then run pair again"
    fi
    echo "== paired: $login@$addr"
    push_utils
    echo "== devkit helper scripts installed in ~/devkit-utils"
    cmd_status
}

cmd_status() {
    echo "== $(host)"
    rsh 'bash -s' <<'EOF'
echo "os:        $(. /etc/os-release; echo "$PRETTY_NAME") | $(uname -m) | kernel $(uname -r)"
echo "uptime:    $(uptime -p)"
STEAMPID=$(cat ~/.steam/steam.pid 2>/dev/null || true)
if [ -n "$STEAMPID" ] && kill -0 "$STEAMPID" 2>/dev/null; then echo "steam:     running (pid $STEAMPID)"; else echo "steam:     NOT running (devkit launch needs the Steam client)"; fi
for p in vrserver vrcompositor vrmonitor; do
  if pgrep -x "$p" >/dev/null; then echo "steamvr:   $p running"; fi
done
pgrep -x vrserver >/dev/null || echo "steamvr:   vrserver not running"
echo "openxr:    active runtime -> $(readlink -f ~/.config/openxr/1/active_runtime.json 2>/dev/null || echo '(user file missing)'); system: $(ls /etc/xdg/openxr/1/ /usr/share/openxr/1/ 2>/dev/null | tr '\n' ' ')"
# What environment do games get? Read it from the Steam client process.
if [ -n "$STEAMPID" ] && [ -r /proc/$STEAMPID/environ ]; then
  echo "session env (from steam):"
  tr '\0' '\n' < /proc/$STEAMPID/environ | grep -E '^(DISPLAY|WAYLAND_DISPLAY|XDG_RUNTIME_DIR|XDG_SESSION_TYPE|GAMESCOPE|STEAM_VR|XR_|SDL_VIDEODRIVER)' | sed 's/^/  /'
fi
echo "gpu:       $(ls /dev/dri 2>/dev/null | tr '\n' ' ')"
command -v vulkaninfo >/dev/null && vulkaninfo --summary 2>/dev/null | grep -m2 -E 'deviceName|driverName' | sed 's/^\s*/           /'
echo "devkit:    games = $(ls ~/devkit-game 2>/dev/null | grep -v -- '-.*\.json$' | tr '\n' ' ')"
echo "logs:      ~/.local/share/Steam/logs ($(ls ~/.local/share/Steam/logs 2>/dev/null | wc -l) files)"
EOF
}

cmd_probe() {
    [ -x "$ROOT/$BUILD/bin/xr_probe" ] || die "build first: make frame"
    rsh 'mkdir -p ~/sfq-tools'
    rsync -az -e "ssh ${SSH_OPTS[*]}" "$ROOT/$BUILD/bin/xr_probe" "$(host):sfq-tools/"
    echo "== xr_probe over SSH (outside Steam; 'run' also records it from inside Steam)"
    rsh "export XDG_RUNTIME_DIR=\${XDG_RUNTIME_DIR:-/run/user/\$(id -u)}; ~/sfq-tools/xr_probe ${*:--v}" || true
}

cmd_deploy() {
    local app=${1:?usage: frame.sh deploy <app>}
    # Only real apps become Steam library entries ("Devkit Game: <app>") -- never
    # tests or tools. Tests run on the build machine; on the headset they reuse the
    # app's own entry (frame.sh run/record/shot pass settings via launch.env).
    [ -f "$ROOT/examples/$app/main.c" ] || [ -f "$ROOT/apps/$app/main.c" ] \
        || die "'$app' is not an app (examples/<app>/ or apps/<app>/); tests and tools are never deployed"
    [ -x "$ROOT/$BUILD/bin/$app" ] || die "no $BUILD/bin/$app -- run 'make frame' first"
    "$ROOT/scripts/package.sh" "$app" "$BUILD" >/dev/null
    push_utils
    local out dir
    out=$(dk "steamos-prepare-upload --gameid $app")
    dir=$(python3 -c 'import json,sys; print(json.loads(sys.stdin.read())["directory"])' <<<"$out")
    echo "== upload dist/$app -> $(host):$dir"
    rsync -az --delete --exclude logs/ --exclude shots/ --exclude recordings/ --exclude launch.env --exclude prefs.cfg -e "ssh ${SSH_OPTS[*]}" "$ROOT/dist/$app/" "$(host):$dir/"
    local parms
    parms=$(python3 - "$app" "$dir" "$RUNTIME" <<'EOF'
import json, sys
app, d, rt = sys.argv[1:4]
print(json.dumps({
    "gameid": app, "directory": d, "argv": ["./launch.sh"], "env": {},
    "settings": {"steam_play": "0", "compat_tool": rt},
    "force_appid": "", "lepton_args": "",
}))
EOF
)
    echo "== register 'Devkit Game: $app' in Steam (runtime $RUNTIME)"
    out=$(rsh "python3 ~/devkit-utils/steam-client-create-shortcut --parms $(printf %q "$parms")")
    echo "$out" | tail -1
    echo "$out" | grep -q '"success"' || die "registration failed (is Steam running on the headset? try: frame.sh status)"
}

follow_log() {   # follow_log <app> <seconds>
    local app=$1 secs=$2
    echo "== following ~/devkit-game/$app/logs/run.log for ${secs}s (Ctrl-C to stop watching; app keeps running)"
    rsh "timeout $secs bash -c 'while [ ! -f ~/devkit-game/$app/logs/run.log ]; do sleep 0.5; done; tail -n +1 -F ~/devkit-game/$app/logs/run.log'" || true
}

cmd_run() {
    local app=${1:?usage: frame.sh run <app> [seconds] [KEY=VAL ...]}; shift
    local secs=20
    if [[ "${1:-}" =~ ^[0-9]+$ ]]; then secs=$1; shift; fi
    cmd_stop "$app" >/dev/null 2>&1 || true
    # launch.env is sourced by launch.sh: per-run settings without re-registering
    { for kv in "$@"; do printf 'export %q\n' "$kv"; done; } | rsh "cat > ~/devkit-game/$app/launch.env; rm -f ~/devkit-game/$app/logs/run.log"
    echo "== launch via Steam: $app ${*:+(env: $*)}"
    dk "steam-devkit-rpc run-game gameid=$app" | tail -1
    follow_log "$app" "$secs"
}

cmd_shot() {
    local app=${1:?usage: frame.sh shot <app> [frame] [KEY=VAL ...]} frame=300
    shift
    if [[ "${1:-}" =~ ^[0-9]+$ ]]; then frame=$1; shift; fi
    local stamp; stamp=$(date +%H%M%S)
    cmd_run "$app" 60 "SFXR_SHOT=shots/eyes-$stamp.png" "SFXR_SHOT_FRAME=$frame" "$@" >/dev/null &
    local runner=$!
    for _ in $(seq 120); do
        rsh "test -f ~/devkit-game/$app/shots/eyes-$stamp.png" 2>/dev/null && break
        sleep 1
    done
    kill $runner 2>/dev/null || true
    mkdir -p "$ROOT/shots/frame"
    if scp -q "${SSH_OPTS[@]}" "$(host):devkit-game/$app/shots/eyes-$stamp.png" "$ROOT/shots/frame/$app-$stamp.png"; then
        echo "== screenshot: shots/frame/$app-$stamp.png"
    else
        echo "== no screenshot (session never rendered? headset asleep / not worn?) -- see: frame.sh logs $app"
    fi
}

cmd_record() {
    local app=${1:?usage: frame.sh record <app> <test-name> [secs]} name=${2:?usage: frame.sh record <app> <test-name> [secs]} secs=${3:-60}
    echo "== recording $app for ${secs}s -- put the headset on and do the thing you want to test"
    cmd_run "$app" "$secs" "SFXR_RECORD=logs/rec-$name.sfxrec" > /dev/null || true
    cmd_stop "$app" >/dev/null
    local dest="$ROOT/tests/regress/$name"
    mkdir -p "$dest"
    scp -q "${SSH_OPTS[@]}" -o IdentitiesOnly=yes "$(host):devkit-game/$app/logs/rec-$name.sfxrec" "$dest/input.sfxrec" \
        || die "no recording came back (see: frame.sh logs $app)"
    echo "$app" > "$dest/app"
    echo "== saved $dest/input.sfxrec ($(du -h "$dest/input.sfxrec" | cut -f1)) -- next: make regress-bless T=$name"
}

cmd_sessions() {
    local app=${1:?usage: frame.sh sessions <app>}
    echo "== recorded sessions of $app on the headset (newest first):"
    rsh "cd ~/devkit-game/$app/recordings 2>/dev/null && ls -lt --time-style=+%Y-%m-%d_%H:%M session-*.sfxrec | awk '{print \$6, \$5, \$7}'" || echo "   (none yet)"
}

cmd_keep() {
    local app=${1:?usage: frame.sh keep <app> <test-name> [session-file]} name=${2:?usage: frame.sh keep <app> <test-name> [session-file]}
    local src=${3:-}
    [ -n "$src" ] || src=$(rsh "ls -1t ~/devkit-game/$app/recordings/session-*.sfxrec 2>/dev/null | head -1")
    [ -n "$src" ] || die "no recorded sessions for $app"
    [[ "$src" == /* ]] || src="devkit-game/$app/recordings/$src"
    local dest="$ROOT/tests/regress/$name"
    mkdir -p "$dest"
    scp -q "${SSH_OPTS[@]}" -o IdentitiesOnly=yes "$(host):$src" "$dest/input.sfxrec" || die "copy failed"
    echo "$app" > "$dest/app"
    echo "== kept $(basename "$src") as tests/regress/$name ($(du -h "$dest/input.sfxrec" | cut -f1)) -- next: make regress-bless T=$name"
}

cmd_push_recording() {   # put a local recording on the headset (e.g. to replay it on the Frame's GPU)
    local app=${1:?usage: frame.sh push-recording <app> <file.sfxrec>} f=${2:?usage: frame.sh push-recording <app> <file.sfxrec>}
    rsh "mkdir -p ~/devkit-game/$app/recordings"
    scp -q "${SSH_OPTS[@]}" -o IdentitiesOnly=yes "$f" "$(host):devkit-game/$app/recordings/" || die "copy failed"
    echo "== on headset: recordings/$(basename "$f")  (replay: frame.sh run $app 60 SFXR_REPLAY=recordings/$(basename "$f") ...)"
}

cmd_pull() {   # everything from a play session, for review: recordings + app and SteamVR logs
    local app=${1:?usage: frame.sh pull <app> [dest]}
    local dest=${2:-$ROOT/local-data/$app-$(date +%Y%m%d-%H%M%S)}
    mkdir -p "$dest/sessions" "$dest/logs" "$dest/steam"
    rsync -az -e "ssh ${SSH_OPTS[*]}" "$(host):devkit-game/$app/recordings/" "$dest/sessions/" || die "copy failed"
    rsync -az -e "ssh ${SSH_OPTS[*]}" "$(host):devkit-game/$app/logs/" "$dest/logs/" 2>/dev/null || true
    # the player's saved preferences (replay with SFQ_PREFS=<this file> to reproduce their setup)
    scp -q "${SSH_OPTS[@]}" "$(host):devkit-game/$app/prefs.cfg" "$dest/" 2>/dev/null || true
    # screenshots taken in the headset (the toolbox's "Screenshot" menu item: sfxr_screenshot)
    rsync -az -e "ssh ${SSH_OPTS[*]}" "$(host):devkit-game/$app/shots/" "$dest/shots/" 2>/dev/null || true
    rsync -az -e "ssh ${SSH_OPTS[*]}" --include='vrserver*.txt' --include='vrcompositor*.txt' --include='controller.txt' \
        --include='*openxr*' --exclude='*' "$(host):.local/share/Steam/logs/" "$dest/steam/" 2>/dev/null || true
    echo "== pulled into ${dest#$ROOT/}"
    ls -la "$dest/sessions"
}
cmd_logs() {
    local app=${1:?usage: frame.sh logs <app>}
    local dest="$ROOT/shots/frame/$app-logs"
    mkdir -p "$dest"
    rsync -az -e "ssh ${SSH_OPTS[*]}" "$(host):devkit-game/$app/logs/" "$dest/app/" 2>/dev/null || true
    rsync -az -e "ssh ${SSH_OPTS[*]}" --include='steam_output.log' --include='vrserver*.txt' --include='vrcompositor*.txt' \
        --include='vrclient*.txt' --include='*openxr*' --exclude='*' "$(host):.local/share/Steam/logs/" "$dest/steam/" 2>/dev/null || true
    echo "== logs in shots/frame/$app-logs/"
    ls -la "$dest/app" "$dest/steam" 2>/dev/null || true
    [ -f "$dest/app/run.log" ] && { echo "== run.log (tail)"; tail -25 "$dest/app/run.log"; }
}

cmd_stop() {
    local app=${1:?usage: frame.sh stop <app>}
    rsh "pkill -f 'devkit-game/$app/$app' || pkill -x '$app' || true"
    echo "== stopped $app"
}

cmd_exec()   { rsh "${1:?usage: frame.sh exec '<command>'}"; }
cmd_list()   { dk steamos-list-games; }
cmd_delete() { dk "steamos-delete --delete-title ${1:?usage: frame.sh delete <app>}"; }
cmd_shell()  { ssh "${SSH_OPTS[@]}" -o IdentitiesOnly=yes -t "$(host)"; }

sub=${1:-help}; shift || true
case "$sub" in
    pair|status|probe|deploy|run|shot|record|sessions|keep|pull|logs|stop|exec|list|delete|shell) "cmd_$sub" "$@" ;;
    push-recording) cmd_push_recording "$@" ;;
    *) sed -n '2,35p' "$0" ;;
esac
