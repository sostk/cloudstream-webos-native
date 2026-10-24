# Plugin runtime: third-party notices

The plugin runtime (`runtime-<arch>/` in the package) is a relocatable copy of Alpine Linux's
`art_standalone` (Android's ART, run outside Android) with the libraries it needs, plus parts
of CloudStream and this project's own code. Each part keeps its own licence; none is modified
except where stated. The app itself and this project's code are GPL-3.0-or-later (`../LICENSE`).

## Alpine Linux packages

This inventory describes the ARMv7 files actually shipped by this runtime, as recorded in
`MANIFEST.json` and `packages.tsv`. ARMv7 Alpine inputs are not yet pinned in a lock file.
ART is rebuilt from the listed Alpine recipe against wolfSSL 5.9.4, without ART source
patches, and signed locally by CI (`runtime/build-art-armv7.sh`). Other Alpine files are
copied from signed upstream packages except the rebuilt wolfSSL library described below.
The licence column is Alpine's (SPDX). Recipes identify the exact aports source commits;
licence texts from those sources are in `texts/<origin>/`.

| Origin | Version | Licence | Shipped | Recipe (aports) | Upstream |
|---|---|---|---|---|---|
| art_standalone | 0_git20251009-r3 | Apache-2.0 | 33 files | [8ac8f5cb2e](https://gitlab.alpinelinux.org/alpine/aports/-/commit/8ac8f5cb2e5c2206a93de5498f6368190625b3cc) | https://gitlab.com/android_translation_layer/art_standalone |
| bionic_translation | 0_git20260317-r0 | Apache-2.0 | 1 file | [136126b9dc](https://gitlab.alpinelinux.org/alpine/aports/-/commit/136126b9dc634e1f23a6b46e0b83ce5d721079ac) | https://gitlab.com/android_translation_layer/bionic_translation |
| ca-certificates | 20261020-r0 | MPL-2.0 AND MIT | trust/cacerts (Mozilla's CA list, converted to a Java trust store) | [1c5642bff0](https://gitlab.alpinelinux.org/alpine/aports/-/commit/1c5642bff0a86736a8ae3a78f13977867add64ff) | https://www.mozilla.org/en-US/about/governance/policies/security-group/certs/ |
| elfutils | 0.195-r0 | GPL-3.0-or-later AND ( GPL-2.0-or-later OR LGPL-3.0-or-later ) | 1 file | [8f13294206](https://gitlab.alpinelinux.org/alpine/aports/-/commit/8f13294206888f3dd44ed74a8eb3dd9934499e81) | https://sourceware.org/elfutils/ |
| expat | 2.9.0-r0 | MIT | 1 file | [46440e4654](https://gitlab.alpinelinux.org/alpine/aports/-/commit/46440e4654dc1e4e7d3a69b8a0a9e22fdeda4a88) | https://libexpat.github.io/ |
| gcc | 15.2.0-r9 | GPL-2.0-or-later AND LGPL-2.1-or-later | 2 files | [21530a8b7b](https://gitlab.alpinelinux.org/alpine/aports/-/commit/21530a8b7b00b34c648a6b3744d461f32203a885) | https://gcc.gnu.org |
| icu | 78.1-r0 | ICU | 4 files | [6755fdb214](https://gitlab.alpinelinux.org/alpine/aports/-/commit/6755fdb21483ba250d374c3244f0f48bc63447e2) | https://icu.unicode.org/ |
| libbsd | 0.12.2-r0 | BSD-3-Clause | 1 file | [02365e78d6](https://gitlab.alpinelinux.org/alpine/aports/-/commit/02365e78d654a7beb23fd35d7d69f4a649009ede) | https://libbsd.freedesktop.org/ |
| libmd | 1.3.0-r0 | BSD-3-Clause AND BSD-2-Clause AND ISC AND Beerware AND Public Domain | 1 file | [b9f624c710](https://gitlab.alpinelinux.org/alpine/aports/-/commit/b9f624c71024df38a4cd84bc84b51fc3498f15a7) | https://www.hadrons.org/software/libmd/ |
| libucontext | 1.5.2-r0 | ISC | 1 file | [91e8cfebbf](https://gitlab.alpinelinux.org/alpine/aports/-/commit/91e8cfebbfc032ebee2ccff6c3e22114c3e495c9) | https://github.com/kaniini/libucontext |
| libunwind | 1.8.3-r0 | MIT | 1 file | [1789f1e1e7](https://gitlab.alpinelinux.org/alpine/aports/-/commit/1789f1e1e73cb18ef6050b7cbb544fc5cfcd65be) | https://www.nongnu.org/libunwind/ |
| lz4 | 1.10.0-r1 | BSD-2-Clause AND GPL-2.0-or-later | 1 file | [1f16962f34](https://gitlab.alpinelinux.org/alpine/aports/-/commit/1f16962f34234a77fab0f4651459c4381b4a0cd6) | https://github.com/lz4/lz4 |
| musl | 1.2.6-r6 | MIT | 2 files | [6a9a5fe7d8](https://gitlab.alpinelinux.org/alpine/aports/-/commit/6a9a5fe7d82a10358a59391e4117cc9aeefcb690) | https://musl.libc.org/ |
| openssl | 3.5.9-r0 | Apache-2.0 | 1 file | [5ebc3527f9](https://gitlab.alpinelinux.org/alpine/aports/-/commit/5ebc3527f96ca84caf382abd7f37d4009110e57d) | https://www.openssl.org/ |
| vixl | 8.0.0-r0 | BSD-3-Clause | 1 file | [0285ee09fc](https://gitlab.alpinelinux.org/alpine/aports/-/commit/0285ee09fc8770308350f13af231f66f970db67f) | https://github.com/Linaro/vixl |
| wolfssl | 5.9.4-r0 | GPL-3.0-only | usr/lib/libwolfssl.so.46 (rebuilt from the same wolfSSL release, runtime/native/build-wolfssl.sh) | [0460af5724](https://gitlab.alpinelinux.org/alpine/aports/-/commit/0460af5724cb447aa15601a37c5a87fd5c9616f5) | https://www.wolfssl.com/ |
| xz | 5.8.4-r0 | GPL-2.0-or-later AND 0BSD AND Public-Domain AND LGPL-2.1-or-later | 1 file | [950af2f9ad](https://gitlab.alpinelinux.org/alpine/aports/-/commit/950af2f9ad0e4518d167e8ce0b41e7be0a7a730e) | https://tukaani.org/xz/ |
| zlib | 1.3.2-r1 | Zlib | 1 file | [ed7d11b8c5](https://gitlab.alpinelinux.org/alpine/aports/-/commit/ed7d11b8c5de5f1744b2cf762c160c2385f28212) | https://zlib.net/ |
| zstd | 1.5.7-r2 | BSD-3-Clause OR GPL-2.0-or-later | 1 file | [3c6e2ee2b1](https://gitlab.alpinelinux.org/alpine/aports/-/commit/3c6e2ee2b16f403d53eab39c4426eb61f003c322) | https://facebook.github.io/zstd/ |

`art_standalone`'s Java class library (`usr/lib/java/dex/art/core-oj-hostdex.jar`) derives from
OpenJDK and is GPL-2.0-only WITH Classpath-exception-2.0 (Android's libcore), whatever the package's
Apache-2.0 label says; its notices are among `texts/art_standalone/`.

## Built by this project

| Part | Source | Licence |
|---|---|---|
| `usr/lib/libwolfssl.so.46` | wolfSSL 5.9.4 release tarball (SHA-512 in `runtime/native/build-wolfssl.sh`), built with `-DWOLFSSL_ASN_ALLOW_0_SERIAL` and otherwise Alpine's options | GPL-3.0-only (`texts/wolfssl/`) |
| `usr/lib/libEGL.so.1`, `bin/dex2oat` | `runtime/native/` of this project | GPL-3.0-or-later |
| `usr/lib/java/dex/art/natives/libcsweb_legacy_math.so` | `runtime/native/legacy_math.c` of this project | GPL-3.0-or-later |
| `csweb/csweb-host.dex` | `runtime/android-compat/`, `runtime/csloader/` of this project | GPL-3.0-or-later |
| `boot/`, `csweb/oat/` | generated by ART's own dex2oat from the files above (`runtime/make-aot.sh`) | as their inputs |

## CloudStream

`csweb/cloudstream.jar` holds the class files (DEX) and Java resources of CloudStream's
pre-release APK unchanged (SHA-256 in `runtime/inputs.lock`), without Android-only parts.
CloudStream is GPL-3.0: https://github.com/recloudstream/cloudstream (source of the pinned build:
commit eb962b4fb7b1387b8de42ec58000cee0a01fe741, with CloudStream's own LICENSE).

The APK also contains the libraries CloudStream bundles (Kotlin, kotlinx, AndroidX, OkHttp,
Jackson, jsoup, NewPipe Extractor, Rhino and others), each under its own licence. The APK carries
licence files only for AndroidX and jsoup (kept in the jar under `META-INF/`); the full list of
libraries and versions is in CloudStream's build files at that commit (`gradle/libs.versions.toml`,
`app/build.gradle.kts`, `library/build.gradle.kts`).
