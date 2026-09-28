#!/usr/bin/env bash
# Build for the Steam Frame inside one of Valve's Steam Linux Runtime arm64 SDK
# containers (default from the Makefile: steamrt4 = "Steam Linux Runtime 4.0
# ARM64"). Binaries built here link only against libraries that runtime
# guarantees, so they run as Devkit Games under it -- and directly over SSH.
#
#   scripts/frame-build.sh <image> all      -> build/frame-release/bin/*
#   scripts/frame-build.sh <image> shell    -> interactive shell in the SDK
#
# The host must be arm64 (e.g. DGX Spark, Ampere, Apple Silicon Linux VM) or
# have qemu-user binfmt registered for aarch64 (slow but works).
set -euo pipefail
IMAGE=${1:?image}; WHAT=${2:-all}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
case "$(uname -m)" in
  aarch64|arm64) PLATFORM=() ;;
  *) PLATFORM=(--platform linux/arm64); echo "note: non-arm64 host, using qemu emulation" ;;
esac
TTY=(); [ -t 0 ] && TTY=(-it)
if [ "$WHAT" = shell ]; then
  exec docker run --rm "${TTY[@]}" "${PLATFORM[@]}" -v "$ROOT":/w -w /w -u "$(id -u):$(id -g)" "$IMAGE" bash
fi
docker run --rm "${PLATFORM[@]}" -v "$ROOT":/w -w /w -u "$(id -u):$(id -g)" -e HOME=/tmp "$IMAGE" \
  bash -c "make -j\$(nproc) TARGET_TAG=frame CONFIG=release all && file build/frame-release/bin/* | sed 's#,.*##'"
