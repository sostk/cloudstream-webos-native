# Runtime service protocol

Status 2026-10-19. Implemented by `runtime/csloader/src/org/csweb/loader/Service.java`.

## Why

A fresh ART process per operation costs about 3 s of runtime start on the TV before the plugin does anything
(imageless boot, verify-only compilation), plus a plugin load. The service starts ART once and keeps
plugins loaded, as CloudStream does in its own process. Each request then pays only the plugin's own
work.

## Start

```
<runtime>/lib/ld-musl-<arch>.so.1 --library-path … <runtime>/usr/bin/dalvikvm -Xmx256m … \
    -cp <runtime>/csweb/csweb-host.dex:<runtime>/csweb/cloudstream.jar \
    org.csweb.loader.Main --serve 1 --data <writable dir> [--workers 4]
```

The first line on stdout is `{"event":"ready","protocol":1}`. Plugin and runtime diagnostics go to stderr.

## Requests (stdin, one JSON object per line)

| op | fields | result events |
|---|---|---|
| `load` | `plugin` (path), `sha256` (required) | `plugin`, `resources`, `compat`, `settings`, `provider`…, `loaded` (`providers` = count; 0 for settings-only plugins) |
| `unload` | `plugin` | as CloudStream's `unloadPlugin`: `beforeUnload`, providers and extractors removed |
| `providers` | | one `provider` event per loaded provider (with its `plugin`, `supportedTypes`, `mainPage` section names; plus, read by reflection and `null` when the API lacks them: `sequentialMainPage`, `sequentialMainPageDelay`, `sequentialMainPageScrollDelay`, `mainPageTimeoutMs`, `usesWebView`, `hasDownloadSupport`, `instantLinkLoading`) |
| `search` | `provider` and/or `plugin`, `query`, [`limit`] | `result`…, `count` / `empty` |
| `mainpage` | `provider`, [`section` index into the provider's `mainPage` list], [`page`], [`limit`] | `section` (`name`, `index`, `page`, `horizontal`, `hasNext`), `result`… |
| `details` | `provider`, `url`, [`limit`] | `details` (plain properties plus `backgroundPosterUrl`, `logoUrl`, `duration`, `score`, `tags`, `actors`, `recommendations`, `trailers`, `seasons`, `showStatus`), `episode`… (`dub` for anime episode lists) |
| `links` | `provider`, `url` (the data from details), [`withHeaders`] | `link`… (`linkType`, `quality`, `referer`; header values only with `withHeaders`, names otherwise), `subtitle`…, `links_returned` |
| `settings` | `plugin` | `dialog`… (see dialog-protocol.md) |
| `network` | `dns` (CloudStream's `dns_key` value: 0 system resolver, 1 Google, 2 Cloudflare, 4 AdGuard, 5 DNS.WATCH, 6 Quad9, 7 DNS.SB, 8 Canadian Shield) | `network` (`clients` changed); applied before the next request is read, so every later request (loads too) resolves through it. CloudStream's `initClient`: `app` and `insecureApp` get the DoH resolver from its `DohProviders.kt` |
| `cancel` | `target` (a request id) | the target ends with `error` (`Cancelled`) and `done ok:false` |
| `shutdown` | | waits for earlier requests, then exits |
| `answer` | `dialog`, plus the dialog answer fields | none; routed to the request showing that dialog |

Every request except `answer` needs a string `id`. Every event caused by a request carries that `id`, and
each request ends with exactly one `{"event":"done","id":…,"ok":…}`; nothing for that id follows it.
Operations name a `provider`, a `plugin` (its only provider; useful because a plugin's settings may
rename its provider), or both when two plugins register the same provider name. Optional
`timeout` (seconds, default 120; 600 for `settings`) cancels a request that runs too long (`Timeout`).
End of input acts as `shutdown`, and dialogs still waiting for an answer end with `EndOfInput`.

## Ordering and concurrency

- `load`, `unload` and `shutdown` run one at a time, after every request received before them.
- All other requests run concurrently on a pool (`--workers`, default 4), each after every
  `load`/`unload` received before it. So "load, then search" in that order always works, and searches
  across providers run in parallel.
- Cancellation cancels the request's coroutine (CloudStream's HTTP calls are suspending) and interrupts its
  thread.
- An uncaught exception in a plugin's own thread is reported as an `error` event (`stage` =
  `thread:<name>`) and the service keeps running. On Android it would end the app.

## Security notes

- `sha256` is mandatory for `load`: the app must have checked the plugin it installed.
- Dialog answers are never logged; password fields are never described (dialog-protocol.md).
- Plugins share one data directory, as all plugins share CloudStream's app data on Android.

## Evidence

Host ART (x86_64): `evidence/runtime-host/runs/service-session.requests.jsonl` → `service-session.jsonl`:
three plugins loaded once; Dailymotion, LibriVox and conformance searches running concurrently; a wrong
hash refused; a 1 s timeout cancelled without later events; settings answered; unload and reload pick
up the new preferences; an unknown op reported. Whole session 16 s including the runtime start.

TV (LG OLED65C1PTB, webOS 6.5.3, probe 0.4.0, ipk sha256 `e5762cf8…9bf0`):
`evidence/device/20261019-111911-runtimeprobe-0.4/` ("service session"). One process served 14 requests
in 11.0 s wall time, peak RSS 125 MiB:

| Request | Time on TV |
|---|---|
| first load (conformance; includes first use of the CloudStream classes) | 3.3 s |
| later loads (Dailymotion, LibriVox, conformance reload) | 0.11-0.22 s |
| searches Dailymotion ×2, LibriVox, concurrently | 2.5-3.6 s (network) |
| LibriVox details (37 chapters) / links | 2.9 s / 0.56 s |
| conformance search (local) / settings dialog answered | 0.06 s / 0.02 s |

The same operations as separate processes take 6.7-10.3 s each (rows above the session in that log).
