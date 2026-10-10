#!/usr/bin/env bash
# Usage : scripts/package.sh <version>
# Build release (CLI + GUI), install dans un staging, archive dist/p2pmsg-<version>-linux-x86_64.tar.gz + dist/SHA256SUMS.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:-}"
if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+([-.][0-9A-Za-z.]+)?$ ]]; then
    echo "usage : scripts/package.sh <version>  (ex. 1.0.0)" >&2
    exit 2
fi
NAME="p2pmsg-${VERSION}-linux-x86_64"
BUILD="$ROOT/build/release"
PKG_DIR="$ROOT/build/package"
STAGE="$PKG_DIR/$NAME"
DIST="$ROOT/dist"

cmake --preset release -S "$ROOT"
cmake --build "$BUILD" --target p2pmsg p2pmsg-gtk -j2

rm -rf "$STAGE"
cmake --install "$BUILD" --prefix "$STAGE"

for bin in "$STAGE/bin/p2pmsg" "$STAGE/bin/p2pmsg-gtk"; do
    if ldd "$bin" | grep -q "not found"; then
        echo "dépendance manquante dans $bin :" >&2
        ldd "$bin" | grep "not found" >&2
        exit 1
    fi
done
out="$("$STAGE/bin/p2pmsg" --help 2>&1 || true)"
if ! grep -q "usage" <<<"$out"; then
    echo "smoke test p2pmsg --help : pas de message d'usage" >&2
    exit 1
fi

mkdir -p "$DIST"
tar --owner=0 --group=0 --numeric-owner -C "$PKG_DIR" -czf "$DIST/$NAME.tar.gz" "$NAME"
(cd "$DIST" && sha256sum "$NAME.tar.gz" > SHA256SUMS)
echo "OK : $DIST/$NAME.tar.gz"
