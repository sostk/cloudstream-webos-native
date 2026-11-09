# Start-up performance

How long the app takes from launch until Home can ask its source for rows, what that time is spent
on, and what has been done about it. Measured on the LG OLED65C1PTB (webOS 6.5.3, firmware 03.53.45,
AArch64 runtime with AOT), app started with `ares-launch`, numbers from `events.log` (ms since start).

## Where the time went (2026-11-09, before)

Plugins installed: DaddyLive, Cricify (Home source), later NTV (Home source), DaddyLive, Sportzx.

| Step | ms |
|---|---|
| runtime ready | 960 |
| first plugin load (DaddyLive, not the Home source) | ~2000 |
| Cricify loaded with the application context, failing its Activity cast, then again with one | ~4000 + ~4000 |
| Home asks its source for rows | **10 530** |

The runtime loads plugins one at a time, in the order the app asks. Three things made Home wait:

1. **Load order.** Plugins were asked for in install order, so Home waited for plugins it does not use.
2. **Activity plugins loaded twice.** A plugin whose `load(Context)` casts to an Activity (Cricify,
   Sportzx, PlayFy) was loaded with the application context, failed, and was loaded again with a
   headless Activity. Their `load()` fetches configuration over the network, both times.
3. **Jackson on the critical path.** Of the first plugin's 1875 ms, 1613 ms were `manifest`: the first
   call to CloudStream's `MainAPIKt.getMapper()` builds Jackson and its Kotlin module, only to read the
   four fields of `manifest.json`. Later plugins took 2 ms for the same step.

## What changed

- **Load hints** (`apps/csweb-tv/src/plugins/load_hints.*`, key `csweb.loadhints.v1`): after each load
  the app remembers, per plugin build (SHA-256), the providers it registered and the Activity type it
  needed. At the next start the plugin behind the Home source is asked for first, and plugins that
  needed an Activity get it from the start (`"activity"` in the `load` request; the runtime checks the
  name is an Activity class in the plugin's class loader and reports `compat … remembered`). A new
  build of a plugin is loaded the normal way and learnt again.
- **Order kept.** Plugins are written to tmpfs and requested one after the other (the writes ran as
  separate worker tasks, so a later plugin could reach the runtime first).
- **Manifest without Jackson** (`runtime/csloader/.../Manifests.java`): `manifest.json` is read with the
  runtime's JSON parser when every field has CloudStream's type, else with the mapper as before. The
  mapper is built on a low-priority background thread when the runtime starts (64-bit runtimes only;
  the legacy ARMv7 runtime keeps its own strict reader from `tools/instrument-legacy-loader.py`).
- **Per-stage timing.** Every load reports `integrity`, `classloader`, `manifest`, `class`, `construct`,
  `resources` and `load` (the plugin's own `load()`) in ms; the app logs them:
  `plugins: NTVProvider loaded in 685 ms, 1 providers (runtime ms: … manifest 4 …)`.

## After (same TV, same plugins: NTV as Home source, DaddyLive, Sportzx)

| Run | runtime ready | Home source loaded / Home asks for rows | all plugins loaded |
|---|---|---|---|
| before, with these plugins (first start) | 1216 | 3177 (DaddyLive first) | 8507 |
| after, start 1 | 791 | 1937 | 5327 |
| after, start 2 | 826 | 1632 | 4896 |
| after, start 3 | 831 | 1622 | 5212 |

Sportzx alone: 7159 ms (loaded twice) → 4037–4954 ms (once; the rest is its own network work).

## Not done yet

- Plugins still load one at a time; CloudStream loads them in parallel. The runtime keeps per-load
  state in statics (`Main.appContext`, CommonActivity's activity during a load), so this needs care.
- Each plugin start tries to compile the plugin with dex2oat, which fails in about 8 ms because the
  boot image is not at the path dex2oat looks for; the class loader stage is 70–120 ms per plugin.
- Home's first rows still depend on the source's own network requests.
