#!/usr/bin/env bash
# Démo UI headless : deux p2pmsg-gtk (alice/bob), pairs croisés, message alice→bob saisi via wtype.
# Produit build/ui-demo-A.png et build/ui-demo-B.png. Nécessite sway (copie sans cap_sys_nice), wtype, grim.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/release/src"
GTK="${P2P_GTK_BIN:-$BUILD/ui/gtk/p2pmsg-gtk}"
CLI="$BUILD/app/cli/p2pmsg"
SWAY="$HOME/.local/share/jev-desktop/sway"
RUN="$(mktemp -d "$HOME/.cache/p2pmsg-demo.XXXXXX")"
DIR_A="$RUN/alice"; DIR_B="$RUN/bob"; XDG="$RUN/xdg"
PORT_A=47821; PORT_B=47822
PIDS=()
cleanup() {
    for pid in "${PIDS[@]}"; do kill -TERM "$pid" 2>/dev/null; done
    sleep 1
    for pid in "${PIDS[@]}"; do kill -KILL "$pid" 2>/dev/null; done
    rm -rf "$RUN"
}
trap cleanup EXIT
mkdir -p "$DIR_A" "$DIR_B" "$XDG" "$ROOT/build"; chmod 700 "$XDG"

self_key() {
    local dir="$1" port="$2" line="" pid i
    "$CLI" --dir "$dir" --port "$port" --no-mdns </dev/null >"$dir/cli.out" 2>&1 &
    pid=$!
    for i in $(seq 50); do
        line="$(grep -m1 '^SELF ' "$dir/cli.out" 2>/dev/null)"
        [ -n "$line" ] && break
        sleep 0.2
    done
    kill -TERM "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
    awk '{print $2}' <<<"$line"
}
KEY_A="$(self_key "$DIR_A" "$PORT_A")"; KEY_B="$(self_key "$DIR_B" "$PORT_B")"
[ -n "$KEY_A" ] && [ -n "$KEY_B" ] || { echo "clés SELF introuvables"; exit 1; }
echo "127.0.0.1:$PORT_B $KEY_B" > "$DIR_A/peers"
echo "127.0.0.1:$PORT_A $KEY_A" > "$DIR_B/peers"

echo "# demo, pas de barre" > "$RUN/sway.conf"
export XDG_RUNTIME_DIR="$XDG"
env -u WAYLAND_DISPLAY -u DISPLAY -u HYPRLAND_INSTANCE_SIGNATURE WLR_BACKENDS=headless WLR_RENDERER=pixman \
    WLR_LIBINPUT_NO_DEVICES=1 "$SWAY" -c "$RUN/sway.conf" >"$RUN/sway.log" 2>&1 &
PIDS+=($!); SWAY_PID=$!
sleep 3
SOCK="$(ls "$XDG" | grep -E '^wayland-[0-9]+$' | head -1)"
[ -n "$SOCK" ] || { echo "sway: pas de socket"; tail -20 "$RUN/sway.log"; exit 1; }
export SWAYSOCK="$XDG/$(ls "$XDG" | grep -E '^sway-ipc' | head -1)"
BUS="$(env -u DBUS_SESSION_BUS_ADDRESS dbus-daemon --session --fork --print-address=1 --print-pid=1)"
BUS_ADDR="$(sed -n 1p <<<"$BUS")"; BUS_PID="$(sed -n 2p <<<"$BUS")"
PIDS+=("$BUS_PID")

app_env() {
    env -u WAYLAND_DISPLAY -u DISPLAY XDG_RUNTIME_DIR="$XDG" WAYLAND_DISPLAY="$SOCK" \
        DBUS_SESSION_BUS_ADDRESS="$BUS_ADDR" GDK_BACKEND=wayland GSK_RENDERER=cairo GTK_A11Y=none "$@"
}
app_env "$GTK" --dir "$DIR_B" --name bob --port "$PORT_B" --no-mdns >"$RUN/bob.out" 2>"$RUN/bob.err" &
PIDS+=($!)
app_env "$GTK" --dir "$DIR_A" --name alice --port "$PORT_A" --no-mdns >"$RUN/alice.out" 2>"$RUN/alice.err" &
PIDS+=($!)
sleep 5

focus_only() {
    swaymsg "[title=\"p2pmsg — $1\"] focus, fullscreen enable" >/dev/null
    sleep 1
}
capture() {
    env XDG_RUNTIME_DIR="$XDG" WAYLAND_DISPLAY="$SOCK" grim -o HEADLESS-1 "$ROOT/build/ui-demo-$2.png"
}

focus_only alice
env XDG_RUNTIME_DIR="$XDG" WAYLAND_DISPLAY="$SOCK" wtype -s 100 "Bonjour bob, message de demo"
sleep 0.5
env XDG_RUNTIME_DIR="$XDG" WAYLAND_DISPLAY="$SOCK" wtype -s 300 -k Return
sleep 4
capture A A && echo "capture A ok"

focus_only bob
capture B B && echo "capture B ok"

for f in "$RUN/alice.err" "$RUN/bob.err"; do tail -n 3 "$f"; done
echo "demo terminée"
