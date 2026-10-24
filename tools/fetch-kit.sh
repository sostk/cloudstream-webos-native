#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Checks out Native App Kit at the commit this project builds on (docs/kit-patches/README.md) into
# external/webos-native-app-kit. A fresh clone is made at that commit; an existing checkout is left
# alone when it is already there, and otherwise moved only if it has no local changes.
#   tools/fetch-kit.sh            fetch or verify
#   tools/fetch-kit.sh --print    print the pinned commit
set -euo pipefail

KIT_REPO=https://github.com/Native-Webos/webos-native-app-kit.git
KIT_COMMIT=7fe49ab791a33fd135aec349c0929c959d99c146

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIR="$ROOT/external/webos-native-app-kit"

if [[ "${1:-}" == --print ]]; then
    echo "$KIT_COMMIT"
    exit 0
fi

if [[ ! -d "$DIR/.git" ]]; then
    mkdir -p "$DIR"
    git -C "$DIR" init -q
    git -C "$DIR" remote add origin "$KIT_REPO"
fi
if [[ "$(git -C "$DIR" rev-parse -q --verify HEAD 2>/dev/null || true)" == "$KIT_COMMIT" ]]; then
    echo "fetch-kit: $DIR at $KIT_COMMIT"
    exit 0
fi
if [[ -n "$(git -C "$DIR" status --porcelain --untracked-files=no 2>/dev/null)" ]]; then
    echo "fetch-kit: $DIR has local changes; not moving it to $KIT_COMMIT" >&2
    exit 1
fi
git -C "$DIR" fetch -q --depth 1 origin "$KIT_COMMIT"
git -C "$DIR" -c advice.detachedHead=false checkout -q "$KIT_COMMIT"
echo "fetch-kit: $DIR at $KIT_COMMIT"
