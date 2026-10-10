#!/usr/bin/env bash
# Échoue si un TODO/FIXME de src/ n'a pas de référence ISSUES.md# sur la même ligne (Q3).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
offenders="$(grep -rnE 'TODO|FIXME' "$ROOT/src" 2>/dev/null | grep -v 'ISSUES\.md#' || true)"
if [ -n "$offenders" ]; then
    echo "$offenders"
    echo "FAIL: TODO/FIXME sans ISSUES.md# sur la ligne"
    exit 1
fi
echo "OK: aucun TODO/FIXME orphelin"
