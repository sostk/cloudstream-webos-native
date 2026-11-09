#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Install Alpine edge art_standalone (+ dependency closure) for one architecture into an empty root,
# with apk's own signature checks and WITHOUT running package scripts or any foreign binary.
#   docker run --rm -v <project>:/p alpine:edge sh /p/runtime/make-rootfs.sh <aarch64|x86_64|armv7>
#       the current packages from Alpine edge (rootfs-<arch>.manifest is rewritten with them)
#   docker run --rm -v <project>:/p -v <inputs dir>/alpine/<arch>:/apks:ro alpine:edge \
#       sh /p/runtime/make-rootfs.sh <arch> /apks
#       exactly the pinned packages (runtime/inputs.lock, tools/fetch-runtime-inputs.sh), no network;
#       fails unless the result is the package list in rootfs-<arch>.manifest
# Output: /p/runtime/rootfs/<arch>/ and rootfs-<arch>.manifest (package list with versions and checksums).
set -eu
ARCH=$1
APKS=${2:-}
ROOT=/p/runtime/rootfs/$ARCH
rm -rf "$ROOT"; mkdir -p "$ROOT/etc/apk"
mkdir -p "$ROOT/etc/apk/keys"
cp /usr/share/apk/keys/"$ARCH"/* "$ROOT/etc/apk/keys/"   # signing keys for the target architecture
printf '%s\n' https://dl-cdn.alpinelinux.org/alpine/edge/main https://dl-cdn.alpinelinux.org/alpine/edge/community \
    https://dl-cdn.alpinelinux.org/alpine/edge/testing > "$ROOT/etc/apk/repositories"
if [ -n "$APKS" ]; then
    # Only the pinned files: apk still checks each package's signature against Alpine's keys, plus the
    # pinned public key of a locally rebuilt package (armv7's ART r3, runtime/inputs-armv7.lock).
    if [ -d "$APKS/keys" ]; then cp "$APKS"/keys/*.pub "$ROOT/etc/apk/keys/"; fi
    apk add -q --root "$ROOT" --arch "$ARCH" --initdb --no-scripts --no-network --keys-dir "$ROOT/etc/apk/keys" \
        --repositories-file /dev/null "$APKS"/*.apk
    apk info --root "$ROOT" -v 2>/dev/null | sort > /tmp/installed
    if ! cmp -s /tmp/installed "/p/runtime/rootfs-$ARCH.manifest"; then
        diff /p/runtime/rootfs-"$ARCH".manifest /tmp/installed >&2 || true
        echo "make-rootfs.sh: installed packages differ from rootfs-$ARCH.manifest" >&2
        exit 1
    fi
elif [ "$ARCH" = armv7 ] && [ -d /art-apks ]; then
    # Locally rebuilt ART r3: retain signature checks using only its public key.
    cp /art-apks/keys/*.pub "$ROOT/etc/apk/keys/"
    apk add -q --root "$ROOT" --arch "$ARCH" --initdb --no-scripts --update-cache --keys-dir "$ROOT/etc/apk/keys" \
        --repositories-file "$ROOT/etc/apk/repositories" /art-apks/art_standalone-*.apk
    apk info --root "$ROOT" -v 2>/dev/null | sort > "/p/runtime/rootfs-$ARCH.manifest"
else
    apk add -q --root "$ROOT" --arch "$ARCH" --initdb --no-scripts --update-cache --keys-dir "$ROOT/etc/apk/keys" \
        --repositories-file "$ROOT/etc/apk/repositories" art_standalone
    apk info --root "$ROOT" -v 2>/dev/null | sort > "/p/runtime/rootfs-$ARCH.manifest"
fi
wc -l < "/p/runtime/rootfs-$ARCH.manifest"
du -sh "$ROOT"
