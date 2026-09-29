#!/usr/bin/env bash
# notes-channel.sh - what .mcp.json starts for the "notes" channel: the Bun
# runtime on tools/notes/channel.ts (docs/NOTES.md). Bun installs to ~/.bun,
# which an MCP client's PATH may not have, so this finds it.
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUN=${BUN:-$(command -v bun || echo "$HOME/.bun/bin/bun")}
[ -x "$BUN" ] || { echo "notes-channel: bun not found (https://bun.sh; scripts/notes.sh install)" >&2; exit 1; }
[ -d "$ROOT/tools/notes/node_modules" ] || ( cd "$ROOT/tools/notes" && "$BUN" install --silent )
exec "$BUN" "$ROOT/tools/notes/channel.ts"
