#!/usr/bin/env bash
# Redémarre p2pmsg : stop puis start.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"$ROOT/stop.sh" && "$ROOT/start.sh"
