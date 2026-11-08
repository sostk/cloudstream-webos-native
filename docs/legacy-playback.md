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

## ACB display control (kit)

Until 2026-11-09 the legacy package built its own `LegacyStarfishBackend`, generated at configure
time from the pinned kit's Starfish backend by `tools/make-legacy-starfish.py`, plus
`src/player/legacy_acb.cpp` (both in git history). Kit `312f365` does the same work itself
(`ffb2c20`, "Play through Starfish on webOS 3.x and 4.x with an ACB video plane"): without an
exported window it opens an ACB of type MSE from a dlopened `libAcbAPI.so.1`, binds the pipeline's
media id at load completion, mirrors the play states, forwards the video/audio-info envelopes and
places the picture with `setCustomDisplayWindow`. The release in `os_info.json` picks which plane is
tried first. The app now uses the kit's `StarfishBackend` in both packages; the legacy package only
sets `StarfishOptions::app_id` to `org.csweb.tv.legacy`.

Reference implementations: [Kodi MediaPipelineWebOS](https://github.com/xbmc/xbmc/blob/a097d6978028aef8840b23695a57c606697f156a/xbmc/cores/VideoPlayer/MediaPipelineWebOS.cpp),
[Moonlight SS4S ACB resource](https://github.com/mariotaku/ss4s/blob/dfba721b85420ccabf91dac65be73984bf1865f9/modules/webos/smp/src/smp_resource_acb.c),
[PlxNative](https://github.com/GLinnik21/plx-native/blob/6ca6fbfd6a0ae8eb312f9cc497828825578f8f1d/src/starfish.c).

## Third report: black picture after a few videos (release 0.6.0)

trmdi's log of 2026-10-25 (webOS 4.4.3, firmware 05.50.70, ARMv7, app 0.6.0 on kit `7fe49ab`):

1. Videos 1-3 played and each `Unload` returned in 160-250 ms.
2. Video 4 (ACB id 4) presented one frame. Unlike videos 1-3, ACB never acknowledged the
   foreground/LOADED request (no `task=1 ... app=1 play=1` callback; a `task=1 event=-2` came 14 s
   after Back). On Back, the playback thread stopped inside Starfish's own `Unload`: the 0007 warning
   says `in backend unload`, and no `Unload ->` line or UnloadCompleted (type 23) ever followed.
   The decoder stayed with that pipeline.
3. Every later `Load` waited 20 s and got event 18 `num=601 Resource Allocation Error`. The 0007
   change failed playback on it, but only after the following `Unload`, which took 30 s, so the
   player showed controls over a black picture for 30 s before "This link failed".

With kit `312f365`: the `Unload` runs on a release worker with a 10 s deadline, so the playback
thread and Back no longer wait on it. The next load waits for that release until its own deadline
and then fails at once with `release_timeout: ...` instead of a 20 s Load and a 601. The player
treats that error as "the decoder is still held" (`player::decoder_still_held`): it does not mark
the link failed or try the other links (they would fail the same way), and says to close and reopen
the app, which ends the process holding the decoder. A 601 or other refusal before the first
picture is first retried on the fallback decoder (NDL, where the firmware has the v2 API).

Still open: why ACB did not acknowledge video 4 and `Unload` never returned. The next tester log
should show whether the kit's ACB sequence avoids it.

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

The ACB plane and the bounded release are tested in the kit (`unit.starfish`,
`unit.starfish_release` against a fake `libplayerAPIs.so.1` whose Unload can hang).

After installing the updated legacy package, test video, audio, aspect ratio,
pause/resume, seeking, returning to browsing and playing a second video. Collect
events.log, runtime.log and a fresh crash.log if generated. On the older firmware,
expect the kit's Starfish ACB messages (initialised, media id bound, LOADED, video info,
display window, play states), followed by `first frame presented`; when switching videos, each
`Unload` should return well within the 10 s release deadline. Return codes and callbacks are diagnostic evidence;
only an actual picture and audio confirm playback. ACB presence alone does not
prove the developer-mode jail grants media access. No root or firmware changes
are part of this fix. Playback on the tester's TV remains unverified.

The runtime log also contains intermittent Bilibili hostname-verification errors,
while later API requests return links. Those errors are separate from the native
SDL crash. Do not disable TLS verification to investigate them. Plugin diagnostics
may include signed URLs; redact them before sharing logs publicly.
