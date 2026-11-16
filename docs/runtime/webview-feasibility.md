# A WebView stand-in from the TV's web engine (to do)

Status 2026-11-09: feasibility partly tested, work paused. Device: LG OLED65C1PTB, webOS 6.5.3,
firmware 03.53.45, developer mode (rooted).

## Why

Some plugins need Android's `android.webkit.WebView`, which the runtime does not have:

- **Cloudflare challenges:** CloudStream's `CloudflareKiller` / `WebViewResolver` pass Cloudflare's
  managed challenge in a hidden WebView. CineVood's `cinevood.net` title pages answer every
  non-browser client with `403`, `cf-mitigated: challenge`; `curl` gets the same.
- **Stream sniffers:** CNCVerse's DaddyLive (`com.cncverse.daddylive.StreamSnifferKt`) and NTV
  (`com.cncverse.ntv.StreamSnifferKt`) load an embed page in a WebView and wait for a request matching
  `\.m3u8` (NTV also `/manifest\.ts`). Today they fail at `android.os.Looper`; the runtime reports
  "needs Android's main thread".

## What was tested

| Question | Result |
|---|---|
| Engine version | WAM and `/usr/lib/libcbe.so` are both Chromium 79.0.3945.79. webOS 6 has no newer engine. |
| Control from code | Chrome DevTools listens on TV port 9998 (developer mode only). `Target.createTarget` is refused ("Not supported"); only pages of installed web apps can be driven. |
| Hidden page | A web app with `"noWindow": true` still has a page DevTools can drive; nothing is shown. Its `appinfo.json` needs an `icon`, or the app manager does not register it. |
| Request sniffing | Works: `Network.requestWillBeSent` saw all 55 requests of `one.1cinevood.beer`, scripts included. |
| Cloudflare challenge | **Fails.** The challenge script throws `SyntaxError: Unexpected token '.'`: optional chaining needs Chrome 80+. No `cf_clearance` after 46 s. |

## Workarounds for users now

- **CineVood:** use the plugin's other provider, CineVood Mirror (`one.1cinevood.beer`). Its title
  pages are not challenged.
- **DNS blocks:** DaddyLive's sites are blocked by the ISP's DNS. Settings > Network > DNS over HTTPS
  fixes the plugin's own requests. Playback and images use the kit's libcurl; for those, set the TV's
  own DNS server (for example 1.1.1.1) in the webOS network settings.

## Next steps

1. **Check the pages:** do the sniffer plugins' embed pages run on Chrome 79? Load them in the hidden
   test page and look for script syntax errors and `.m3u8` requests. Candidate hosts: NTV's
   `ntv.cx`/`ntv.st`, `embedsports.me`, `livelive24.st`; DaddyLive's `thedaddy.*` embeds. DaddyLive's
   channel list is built by JavaScript, so its embed URLs are easiest to get from the plugin itself.
   The TV's system DNS blocks `thedaddy.*`, so the TV needs another DNS server for this test.
2. **If they work, design the stand-in:**
   - a hidden companion web app (`noWindow`) in the package;
   - in the runtime, `android.webkit.WebView` / `WebViewClient.shouldInterceptRequest` /
     `CookieManager` backed by DevTools commands to that page (navigate, request events, cookies);
   - plus `android.os.Looper`/`Handler` for `Dispatchers.Main`.
   It works only on TVs in developer mode, where DevTools is open, and it can't pass Cloudflare on
   webOS 6.
3. **Test tooling:** the probes were scratch scripts. If this goes ahead, keep them under
   `tools/` with tests.
