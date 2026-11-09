#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Experiment: a relocatable Alpine edge root with Chromium for aarch64 (no system install), to test
# whether a webOS app could carry its own current browser engine (README.md).
#   docker run --rm -v <project>:/p alpine:edge sh /p/experiments/bundled-chromium/make-rootfs.sh
# Output: experiments/bundled-chromium/out/rootfs-aarch64/ and a size report.
set -eu
ARCH=aarch64
OUT=/p/experiments/bundled-chromium/out
ROOT=$OUT/rootfs-$ARCH
rm -rf "$ROOT"; mkdir -p "$ROOT/etc/apk/keys"
cp /usr/share/apk/keys/"$ARCH"/* "$ROOT/etc/apk/keys/"
printf '%s\n' https://dl-cdn.alpinelinux.org/alpine/edge/main https://dl-cdn.alpinelinux.org/alpine/edge/community \
    > "$ROOT/etc/apk/repositories"
apk add -q --root "$ROOT" --arch "$ARCH" --initdb --no-scripts --update-cache --keys-dir "$ROOT/etc/apk/keys" \
    --repositories-file "$ROOT/etc/apk/repositories" chromium
apk info --root "$ROOT" -v 2>/dev/null | sort > "$OUT/packages-$ARCH.txt"
{
    echo "packages: $(wc -l < "$OUT/packages-$ARCH.txt")"
    echo "chromium: $(grep '^chromium-[0-9]' "$OUT/packages-$ARCH.txt")"
    echo "root total: $(du -sh "$ROOT" | cut -f1)"
    echo "largest packages:"
    apk info --root "$ROOT" -s $(apk info --root "$ROOT" 2>/dev/null) 2>/dev/null | paste - - - | awk '{print $NF, $(NF-1), $1}' | sort -rn | head -15
    echo "compressed (xz -6) estimate: $(tar -C "$ROOT" -cf - . | xz -6 -T0 | wc -c | awk '{printf "%.0f MiB", $1/1048576}')"
} | tee "$OUT/size-$ARCH.txt"
chown -R "$(stat -c %u:%g /p)" "$OUT"
