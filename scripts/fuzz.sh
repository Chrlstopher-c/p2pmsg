#!/usr/bin/env bash
# Usage : scripts/fuzz.sh <module> <secondes>   (ex. scripts/fuzz.sh frame 600)
# Build la cible fuzz_<module> (clang, libFuzzer+ASan+UBSan) hors CTest, puis la lance sur une copie
# du corpus (les entrées nouvelles restent dans build/fuzz-<module>/work). Crash -> artefacts dans build/.
set -eu
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MOD="${1:?usage: scripts/fuzz.sh <module> <secondes>}"
SECS="${2:?usage: scripts/fuzz.sh <module> <secondes>}"

if ! command -v clang >/dev/null 2>&1; then
    echo "clang requis pour libFuzzer" >&2
    exit 1
fi
FUZZ_DIR="$(ls -d "$ROOT"/tests/*/"$MOD" 2>/dev/null | head -n 1 || true)"
if [ -z "$FUZZ_DIR" ] || [ ! -f "$FUZZ_DIR/fuzz_$MOD.c" ]; then
    echo "pas de cible fuzz_$MOD (tests/*/$MOD/fuzz_$MOD.c)" >&2
    exit 1
fi

BUILD="$ROOT/build/fuzz-$MOD"
WORK="$BUILD/work"
export CMAKE_BUILD_PARALLEL_LEVEL=2
cmake -S "$FUZZ_DIR" -B "$BUILD" -DCMAKE_C_COMPILER=clang >/dev/null
cmake --build "$BUILD" --target "fuzz_$MOD"

rm -rf "$WORK" && mkdir -p "$WORK"
cp -r "$FUZZ_DIR/corpus/." "$WORK/" 2>/dev/null || true
"$BUILD/fuzz_$MOD" -max_total_time="$SECS" -artifact_prefix="$BUILD/" "$WORK"
