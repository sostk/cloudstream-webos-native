#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the plugin runtime the TV package carries (runtime/dist/runtime-aarch64) from the pinned
# inputs only (runtime/inputs.lock), in the order docs/runtime/tls.md gives, each step in its own
# alpine:edge container as the step's script says. Needs docker, python3 and readelf.
#   tools/build-runtime.sh [<inputs dir>]     default: ~/.cache/csweb-runtime-inputs
#   tools/build-runtime.sh --latest           instead: the current Alpine edge packages and CloudStream
#                                             pre-release (untested; for the weekly upstream check). The
#                                             manifests are rewritten; the notices check reports, not fails.
#   tools/build-runtime.sh --arch armv7 [...]  the legacy runtime (runtime/dist/runtime-armv7) for TVs with a
#                                             32-bit kernel (docs/legacy-build.md). Default --arch aarch64:
#                                             without the option every step is exactly the aarch64 build.
# Also builds the conformance test plugin (the project's own fixture) that runtime/build.sh tests with.
# No plugin from a repository is downloaded or run.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LATEST=0
ARCH=aarch64
while [[ "${1:-}" == --* ]]; do
    case $1 in
        --latest) LATEST=1; shift ;;
        --arch) ARCH=${2:?--arch needs aarch64 or armv7}; shift 2 ;;
        *) echo "build-runtime: unknown option $1" >&2; exit 2 ;;
    esac
done
INPUTS="${1:-$HOME/.cache/csweb-runtime-inputs}"
# Where the app links its runtime before starting ART (runtime_launcher.cpp): /tmp/<app id>/rt.
case $ARCH in
    aarch64) STABLE=/tmp/org.csweb.tv/rt ;;
    armv7) STABLE=/tmp/org.csweb.tv.legacy/rt ;;  # the legacy package's app id (apps/csweb-tv, CSWEB_LEGACY)
    *) echo "build-runtime: unsupported arch $ARCH (aarch64 or armv7)" >&2; exit 2 ;;
esac
DIST="$ROOT/runtime/dist/runtime-$ARCH"
# aarch64's Alpine pins are in runtime/inputs.lock; another arch has its own lock (runtime/inputs-<arch>.lock).
# Until that lock exists the arch's Alpine packages are taken unpinned from edge (CloudStream stays pinned),
# its manifest is rewritten. ARMv7 notices become strict once their inventory is committed below.
ARCH_LOCK="$ROOT/runtime/inputs-$ARCH.lock"
PIN_ALPINE=1
if [[ $ARCH != aarch64 && ! -f $ARCH_LOCK ]]; then PIN_ALPINE=0; fi
NOTICES_STRICT=1
if (( LATEST || ! PIN_ALPINE )); then NOTICES_STRICT=0; fi
# Once ARMv7 notices are committed, normal legacy builds must match them even
# while its APK inputs remain unpinned. --latest remains an exploratory build.
if [[ $ARCH == armv7 && $LATEST == 0 && -f "$ROOT/runtime/licenses-armv7/packages.tsv" ]]; then
    NOTICES_STRICT=1
fi
step() { echo "== build-runtime: $*"; }
run() { docker run --rm -v "$ROOT":/p "$@"; }

mkdir -p "$ROOT/evidence/cloudstream-app"
if (( LATEST )); then
    step "inputs: latest CloudStream pre-release"
    for f in app-prerelease-release.apk classes.jar app-sources.jar; do
        curl -fsSL --retry 3 -o "$ROOT/evidence/cloudstream-app/$f" \
            "https://github.com/recloudstream/cloudstream/releases/download/pre-release/$f"
    done
    APKS=() TRUST_APKS=()
else
    step "inputs"
    if [[ $ARCH == aarch64 ]]; then
        "$ROOT/tools/fetch-runtime-inputs.sh" "$INPUTS"
    elif (( PIN_ALPINE )); then
        "$ROOT/tools/fetch-runtime-inputs.sh" "$INPUTS" "$ROOT/runtime/inputs.lock" "$ARCH_LOCK"
    else
        echo "build-runtime: no runtime/inputs-$ARCH.lock: $ARCH packages from Alpine edge, unpinned" >&2
        "$ROOT/tools/fetch-runtime-inputs.sh" "$INPUTS"
    fi
    cp "$INPUTS"/cloudstream/* "$ROOT/evidence/cloudstream-app/"
    APKS=() TRUST_APKS=(-v "$INPUTS/alpine/x86_64":/apks:ro)
    if (( PIN_ALPINE )); then APKS=(-v "$INPUTS/alpine/$ARCH":/apks:ro); fi
fi

if [[ ! -f "$ROOT/test-plugins/conformance/out/CswebConformance.cs3" ]]; then
    step "conformance test plugin"
    run eclipse-temurin:17-jdk sh /p/test-plugins/conformance/build.sh
fi

ART_APKS=()
if [[ $ARCH == armv7 ]] && (( LATEST || ! PIN_ALPINE )); then
    # edge's armv7 ART r2 still needs wolfSSL ABI 45, but edge now ships ABI 46.
    # Rebuild Alpine's r3 recipe only for the unpinned legacy path; pinned builds use their APKs.
    step "ART for armv7 (wolfSSL 5.9.4)"
    if [[ -n ${CSWEB_ARMV7_ART_DIR:-} ]]; then
        # CI builds this APK on a native ARM runner and retains it independently of the runtime.
        ART_DIR=$(cd "$CSWEB_ARMV7_ART_DIR" && pwd)
        test -s "$ART_DIR/art_standalone-0_git20251009-r3.apk"
        test -d "$ART_DIR/keys"
    else
        run --platform linux/arm/v7 alpine:edge sh /p/runtime/build-art-armv7.sh
        ART_DIR="$ROOT/runtime/native/out/art-armv7"
    fi
    ART_APKS=(-v "$ART_DIR":/art-apks:ro)
fi
step "root filesystem ($ARCH)"
run "${APKS[@]}" "${ART_APKS[@]}" alpine:edge sh /p/runtime/make-rootfs.sh "$ARCH" ${APKS:+/apks}
step "trust store"
run "${TRUST_APKS[@]}" alpine:edge sh /p/runtime/make-trust.sh
if [[ $ARCH == armv7 ]]; then  # the legacy package's bundled libcurl reads it (tools/build-libcurl-webos.sh)
    run "${TRUST_APKS[@]}" alpine:edge sh /p/runtime/make-trust-pem.sh
fi
step "native helpers"
if [[ $ARCH == aarch64 ]]; then
    run alpine:edge sh /p/runtime/native/build-native.sh
    step "wolfSSL"
    run alpine:edge sh /p/runtime/native/build-wolfssl.sh
else
    run alpine:edge sh /p/runtime/native/build-native.sh "$ARCH"
    step "wolfSSL"
    run alpine:edge sh /p/runtime/native/build-wolfssl.sh "$ARCH"
fi
step "compat layer and csloader (DEX)"
if [[ $ARCH == armv7 ]]; then
    run -e CSWEB_LEGACY_LOAD_TRACE=1 alpine:edge sh /p/runtime/build.sh
else
    run alpine:edge sh /p/runtime/build.sh
fi

step "dist"
python3 "$ROOT/runtime/make_dist.py" "$ROOT/runtime/rootfs/$ARCH" "$ARCH" "$DIST"
step "ahead-of-time compilation (for $STABLE)"
if [[ $ARCH == aarch64 ]]; then
    run alpine:edge sh /p/runtime/make-aot.sh "$ARCH" "$STABLE"
elif ! run alpine:edge sh /p/runtime/make-aot.sh "$ARCH" "$STABLE"; then
    # Optional for the legacy runtime: without AOT.json the app starts ART without the boot image (slower).
    echo "build-runtime: AOT for $ARCH failed; the runtime ships without it" >&2
    run alpine:edge rm -rf "/p/runtime/dist/runtime-$ARCH/boot" "/p/runtime/dist/runtime-$ARCH/csweb/oat" \
        "/p/runtime/dist/runtime-$ARCH/AOT.json"
fi
python3 "$ROOT/runtime/make_dist.py" --manifest "$DIST"

step "licence notices"
if [[ $ARCH != aarch64 ]]; then export CSWEB_LICENSES_DIR="$ROOT/runtime/licenses-$ARCH"; fi
if (( ! NOTICES_STRICT )); then
    python3 "$ROOT/runtime/tools/notices.py" check "$ROOT/runtime/rootfs/$ARCH" "$DIST" || true
else
    python3 "$ROOT/runtime/tools/notices.py" check "$ROOT/runtime/rootfs/$ARCH" "$DIST"
fi
echo "build-runtime: $DIST"
