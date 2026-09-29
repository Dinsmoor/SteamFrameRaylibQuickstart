#!/usr/bin/env bash
# blender-mcp.sh - what .mcp.json starts for the "blender" MCP server: Blender
# Lab's `blender-mcp` (the MCP side; pip/uv-installed), after making sure the
# bridge it talks to (Blender in background mode, scripts/blender.sh mcp) is up.
# docs/ASSETS.md.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN=$(command -v blender-mcp || echo "$HOME/.local/bin/blender-mcp")
[ -x "$BIN" ] || { echo "blender-mcp: not installed (uv tool install 'git+https://projects.blender.org/lab/blender_mcp.git#subdirectory=mcp')" >&2; exit 1; }
if ! docker ps --format '{{.Names}}' 2>/dev/null | grep -qx sfq-blender-mcp; then
    "$ROOT/scripts/blender.sh" mcp >&2 || exit 1
fi
exec "$BIN" "$@"
