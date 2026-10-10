#!/usr/bin/env bash
# Lance p2pmsg en arrière-plan. PID dans .p2pmsg.pid, logs reset à chaque lancement.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$ROOT/build/release/src/app/cli/p2pmsg"
PID_FILE="$ROOT/.p2pmsg.pid"
LOG_DIR="$ROOT/logs"

if [ -f "$PID_FILE" ] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
    echo "déjà lancé (pid $(cat "$PID_FILE"))"
    exit 0
fi
if [ ! -x "$BIN" ]; then
    echo "binaire absent : $BIN (cmake --build build/release)" >&2
    exit 1
fi

rm -rf "$LOG_DIR" && mkdir -p "$LOG_DIR"
nohup "$BIN" >"$LOG_DIR/p2pmsg.log" 2>&1 &
echo $! >"$PID_FILE"
echo "lancé (pid $!)"
