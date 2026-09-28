#!/usr/bin/env bash
# Start a new app from the hello example:  scripts/new-app.sh mygame
# Creates apps/mygame/main.c (+ vrpreferences.json); build with
#   make EX=mygame run      (desktop / simulator)
#   make frame && make package EX=mygame
set -euo pipefail
NAME=${1:?name}
[[ "$NAME" =~ ^[a-z0-9_]+$ ]] || { echo "use lowercase letters, digits, underscore"; exit 1; }
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DIR="$ROOT/apps/$NAME"
[ -e "$DIR" ] && { echo "$DIR exists"; exit 1; }
mkdir -p "$DIR/resources"
sed "s/\"sfxr hello\"/\"$NAME\"/; s#^// hello - the smallest useful sfxr program.#// $NAME - started from examples/hello.#" \
  "$ROOT/examples/hello/main.c" > "$DIR/main.c"
cp "$ROOT/templates/vrpreferences.json" "$DIR/"
echo "created apps/$NAME -- try: make EX=$NAME run"
