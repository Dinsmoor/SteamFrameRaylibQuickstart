#!/usr/bin/env bash
# frame-perf.sh - measure an app on the headset under different settings, one
# launch per setting, and print a table (docs/PERFORMANCE.md).
#
#   scripts/frame-perf.sh <app> [seconds=40] [variant ...]
#
# Variants (default: all of them, in this order):
#   base       as shipped
#   nodepth    SFXR_DEPTH=0: no depth submission (compare reprojection cost/quality)
#   nolayers   VK_INSTANCE_LAYERS= : without Valve's Vulkan layers (rpo, fdm_injection)
#              that SteamVR injects into every app. Our GL goes through Zink, which is
#              Vulkan underneath, so those layers sit inside our rendering too.
#   tufdm      TU_DEBUG=fdm: Turnip's fragment-density-map debug option (what it does
#              for an app like this is exactly what we want to find out)
#   vk         SFXR_BACKEND=vk: raylib's GL frames copied into Vulkan swapchains
#
# Wear the headset while this runs: an idle, unworn headset renders at a
# throttled rate and the numbers mean nothing. Every run logs the runtime's
# counters (SFXR_PERF_LOG=1) next to sfxr's own fps and CPU time.
#
# Output: local-data/perf-<time>/<variant>.log (full logs), layers.txt (the
# implicit Vulkan and OpenXR layer manifests on the headset: what fdm_injection
# is and which variable turns it off), and the table on stdout.
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
APP=${1:?usage: frame-perf.sh <app> [seconds] [variant ...]}; shift
SECS=40
if [[ "${1:-}" =~ ^[0-9]+$ ]]; then SECS=$1; shift; fi
VARIANTS=("$@")
[ ${#VARIANTS[@]} -gt 0 ] || VARIANTS=(base nodepth nolayers tufdm vk)
OUT="$ROOT/local-data/perf-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"
FRAME="$ROOT/scripts/frame.sh"

env_for() {
    case "$1" in
        base)     ;;
        nodepth)  echo "SFXR_DEPTH=0" ;;
        nolayers) echo "VK_INSTANCE_LAYERS=" ;;
        tufdm)    echo "TU_DEBUG=fdm" ;;
        vk)       echo "SFXR_BACKEND=vk" ;;
        *)        echo "unknown variant: $1" >&2; return 1 ;;
    esac
}

echo "== layer manifests -> $OUT/layers.txt"
"$FRAME" exec 'for d in /usr/share/vulkan/implicit_layer.d /etc/vulkan/implicit_layer.d ~/.local/share/vulkan/implicit_layer.d \
    /usr/share/openxr/1/api_layers/implicit.d /etc/openxr/1/api_layers/implicit.d ~/.local/share/openxr/1/api_layers/implicit.d; do
    [ -d "$d" ] || continue; for f in "$d"/*.json; do [ -f "$f" ] && { echo "### $f"; cat "$f"; echo; }; done; done' \
    > "$OUT/layers.txt" 2>&1 || echo "   (couldn't read them: $OUT/layers.txt)"

for v in "${VARIANTS[@]}"; do
    extra=$(env_for "$v") || exit 1
    echo "== $v ${extra:+($extra)} for ${SECS}s"
    # shellcheck disable=SC2086
    "$FRAME" run "$APP" "$SECS" SFXR_PERF_LOG=1 SFQ_AUTORECORD=0 $extra > "$OUT/$v.log" 2>&1 || true
    "$FRAME" stop "$APP" >/dev/null 2>&1 || true
    sleep 3
done

# Average everything after the first two heartbeats (startup, shader compiles).
echo
printf '%-10s %9s %9s %11s  %s\n' variant "fps" "cpu ms" "backend" "runtime counters (averages)"
for v in "${VARIANTS[@]}"; do
    python3 - "$OUT/$v.log" "$v" <<'PY'
import re, sys, collections
path, name = sys.argv[1], sys.argv[2]
text = open(path, errors="replace").read()
hb = re.findall(r"heartbeat: loop [\d.]+ fps, rendered ([\d.]+) fps, cpu ([\d.]+) ms", text)[2:]
perf = collections.defaultdict(list)
for line in re.findall(r"perf:(.*)", text)[2:]:
    for k, val in re.findall(r"(\S+?)=([-\d.]+)", line):
        perf[k].append(float(val))
m = re.search(r"ready: backend=OpenXR \((\w+)", text)
backend = {"OpenGL": "gl", "Vulkan": "vk"}.get(m.group(1), m.group(1)) if m else "?"
fps = sum(float(a) for a, _ in hb) / len(hb) if hb else 0
cpu = sum(float(b) for _, b in hb) / len(hb) if hb else 0
ctr = "  ".join(f"{k}={sum(v)/len(v):.2f}" for k, v in sorted(perf.items()))
print(f"{name:<10} {fps:9.1f} {cpu:9.2f} {backend:>11}  {ctr if ctr else '(no counters logged)'}")
if not hb: print(f"{'':<10} no heartbeats: see {path}")
PY
done
echo
echo "logs: $OUT"
