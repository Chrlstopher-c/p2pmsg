#!/usr/bin/env bash
# Usage : scripts/mdns-test.sh   (valide le chemin mDNS reel dans p2pmsg-dev, sans toucher l'hote)
# Le conteneur (reseau prive, pas de LAN) demarre dbus-daemon et avahi-daemon en root, puis build
# net_discovery_test_mdns : une fois nu, une fois sous valgrind (preset debug). Code 0 seulement si vert.
set -eu
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE=p2pmsg-dev
docker build -q -t "$IMAGE" -f "$ROOT/docker/Dockerfile" "$ROOT/docker" >/dev/null
INNER='set -eu
mkdir -p /run/dbus /run/avahi-daemon
dbus-daemon --system --fork
avahi-daemon --no-chroot -D
for _ in $(seq 1 20); do avahi-daemon --check >/dev/null 2>&1 && break; sleep 0.5; done
avahi-daemon --check
cmake -S /work -B /work/build/mdns-plain -DCMAKE_BUILD_TYPE=Debug -DP2P_VALGRIND=OFF >/dev/null
cmake --build /work/build/mdns-plain -j2 --target net_discovery_test_mdns
cd /work/build/mdns-plain && ./src/net/discovery/net_discovery_test_mdns; echo "plain rc=$?"
cmake -S /work -B /work/build/mdns-vg -DCMAKE_BUILD_TYPE=Debug -DP2P_VALGRIND=ON >/dev/null
cmake --build /work/build/mdns-vg -j2 --target net_discovery_test_mdns
ctest --test-dir /work/build/mdns-vg -R net/discovery/test_mdns --output-on-failure --no-tests=error
echo "valgrind done"'
docker run --rm -v "$ROOT:/work" -w /work "$IMAGE" bash -c "$INNER"
