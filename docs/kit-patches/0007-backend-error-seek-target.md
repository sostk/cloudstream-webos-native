# Kit patch 0007: backend errors fail playback; stuck-call warning; seek target as position

> Merged upstream as https://github.com/Native-Webos/webos-native-app-kit/pull/12 (merge `7fe49ab`, 2026-10-24); since kit `7fe49ab` the project no longer carries this patch. Kept as the record of why and how it was tested; the patch file is in git history.

Applies to Native App Kit `5b07b2df724e414b9efed9c46ed18df6e4f647b3`.
Patch (in git history): `0007-backend-error-seek-target.patch` (`git am`; media/src/controller.cpp,
media/include/wnk/media/controller.hpp, tests/unit/test_media.cpp).

## Why

Issue [#15](https://github.com/sostk/cloudstream-webos-native/issues/15), second report (legacy
build, webOS 4.4.3, firmware 05.50.70, ARMv7, legacy ACB display path): "after switching a few
videos ... the screen is just black with the player controller". Its `events.log` shows:

1. Video 4 (playback thread 5139, ACB id 4) presented its first frame at 155.8 s and nothing after:
   no further `sink:` summaries, no ACB play callbacks. Back at 166.5 s; `stop()` gave up after
   2 s ("playback thread did not stop within 2000 ms; detached"). The thread never logged again
   and that pipeline was **never unloaded** (no `Unload`/`destroyed id=4` up to 545 s).
2. Every later Load took 20 s and Starfish answered event 18 `num=601 Resource Allocation Error`.
   The backend turned that into `BackendEvent::Error`, but the controller ignored it and went
   Ready -> Playing: controls over a black picture. Those later threads also missed the 2 s
   stop deadline (blocked in the 20 s Load), then unloaded about 30 s later.

The log cannot say which call video 4's thread was blocked in (Starfish Feed, an ACB call, a source
read, or the reader join), so the root cause of the first stall is still open.

Issue [#18](https://github.com/sostk/cloudstream-webos-native/issues/18): "while seeking with
forward/rewind buttons, the timestamp does not update simultaneously". The position comes from
Starfish's presented PTS, which after a seek still shows the old content through the source seek,
priming and the backlog the splice plays first.

## What (one commit each)

1. **Name the blocking call when the playback thread misses its stop deadline.** A `Busy` marker
   around every backend call, source open/seek and the reader park/join. The warning becomes e.g.
   `playback thread did not stop within 2000 ms (in backend feed for 12940 ms); detached`.
2. **Fail playback when the backend reports an error.** `BackendEvent::Error` queues a Fail command:
   the playback thread unloads and ends in Error with the backend's reason, once. The app already
   handles Error (toast, next link), so instead of a black screen the viewer sees "This link failed"
   and the next source is tried.
3. **Report a seek's target as the position until it reaches the screen.** `seek()` records the
   target; `snapshot()` reports it as the position until a packet of that seek went in or, with a
   splicing backend, until `splice_settled()`. Only the newest seek clears it. The app's time label
   and bar follow the remote at once without app changes.

## Tests

`tests/unit/test_media.cpp`: the stuck-load test checks the warning names "backend load"; an error
event ends in Error with one unload; a repeated event fails once; a seek target shows at once and,
with a splicing backend, until settled; of two seeks the newer one shows; a seek before open leaves
nothing behind. Each new test fails on `5b07b2d`. Host suite `tools/test.sh host-debug` 111/111
(including the app's tests); `unit.media` under `host-tsan` passes. `tools/build-webos.sh` builds and
`check-package` passes. Not run on a TV yet.

## Still open

- Why video 4 stalled after one frame. With 1 in place, the next tester log names the call.
- A permanently stuck thread still keeps the decoder; 2 makes later links fail visibly instead of
  black, but they cannot play until the app restarts.
