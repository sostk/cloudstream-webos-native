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

Missing protocol state now returns NULL with an SDL error before allocating or
touching the exported-window list. The kit's existing NULL-window handling logs a
warning and constructs the Starfish payload without a windowId. The guard is not a
claim that the older firmware can display video without an exported window.
That fallback still requires a TV test; any subsequent failure must be read from
the new logs.

Kodi's MediaPipelineWebOS checks SupportsExportedWindow and, when false, uses
AcbAPI_initialize; RendererStarfish uses AcbAPI_setCustomDisplayWindow on that path.
That is evidence of a separate legacy display-control mechanism, not proof that
omitting windowId alone suffices. If the crash guard exposes a black video plane,
inspect the TV's ACB API before implementing a bridge; do not guess its ABI or
claim full playback from the guard tests.

## Validation and next TV test

The host regression test compiles the original pinned SDL create function with
a minimal compositor environment. It reproduces SIGSEGV when the table is missing.
The guarded function returns an error for missing driver data, protocol, table or
both, and still creates an exported window when both are present. The test also
checks fail-closed anchors and repeat application.

After installing the updated legacy package, test one video and collect events.log,
runtime.log and a fresh crash.log if generated. Check for the exported-window warning
and subsequent player load/state messages. No root, SIGQUIT, altered extension,
certificate-verification bypass or change to the aarch64 package is needed.

The runtime log also contains intermittent Bilibili hostname-verification errors,
while later API requests return links. Those errors are separate from the native
SDL crash. Do not disable TLS verification to investigate them. Plugin diagnostics
may include signed URLs; redact them before sharing logs publicly.
