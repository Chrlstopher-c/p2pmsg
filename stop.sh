#!/usr/bin/env bash
# Arrête p2pmsg via .p2pmsg.pid (SIGTERM, puis SIGKILL après 10 s).
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PID_FILE="$ROOT/.p2pmsg.pid"

if [ ! -f "$PID_FILE" ]; then
    echo "pas de pid : rien à arrêter"
    exit 0
fi
pid="$(cat "$PID_FILE")"
if kill -0 "$pid" 2>/dev/null; then
    kill -TERM "$pid"
    for _ in $(seq 1 10); do
        kill -0 "$pid" 2>/dev/null || break
        sleep 1
    done
    kill -0 "$pid" 2>/dev/null && kill -KILL "$pid"
    echo "arrêté (pid $pid)"
else
    echo "pid $pid absent : nettoyage"
fi
rm -f "$PID_FILE"
