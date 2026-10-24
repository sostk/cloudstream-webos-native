# Legacy playback crash: exported video window

Issue: https://github.com/sostk/cloudstream-webos-native/issues/15

The tester's webOS 4.4.3 / firmware 05.50.70 / ARMv7 logs show HTTP working,
ART ready, providers loaded and nine playback links returned. The native app then
reports Starfish decoder limits and crashes before logging an exported window or
starting the playback controller. ART is a separate process and is not the process
recorded by this crash handler.

The crash record identifies app build ID
`3be8d79e0e67d88c2edccf10c1d8c823b311bb20`, SIGSEGV at address `0x4`,
PC `0x764ec464` and LR `0x7647b6c0`. Both addresses fall inside the bundled SDL
mapping beginning at `0x7640e000` (relative offsets `0xde464` and `0x6d6c0`).

The kit is pinned at `5b07b2df724e414b9efed9c46ed18df6e4f647b3` and builds SDL
`release-2.30.12-webos.6`. Its Starfish constructor calls
`SDL_webOSCreateExportedWindow(0)` after logging decoder limits. SDL allocates
`webos_foreign_table` only when its Wayland registry sees `wl_webos_foreign`,
but `WaylandWebOS_CreateExportedWindow` dereferences that table without checking it.
The table's windows pointer is at offset 4 on ARM32. This is consistent with the
recorded null access. CI analysis run 39885245483 downloaded artifact 12456526352
from run 39847551314 and confirmed the executable's build ID matches the crash record.
SDL's executable LOAD segment has virtual address zero. The fault is exactly:

```text
de460: e596208c  ldr r2, [r6, #140]  ; data->webos_foreign_table
de464: e5923004  ldr r3, [r2, #4]    ; table->windows; NULL + 4
```

The preceding allocation and list code match the pinned SDL create function.
This confirms a missing table, rather than an ART, extractor or curl crash.

## Legacy-only guard

The app's CMake registers an extra SDL ExternalProject step only when both
`CSWEB_LEGACY` and `WNK_WEBOS` are enabled. After the kit's existing SDL patch and
before SDL configure, `tools/patch-legacy-sdl.py` checks for the pinned source anchor
and adds a missing-protocol/table guard. It changes SDL's generated source checkout;
no kit source, kit pin, ART package, runtime inputs or main SDL configuration changes.
The hook is deferred to the kit's root CMake directory, where SDL's ExternalProject
stamp commands are defined. Registering it directly from the app subdirectory
failed at generation in run 39885909950; that build-hook error is corrected.

Missing protocol state returns NULL with an SDL error before allocating or
touching the exported-window list. This protects window creation; older firmware
also needs a display-control fallback to connect decoded video to the panel.

## Legacy-only ACB display control

With `CSWEB_LEGACY=ON` and `WNK_WEBOS=ON`, the app builds a separate
`LegacyStarfishBackend`. `tools/make-legacy-starfish.py` reads the pinned kit's
backend/header, verifies both SHA-256 hashes, and writes the adapted sources into
the app's build directory. It never rewrites kit files. A kit update that changes
these inputs requires reviewing the integration; generation fails closed.
The main and host app builds continue to use the original kit backend. No ART,
runtime inputs, kit pin or main-package dependencies change.

The legacy backend first tries the guarded SDL exported-window API. A returned
window is used exactly as before, including `option.windowId`, with no ACB lookup.
If the SDL entry points are absent or creation returns NULL, it dynamically loads
the TV's `libAcbAPI.so.1`. The library is neither bundled nor added to DT_NEEDED.
Required symbols and Starfish's `getMediaID()` are checked before playback. Missing
symbols, initialization failures and media-binding failures produce a playback
error rather than continuing with an unbound video plane.

The bridge uses the NDK `AcbAPI.h` declarations for stable functions. CI's older
NDK declares video metadata with two arguments and omits audio metadata, so those
two dynamic function pointers explicitly use the modern three-argument ABI from
the published header and reference players, including real `long *taskId` output
arguments. Tests compile with both declaration sets. It initializes an
MSE player for `org.csweb.tv.legacy`, selects the main sink, binds Starfish's media
ID on load completion, forwards video/audio-info callbacks verbatim, sets the
source/destination rectangle, and mirrors loaded/playing/paused/unloaded states.
Audio metadata, custom display rectangles and finalize are optional symbols;
setDisplayWindow and destroy provide the required baseline.

Firmware callbacks only queue data. The playback thread applies ACB work after
Starfish `Load()` has returned, so synchronous callbacks cannot bind a half-loaded
object or call back into Starfish from inside its Load call. The UI thread queues
display-area changes. Each playback has a new ACB handle, released after Starfish
unload; the kit's epoch registry drops events from retired playback instances.

Reference implementations and ABI declarations reviewed:

- [Kodi optional export and ACB load path](https://github.com/xbmc/xbmc/blob/a097d6978028aef8840b23695a57c606697f156a/xbmc/cores/VideoPlayer/MediaPipelineWebOS.cpp)
  and [display placement](https://github.com/xbmc/xbmc/blob/a097d6978028aef8840b23695a57c606697f156a/xbmc/cores/VideoPlayer/VideoRenderers/HwDecRender/RendererStarfish.cpp).
- [Moonlight SS4S ACB resource](https://github.com/mariotaku/ss4s/blob/dfba721b85420ccabf91dac65be73984bf1865f9/modules/webos/smp/src/smp_resource_acb.c).
- [PlxNative display-path resolution and ACB ABI notes](https://github.com/GLinnik21/plx-native/blob/6ca6fbfd6a0ae8eb312f9cc497828825578f8f1d/src/starfish.c).
- [webOS SDK-compatible ACB declarations](https://github.com/webosbrew/webos-userland/blob/00a5f8721e15d5c10030b99b2f3e73e0c527bcce/include/appswitching-control-block/AcbAPI.h)
  and [Starfish declarations/event numbers](https://github.com/webosbrew/webos-userland/blob/00a5f8721e15d5c10030b99b2f3e73e0c527bcce/include/starfish-media-pipeline/StarfishMediaAPIs.h).

## Validation and next TV test

The host regression test compiles the original pinned SDL create function with
a minimal compositor environment. It reproduces SIGSEGV when the table is missing.
The guarded function returns an error for missing driver data, protocol, table or
both, and still creates an exported window when both are present. The test also
checks fail-closed anchors and repeat application.
An additional CMake integration test builds a minimal root ExternalProject with
the actual app hook, checks the guard runs before configure, and verifies the
main and host modes leave SDL unchanged. It runs where CMake is available (CI);
this local environment lacks CMake, so only that integration test is skipped locally.

`tests/test_legacy_acb.py` compiles the generated backend against fake firmware
libraries. It covers early/delayed callbacks, media and metadata binding, display
placement, pause/resume, repeated playback, stale callbacks, missing libraries and
symbols, initialization/load/binding failures, optional functions, and absence of
link-time ACB/playerAPIs dependencies. The exported-window case works without ACB
or getMediaID. A separate CMake test checks main/host opt-out and builds the legacy
target where CMake is available.

After installing the updated legacy package, test video, audio, aspect ratio,
pause/resume, seeking, returning to browsing and playing a second video. Collect
events.log, runtime.log and a fresh crash.log if generated. On the older firmware,
expect `[legacy-acb] display path ACB: initialized`, then `bind`, `state LOADED`,
`video metadata`, `display`, and playing state messages, followed by Starfish's
`first frame presented`. Return codes and callbacks are diagnostic evidence;
only an actual picture and audio confirm playback. ACB presence alone does not
prove the developer-mode jail grants media access. No root or firmware changes
are part of this fix. Playback on the tester's TV remains unverified.

The runtime log also contains intermittent Bilibili hostname-verification errors,
while later API requests return links. Those errors are separate from the native
SDL crash. Do not disable TLS verification to investigate them. Plugin diagnostics
may include signed URLs; redact them before sharing logs publicly.
