#!/usr/bin/env bash
# Usage : scripts/e2e.sh [F1-static F1-mdns F4 F6]   (sans argument : tous)
# Deux instances p2pmsg sur 127.0.0.1, dossiers temporaires. Chaque cas affiche PASS/FAIL. Code 0 seulement si tout passe.
# F1-mdns : si avahi-daemon est absent de l'hôte, le cas tourne dans le conteneur p2pmsg-dev (voir mdns-test.sh).
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${P2PMSG_BIN:-$ROOT/build/asan/src/app/cli/p2pmsg}"
IMAGE=p2pmsg-dev
WORK="$(mktemp -d /tmp/p2pmsg-e2e-XXXXXX)"
PORT_A=47801
PORT_B=47802
PORT_MDNS_A=47811
PORT_MDNS_B=47812
FAILS=0
declare -A PIDS=()

cleanup() {
    local n
    for n in "${!PIDS[@]}"; do kill -9 "${PIDS[$n]}" 2>/dev/null; done
    pkill -f "tail -n +1 -f $WORK" 2>/dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT

pass() { echo "PASS $1${2:+ ($2)}"; }
fail() { echo "FAIL $1: $2"; FAILS=$((FAILS + 1)); }

start_inst() { # nom port [flags...]
    local name="$1" port="$2"; shift 2
    local dir="$WORK/$name"
    mkdir -p "$dir"
    pkill -f "tail -n +1 -f $dir/in" 2>/dev/null
    : >"$dir/in"
    "$BIN" --dir "$dir" --port "$port" --name "$name" "$@" < <(tail -n +1 -f "$dir/in") >"$dir/out" 2>"$dir/err" &
    PIDS[$name]=$!
}

stop_inst() { # nom [-9]
    local name="$1" sig="${2:--TERM}"
    if [ -n "${PIDS[$name]:-}" ]; then
        kill "$sig" "${PIDS[$name]}" 2>/dev/null
        wait "${PIDS[$name]}" 2>/dev/null
        unset "PIDS[$name]"
    fi
    pkill -f "tail -n +1 -f $WORK/$name/in" 2>/dev/null
}

send() { printf '%s\n' "$2" >>"$WORK/$1/in"; }
out_of() { echo "$WORK/$1/out"; }
key_of() { awk '$1 == "SELF" { print $2; exit }' "$WORK/$1/out"; }
id_of_sent() { awk -v k="$2" '$1 == "SENT" && $2 == k { id = $3 } END { print id }' "$WORK/$1/out"; }
now_ms() { date +%s%3N; }

# wait_for NOM REGEX TIMEOUT_S : 0 si la ligne apparaît dans la sortie de NOM
wait_for() {
    local name="$1" re="$2" deadline=$(( $(date +%s) + $3 ))
    while [ "$(date +%s)" -le "$deadline" ]; do
        grep -qE "$re" "$(out_of "$name")" 2>/dev/null && return 0
        sleep 0.1
    done
    return 1
}

count_re() { grep -cE "$2" "$(out_of "$1")" 2>/dev/null || true; }

# Phase 1 : chaque instance écrit son identité et annonce SELF, puis s'arrête.
capture_keys() {
    rm -rf "$WORK/A" "$WORK/B"
    start_inst A "$PORT_A" --no-mdns
    start_inst B "$PORT_B" --no-mdns
    wait_for A '^SELF ' 10 && wait_for B '^SELF ' 10 || return 1
    KEY_A="$(key_of A)"; KEY_B="$(key_of B)"
    stop_inst A; stop_inst B
    [ -n "$KEY_A" ] && [ -n "$KEY_B" ]
}

write_peers() { # nom port_pair clé_pair
    printf '127.0.0.1:%s %s %s\n' "$2" "$3" "$4" >"$WORK/$1/peers"
}

# Mesure F1 : A envoie à B, B reçoit, A voit delivered en moins de 2 s après l'envoi.
check_delivery() { # label timeout_connexion_s
    local label="$1" t0 t1 id
    if ! wait_for A "^PEER $KEY_B connected" "$2"; then fail "$label" "A ne passe jamais connecté à B"; return; fi
    send A "/to $KEY_B"
    send A "hello $label"
    wait_for A '^SENT ' 3 || { fail "$label" "pas de SENT"; return; }
    id="$(id_of_sent A "$KEY_B")"
    t0="$(now_ms)"
    wait_for B "^RECV $KEY_A $id hello $label\$" 2 || { fail "$label" "B n'affiche pas RECV"; return; }
    wait_for A "^STATUS $KEY_B $id delivered\$" 2 || { fail "$label" "A n'affiche pas STATUS delivered"; return; }
    t1="$(now_ms)"
    if [ $((t1 - t0)) -lt 2000 ]; then pass "$label" "$((t1 - t0)) ms"; else fail "$label" "délai $((t1 - t0)) ms >= 2 s"; fi
}

case_f1_static() {
    capture_keys || { fail F1-static "identités non capturées"; return; }
    write_peers A "$PORT_B" "$KEY_B" B
    write_peers B "$PORT_A" "$KEY_A" A
    start_inst A "$PORT_A" --no-mdns
    start_inst B "$PORT_B" --no-mdns
    check_delivery F1-static 10
    stop_inst A; stop_inst B
}

case_f1_mdns() {
    if ! avahi-daemon --check >/dev/null 2>&1; then
        run_mdns_in_container
        return
    fi
    capture_keys || { fail F1-mdns "identités non capturées"; return; }
    rm -f "$WORK/A/peers" "$WORK/B/peers"
    start_inst A "$PORT_MDNS_A"
    start_inst B "$PORT_MDNS_B"
    check_delivery F1-mdns 30
    stop_inst A; stop_inst B
}

run_mdns_in_container() {
    local inner='set -u
mkdir -p /run/dbus /run/avahi-daemon
dbus-daemon --system --fork
avahi-daemon --no-chroot -D
for _ in $(seq 1 20); do avahi-daemon --check >/dev/null 2>&1 && break; sleep 0.5; done
cmake -S /work -B /work/build/e2e-mdns -DCMAKE_BUILD_TYPE=Debug -DP2P_VALGRIND=OFF >/dev/null \
  && cmake --build /work/build/e2e-mdns -j2 --target p2pmsg >/dev/null \
  && P2PMSG_BIN=/work/build/e2e-mdns/src/app/cli/p2pmsg /work/scripts/e2e.sh F1-mdns'
    docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -q -t "$IMAGE" -f "$ROOT/docker/Dockerfile" "$ROOT/docker" >/dev/null
    if docker run --rm -v "$ROOT:/work" -w /work "$IMAGE" bash -c "$inner"; then
        echo "PASS F1-mdns (conteneur $IMAGE)"
    else
        fail F1-mdns "échec dans le conteneur $IMAGE"
    fi
}

case_f4() {
    capture_keys || { fail F4 "identités non capturées"; return; }
    local wrong
    wrong="$(printf '%s' "$KEY_B" | sed 's/^./0/')"
    [ "$wrong" = "$KEY_B" ] && wrong="1${KEY_B:1}"
    write_peers A "$PORT_B" "$wrong" B
    write_peers B "$PORT_A" "$KEY_A" A
    start_inst A "$PORT_A" --no-mdns
    start_inst B "$PORT_B" --no-mdns
    wait_for A '^SELF ' 5 && wait_for B '^SELF ' 5 || { fail F4 "instances non démarrées"; return; }
    sleep 5
    send A "/to $wrong"
    send A "ne doit pas arriver"
    sleep 3
    if grep -qE "^PEER $wrong connected" "$(out_of A)" || grep -qE "^PEER $KEY_B connected" "$(out_of A)"; then
        fail F4 "A passe CONNECTED avec une mauvaise clé"
    elif grep -q '^RECV ' "$(out_of B)"; then
        fail F4 "B reçoit un message malgré la mauvaise clé"
    else
        pass F4
    fi
    stop_inst A; stop_inst B
}

case_f6() {
    local id1 id2
    capture_keys || { fail F6 "identités non capturées"; return; }
    write_peers A "$PORT_B" "$KEY_B" B
    write_peers B "$PORT_A" "$KEY_A" A
    start_inst A "$PORT_A" --no-mdns
    start_inst B "$PORT_B" --no-mdns
    wait_for A "^PEER $KEY_B connected" 15 || { fail F6 "connexion initiale impossible"; return; }
    stop_inst B
    sleep 1
    send A "/to $KEY_B"
    send A "pending-1"
    send A "pending-2"
    wait_for A "^SENT $KEY_B [0-9a-f]+\$" 3 && wait_for A "^SENT $KEY_B [0-9a-f]+\$" 3 || { fail F6 "pas de SENT"; stop_inst A; return; }
    id1="$(awk -v k="$KEY_B" '$1 == "SENT" && $2 == k { n++; if (n == 1) print $3 }' "$(out_of A)")"
    id2="$(awk -v k="$KEY_B" '$1 == "SENT" && $2 == k { n++; if (n == 2) print $3 }' "$(out_of A)")"
    send A "/hist 10"
    wait_for A "^HIST $KEY_B $id1 out pending pending-1\$" 5 && wait_for A "^HIST $KEY_B $id2 out pending pending-2\$" 5 \
        || { fail F6 "messages non persistés en pending"; cat "$WORK/A/out" "$WORK/A/err" >&2; stop_inst A; return; }
    stop_inst A -9
    start_inst A "$PORT_A" --no-mdns
    start_inst B "$PORT_B" --no-mdns
    if wait_for B "^RECV $KEY_A $id1 pending-1\$" 40 && wait_for B "^RECV $KEY_A $id2 pending-2\$" 5; then
        sleep 5
        if [ "$(count_re B '^RECV ')" = "2" ]; then pass F6; else fail F6 "B a reçu $(count_re B '^RECV ') RECV au lieu de 2"; fi
    else
        fail F6 "B ne reçoit pas les 2 pending après reprise"
    fi
    stop_inst A; stop_inst B
}

if [ ! -x "$BIN" ]; then
    cmake --preset asan -S "$ROOT" >/dev/null && cmake --build "$ROOT/build/asan" --target p2pmsg >/dev/null \
        || { echo "FAIL build : binaire p2pmsg introuvable ($BIN)"; exit 2; }
fi

CASES=("$@")
[ "${#CASES[@]}" -gt 0 ] || CASES=(F1-static F1-mdns F4 F6)
for c in "${CASES[@]}"; do
    case "$c" in
        F1-static) case_f1_static ;;
        F1-mdns)   case_f1_mdns ;;
        F4)        case_f4 ;;
        F6)        case_f6 ;;
        *)         fail "$c" "cas inconnu"; ;;
    esac
done
[ "$FAILS" -eq 0 ] && echo "RESULT: PASS" || echo "RESULT: FAIL ($FAILS)"
exit "$FAILS"
