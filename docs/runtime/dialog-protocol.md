# Plugin dialog protocol

Status 2026-10-19. Implemented by `runtime/android-compat` (`android.app.AlertDialog`, `android.view.*`,
`android.widget.*`, `org.csweb.compat.ui.DialogProtocol`) and `runtime/csloader` (`--op settings`,
`--ui stdin`). Host tests: `runtime/android-compat/test/CompatTest.java` (`ui()`).

## Why

CloudStream plugins offer settings through `Plugin.openSettings(context)`, written with Android views and
dialogs. The runtime has no window system, so a dialog is not drawn: it is described to the native app,
which renders it with its own widgets and sends back what the user did. The plugin's own code runs
unchanged; only the Android framework classes underneath it are headless.

## What is supported

| Plugin code | Runtime behaviour |
|---|---|
| `AlertDialog.Builder`: title, message, view (`setView(View)` or a layout id), positive/negative/neutral buttons, `setItems`, `setSingleChoiceItems`, `setMultiChoiceItems`, `setCancelable`, cancel/dismiss listeners | described and answered |
| `Dialog.setContentView`, `dialog.getButton(…).setOnClickListener` (dialog stays open, as on Android) | supported |
| Layouts from the plugin's resources via `LayoutInflater.inflate(resources.getLayout(id), …)` | framework widgets only: `LinearLayout`, `FrameLayout`, `ScrollView`, `TextView`, `EditText`, `Button`, `CheckBox`, `Switch`, `View`/`Space`; `<merge>` as root, `<requestFocus>` |
| Views built in code (`EditText(ctx)`, `LinearLayout(ctx).addView(…)`) | same widgets |
| `Toast.makeText(…).show()` | `toast` event |
| Custom view classes, AndroidX/Material views, `<include>`, `<fragment>`, `DialogFragment`, `BottomSheetDialog`, `PreferenceFragment`, drawables | reported `unsupported` (named API) |

Layout and styling attributes (sizes, margins, colours, fonts) are ignored: the app lays dialogs out
for a TV. As on Android, `LayoutInflater.from(context).inflate(<plugin layout id>)` with CloudStream's
context does not see plugin resources; plugins must use their own `Resources` (they do so on Android too).

`openSettings` receives the headless application context (CloudStream passes its Activity). Callbacks
run on the runtime's main thread, the thread that called `openSettings`, as dialog callbacks run on the
UI thread on Android.

Example of an unsupported settings screen (issue #21): CNCVerse `PlayFyProvider` v19 (sha256
`e6a861d7…84b7d7`, checked statically, not run) shows its settings as a Material
`BottomSheetDialogFragment` with the inflated `res/layout/settings.xml` (playlist check boxes, an
`ImageButton`) through `AppCompatActivity.getSupportFragmentManager()`, and asks to restart the app
afterwards. Its `openSettings` casts the Activity it kept from `load` (null with the headless
context) to `AppCompatActivity`, so it fails with "null cannot be cast to non-null type
androidx.appcompat.app.AppCompatActivity". The app recognises such failures
(`plugins::settings_need_activity`) and says the plugin's settings are an Android screen it cannot
show yet; the plugin's providers are unaffected. Supporting them needs an Activity with a fragment
manager and view lifecycle mapped onto this protocol.

## Messages

Runtime → app, JSON lines on stdout:

```json
{"event":"settings","available":true}
{"event":"dialog","dialog":1,"stage":"settings","title":"Conformance settings","cancelable":true,
 "message":null,"buttons":{"positive":"Save","negative":"Cancel"},
 "view":{"key":"v0","type":"column","children":[
   {"key":"label_name","type":"label","text":"Display name"},
   {"key":"name_field","type":"text-field","input":"text","value":""},
   {"key":"enabled_box","type":"checkbox","text":"Show extra results","checked":false}]}}
{"event":"dialog_closed","dialog":1}
{"event":"toast","text":"Saved","long":false}
```

- View `type`: `column`, `row` (with `children`), `label`, `text-field`, `checkbox`, `switch`, `toggle`,
  `button`, `space`. Optional `"enabled": false`. Hidden (`GONE`/`INVISIBLE`) views are left out.
- `key`: the resource entry name of the view's id (`name_field`), else `id<n>` for an id set in code,
  else `v<n>` (position in the tree). Keys are what answers refer to.
- `text-field` `input`: `text`, `number`, `phone`, `datetime`, `email`, `uri`, `password`. For
  `password` the value is never sent; `hasValue` says whether one is set.
- Lists: `items` plus `choice` = `items` | `single` (`checkedItem`) | `multiple` (`checkedItems`).
- Without `--ui stdin` a dialog is reported once with `"answerable": false` and stays open.

App → runtime, one JSON object per line on stdin, answering the dialog last described:

```json
{"dialog":1,"fields":{"name_field":"My Conformance","enabled_box":true},"button":"positive"}
```

Applied in this order: `fields` (text for text fields, booleans for checkboxes/switches; check listeners
fire), `click` (key of a `button` view), `item` (list index: plain list picks and dismisses, single
choice selects, multiple choice toggles), `button` (`positive`, `negative`, `neutral`, or `cancel`,
which is ignored for non-cancelable dialogs). An answer without a closing action leaves the dialog
open; the runtime then describes it again with its new state. Unknown keys, wrong value types, a wrong
dialog id or the end of input are errors (`error` event, `ok:false`). Answers are never logged.

## Evidence

Host ART (x86_64), unmodified conformance plugin `CswebConformance.cs3` (sha256 `579d0d25…8632`):

- `evidence/runtime-host/runs/conformance-settings-answered.jsonl`: settings dialog inflated from the
  plugin's compiled layout, answered, `SharedPreferences` written by the plugin's own listener.
- `evidence/runtime-host/runs/conformance-search-after-settings.jsonl`: the next process loads the
  plugin with those preferences: provider named "My Conformance", 3 results instead of 2.
- `evidence/runtime-host/runs/conformance-settings-unanswered.jsonl`: no app attached.

TV (LG OLED65C1PTB, webOS 6.5.3, probe 0.3.0 feeding the answer on stdin): same dialog description,
answer applied, next load named "My Conformance" with 3 results
(`evidence/device/20261019-110528-runtimeprobe-0.3/`).

Not yet done: rendering the dialog in the native app (Phase 2).
