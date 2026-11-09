# Experiment: an app carrying its own current Chromium

Question: can CSWeb TV ship its own up-to-date browser engine, to stand in for Android's WebView?
That would cover Cloudflare challenges and the stream sniffers (DaddyLive, NTV). The TV's own engine is
Chromium 79 and fails Cloudflare's challenge (`docs/runtime/webview-feasibility.md`).

Approach: the same way the plugin runtime ships ART, a relocatable Alpine edge root started through
its own musl loader, with Alpine's `chromium` for aarch64, run headless and driven over DevTools
from the runtime. Not part of the app; nothing here is built by CI.

Device: LG OLED65C1PTB, webOS 6.5.3, firmware 03.53.45, kernel 4.4.84, aarch64, 3 GB RAM.

## Results so far (2026-11-09)

### Size: the deciding problem

`make-rootfs.sh` (Docker, `alpine:edge`) installs `chromium` for aarch64:

| | Size |
|---|---|
| Alpine edge `chromium-156.0.8078.79-r0`, with every dependency (175 packages) | 722 MiB |
| of which: chromium 297, llvm23-libs 169 (Mesa's llvmpipe), mesa 36, icu-data-full 31 | |
| only what the binary loads (its DT_NEEDED closure: 138 files) | 358 MiB |
| of which: the `chromium` binary | 259 MiB |
| closure + resources (paks, `icudtl.dat`, V8 snapshots), estimated | about 375 MiB |

On the TV, `/media/developer` (where apps install) has **343 MiB free**. CSWeb TV already takes
226 MiB. A package carrying this Chromium cannot be installed on this TV, even trimmed to the
load closure. The closure keeps FFmpeg, GTK and GnuTLS because Alpine's build links them; dropping
them needs a custom Chromium build (`headless_shell` with media and UI off), which is far smaller but
a large build to maintain.

### Memory

`free -m` on the TV with CSWeb TV installed: 281 MiB available, swap full. Headless Chromium needs on
the order of 150–300 MiB for a simple page. Running it next to the plugin runtime is risky on this
model.

## Not yet tested

1. **Running on the TV:** does Chromium 156 start on kernel 4.4 through the bundled musl loader
   (`--headless=new --no-sandbox --disable-gpu --remote-debugging-port`)? It would run from the USB
   drive (FAT, so files copied without symlinks).
2. **Cloudflare:** if it runs, does it pass the challenge (`cf_clearance`) that Chromium 79 fails?
   Script: `docs/runtime/webview-feasibility.md`'s probe, pointed at the bundled browser.
3. **Smaller builds:** Chromium's `headless_shell` or the `chrome-headless-shell` builds are
   x86_64-only from Google; an aarch64 musl build would have to be made here.

## Existing community work (searched 2026-11-09)

- **[An4kii/webos-chromium](https://github.com/An4kii/webos-chromium)**: experimental Chromium port for
  rooted webOS 6.x TVs (our TV's generation: kernel 4.4.84, 32-bit userspace, Mali-G51).
  - **Already measured on such a TV:** an ungoogled-chromium **151** AArch64 binary ran with its own
    AArch64 loader and libraries. That is the approach here, so a current Chromium does run on webOS 6's
    kernel.
  - **What failed for them:** only the on-screen window (`No Wayland shell found`: LG's compositor offers
    `wl_shell`/`wl_webos_shell`, not `xdg-shell`). A headless browser for CSWeb TV needs no window.
  - **Their main path:** LG's webOS OSE Chromium **120** fork, built for ARMv7 (`browser_shell_webos` +
    `libcbe.so`, linked on an 80-CPU cluster). Not yet run on a TV, and no binaries are published
    (artifacts are on their cluster). Last push 2026-09-07, no release.
- **[z3d6380/webos-libcbe-patch](https://github.com/z3d6380/webos-libcbe-patch)**: bind-mounts a patched
  `libcbe.so` (a Brotli fix, same engine version) on webOS 11 sets. Not a newer engine; no builds
  published.
- **webosbrew/chromium84-temp**: a 2020 source dump of LG's Chromium 84, not a built engine.
- **openlgtv:** nothing on browsers or engines.

Nothing usable is published that gives a newer engine to apps (WAM) on webOS 6. For CSWeb TV, a
headless current Chromium carried by the app remains the path, and the measured blocker is size
(about 375 MiB). A dedicated `headless_shell` build (no media, UI or GTK) would be far smaller.
That is the next thing to estimate.
