# Kit patches

The project builds on Native App Kit `5b07b2df724e414b9efed9c46ed18df6e4f647b3` (upstream `main`,
2026-10-21) **without local changes**. `external/webos-native-app-kit` is a plain checkout of that commit,
made or checked by `tools/fetch-kit.sh` (which holds the pinned commit; CI uses it too):

    tools/fetch-kit.sh

Until then the project carried six patches on `e10f35ecd6e27987cc866b28caea6be66a023fcc`. All six were
offered upstream on 2026-10-20 as separate pull requests (one commit each, with extra tests and guide
updates, checked with `tools/test.sh` host-debug and host-asan; 0006 also under ThreadSanitizer) and
were merged:

| Note | Upstream pull request | Merge commit |
|---|---|---|
| [0001 extra app dirs](0001-extra-app-dirs.md) | https://github.com/Native-Webos/webos-native-app-kit/pull/1 | `bb22abc` |
| [0002 private runtimes](0002-private-runtimes.md) | https://github.com/Native-Webos/webos-native-app-kit/pull/2 | `0410e9c` |
| [0003 redact URL queries](0003-redact-url-queries.md) | https://github.com/Native-Webos/webos-native-app-kit/pull/3 | `68cc31f` |
| [0004 label ellipsis cost](0004-label-ellipsis-cost.md) | https://github.com/Native-Webos/webos-native-app-kit/pull/4 | `2dc11f7` |
| [0005 DASH periods, WebM](0005-dash-periods-webm.md) | https://github.com/Native-Webos/webos-native-app-kit/pull/5 | `febd6fa` |
| [0006 HTTP cookies](0006-http-cookies.md) | https://github.com/Native-Webos/webos-native-app-kit/pull/6 | `5f91c56` |

The notes stay as the record of why each change was needed and how it was tested on the TV. The
`*.patch` files were removed when the project moved to `5f91c56`; they are in this repository's
history. The upstream versions differ slightly from them: 0001 lives in `cmake/ExtraApps.cmake` with
path checks, 0002's CMake comment is more precise, and every one has more tests.

A future kit change goes here again as `NNNN-name.md` + `NNNN-name.patch` against the commit above,
and upstream as a pull request.

## Later upstream changes from this project

| Change | Pull request | Merge commit |
|---|---|---|
| `HttpClient::send` and `auth::Session::send` are `[[nodiscard]]` (a refused request never calls back; the app's plugin install waited for ever on a URL with a space, fixed in the app in `0d8f40e`) | https://github.com/Native-Webos/webos-native-app-kit/pull/7 | `74f339a` |
| `e2e.shows.back_cancels_details` accepts any cancel before the slow reply (it failed on CI runners at 1.0-1.3 s) | https://github.com/Native-Webos/webos-native-app-kit/pull/8 | `085b06d` |
| Live DASH follows new segments by the clock between MPD updates and keeps every rendition of the bitrate ladder listed (stalls with dropped frames, and switches landing up to 30 s in the past, on the LG C1; test stream: `tools/abr-test/`, project PR #7) | https://github.com/Native-Webos/webos-native-app-kit/pull/9 | `937da4f` |
| The controller reads the source on its own thread, so a waiting read no longer starves the decoder; it pauses the backend and the clock to buffer when a stream runs dry instead of letting Starfish drop every late frame; live DASH joins behind the edge of every rendition by its longest segments (stutter and dropped frames on live DASH, LG C1) | https://github.com/Native-Webos/webos-native-app-kit/pull/10 | `5b07b2d` |

## Pending

| Note | State |
|---|---|
| [0007 backend errors, stuck-call warning, seek target](0007-backend-error-seek-target.md) | patch here and on the local kit branch `fix/backend-error-seek-target`; not yet offered upstream, the pin is unchanged |
