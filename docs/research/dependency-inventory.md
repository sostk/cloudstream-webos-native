# Dependency and licence inventory

Status 2026-10-19. "Resolved on TV" means observed on the target TV; nothing below the kit has been
run on the TV yet. Licence entries are from package metadata and must be confirmed against the actual
licence files before any redistribution (Phase 7 audit).

## 1. Native App Kit (`e10f35e…`)

GPL-3.0-or-later. Bundled/compiled-in components per kit `LICENSING.md` / `THIRD-PARTY-NOTICES.md`
(audited by the kit 2026-10-12): SDL-webOS 2.30.12 (zlib), stb (MIT/PD), nlohmann/json (MIT), curl
headers (curl), nanosvg (zlib), qrcodegen (MIT), libwebp (BSD-3), Atkinson Hyperlegible (OFL-1.1),
FFmpeg (LGPL-2.1+, player only). LG libraries and libcurl are used from the TV, never shipped.
The app adds Noto Emoji (OFL-1.1, google/fonts `8b0a1d0`, `emoji-font/` with its OFL.txt; fetched by
apps/csweb-tv/CMakeLists.txt and checked by SHA-256, not stored in git) as a fallback font.
Resolved on TV: yes (kit records; our probe ipk installs and runs) `[HW]`.

## 2. Candidate on-device runtime: art_standalone (Alpine edge, prebuilt)

Upstream https://gitlab.com/android_translation_layer/art_standalone, inspected at
`66a5d9079b159e9c5819bfeef4a6fff6a5f32719` (2026-08-03). ART from AOSP with Linux (glibc/musl) patches;
oat version 170 (Android 10 era); OpenJDK-based libcore (`ojluni`: `java.time`, `java.lang.invoke`,
`CompletableFuture`); boot classpath: core-oj, core-libart, okhttp, bouncycastle, apache-xml,
apachehttp, wolfssljni. `ART_HOST_SUPPORTED_ARCH := x86 x86_64 arm64 arm`. Only self-hosted builds are
supported upstream.

Alpine packages `art_standalone 0_git20251009-r3` exist for aarch64, x86, x86_64 (and `-r2` for armv7),
repository *testing*, licence Apache-2.0 (package metadata).

### aarch64 closure (`tools/apk_closure.py` over edge APKINDEX; `evidence/runtime-host/alpine-aarch64-closure.json`)

Naive closure: 54 packages, 279.6 MiB. Two dependency edges cause most of it and are not needed at
run time for DEX-only plugins:
- `bionic_translation → so:libEGL.so.1 → mesa → llvm23-libs, libx11 …` (EGL only resolves `gl*` symbols for
  Android native libraries; the test plugins have none). Needs a small `libEGL.so.1` provider (e.g.
  libglvnd's dispatcher) or proof that it is not loaded `[UNK]`.
- `java-cacerts → p11-kit, bash, ca-certificates` (install script that generates the Java trust store;
  the store can be generated at build time).

Runtime set (approximate, installed sizes): art_standalone 28.1 MiB; icu-libs 4.9 + icu-data-en 2.9;
libcrypto3 4.7; vixl 2.8; libstdc++ 2.7; wolfssl 0.7 + wolfssl-jni 1.5; musl 0.7; libunwind 0.3;
bionic_translation 0.3; lz4, xz, zlib, zstd, libbsd, libmd, libexpat, libelf, libucontext, libatomic,
libgcc (<1 MiB together) → **≈ 55 MiB**, plus the generated cacerts.

| Component | Licence (metadata) | Notes |
|---|---|---|
| ART, libcore Android parts, libnativehelper, libziparchive, bionic_translation | Apache-2.0 | |
| libcore `ojluni` (OpenJDK) | GPL-2.0-only **with Classpath Exception** `[INF from AOSP]` | run as a separate process; confirm combination with GPL-3.0 app |
| wolfSSL, wolfssl-jni | **GPL-3.0-only** | compatible with distributing the whole as GPL-3.0 `[INF]`; GPL-3.0-only fixes the version |
| bouncycastle | MIT-style | |
| musl | MIT | |
| libstdc++, libgcc, libatomic | GPL-3.0+ with GCC Runtime Library Exception | metadata says GPL-2.0+/LGPL; confirm |
| ICU | Unicode/ICU | |
| vixl | BSD-3-Clause | |
| libcrypto3 (OpenSSL 3) | Apache-2.0 | |
| zlib, lz4, xz, zstd, libbsd, libmd, expat, libunwind, libelf, libucontext | permissive or (L)GPL | libelf: GPL-3.0+/(GPL-2.0+ or LGPL-3.0+) |

### Behaviour measured on the host (x86_64 Alpine container, same package version) `[TEST]`
Log: `evidence/runtime-host/artprobe-x86_64.log`, `tls-matrix-x86_64.log`.

| Check | Result |
|---|---|
| D8-compiled DEX (`r8 8.5.35`, `--min-api 26`) loads, dex2oat AOT, runs | ok, `java.vm.version 2.1.0` |
| Desugared lambdas, `CompletableFuture` | ok |
| `java.util.stream` `Collectors.toList/joining` **inside libcore** | **FAIL** `BootstrapMethodError` (un-desugared invokedynamic in core-oj) |
| HTTPS (HttpsURLConnection → wolfSSL JSSE), cert validation on | ok for example.com, google.com, api.github.com, dailymotion.com, letsencrypt.org, debian.org, kernel.org |
| HTTPS to **archive.org** | **FAIL** with Alpine's libwolfssl: `SSLHandshakeException: ASN parsing error (-140)`; archive.org sends the legacy "Go Daddy Class 2" root, serial 0. **Fixed 2026-10-19** by rebuilding wolfSSL with `WOLFSSL_ASN_ALLOW_0_SERIAL` (docs/runtime/tls.md) |
| Process exit | **abort (SIGABRT) in "Shutdown thread"** after `main` returns (exit-path defect) |

None of these was run on the TV yet; the TV needs the aarch64 (or armv7) packages.

## 3. CloudStream API and its dependencies (to be dexed, unmodified)

From `gradle/libs.versions.toml` at CloudStream `eb962b4…` (library module, commonMain/jvmCommonMain):

| Library | Version | Licence | Used by InternetArchive DEX |
|---|---|---|---|
| CloudStream `library` (JVM target) | `1.0.1` (module version) | GPL-3.0 | yes |
| Kotlin stdlib / kotlin-reflect | 2.4.0 | Apache-2.0 | stdlib yes |
| kotlinx-coroutines-core | 1.11.0 | Apache-2.0 | yes |
| jackson-module-kotlin (strictly) | 2.13.1 | Apache-2.0 | yes |
| NiceHttp (→ OkHttp, Okio) | 0.4.18 | MIT (NiceHttp) / Apache-2.0 | yes |
| Jsoup | 1.22.1 | MIT | yes |
| Rhino (strictly) | 1.8.1 | MPL-2.0 | no |
| kotlinx-serialization-json, datetime, io, atomicfu | 1.11.0, 0.8.0, 0.9.1, 0.33.0 | Apache-2.0 | no direct refs |
| ksoup, ktor-http, cryptography-kotlin, NewPipeExtractor | 0.2.6, 3.5.0, 0.6.0, v0.26.3 | Apache-2.0 / GPL-3.0 (NewPipe) | no direct refs |

These run on the JVM upstream (CloudStream's `jvm()` target) and are Android-compatible (CloudStream's
Android target uses them with minSdk 23). Required step: obtain or build the JVM artifacts, dex them with
D8 for the runtime's API level, and confirm every class the plugin touches resolves (Phase 1 test T2).

## 4. Build-time tools (not redistributed)

| Tool | Version | Licence | Use |
|---|---|---|---|
| D8/R8 (`com.android.tools:r8`) | 8.5.35, sha256 `4733945987ee0a840fafc34080b135259e01678412e07212b23f706334290294` | BSD-3-Clause | JVM bytecode → DEX for host/runtime code (never applied to plugins) |
| OpenJDK 17 (Alpine container) | apk `openjdk17-jdk` | GPL-2.0 + CE | compile harness Java |
| Docker `alpine:edge` | digest `sha256:020dfcbaaf4cc1078bf2d9c7ba31a8466e334061dcd2f248001d68f79e52c000` | MIT (Alpine) | host experiments |
