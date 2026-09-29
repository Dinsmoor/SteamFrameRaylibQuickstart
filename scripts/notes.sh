#!/usr/bin/env bash
# notes.sh - the design-note loop on the build machine (docs/NOTES.md):
# notes said in the headset -> pulled here -> transcribed on this machine's
# GPU -> pushed into a Claude Code session as they arrive.
#
#   scripts/notes.sh build            build whisper.cpp's server (CUDA if nvcc is here) into
#                                     build/host-whisper and fetch the large-v3-turbo model
#   scripts/notes.sh install [app]    write and enable two systemd --user services:
#                                     sfq-whisper (the transcriber) and sfq-notesd (pull + transcribe)
#   scripts/notes.sh start|stop|restart|status|log
#   scripts/notes.sh once [app]       pull and transcribe once, in the foreground, no services
#   scripts/notes.sh claude [args]    start Claude Code here with the notes channel enabled
#                                     (a research-preview flag) and the Blender MCP bridge up
#
# Ports: whisper-server on 127.0.0.1:9878 (SFQ_WHISPER_URL). Model:
# SFQ_NOTES_MODEL (default external/speech/ggml-large-v3-turbo-q8_0.bin). The
# transcriber is told the words to expect through SFQ_NOTES_PROMPT.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
APP_DEFAULT=toolbox
WHISPER_TAG=${WHISPER_TAG:-v1.9.4}
MODEL=${SFQ_NOTES_MODEL:-external/speech/ggml-large-v3-turbo-q8_0.bin}
PORT=${SFQ_WHISPER_PORT:-9878}
UNITS="$HOME/.config/systemd/user"
PROMPT=${SFQ_NOTES_PROMPT:-"Steam Frame, raylib, sfxr, vrui, toolbox, the garden, Daddy Bug Smasher, the Bugmaster, bug, hammer, variant, spawn, hover, slop, glb, Blender, mechanism, lever, knob, teleport, haptics, panel, station, wave, enemy."}

cmd_build() {
    SRC=external/whisper.cpp
    [ -d "$SRC" ] || git clone --depth 1 --branch "$WHISPER_TAG" https://github.com/ggml-org/whisper.cpp "$SRC"
    mkdir -p external/speech build/host-whisper
    if [ ! -f "$MODEL" ]; then
        echo "== fetching $(basename "$MODEL") (about 870 MB)"
        curl -L --fail -o "$MODEL.part" "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/$(basename "$MODEL")"
        mv "$MODEL.part" "$MODEL"
    fi
    local cuda=()
    if command -v nvcc >/dev/null || [ -x /usr/local/cuda/bin/nvcc ]; then
        cuda=(-DGGML_CUDA=1 -DCMAKE_CUDA_COMPILER="$(command -v nvcc || echo /usr/local/cuda/bin/nvcc)")
        echo "== CUDA found: a GPU build"
    else
        echo "== no nvcc: a CPU build (fine for a big model on a workstation, slow on a laptop)"
    fi
    # static: the binary can move (and be run by systemd) without an rpath to its own build tree
    cmake -S "$SRC" -B build/host-whisper/whisper -DCMAKE_BUILD_TYPE=Release "${cuda[@]}" -DBUILD_SHARED_LIBS=OFF \
          -DWHISPER_BUILD_TESTS=OFF -DWHISPER_BUILD_EXAMPLES=ON -DWHISPER_BUILD_SERVER=ON -DGGML_NATIVE=ON >/dev/null
    cmake --build build/host-whisper/whisper -j"$(nproc)" --target whisper-server whisper-cli >/dev/null
    echo "built build/host-whisper/whisper/bin/whisper-server"
}

server_bin() { echo "$ROOT/build/host-whisper/whisper/bin/whisper-server"; }

cmd_install() {
    local app=${1:-$APP_DEFAULT}
    [ -x "$(server_bin)" ] || { echo "no whisper-server yet: scripts/notes.sh build"; exit 1; }
    [ -f "$MODEL" ] || { echo "no model $MODEL: scripts/notes.sh build"; exit 1; }
    ( cd tools/notes && "${BUN:-$HOME/.bun/bin/bun}" install --silent ) || echo "warning: bun install failed (the channel needs bun: https://bun.sh)"
    mkdir -p "$UNITS"
    cat > "$UNITS/sfq-whisper.service" <<EOF
[Unit]
Description=sfq whisper-server: transcribes design notes from the headset (docs/NOTES.md)
[Service]
WorkingDirectory=$ROOT
ExecStart=$(server_bin) -m $ROOT/$MODEL --host 127.0.0.1 --port $PORT -t 4 -nt -l en
Restart=on-failure
RestartSec=3
[Install]
WantedBy=default.target
EOF
    cat > "$UNITS/sfq-notesd.service" <<EOF
[Unit]
Description=sfq notesd: pulls design notes from the headset and transcribes them (docs/NOTES.md)
After=sfq-whisper.service
Wants=sfq-whisper.service
[Service]
WorkingDirectory=$ROOT
Environment=SFQ_WHISPER_URL=http://127.0.0.1:$PORT
Environment="SFQ_NOTES_PROMPT=$PROMPT"
ExecStart=/usr/bin/env python3 $ROOT/tools/notes/notesd.py --app $app --watch $ROOT/notes
Restart=on-failure
RestartSec=3
[Install]
WantedBy=default.target
EOF
    systemctl --user daemon-reload
    systemctl --user enable --now sfq-whisper.service sfq-notesd.service
    echo "== installed and started: sfq-whisper (port $PORT), sfq-notesd (app $app). scripts/notes.sh status"
}

cmd_start()   { systemctl --user start sfq-whisper.service sfq-notesd.service; cmd_status; }
cmd_stop()    { systemctl --user stop sfq-notesd.service sfq-whisper.service; echo "== stopped"; }
cmd_restart() { systemctl --user restart sfq-whisper.service sfq-notesd.service; cmd_status; }
cmd_status()  { systemctl --user --no-pager status sfq-whisper.service sfq-notesd.service 2>&1 | grep -E "●|Active:|notesd:|Description" || true; }
cmd_log()     { journalctl --user -f -u sfq-whisper.service -u sfq-notesd.service; }

ONCE_PID=
cmd_once() {   # once [app] [--no-frame]  (no headset around: skip pulling from it)
    local app=${1:-$APP_DEFAULT}; shift || true
    if ! curl -fs "http://127.0.0.1:$PORT/" >/dev/null 2>&1; then
        echo "== whisper-server isn't up on :$PORT; starting one for this run"
        "$(server_bin)" -m "$ROOT/$MODEL" --host 127.0.0.1 --port "$PORT" -t 4 -nt -l en >/dev/null 2>&1 &
        ONCE_PID=$!
        trap '[ -n "$ONCE_PID" ] && kill $ONCE_PID 2>/dev/null || true' EXIT
        for _ in $(seq 60); do curl -fs "http://127.0.0.1:$PORT/" >/dev/null 2>&1 && break; sleep 0.5; done
    fi
    SFQ_WHISPER_URL="http://127.0.0.1:$PORT" SFQ_NOTES_PROMPT="$PROMPT" python3 tools/notes/notesd.py --app "$app" --watch "$ROOT/notes" --once "$@"
}

cmd_claude() {
    scripts/blender.sh mcp >/dev/null 2>&1 || echo "warning: the Blender MCP bridge didn't start (docker?)"
    exec claude --dangerously-load-development-channels server:notes "$@"
}

sub=${1:-help}; shift || true
case "$sub" in
    build|install|start|stop|restart|status|log|once|claude) "cmd_$sub" "$@" ;;
    *) sed -n '2,20p' "$0" ;;
esac
