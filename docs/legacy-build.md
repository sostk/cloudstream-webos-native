# Legacy package: TVs with a 32-bit kernel

Legacy playback also has an opt-in ACB video-plane fallback for firmware without
SDL exported-window support. See [legacy-playback.md](legacy-playback.md) for the
implementation, tests and TV validation checklist. The main package keeps the
kit's original playback backend.

The main package (`org.csweb.tv`) needs two things some older LG TVs do not have:

| Needs | Why | Seen on an old TV (webOS 4.4.3, Mali-G51) |
|---|---|---|
| a 64-bit kernel | the plugin runtime is `runtime-aarch64` (ART, musl, aarch64) | `kernel … armv7l` in the log, then `runtime: exited (code 127, signal 0)` four times: `execve` of an aarch64 ELF fails |
| `libcurl.so.4` in the firmware | the kit dlopens it for all HTTP (webOS 5.3.1+ has it) | `no HTTP transport: libcurl.so.4: cannot open shared object file`, `net.http unsupported` |

`armv7l` from `uname` means a 32-bit kernel. (A 64-bit kernel reports `aarch64`, or `armv8l` to a 32-bit
process.) Such a TV cannot start any 64-bit program, so a 64→32 bridge such as webos-bridge-64to32 does not
help either.

The legacy package, `org.csweb.tv.legacy`, is the same app built with `-DCSWEB_LEGACY=ON`:

- `runtime-armv7/`: the plugin runtime from Alpine's **armv7** packages (`art_standalone` is built for
  armv7 in Alpine edge/testing), started through `ld-musl-armhf.so.1`;
- `lib/libcurl.so.4`: curl + mbedTLS built with the webOS NDK (`tools/build-libcurl-webos.sh`), verifying
  against `runtime-armv7/trust/ca-bundle.pem` (the same Mozilla list as the runtime's Java trust store);
- its own app id, so it installs beside the main package and a Homebrew Channel update of one never
  replaces the other.

## Nothing changes for the main package

Every change is opt-in. Without the new options each script and the app build exactly as before:

| Piece | Default (main package) | Legacy |
|---|---|---|
| `tools/build-runtime.sh` | `--arch aarch64` (default): unchanged steps | `--arch armv7` |
| `tools/fetch-runtime-inputs.sh` | `runtime/inputs.lock` | also `runtime/inputs-armv7.lock` |
| `runtime/native/build-native.sh`, `build-wolfssl.sh` | aarch64 + x86_64 | `armv7` argument |
| `runtime/make-aot.sh`, `make_dist.py` | aarch64 / x86_64 cases untouched | new `armv7` case |
| `runtime/tools/notices.py`, `collect-licenses.sh` | `runtime/licenses/` | `CSWEB_LICENSES_DIR=…/licenses-armv7` |
| app sources | the same preprocessed code | `#ifdef CSWEB_LEGACY` (app id, runtime folder, loader name) |
| `apps/csweb-tv/CMakeLists.txt` | `CSWEB_LEGACY=OFF` | `org.csweb.tv.legacy`, `runtime-armv7`, bundled libcurl |
| CI | `host`, `runtime`, `webos` unchanged | new `runtime-legacy`, `webos-legacy` jobs, `continue-on-error` |
| release | the same files | legacy ipk + manifest added only when built **and** `runtime/licenses-armv7/` exists |

## Building it

```
tools/build-runtime.sh --arch armv7          # runtime/dist/runtime-armv7 (docker)
cd external/webos-native-app-kit
cmake --preset webos-release -B build/webos-legacy \
  -DWNK_EXTRA_APP_DIRS=$PWD/../../apps/csweb-tv -DCSWEB_LEGACY=ON
cmake --build build/webos-legacy --target csweb-tv-ipk
# -> build/webos-legacy/dist/org.csweb.tv.legacy_<version>_arm.ipk
```

Configure runs `tools/build-libcurl-webos.sh` once (cached by its inputs), like the kit's FFmpeg.

### Rebuilding ART against current wolfSSL

Alpine edge's available armv7 `art_standalone-0_git20251009-r2` needs `libwolfssl.so.45`, while
`wolfssl-jni-5.9.4-r0` provides `.46`. Until the armv7 inputs are pinned, the legacy build runs
`runtime/build-art-armv7.sh` in an armv7 Alpine container to rebuild Alpine's `r3` recipe at
aports commit `8ac8f5cb2e5c2206a93de5498f6368190625b3cc` against wolfSSL 5.9.4. The recipe has a
SHA-256 check and abuild verifies the source archive's SHA-512. No ART source patch or ABI symlink
is used. The resulting APK is signed with an ephemeral key; only its public key leaves the container.
The rootfs installer verifies that signature, and the rebuild checks that its libraries require
wolfSSL `.46` and do not require `.45`. The custom wolfSSL build retains the serial-zero certificate fix.

CI builds ART in a separate `art-legacy` job on `ubuntu-24.04-arm`. It verifies that a 32-bit armv7
container runs without a qemu-arm registration before compilation. The verified APK has its own cache,
keyed by the build script (including recipe/source and wolfSSL pins), Alpine image and selected toolchain
package versions. That cache is saved before runtime assembly, so a later runtime failure does not
discard a successful ART build. `runtime-legacy` downloads the signed APK and public key and supplies
them with `CSWEB_ARMV7_ART_DIR`; this setting is used only for unpinned armv7 builds. Compilation emits
a heartbeat every minute to distinguish quiet compiler phases from missing log updates.

For local builds, run the ART container on an ARM host with 32-bit userspace support. On x86_64,
register qemu-arm/binfmt first; emulating the compiler can take hours. Alternatively, extract CI's
`art-armv7` artifact and run `CSWEB_ARMV7_ART_DIR=/absolute/path/to/artifact tools/build-runtime.sh --arch armv7`
to reuse it. The rootfs installer still verifies the APK's signature using the artifact's public key.

CI keeps the rebuilt APK, public key, recipe and provenance in the `art-armv7` artifact before the
runtime job starts. The local output is `runtime/native/out/art-armv7/`. Retain these for the
eventual input mirror and licence collection. Pinning this locally signed APK will also require
retaining its public key in the pinned-input installation path; the current rebuild is for the
unpinned bootstrap only. Pinned aarch64 builds and their inputs are unchanged.

## First-time setup (once per move of the pins)

Until `runtime/inputs-armv7.lock` exists, `build-runtime.sh --arch armv7` takes the armv7 packages unpinned
from Alpine edge (CloudStream stays pinned) and only reports missing licence notices. To pin and license it,
after a build that worked on a TV:

```
python3 runtime/tools/make_lock.py armv7          # writes runtime/inputs-armv7.lock from rootfs-armv7.manifest
#  upload ~/.cache/csweb-runtime-inputs/alpine/armv7/*.apk to a runtime-inputs-N release (asset name =
#  path with / -> _) and add its "# mirror: <url>" line to the lock
export CSWEB_LICENSES_DIR=$PWD/runtime/licenses-armv7
python3 runtime/tools/notices.py packages runtime/rootfs/armv7 runtime/dist/runtime-armv7
docker run --rm -v $PWD:/p -e CSWEB_LICENSES_DIR=/p/runtime/licenses-armv7 alpine:edge sh /p/runtime/collect-licenses.sh
python3 runtime/tools/notices.py render
```

Commit `runtime/rootfs-armv7.manifest`, `runtime/inputs-armv7.lock` and `runtime/licenses-armv7/`. From then
on the armv7 build is pinned, its notices check is strict, and releases include the legacy package.

## To check on the TV

The legacy package is built by CI but, like the main one, not run on a TV by CI. Things to watch in its log
(`/tmp/org.csweb.tv.legacy/`):

- `runtime: launching` followed by the runtime becoming ready, not `exited (code 127)`;
- `net.http supported libcurl/8.23.0 mbedTLS/3.6.7`; adding a repository then works;
- memory: a 32-bit process has about 3 GB of address space and old TVs have little free RAM. The heap is
  `-Xmx256m` (`runtime_launcher.hpp`); lower it if ART cannot reserve it;
- AOT: `runtime/make-aot.sh` compiles for a conservative ARMv7 feature set. If the build could not AOT-compile,
  the log says `AOT: no AOT.json` and ART starts without a boot image (slower, still works). Once dalvikvm has
  run on the TV, put the `--instruction-set-features` it logs into `make-aot.sh`.
- The libcurl's CA bundle path is compiled in as
  `/media/developer/apps/usr/palm/applications/org.csweb.tv.legacy/runtime-armv7/trust/ca-bundle.pem`
  (where both developer mode and the Homebrew Channel install apps; `CSWEB_LEGACY_INSTALL_DIR` changes it).
  A TLS failure naming that file means the app is installed elsewhere.


### Diagnosing curl initialization

The legacy webOS build logs `[legacy-curl]` lines in `events.log` after the kit initializes HTTP.
They identify the retained libcurl's path and version, and any initialization errors captured by
our bundled curl/mbedTLS. Errors include the failed global initializer, PSA/DRBG error codes,
and entropy-source failures (`getrandom`, device open/read) with numeric errno snapshots and device paths.
An errno snapshot can be stale for failures that do not set errno; use it with the named stage
and its return code. A missing diagnostic symbol indicates a different or older libcurl was loaded.
The diagnostics inspect the existing library without retrying initialization or making requests.
They record no random bytes, credentials, or URLs. No initialization failure recorded means only
that these hooks captured no failure; refer to the kit's `net.http` capability for transport status.

The source instrumentation runs only in `tools/build-libcurl-webos.sh`, and its hash participates
in the curl build cache key. The main app, firmware libraries, and pinned kit are unchanged.

### Legacy entropy and interpreter comparison

On the webOS 4.4.3 TV, the diagnostic package loaded its bundled curl but failed in mbedTLS's
platform entropy device open with ENOENT, followed by PSA error -148. mbedTLS 3.6.7 defaults
to `/dev/random`, which this TV lacks. The first diagnostic build incorrectly labeled that
failure `urandom fopen`; the label did not reflect the actual configured device path.

The legacy build supplies the Linux ARM EABI `getrandom` syscall number (384) only if the old
NDK headers omit it. mbedTLS uses this dedicated kernel interface first and falls back only on
ENOSYS. Its device fallback is explicitly `/dev/urandom`. All other syscall/device errors remain
fatal to initialization; no predictable entropy fallback or certificate-verification bypass is used.

The same TV starts ART but repeatedly crashes with SIGSEGV in compiled boot code, with a frame
reported as `WolfSSLKeyX509.getPrivateKey`. The legacy launcher now adds `-Xint` for an interpreter
comparison, retaining the boot image's data and resource paths. Its launch status includes
`interpreter (-Xint)`. This may slow startup; it is a diagnostic comparison, not a confirmed ART fix.
The main package's launcher options and all ART/runtime build inputs remain unchanged.

### Plugin compiler boot-classpath inheritance

The next TV log confirms HTTP/HTTPS downloads succeed and ART becomes ready with `-Xint`.
Plugin installation succeeds, but plugin loading times out after an on-device dex2oat error:
the image records `/tmp/org.csweb.tv.legacy/rt/...` jar locations while the compiler expects
real installation paths under `/media/developer/apps/.../runtime-armv7/...`.

The exact ART source at `e78bf68917bcaaf58fef3960cd88793b3b7f39cc` explains the difference:
`art/runtime/oat_file_assistant.cc::GetBestOatFile` still invokes dex2oat in interpreter mode
and does not forward the VM's explicit boot class path. In `art/runtime/parsed_options.cc`,
the compiler accepts `BOOTCLASSPATH` from its environment; without it, its fallback applies
`realpath` to libart's jar directory. The legacy launcher therefore exports the validated
`-Xbootclasspath` value verbatim as `BOOTCLASSPATH`, preserving stable paths and jar order.
Its launch status includes `compiler bootclasspath inherited`. Missing/rejected AOT metadata
keeps the existing imageless fallback and supplies no new override. Main launch behavior is unchanged.

This repairs the demonstrated compiler-path mismatch without rebuilding ART or changing its sources.
It still needs a TV plugin-load test; the existing timeouts do not prove this is the only remaining
loading problem. Success means providers register, not merely that the plugin download completes.

### Reading runtime.log in Dev Manager

The legacy build sets the open runtime stderr log to mode `0640` on every runtime start, including
existing `0600` logs from earlier builds. The app owns the file and webOS group `5000` can read it;
the tested Dev Manager `prisoner` account belongs to that group. Permissions are set on the open
descriptor before launching ART, so a restrictive umask does not remove group access. Existing
contents are preserved and new output is appended. Permission setup failures are reported as launch
errors. No manual chmod or root account is needed after installing and launching the updated package.
The log remains `/tmp/org.csweb.tv.legacy/runtime.log`; main-package permissions remain `0600`.

### Locating the remaining plugin-load stall

The compiler-path fix let the TV complete dex2oat in 244–674 ms, but plugin loads still timed out.
A tester sent SIGQUIT to the correct VM while loading; ART's Signal Catcher received it and then
crashed with SIGSEGV before producing a Java thread dump. Do not repeat that diagnostic on this build.

`tools/build-runtime.sh --arch armv7` now opts into `CSWEB_LEGACY_LOAD_TRACE=1` for the DEX build.
`tools/instrument-legacy-loader.py` validates insertion points and creates a temporary source copy;
it never edits the shared csloader sources. Only this legacy DEX includes `[legacy-load]` stderr lines
with elapsed milliseconds, thread name, and fixed stage labels. They cover queued/started requests,
integrity checking, class-loader creation, manifest open/read/parse, plugin class resolution and
construction, context/resources, `plugin.load`, dialogs and provider registration. A begin marker
without its end marker identifies the unfinished stage; queued requests without a start marker may
be waiting behind an earlier stuck request. Exceptions can also end a stage without an end marker,
so consult accompanying runtime error events. No signal, stack-dump attempt, extra thread, or request
timeout change is introduced. Progress logs contain no plugin configuration, credentials or URLs.

After installing the new legacy package, reproduce one plugin load and send `runtime.log` and
`events.log`. Root access and shell commands are not needed. The main build uses its original
Java sources and gains no progress logging; ART's source/package build is unchanged.

### Native ARM runtime reproduction test

The non-blocking `legacy-runtime-test` CI job consumes the packaged `runtime-armv7` artifact on
`ubuntu-24.04-arm`, verifies native 32-bit execution without qemu-arm registration, and sends real
service `load` requests for unmodified BilibiliProvider and NguonC plugins. The download URLs use
fixed repository commits; logs record plugin SHA-256 values, runtime-archive SHA-256 and host kernel.
Only plugin loading/provider registration is tested, not searches, playback, or webOS integration.

The plugins run in a disposable ARMv7 container with networking disabled, no injected credentials,
read-only runtime/plugin mounts, and fresh temporary state for each plugin. The VM options and
stable boot paths match the legacy launcher, including `-Xint` and inherited `BOOTCLASSPATH`.
`tools/test-legacy-runtime.py` requires both `loaded` and a successful matching `done` event;
`ready` alone is not success. Startup and load waits are bounded, stderr/protocol/progress logs are
retained, and timed-out process groups are killed without SIGQUIT. The `legacy-runtime-load-test`
artifact retains summaries and thread CPU/wait-state snapshots. No production TV is involved.

These results can reproduce generic ARMv7/ART/class-loading failures but cannot reproduce the TV's
webOS sandbox, kernel, or firmware libraries. Network-dependent plugin initialization may fail in
this offline test for a separate reason; inspect the recorded stage and protocol error before
attributing that failure to the TV problem. Existing main-package jobs do not depend on this job.

Native reproduction result (2026-10-22, run 39768963687, commit f232fe9): both BilibiliProvider
(SHA-256 a16eeeaab62abeba25eb0be5e189b769e319614ca3cb11b114ad9dd5a6cfc82b) and NguonC
(SHA-256 47e98cc384468af20117e1ed0168f653192b312ff5eff1d583a29be99067636e) reach ready,
finish compilation, construct the class loader, and open/read manifest.json, but stop at
`manifest.parse.begin` and time out after 90 seconds without registering providers. This reproduces
a loading failure on native ARM Linux and isolates the combined `MainAPIKt.getMapper().readValue(...)`
step, before plugin class resolution. The class-loader-context warning did not prevent reaching
manifest parsing. The exact operation inside mapper initialization/deserialization is not yet known.
Two earlier test-harness setup failures (log-directory ownership and missing passwd entry for the
numeric test UID) were corrected before this result; they were not TV runtime diagnoses.

The next trace separates that combined expression into `mapper.initialize.begin/end` around
`MainAPIKt.getMapper()` and `manifest.deserialize.begin/end` around `readValue`. The same
mapper and manifest types are used, in the same order, only in the generated legacy source copy.
The armv7 runtime cache key now includes the instrumentation script, so trace-only changes cannot
silently reuse an older DEX. The aarch64 runtime cache key and Java sources are unchanged.

Split trace result (run 39771097878, commit 137d234): both fixed plugins emit
`mapper.initialize.begin` and time out at 90 seconds without `mapper.initialize.end`. Neither
reaches `manifest.deserialize.begin`. This isolates `MainAPIKt.getMapper()` and the class
initialization it triggers; `readValue` is not reached and has not been independently cleared.
The exact initializer or operation inside that boundary is still unknown. Both runtime and
webOS package jobs passed; the native plugin-load job fails because these loads time out.

The next legacy-only loader change reads the four manifest fields with the existing bounded
`MiniJson` parser instead of triggering CloudStream's global mapper. It fills the original
`BasePlugin.Manifest` DTO through its normal setters. Missing nullable name/version remain null,
resources default false, unknown fields are ignored, and the plugin class name must be a nonempty
string. Present fields are type-checked and versions must fit a 32-bit integer. The existing
64-KiB manifest-read limit and MiniJson's depth limit remain in force. The shared loader sources,
main DEX build, CloudStream API and plugin bytes are unchanged. This avoids global initialization
for the loader's manifest step only; plugins may still call that mapper later, so real native
plugin registration and eventual TV operation must be tested before claiming the app works.

Run 39841593705 gets past the mapper stall but both real plugins fail the manifest version's
floating-point validation on ARM. The local JVM schema test passes, so this difference is not
yet explained. The legacy reader now accepts MiniJson's Integer/Long tokens and checks bounds
using integer arithmetic, rejecting fractional/exponent tokens. No floating-point conversion
or Math.rint is needed for an integer schema field. Native plugin registration is being retested;
this change does not establish the cause of the global mapper stall.

Run 39842858494 confirms the integer reader parses both real manifests. Both plugins finish
class resolution/construction and context/resources in under one second, then time out at
`plugin.load.begin`; neither registers a provider. The manifest workaround is therefore partial.
The native test now also invokes separate, bounded probe VMs for floating-point operations,
kotlinx JSON initialization, Jackson's builder, KotlinModule's builder, and MainAPIKt initialization.
The probe entry point exists only in the generated legacy DEX and is never called by the app.
Probe logs accompany the real plugin results; a successful probe alone does not pass the job.

Run 39844484434 isolates a native math failure: conversion of integer 27 yields the correct
double bits `403b000000000000`, but Math.rint returns `403f1576bf0`. kotlinx JSON and plain
Jackson initialization complete; KotlinModule.Builder construction and MainAPIKt initialization
time out. Jackson Kotlin 2.13.1's KotlinFeature initializer calls `2.0.pow(ordinal)`.
The pinned ART ARM JNI frame builder assumes soft-float arguments/results, while Alpine's
libopenjdk Math entry points use the hard-float compiler ABI (JNICALL is empty).

The legacy runtime now tests a small Math JNI bridge: AAPCS entry points receive ART's arguments
and call the same musl libm functions using the compiler's normal AAPCS-VFP convention. It registers
only java.lang.Math's existing native signatures, before service startup; StrictMath and other
JNI classes are untouched. It is compiled/packaged only for armv7, and startup fails if rint/pow
self-checks fail. CI compares baseline and bridged math in separate VMs (including binary arguments,
rounding, signed zero, NaN and infinity), then retests initializers and real plugin registration.
The ART APK and aarch64 behavior are unchanged. This is under test, not a compatibility guarantee
for every JNI method or plugin operation.

Native result (run 39846264197, commit dab0178): the baseline still returns incorrect rint/pow
bits, while the bridge passes every Math edge-case probe. KotlinModule initializes in 3 ms and
MainAPIKt in 114 ms. Both real unmodified plugins register providers with successful loaded/done
events: BilibiliTV in 0.410 s and Nguồn C in 0.343 s. No timeout occurs. This confirms the bridge
repairs the reproduced load failure; it does not test webOS searches/playback or every JNI class.
