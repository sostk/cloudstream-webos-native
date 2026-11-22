#!/usr/bin/env bash
# Builds TorrServer (https://github.com/YouROK/TorrServer, GPL-3.0) for the TV from a pinned tag:
# static Go binaries, no cgo, as upstream's build-all.sh builds its Linux releases.
#   tools/build-torrserver.sh [out dir]      -> <out>/torrserver-arm64, <out>/torrserver-armv7
# Both are then packed with UPX (61 MB -> 17 MB on the TV's storage; unpacked into RAM when started).
# CloudStream Android embeds the same server (a fork) for torrent and magnet links.
set -euo pipefail
TAG=${TORRSERVER_TAG:-MatriX.151}
COMMIT=${TORRSERVER_COMMIT:-d8ff8c76984e9998b7b1b22ee2280ca2a23ee76d}
GO_IMAGE=${GO_IMAGE:-golang:1.25}
root=$(cd "$(dirname "$0")/.." && pwd)
out=$(realpath -m "${1:-$root/build/torrserver}")
src="$root/build/torrserver-src"
mkdir -p "$out"
if [ ! -d "$src/.git" ]; then
  git clone -q --depth 1 --branch "$TAG" https://github.com/YouROK/TorrServer.git "$src"
fi
got=$(git -C "$src" rev-parse HEAD)
[ "$got" = "$COMMIT" ] || { echo "TorrServer $TAG is $got, expected $COMMIT" >&2; exit 1; }
for target in arm64:arm64: armv7:arm:7; do
  IFS=: read -r name arch arm <<<"$target"
  docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -e GOCACHE=/tmp/gocache -e GOMODCACHE=/tmp/gomod \
    -v "$src":/src -v "$out":/out -w /src/server "$GO_IMAGE" \
    sh -c "CGO_ENABLED=0 GOOS=linux GOARCH=$arch GOARM=$arm go build -ldflags '-s -w -checklinkname=0' -tags=nosqlite -trimpath -o /out/torrserver-$name ./cmd"
done
docker run --rm -v "$out":/w -w /w alpine:3.20 sh -c \
  "apk add -q upx >/dev/null && for f in torrserver-arm64 torrserver-armv7; do upx -q --best --lzma \$f >/dev/null; done && chown $(id -u):$(id -g) torrserver-*"
cp "$src/LICENSE" "$out/LICENSE"
printf 'TorrServer %s (commit %s)\nSource: https://github.com/YouROK/TorrServer/tree/%s\nLicense: GPL-3.0 (LICENSE)\nBuilt with tools/build-torrserver.sh (CGO_ENABLED=0, -tags=nosqlite, UPX --best --lzma).\n' \
  "$TAG" "$COMMIT" "$COMMIT" > "$out/SOURCE.txt"
( cd "$out" && ls -l torrserver-* && sha256sum torrserver-* )
