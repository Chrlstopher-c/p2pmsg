#!/usr/bin/env bash
# Usage : scripts/check.sh [label]   (label = <dom>/<mod>, sans label = tout)
# asan : build + ctest · tsan : si .concurrent · valgrind : ctest dans docker/ (ubuntu 24.04), ou natif si CHECK_NO_DOCKER=1.
# Code 0 seulement si tout vert.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LABEL="${1:-}"
export CMAKE_BUILD_PARALLEL_LEVEL=2
IMAGE=p2pmsg-dev
FAILED=0

run_step() {
    local name="$1"; shift
    local log="$ROOT/build/check-${name//[^a-z0-9]/_}.log"
    mkdir -p "$ROOT/build"
    if "$@" >"$log" 2>&1; then
        echo "[ OK ] $name"
    else
        echo "[FAIL] $name (log : ${log#"$ROOT"/})"
        tail -n 15 "$log"
        FAILED=1
    fi
}

ctest_preset() {
    local preset="$1" filter="$2"
    local args=(--output-on-failure)
    if [ -n "$filter" ]; then
        args+=(-L "$filter")
    fi
    if [ -n "$LABEL" ]; then
        args+=(--no-tests=error)
    fi
    ctest --test-dir "$ROOT/build/$preset" "${args[@]}"
}

label_targets() {
    local f name
    for f in "$ROOT/tests/$LABEL"/test_*.c; do
        [ -e "$f" ] || continue
        name="$(basename "$f" .c)"
        echo "${LABEL//\//_}_$name"
    done
}

build_preset() {
    local preset="$1" targets=()
    if [ -n "$LABEL" ]; then
        mapfile -t targets < <(label_targets)
        [ "${#targets[@]}" -gt 0 ] || { echo "aucun test dans tests/$LABEL" >&2; return 1; }
    fi
    cmake --preset "$preset" -S "$ROOT" \
        && cmake --build "$ROOT/build/$preset" ${targets[@]:+--target "${targets[@]}"}
}

concurrent_labels() {
    local marker dir
    for marker in "$ROOT"/src/*/*/.concurrent; do
        [ -e "$marker" ] || continue
        dir="$(dirname "$marker")"
        echo "${dir#"$ROOT/src/"}"
    done
}

run_step "asan build" build_preset asan
run_step "asan ctest ${LABEL:-all}" ctest_preset asan "$LABEL"

if [ -n "$LABEL" ]; then
    tsan_filter=""
    [ -e "$ROOT/src/$LABEL/.concurrent" ] && tsan_filter="$LABEL"
else
    tsan_filter="$(concurrent_labels | paste -sd'|' -)"
    [ -n "$tsan_filter" ] && tsan_filter="^($tsan_filter)\$"
fi
if [ -n "$tsan_filter" ]; then
    run_step "tsan build" build_preset tsan
    run_step "tsan ctest $tsan_filter" ctest_preset tsan "$tsan_filter"
else
    echo "[SKIP] tsan (aucun module concurrent${LABEL:+ dans $LABEL})"
fi

ensure_image() {
    docker image inspect "$IMAGE" >/dev/null 2>&1 \
        || docker build -t "$IMAGE" -f "$ROOT/docker/Dockerfile" "$ROOT/docker"
}

valgrind_in_container() {
    local build_dir="/work/build/ubuntu-debug"
    local build='set -eu
cmake -S /work -B "$1" -DCMAKE_BUILD_TYPE=Debug -DP2P_VALGRIND=ON
cmake --build "$1" -j2 ${P2P_TARGETS:+--target $P2P_TARGETS}
args=(--output-on-failure)
if [ -n "$P2P_LABEL" ]; then args+=(-L "$P2P_LABEL" --no-tests=error); fi
ctest --test-dir "$1" "${args[@]}"'
    local daemons='set -eu
mkdir -p /run/dbus /run/avahi-daemon
dbus-daemon --system --fork
avahi-daemon --no-chroot -D
for _ in $(seq 1 20); do avahi-daemon --check >/dev/null 2>&1 && break; sleep 0.5; done
exec setpriv --reuid="$P2P_UID" --regid="$P2P_GID" --clear-groups bash -c "$P2P_INNER" _ "$1"'
    local targets=""
    if [ -n "$LABEL" ]; then
        targets="$(label_targets | paste -sd' ' -)"
        [ -n "$targets" ] || { echo "aucun test dans tests/$LABEL" >&2; return 1; }
    fi
    docker run --rm -e HOME=/tmp -e P2P_LABEL="$LABEL" -e P2P_TARGETS="$targets" \
        -e P2P_UID="$(id -u)" -e P2P_GID="$(id -g)" -e P2P_INNER="$build" \
        -v "$ROOT:/work" -w /work "$IMAGE" bash -c "$daemons" _ "$build_dir"
}

valgrind_step() {
    if [ "${CHECK_NO_DOCKER:-0}" = "1" ]; then
        build_preset debug && ctest_preset debug "$LABEL"
    else
        ensure_image && valgrind_in_container
    fi
}

run_step "valgrind ${LABEL:-all}" valgrind_step

if [ "$FAILED" -eq 0 ]; then
    echo "RESULT: PASS"
else
    echo "RESULT: FAIL"
fi
exit "$FAILED"
