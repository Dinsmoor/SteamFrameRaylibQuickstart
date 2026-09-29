#!/usr/bin/env bash
# blender.sh - run Blender headless in its container (docker/blender.Dockerfile),
# with the repo mounted at /w, as you (so files it writes are yours).
#
#   scripts/blender.sh -b --python tools/assets/build.py -- examples/toolbox/assets/bug.py out.glb
#   scripts/blender.sh --version
#   scripts/blender.sh mcp        start the MCP bridge (Blender in background mode, the Lab
#                                 add-on's socket server on 127.0.0.1:9876) as a container
#                                 named sfq-blender-mcp; `blender-mcp` (the MCP server the
#                                 LLM client spawns, .mcp.json) connects to it
#   scripts/blender.sh mcp-stop   stop it
#   scripts/blender.sh shell      a shell inside the image
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMAGE=${BLENDER_IMAGE:-sfq-blender}
PORT=${BLENDER_MCP_PORT:-9876}

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "== building $IMAGE (once; docker/blender.Dockerfile)" >&2
    docker build -t "$IMAGE" --build-arg UID="$(id -u)" --build-arg GID="$(id -g)" -f "$ROOT/docker/blender.Dockerfile" "$ROOT/docker" >&2
fi

RUN=(docker run --rm -u "$(id -u):$(id -g)" -e HOME=/home/blender -v "$ROOT:/w" -w /w)
case "${1:-}" in
  mcp)
    docker rm -f sfq-blender-mcp >/dev/null 2>&1 || true
    docker run -d --name sfq-blender-mcp -u "$(id -u):$(id -g)" -e HOME=/home/blender -v "$ROOT:/w" -w /w \
        -p "127.0.0.1:$PORT:9876" "$IMAGE" -b --command blender_mcp --host 0.0.0.0 --port 9876 >/dev/null
    sleep 2
    docker logs sfq-blender-mcp 2>&1 | tail -3
    echo "== Blender MCP bridge: 127.0.0.1:$PORT (container sfq-blender-mcp). Files: /w = $ROOT" ;;
  mcp-stop) docker rm -f sfq-blender-mcp >/dev/null 2>&1 && echo "== stopped" || echo "== not running" ;;
  shell) exec docker run --rm -it -u "$(id -u):$(id -g)" -e HOME=/home/blender -v "$ROOT:/w" -w /w --entrypoint bash "$IMAGE" ;;
  *) exec "${RUN[@]}" "$IMAGE" "$@" ;;
esac
