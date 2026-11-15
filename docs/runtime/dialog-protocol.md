# Plugin dialog protocol

Status 2026-11-16. Implemented by `runtime/android-compat` (`android.app.AlertDialog`, `android.os.Looper`, `android.view.*`,
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
| Layouts from the plugin's resources via `LayoutInflater.inflate(resources.getLayout(id), …)` | framework widgets only: `LinearLayout`, `FrameLayout`, `RelativeLayout`, `ScrollView`, `TextView`, `EditText`, `Button`, `CheckBox`, `Switch`, `ImageView`, `ImageButton`, `View`/`Space`; `<merge>` as root, `<requestFocus>`; plus AndroidX `CardView` and Material `MaterialCardView` as plain containers |
| Views built in code (`EditText(ctx)`, `LinearLayout(ctx).addView(…)`) | same widgets |
| Clickable views that are not buttons (a `TextView` or `ImageButton` with an `OnClickListener`) | described as buttons, named by their text or `contentDescription` |
| A `CheckBox`/`Switch` without text followed by a plain `TextView` (a row of box and label) | one control with the label's text |
| `androidx.appcompat.app.AlertDialog.Builder` | as the framework builder (the runtime's own class replaces the APK's, which needs a window) |
| Material `BottomSheetDialogFragment` subclasses shown with `show(activity.getSupportFragmentManager(), tag)` | `onCreate`, `onCreateView`, `onViewCreated`, the view as a `BottomSheetDialog`, then `onStart`; closing runs `onStop`, `onDestroyView`, `onDestroy` (see below) |
| `BottomSheetDialog`, its `getBehavior()` (`state`, `skipCollapsed`, `isDraggable`, peek height, callbacks) | a dialog; the behaviour's settings are kept and change nothing (the app lays the sheet out) |
| `Handler`/`Looper` (`Looper.getMainLooper()`, `post`, `postDelayed`, messages), `Dispatchers.Main`, `Activity.runOnUiThread` | run on the runtime's main looper, a thread of its own, as on Android's UI thread (see below) |
| `BitmapFactory.decode*` + `ImageView.setImageBitmap` (PNG, JPEG, GIF, WebP up to 4 MiB) | an `image` the app draws (a login QR code); pixel graphics (`createBitmap`, `Canvas`) unsupported |
| `context.startActivity(Intent(ACTION_VIEW, Uri.parse("https://…")))` | `open_url` event: the app shows the link to open on a phone; other intents unsupported |
| `openSettings` casting its context to an Activity type (`context as AppCompatActivity`) | called again with a headless instance of that type (see below) |
| `Resources.getDrawable` / `ResourcesCompat.getDrawable` | a named placeholder that views keep; nothing is drawn |
| `Toast.makeText(…).show()` | `toast` event |
| Custom view classes, other AndroidX/Material views, `<include>`, `<fragment>`, other `DialogFragment`s and fragment transactions, `PreferenceFragment` | reported `unsupported` (named API) or failing with the Android error, which the app shows as "an Android screen CSWeb TV cannot show yet" when it names an Activity type |

Layout and styling attributes (sizes, margins, colours, fonts) are ignored: the app lays dialogs out
for a TV. As on Android, `LayoutInflater.from(context).inflate(<plugin layout id>)` with CloudStream's
context does not see plugin resources; plugins must use their own `Resources` (they do so on Android too).

`openSettings` receives the context the plugin was loaded with: the headless application context, or a
headless Activity for plugins that needed one (CloudStream passes its Activity). It runs on the runtime's
main looper (`Looper.getMainLooper()`, thread `csweb-main`), and so do every listener the dialog
protocol calls and every `Handler` callback or `Dispatchers.Main` coroutine the plugin starts, one at a
time, as on Android's UI thread. kotlinx-coroutines' Android `Dispatchers.Main` comes from CloudStream's
APK (`AndroidDispatcherFactory`, built on `Handler`) unchanged. A callback that throws is reported on
stderr and the looper goes on.

While a dialog waits for the app, the runtime describes it again every 250 ms when that main-thread work
changed it (new text, an image, a button enabled or hidden), and follows it when the work closes it or
opens another. Requests without dialogs never wait for the main looper.

### Settings as a Material bottom sheet (issue #21)

Some plugins build their settings as an AndroidX fragment: CNCVerse `PlayFyProvider` v19 (sha256
`e6a861d7…84b7d7`) subclasses Material's `BottomSheetDialogFragment`, inflates
`res/layout/settings.xml` (a `RelativeLayout` header with an `ImageButton` "Save settings", a list of
playlist rows, each a `RelativeLayout` with a `CheckBox` and a clickable `TextView`), saves the choice
to `SharedPreferences` from `CommonActivity.activity`, and then asks to restart the app. Its
`openSettings` uses the `AppCompatActivity` it was given in `load`. What makes that work:

- CloudStream's APK has AndroidX `Fragment`/`DialogFragment`/`FragmentManager` but not Material's
  `BottomSheetDialogFragment` (R8 removed it). The runtime ships its own
  (`com.google.android.material.bottomsheet.BottomSheetDialogFragment`, extending the APK's
  `DialogFragment`). Its `show()` runs `onCreate(null)`, `onCreateView(LayoutInflater, null, null)` and
  `onViewCreated(view, null)` and shows the view in a framework `Dialog`; `getContext()` is the activity;
  `dismiss()` closes the dialog. No transactions, back stack, child fragments, saved state or lifecycle
  observers: `getActivity()` and `getView()` stay null.
- A headless `FragmentActivity` (created without its constructors, `HeadlessActivities`) gets a fragment
  host whose `getSupportFragmentManager()` is a `HeadlessFragmentManager` carrying the activity.
- For a plugin that needs an Activity, `CommonActivity.activity` is that headless activity while the plugin
  is constructed and loaded (CloudStream's MainActivity sets it on Android), so
  `getActivity()?.getSharedPreferences(…)` works and the choice is kept in the plugin data.
- `Context.getPackageManager().getLaunchIntentForPackage()` returns null (there is no launcher), so a
  plugin's "restart the app" (`startActivity` + `Runtime.exit(0)`) is skipped and never ends the
  runtime; the app reloads the plugin when its settings close instead.
- Framework types the AndroidX classes link against: `ComponentCallbacks`,
  `View.OnCreateContextMenuListener`, `View.OnAttachStateChangeListener`, `LayoutInflater.Factory2`,
  `ContextMenu`, `Menu` (types only) and a small `android.os.Bundle`.

Evidence (host runtime, x86_64, 2026-11-09): `--op settings` describes the sheet ("PlayFy Settings",
a "Save settings" button, 37 labelled check boxes); answering `{"fields":{<JIO TV>:true},"click":"save_btn"}`
shows "Restart Required" (Yes/No) and writes `shared_prefs/PlayFy.json` = `{"JIO TV (IND)":true}`;
`--op load-only` then registers "📺 JIO TV (IND)". In the host app (Xvfb, scripted keys): the gear
opens the sheet, a box ticked and saved, Yes, the plugin is reloaded with "📺 News", and after a restart
the restored plugin data loads it again. Not yet run on a TV.

### Device-code login in a bottom sheet (issue #38)

Kartoons v5 (`https://cloudstream.lasyhost.tech/Kartoons.cs3`, sha256 `7eec1302…3f56`) logs in with a
device code. Its `openSettings` casts its
context to `AppCompatActivity` (its `load()` does not, so the plugin was loaded without an Activity) and
shows `SettingsFragment`, a `BottomSheetDialogFragment` inflating `settings_fragment.xml` (its login
controls inside a `MaterialCardView`, plus "Open in Browser" and "Logout" buttons). In `onStart`
it expands the sheet through `BottomSheetDialog.getBehavior()`. "Get Login Code" posts to its API on
`Dispatchers.IO`, then on `Dispatchers.Main` shows the code and a QR code (`BitmapFactory.decodeByteArray`
into an `ImageView`), and polls the status with `Handler(Looper.getMainLooper()).postDelayed`; the token
goes to `SharedPreferences`. `onDestroyView` stops the polling. Every API it calls is listed above.

What makes it work: the settings call is retried with a headless `AppCompatActivity` when
`openSettings` fails its cast (`compat` event, reason "openSettings casts its context to an Activity");
the runtime's main looper and `Handler`; `BottomSheetDialog`/`BottomSheetBehavior`; `MaterialCardView`;
the fragment's `onStart`/`onDestroyView`; `Bitmap`/`BitmapFactory`; and dialogs described again as they
change.

Evidence (host ART, x86_64, 2026-11-16):

- The unmodified plugin, run with the user's approval on the host only (a throwaway container, the
  plugin mounted read-only, plugin data in the container's `/tmp`, no login made):
  `evidence/runtime-host/runs/kartoons-settings-device-login.jsonl`. The login code and the QR image
  (which encodes it) are redacted. Sequence:
  - the sheet is described: "Get Login Code";
  - the click shows "Generating code..." with the button disabled;
  - 1.3 s later the code from Kartoons' API arrives, with "Open in Browser";
  - then a 200×200 PNG QR code;
  - then "Waiting for login... (9m remaining)" from its polling;
  - a cancel closes it (`dialog_closed`, `done ok:true`).
- Compat tests (`looper`, `bitmaps`, `deviceLoginSheet`) on the JVM.
- On ART, `Dispatchers.Main` resolves to kotlinx's `HandlerContext` and runs on `csweb-main`.

Not yet seen in the app's UI or on a TV.

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
  `button`, `image`, `space`. Optional `"enabled": false`. Hidden (`GONE`/`INVISIBLE`) views are left out.
- `image`: `mime` (`image/png`, `image/jpeg`, `image/gif`, `image/webp`), `width`, `height` (from the
  image's header) and `data`, the encoded bytes in base64 (at most 4 MiB); optional `text` (the content
  description). The app checks the bytes match the type, writes them to `<tmp>/dialog-images/<sha256>.<ext>`
  and draws them up to 360 px.
- A dialog may be described again with the same `dialog` id at any time: after an answer, or because
  plugin code changed it. The app redraws it in place and keeps focus on the view with the same key.
- `{"event":"open_url","url":"https://…"}`: the plugin asked to open a web page; the app shows the link
  (it may carry a login code, so it is never logged).
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
