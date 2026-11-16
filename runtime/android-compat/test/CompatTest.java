// SPDX-License-Identifier: GPL-3.0-or-later
// Host tests for the Android compat layer (plain JVM; run by runtime/build.sh). Expected values follow
// the Android SDK documentation for each API.
import android.content.SharedPreferences;
import android.net.Uri;

import org.csweb.compat.HeadlessContext;
import org.csweb.compat.MiniJson;

import java.io.File;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.Map;

public final class CompatTest {
    private static int passed, failed;

    private static void eq(String name, Object want, Object got) {
        if (want == null ? got == null : want.equals(got)) {
            passed++;
        } else {
            failed++;
            System.out.println("FAIL " + name + ": want <" + want + "> got <" + got + ">");
        }
    }

    static void base64() {
        byte[] hello = "hello".getBytes();
        eq("b64 default ends with newline", "aGVsbG8=\n", android.util.Base64.encodeToString(hello, android.util.Base64.DEFAULT));
        eq("b64 no wrap", "aGVsbG8=", android.util.Base64.encodeToString(hello, android.util.Base64.NO_WRAP));
        eq("b64 no padding", "aGVsbG8", android.util.Base64.encodeToString(hello, android.util.Base64.NO_WRAP | android.util.Base64.NO_PADDING));
        eq("b64 url safe", "-_8=", android.util.Base64.encodeToString(new byte[]{(byte) 0xfb, (byte) 0xff}, android.util.Base64.NO_WRAP | android.util.Base64.URL_SAFE));
        eq("b64 crlf", "aGk=\r\n", android.util.Base64.encodeToString("hi".getBytes(), android.util.Base64.CRLF));
        byte[] big = new byte[60];
        String wrapped = android.util.Base64.encodeToString(big, android.util.Base64.DEFAULT);
        eq("b64 wraps at 76", 76, wrapped.indexOf('\n'));
        eq("b64 matches JDK when unwrapped", java.util.Base64.getEncoder().encodeToString(big), wrapped.replace("\n", ""));
        eq("b64 decode skips whitespace", "hello", new String(android.util.Base64.decode(" aGVs\nbG8= ", 0)));
        eq("b64 decode missing padding", "hello", new String(android.util.Base64.decode("aGVsbG8", 0)));
        eq("b64 decode url", "fbff", String.format("%02x%02x", android.util.Base64.decode("-_8", android.util.Base64.URL_SAFE)[0], android.util.Base64.decode("-_8", android.util.Base64.URL_SAFE)[1]));
        boolean threw = false;
        try {
            android.util.Base64.decode("aGVs=bG8", 0);
        } catch (IllegalArgumentException e) {
            threw = true;
        }
        eq("b64 misplaced padding throws", true, threw);
        threw = false;
        try {
            android.util.Base64.decode("a", 0);
        } catch (IllegalArgumentException e) {
            threw = true;
        }
        eq("b64 dangling char throws", true, threw);
        java.util.Random r = new java.util.Random(1);
        for (int n = 0; n < 50; n++) {
            byte[] b = new byte[r.nextInt(300)];
            r.nextBytes(b);
            eq("b64 roundtrip " + n, true, java.util.Arrays.equals(b, android.util.Base64.decode(android.util.Base64.encode(b, 0), 0)));
        }
    }

    static void resources() throws Exception {
        java.io.File cs3 = new java.io.File(System.getProperty("conformance", ""));
        if (!cs3.isFile()) {
            System.out.println("SKIP resources: conformance plugin not built (test-plugins/conformance/build.sh)");
            return;
        }
        android.content.res.AssetManager am = android.content.res.AssetManager.class.getDeclaredConstructor().newInstance();
        eq("addAssetPath cookie", 1, am.getClass().getMethod("addAssetPath", String.class).invoke(am, cs3.getPath()));
        android.content.res.Resources sys = android.content.res.Resources.getSystem();
        android.content.res.Resources r = new android.content.res.Resources(am, sys.getDisplayMetrics(), sys.getConfiguration());
        String pkg = "org.csweb.conformance";
        int name = r.getIdentifier("provider_name", "string", pkg);
        eq("identifier found", true, name != 0);
        eq("identifier forms agree", name, r.getIdentifier(pkg + ":string/provider_name", null, null));
        eq("identifier type/name", name, r.getIdentifier("string/provider_name", null, pkg));
        eq("other package -> 0", 0, r.getIdentifier("provider_name", "string", "android"));
        eq("missing -> 0", 0, r.getIdentifier("nope", "string", pkg));
        eq("string", "Conformance", r.getString(name));
        eq("escaped apostrophe", true, r.getString(r.getIdentifier("plot", "string", pkg)).contains("plugin's"));
        eq("format", "Conformance!", r.getString(name) + "!");
        eq("resource name", pkg + ":string/provider_name", r.getResourceName(name));
        eq("string array", Arrays.asList("Conformance Alpha", "Conformance Beta", "Conformance Gamma"),
                Arrays.asList(r.getStringArray(r.getIdentifier("titles", "array", pkg))));
        eq("color", 0xFF2F8F5B, r.getColor(r.getIdentifier("accent", "color", pkg)));
        int pad = r.getIdentifier("padding", "dimen", pkg);
        eq("dimension 16dp at xhdpi", 32.0f, r.getDimension(pad));
        eq("dimension pixel size", 32, r.getDimensionPixelSize(pad));
        boolean threw = false;
        try {
            r.getString(0x7f7f0000);
        } catch (android.content.res.Resources.NotFoundException e) {
            threw = true;
        }
        eq("unknown id throws NotFoundException", true, threw);
        // No graphics stack: a drawable resource is a named placeholder that views keep (PlayFy's
        // settings set one as a background and as its save button's icon).
        android.graphics.drawable.Drawable icon = r.getDrawable(r.getIdentifier("icon", "drawable", pkg));
        eq("drawable placeholder", "drawable/icon",
                icon instanceof org.csweb.compat.res.ResourceDrawable ? ((org.csweb.compat.res.ResourceDrawable) icon).name() : null);
        threw = false;
        try {
            r.getDrawable(r.getIdentifier("settings", "layout", pkg));
        } catch (android.content.res.Resources.NotFoundException e) {
            threw = true;
        }
        eq("a layout is not a drawable", true, threw);

        android.content.res.XmlResourceParser x = r.getLayout(r.getIdentifier("settings", "layout", pkg));
        String android = "http://schemas.android.com/apk/res/android";
        java.util.List<String> tags = new java.util.ArrayList<>();
        String orientation = null, text = null, inputType = null, padding = null;
        int textRes = 0, idRes = 0;
        for (int ev = x.getEventType(); ev != org.xmlpull.v1.XmlPullParser.END_DOCUMENT; ev = x.next()) {
            if (ev != org.xmlpull.v1.XmlPullParser.START_TAG) continue;
            tags.add(x.getName() + "@" + x.getDepth());
            if (x.getName().equals("LinearLayout")) {
                orientation = x.getAttributeValue(android, "orientation");
                padding = x.getAttributeValue(android, "padding");
            }
            if (x.getName().equals("TextView")) {
                text = x.getAttributeValue(android, "text");
                textRes = x.getAttributeResourceValue(android, "text", 0);
                idRes = x.getIdAttributeResourceValue(0);
            }
            if (x.getName().equals("EditText")) inputType = x.getAttributeValue(android, "inputType");
        }
        eq("layout tags", Arrays.asList("LinearLayout@1", "TextView@2", "EditText@2", "CheckBox@2"), tags);
        eq("enum attribute as int text", "1", orientation);
        eq("reference attribute text", "@" + r.getIdentifier("label_name", "string", pkg), text);
        eq("reference attribute value", r.getIdentifier("label_name", "string", pkg), textRes);
        eq("id attribute", r.getIdentifier("label_name", "id", pkg), idRes);
        eq("padding attribute references dimen", "@" + pad, padding);
        eq("flag attribute", "0x1", inputType);
    }

    static final class TestUi extends org.csweb.compat.ui.UiHost {
        final java.util.List<android.app.Dialog> shown = new java.util.ArrayList<>();
        final java.util.List<String> toasts = new java.util.ArrayList<>();

        @Override public void shown(android.app.Dialog d) { shown.add(d); }
        @Override public void dismissed(android.app.Dialog d) { shown.remove(d); }
        @Override public void toast(CharSequence text, int duration) { toasts.add(text + "/" + duration); }
        final java.util.List<String> urls = new java.util.ArrayList<>();
        @Override public void openUrl(String url) { urls.add(url); }
    }

    @SuppressWarnings("unchecked")
    static Map<String, Object> answer(String json) {
        return (Map<String, Object>) MiniJson.decode(json);
    }

    static void ui() throws Exception {
        File dir = Files.createTempDirectory("csweb-ui").toFile();
        HeadlessContext ctx = new HeadlessContext(dir, "test.app");
        boolean threw = false;
        try {
            new android.app.AlertDialog.Builder(ctx).setTitle("x").show();
        } catch (org.csweb.compat.Unsupported e) {
            threw = true;
        }
        eq("show without UI host is unsupported", true, threw);
        TestUi ui = new TestUi();
        org.csweb.compat.ui.UiHost.install(ui);

        // programmatic view + dialog, as many plugin settings screens are written
        android.widget.LinearLayout col = new android.widget.LinearLayout(ctx);
        col.setOrientation(android.widget.LinearLayout.VERTICAL);
        android.widget.EditText user = new android.widget.EditText(ctx);
        user.setHint("User");
        android.widget.EditText pass = new android.widget.EditText(ctx);
        pass.setInputType(0x81);  // TYPE_CLASS_TEXT | TYPE_TEXT_VARIATION_PASSWORD
        pass.setText("old-secret");
        android.widget.Switch sw = new android.widget.Switch(ctx);
        sw.setText("Enable");
        final boolean[] changed = {false};
        sw.setOnCheckedChangeListener((b, c) -> changed[0] = c);
        android.widget.TextView gone = new android.widget.TextView(ctx);
        gone.setVisibility(android.view.View.GONE);
        col.addView(user);
        col.addView(pass);
        col.addView(sw);
        col.addView(gone);
        eq("findViewById none", null, col.findViewById(42));
        user.setId(42);
        eq("findViewById", user, col.findViewById(42));
        final String[] saved = new String[3];
        android.app.AlertDialog d = new android.app.AlertDialog.Builder(ctx)
                .setTitle("Login").setMessage("Account").setView(col)
                .setPositiveButton("Save", (dlg, which) -> {
                    saved[0] = user.getText().toString();
                    saved[1] = pass.getText().toString();
                    saved[2] = String.valueOf(which);
                })
                .setNegativeButton("Cancel", null)
                .show();
        eq("shown", true, d.isShowing() && ui.shown.contains(d));
        String desc = MiniJson.encode(org.csweb.compat.ui.DialogProtocol.describe(d));
        eq("describe", "{\"title\":\"Login\",\"cancelable\":true,\"message\":\"Account\",\"buttons\":{\"positive\":\"Save\",\"negative\":\"Cancel\"},"
                + "\"view\":{\"key\":\"v0\",\"type\":\"column\",\"children\":["
                + "{\"key\":\"id42\",\"type\":\"text-field\",\"input\":\"text\",\"hint\":\"User\",\"value\":\"\"},"
                + "{\"key\":\"v2\",\"type\":\"text-field\",\"input\":\"password\",\"hasValue\":true},"
                + "{\"key\":\"v3\",\"type\":\"switch\",\"text\":\"Enable\",\"checked\":false}]}}", desc);
        eq("password never described", false, desc.contains("old-secret"));
        threw = false;
        try {
            org.csweb.compat.ui.DialogProtocol.answer(d, answer("{\"fields\":{\"v4\":\"x\"}}"));
        } catch (IllegalArgumentException e) {
            threw = true;
        }
        eq("hidden view not answerable", true, threw);
        threw = false;
        try {
            org.csweb.compat.ui.DialogProtocol.answer(d, answer("{\"fields\":{\"v3\":\"yes\"}}"));
        } catch (IllegalArgumentException e) {
            threw = true;
        }
        eq("wrong value type rejected", true, threw);
        org.csweb.compat.ui.DialogProtocol.answer(d, answer(
                "{\"dialog\":1,\"fields\":{\"id42\":\"alice\",\"v2\":\"s3cret\",\"v3\":true},\"button\":\"positive\"}"));
        eq("listener saw fields", "alice|s3cret|-1", saved[0] + "|" + saved[1] + "|" + saved[2]);
        eq("checked listener fired", true, changed[0]);
        eq("positive dismisses", false, d.isShowing() || ui.shown.contains(d));

        // getButton() override keeps the dialog open, as plugins use it for validation
        android.app.AlertDialog v = new android.app.AlertDialog.Builder(ctx).setPositiveButton("OK", null).create();
        eq("getButton before create", null, v.getButton(android.content.DialogInterface.BUTTON_POSITIVE));
        v.show();
        final int[] clicks = {0};
        v.getButton(android.content.DialogInterface.BUTTON_POSITIVE).setOnClickListener(b -> clicks[0]++);
        org.csweb.compat.ui.DialogProtocol.answer(v, answer("{\"button\":\"positive\"}"));
        eq("button override ran, dialog stays", "1 true", clicks[0] + " " + v.isShowing());
        v.dismiss();

        // lists
        final java.util.List<String> events = new java.util.ArrayList<>();
        android.app.AlertDialog items = new android.app.AlertDialog.Builder(ctx)
                .setItems(new CharSequence[]{"a", "b"}, (dlg, w) -> events.add("item" + w))
                .setOnDismissListener(dlg -> events.add("dismiss")).show();
        eq("items described", true, MiniJson.encode(org.csweb.compat.ui.DialogProtocol.describe(items)).contains("\"items\":[\"a\",\"b\"],\"choice\":\"items\""));
        org.csweb.compat.ui.DialogProtocol.answer(items, answer("{\"item\":1}"));
        eq("item pick dismisses", Arrays.asList("item1", "dismiss"), events);
        events.clear();
        android.app.AlertDialog single = new android.app.AlertDialog.Builder(ctx)
                .setSingleChoiceItems(new CharSequence[]{"x", "y", "z"}, 0, (dlg, w) -> events.add("single" + w))
                .setPositiveButton("OK", (dlg, w) -> events.add("ok")).show();
        org.csweb.compat.ui.DialogProtocol.answer(single, answer("{\"item\":2}"));
        eq("single choice stays open", true, single.isShowing());
        eq("single choice described", true, MiniJson.encode(org.csweb.compat.ui.DialogProtocol.describe(single)).contains("\"checkedItem\":2"));
        org.csweb.compat.ui.DialogProtocol.answer(single, answer("{\"button\":\"positive\"}"));
        eq("single then ok", Arrays.asList("single2", "ok"), events);
        events.clear();
        boolean[] flags = {true, false};
        android.app.AlertDialog multi = new android.app.AlertDialog.Builder(ctx)
                .setMultiChoiceItems(new CharSequence[]{"p", "q"}, flags, (dlg, w, c) -> events.add(w + "=" + c)).show();
        org.csweb.compat.ui.DialogProtocol.answer(multi, answer("{\"item\":1}"));
        eq("multi toggles", "[1=true] [true, true]", events + " " + Arrays.toString(flags));
        eq("multi described", true, MiniJson.encode(org.csweb.compat.ui.DialogProtocol.describe(multi)).contains("\"checkedItems\":[0,1]"));
        events.clear();
        multi.setOnCancelListener(dlg -> events.add("cancel"));
        multi.setOnDismissListener(dlg -> events.add("dismiss"));
        org.csweb.compat.ui.DialogProtocol.answer(multi, answer("{\"button\":\"cancel\"}"));
        eq("cancel then dismiss", Arrays.asList("cancel", "dismiss"), events);
        android.app.AlertDialog fixed = new android.app.AlertDialog.Builder(ctx).setCancelable(false).show();
        org.csweb.compat.ui.DialogProtocol.answer(fixed, answer("{\"button\":\"cancel\"}"));
        eq("non-cancelable ignores cancel", true, fixed.isShowing());
        fixed.dismiss();

        android.widget.Toast.makeText(ctx, "hello", android.widget.Toast.LENGTH_LONG).show();
        eq("toast", Arrays.asList("hello/1"), ui.toasts);
        android.text.Editable e = new android.text.SpannableStringBuilder("abc");
        e.append("d").insert(0, ">").delete(1, 2);
        eq("editable", ">bcd", e.toString());

        // the conformance plugin's compiled layout
        java.io.File cs3 = new java.io.File(System.getProperty("conformance", ""));
        if (!cs3.isFile()) {
            System.out.println("SKIP layout inflation: conformance plugin not built");
            return;
        }
        android.content.res.AssetManager am = android.content.res.AssetManager.class.getDeclaredConstructor().newInstance();
        am.getClass().getMethod("addAssetPath", String.class).invoke(am, cs3.getPath());
        android.content.res.Resources sys = android.content.res.Resources.getSystem();
        android.content.res.Resources r = new android.content.res.Resources(am, sys.getDisplayMetrics(), sys.getConfiguration());
        String pkg = "org.csweb.conformance";
        android.view.View root = android.view.LayoutInflater.from(ctx).inflate(r.getLayout(r.getIdentifier("settings", "layout", pkg)), null, false);
        eq("inflated root", android.widget.LinearLayout.class, root.getClass());
        eq("vertical", android.widget.LinearLayout.VERTICAL, ((android.widget.LinearLayout) root).getOrientation());
        eq("children", 3, ((android.view.ViewGroup) root).getChildCount());
        android.widget.EditText name = root.findViewById(r.getIdentifier("name_field", "id", pkg));
        eq("edit text by id", true, name != null);
        android.widget.TextView label = root.findViewById(r.getIdentifier("label_name", "id", pkg));
        eq("label text from @string", r.getString(r.getIdentifier("label_name", "string", pkg)), label.getText().toString());
        android.app.AlertDialog s = new android.app.AlertDialog.Builder(ctx).setView(root).show();
        String sd = MiniJson.encode(org.csweb.compat.ui.DialogProtocol.describe(s));
        eq("keys are id names", true, sd.contains("\"key\":\"name_field\"") && sd.contains("\"key\":\"enabled_box\""));
        org.csweb.compat.ui.DialogProtocol.answer(s, answer("{\"fields\":{\"name_field\":\"Mine\",\"enabled_box\":true}}"));
        android.widget.CheckBox box = root.findViewById(r.getIdentifier("enabled_box", "id", pkg));
        eq("answer applied by key", "Mine true", name.getText() + " " + box.isChecked());
        s.dismiss();
        threw = false;
        try {
            android.view.LayoutInflater.from(ctx).inflate(r.getIdentifier("settings", "layout", pkg), null);
        } catch (android.content.res.Resources.NotFoundException ex) {
            threw = true;
        }
        eq("plugin layout id via app context not found (as on Android)", true, threw);
    }

    public static void main(String[] args) throws Exception {
        resources();
        ui();
        settingsSheetViews();
        looper();
        bitmaps();
        deviceLoginSheet();
        base64();
        uri();
        json();
        prefs();
        System.out.println("compat tests: " + passed + " passed, " + failed + " failed");
        if (failed > 0) System.exit(1);
    }

    /** android.os.Looper/Handler: the main looper's thread, order by time then posting, removal, messages. */
    static void looper() throws Exception {
        android.os.Looper main = android.os.Looper.getMainLooper();
        eq("main looper is a singleton", true, main == android.os.Looper.getMainLooper());
        eq("main looper thread", "csweb-main", main.getThread().getName());
        eq("caller is not on it", false, main.isCurrentThread());
        eq("no looper on this thread", null, android.os.Looper.myLooper());
        boolean threw = false;
        try {
            new android.os.Handler();
        } catch (RuntimeException e) {
            threw = true;
        }
        eq("Handler() without a looper throws", true, threw);

        android.os.Handler h = new android.os.Handler(main);
        final java.util.List<String> order = java.util.Collections.synchronizedList(new java.util.ArrayList<String>());
        final java.util.concurrent.CountDownLatch done = new java.util.concurrent.CountDownLatch(1);
        Runnable never = () -> order.add("removed");
        h.postDelayed(() -> { order.add("late"); done.countDown(); }, 80);
        h.postDelayed(never, 40);
        h.post(() -> order.add("first:" + (android.os.Looper.myLooper() == main)));
        h.post(() -> order.add("second"));
        eq("has callbacks", true, h.hasCallbacks(never));
        h.removeCallbacks(never);
        eq("removed", false, h.hasCallbacks(never));
        done.await(5, java.util.concurrent.TimeUnit.SECONDS);
        eq("run in order, on the main looper", java.util.Arrays.asList("first:true", "second", "late"), new java.util.ArrayList<>(order));

        final java.util.concurrent.atomic.AtomicInteger what = new java.util.concurrent.atomic.AtomicInteger();
        final java.util.concurrent.CountDownLatch got = new java.util.concurrent.CountDownLatch(1);
        android.os.Handler withCallback = new android.os.Handler(main, m -> {
            what.set(m.what * 10 + m.arg1);
            got.countDown();
            return true;
        });
        withCallback.sendMessage(withCallback.obtainMessage(4, 2, 0));
        got.await(5, java.util.concurrent.TimeUnit.SECONDS);
        eq("message to callback", 42, what.get());

        // A throwing callback is reported; the loop keeps serving.
        h.post(() -> { throw new IllegalStateException("plugin bug (expected in this test)"); });
        eq("runOnMain after a throwing callback", "csweb-main", android.os.Looper.runOnMain(() -> Thread.currentThread().getName()));
        threw = false;
        try {
            android.os.Looper.runOnMain(() -> { throw new IllegalArgumentException("x"); });
        } catch (IllegalArgumentException e) {
            threw = true;
        }
        eq("runOnMain rethrows", true, threw);
        eq("createAsync", main, android.os.Handler.createAsync(main).getLooper());
    }

    /** A PNG IHDR, a GIF and a JPEG SOF0 header of the given size (enough for BitmapFactory's sniffing). */
    static byte[] png(int w, int h) {
        byte[] b = new byte[33];
        byte[] head = {(byte) 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
        System.arraycopy(head, 0, b, 0, head.length);
        for (int i = 0; i < 4; i++) {
            b[16 + i] = (byte) (w >>> (24 - 8 * i));
            b[20 + i] = (byte) (h >>> (24 - 8 * i));
        }
        return b;
    }

    static void bitmaps() {
        android.graphics.Bitmap bm = android.graphics.BitmapFactory.decodeByteArray(png(300, 200), 0, 33);
        eq("png size", "300x200 image/png", bm.getWidth() + "x" + bm.getHeight() + " " + bm.mimeType());
        eq("bytes kept", 33, bm.encodedBytes().length);
        byte[] gif = {'G', 'I', 'F', '8', '9', 'a', 10, 0, 5, 0};
        eq("gif size", 10 * 5, android.graphics.BitmapFactory.decodeByteArray(gif, 0, gif.length).getWidth() * 5);
        byte[] jpeg = {(byte) 0xFF, (byte) 0xD8, (byte) 0xFF, (byte) 0xE0, 0, 4, 0, 0,
                (byte) 0xFF, (byte) 0xC0, 0, 11, 8, 0, 64, 0, (byte) 128, 3, 0, 0, 0, 0, 0};
        android.graphics.Bitmap j = android.graphics.BitmapFactory.decodeByteArray(jpeg, 0, jpeg.length);
        eq("jpeg size past APP0", "128x64 image/jpeg", j.getWidth() + "x" + j.getHeight() + " " + j.mimeType());
        eq("not an image gives null", null, android.graphics.BitmapFactory.decodeByteArray(new byte[]{1, 2, 3, 4}, 0, 4));
        android.graphics.BitmapFactory.Options o = new android.graphics.BitmapFactory.Options();
        o.inJustDecodeBounds = true;
        eq("bounds only returns null", null, android.graphics.BitmapFactory.decodeByteArray(png(7, 9), 0, 33, o));
        eq("bounds filled", "7x9 image/png", o.outWidth + "x" + o.outHeight + " " + o.outMimeType);
        boolean threw = false;
        try {
            android.graphics.BitmapFactory.decodeByteArray(new byte[4], 2, 3);
        } catch (ArrayIndexOutOfBoundsException e) {
            threw = true;
        }
        eq("range checked", true, threw);
        eq("stream", 300, android.graphics.BitmapFactory.decodeStream(new java.io.ByteArrayInputStream(png(300, 1))).getWidth());
    }

    /**
     * A device-code login sheet built like Kartoons' (issue #38): a BottomSheetDialog expanded through its
     * behaviour, a button that fetches a code on the main looper later (Handler.postDelayed, as its
     * polling does), a QR bitmap shown in an ImageView that was hidden, a link opened with ACTION_VIEW,
     * and the sheet closing itself when the login completes. Built and changed on the main looper.
     */
    static void deviceLoginSheet() throws Exception {
        File dir = Files.createTempDirectory("csweb-login").toFile();
        final HeadlessContext ctx = new HeadlessContext(dir, "test.app");
        final TestUi ui = new TestUi();
        org.csweb.compat.ui.UiHost.install(ui);
        final android.os.Handler main = new android.os.Handler(android.os.Looper.getMainLooper());
        final com.google.android.material.bottomsheet.BottomSheetDialog[] sheet = {null};
        final android.widget.TextView[] status = {null};
        final java.util.concurrent.CountDownLatch codeShown = new java.util.concurrent.CountDownLatch(1);
        android.os.Looper.runOnMain(() -> {
            android.widget.LinearLayout root = new android.widget.LinearLayout(ctx);
            root.setOrientation(android.widget.LinearLayout.VERTICAL);
            android.widget.TextView text = new android.widget.TextView(ctx);
            text.setText("Click below to get a login code");
            final android.widget.ImageView qr = new android.widget.ImageView(ctx);
            qr.setVisibility(android.view.View.GONE);
            final android.widget.Button get = new android.widget.Button(ctx);
            get.setText("Get Login Code");
            get.setOnClickListener(v -> {
                get.setEnabled(false);
                text.setText("Generating code...");
                main.postDelayed(() -> {
                    text.setText("Enter this code at https://example.test/link: ABCD");
                    qr.setImageBitmap(android.graphics.BitmapFactory.decodeByteArray(png(4, 4), 0, 33));
                    qr.setVisibility(android.view.View.VISIBLE);
                    codeShown.countDown();
                }, 50);
            });
            root.addView(text);
            root.addView(qr);
            root.addView(get);
            com.google.android.material.bottomsheet.BottomSheetDialog d =
                    new com.google.android.material.bottomsheet.BottomSheetDialog(ctx);
            d.setContentView(root);
            d.show();
            d.getBehavior().setState(com.google.android.material.bottomsheet.BottomSheetBehavior.STATE_EXPANDED);
            d.getBehavior().setSkipCollapsed(true);
            d.getBehavior().setDraggable(false);
            sheet[0] = d;
            status[0] = text;
            return null;
        });
        com.google.android.material.bottomsheet.BottomSheetBehavior<?> behavior = sheet[0].getBehavior();
        eq("behaviour kept", "3 true false", behavior.getState() + " " + behavior.getSkipCollapsed() + " " + behavior.isDraggable());
        boolean threw = false;
        try {
            behavior.setState(com.google.android.material.bottomsheet.BottomSheetBehavior.STATE_DRAGGING);
        } catch (IllegalArgumentException e) {
            threw = true;
        }
        eq("dragging cannot be set", true, threw);
        String before = MiniJson.encode(android.os.Looper.runOnMain(() -> org.csweb.compat.ui.DialogProtocol.describe(sheet[0])));
        eq("hidden image left out", "{\"title\":null,\"cancelable\":true,\"view\":{\"key\":\"v0\",\"type\":\"column\",\"children\":["
                + "{\"key\":\"v1\",\"type\":\"label\",\"text\":\"Click below to get a login code\"},"
                + "{\"key\":\"v3\",\"type\":\"button\",\"text\":\"Get Login Code\"}]}}", before);
        android.os.Looper.runOnMain(() -> {
            org.csweb.compat.ui.DialogProtocol.answer(sheet[0], answer("{\"click\":\"v3\"}"));
            return null;
        });
        eq("click handled at once", "Generating code...", android.os.Looper.runOnMain(() -> status[0].getText().toString()));
        codeShown.await(5, java.util.concurrent.TimeUnit.SECONDS);
        String after = MiniJson.encode(android.os.Looper.runOnMain(() -> org.csweb.compat.ui.DialogProtocol.describe(sheet[0])));
        String data = java.util.Base64.getEncoder().encodeToString(png(4, 4));
        eq("code, QR image and disabled button described", "{\"title\":null,\"cancelable\":true,\"view\":{\"key\":\"v0\",\"type\":\"column\",\"children\":["
                + "{\"key\":\"v1\",\"type\":\"label\",\"text\":\"Enter this code at https://example.test/link: ABCD\"},"
                + "{\"key\":\"v2\",\"type\":\"image\",\"mime\":\"image/png\",\"width\":4,\"height\":4,\"data\":\"" + data + "\"},"
                + "{\"key\":\"v3\",\"enabled\":false,\"type\":\"button\",\"text\":\"Get Login Code\"}]}}", after);

        // Kartoons wraps its controls in a MaterialCardView: a container, described as a column.
        com.google.android.material.card.MaterialCardView card = new com.google.android.material.card.MaterialCardView(ctx);
        android.widget.TextView inCard = new android.widget.TextView(ctx);
        inCard.setText("Login");
        card.addView(inCard);
        card.setCheckable(true);
        card.toggle();
        eq("card checked state", true, card.isChecked());
        eq("card is a CardView and a FrameLayout", true,
                card instanceof androidx.cardview.widget.CardView && card instanceof android.widget.FrameLayout);
        android.app.Dialog carded = new android.app.Dialog(ctx);
        carded.setContentView(card);
        eq("card described as a column", "{\"title\":null,\"cancelable\":true,\"view\":{\"key\":\"v0\",\"type\":\"column\",\"children\":["
                + "{\"key\":\"v1\",\"type\":\"label\",\"text\":\"Login\"}]}}",
                MiniJson.encode(org.csweb.compat.ui.DialogProtocol.describe(carded)));

        // ACTION_VIEW of a web link reaches the UI host; other intents stay unsupported.
        ctx.startActivity(new android.content.Intent(android.content.Intent.ACTION_VIEW, Uri.parse("https://example.test/link?code=ABCD")));
        eq("link shown by the app", java.util.Collections.singletonList("https://example.test/link?code=ABCD"), ui.urls);
        threw = false;
        try {
            ctx.startActivity(new android.content.Intent(android.content.Intent.ACTION_VIEW, Uri.parse("market://details?id=x")));
        } catch (org.csweb.compat.Unsupported e) {
            threw = true;
        }
        eq("other intents unsupported", true, threw);

        final java.util.concurrent.CountDownLatch closed = new java.util.concurrent.CountDownLatch(1);
        sheet[0].setOnDismissListener(d -> closed.countDown());
        main.postDelayed(() -> sheet[0].dismiss(), 10);  // login complete: the plugin closes its sheet
        closed.await(5, java.util.concurrent.TimeUnit.SECONDS);
        eq("closed by main-thread work", false, sheet[0].isShowing());
        eq("host told", true, ui.shown.isEmpty());
    }

    /**
     * The views and dialogs of a settings screen built like CNCVerse PlayFy's (issue #21): a RelativeLayout
     * row per choice with a CheckBox and a clickable TextView that toggles it, an ImageButton that saves,
     * and an AppCompat AlertDialog after saving. Run after ui(), which installs the UI host.
     */
    static void settingsSheetViews() throws Exception {
        File dir = Files.createTempDirectory("csweb-sheet").toFile();
        HeadlessContext ctx = new HeadlessContext(dir, "test.app");
        int a = android.view.View.generateViewId(), b = android.view.View.generateViewId();
        eq("generated ids positive and distinct", true, a > 0 && b > 0 && a != b && b < 0x01000000);

        android.widget.LinearLayout root = new android.widget.LinearLayout(ctx);
        root.setOrientation(android.widget.LinearLayout.VERTICAL);
        android.widget.ImageButton save = new android.widget.ImageButton(ctx);
        save.setContentDescription("Save");
        save.setPadding(1, 2, 3, 4);
        eq("padding kept", 2 + 4, save.getPaddingTop() + save.getPaddingBottom());
        android.widget.RelativeLayout row = new android.widget.RelativeLayout(ctx);
        android.widget.CheckBox box = new android.widget.CheckBox(ctx);
        box.setId(a);
        android.widget.TextView label = new android.widget.TextView(ctx);
        label.setText("Sports");
        android.widget.RelativeLayout.LayoutParams lp = new android.widget.RelativeLayout.LayoutParams(-2, -2);
        lp.addRule(android.widget.RelativeLayout.END_OF, a);
        lp.addRule(android.widget.RelativeLayout.CENTER_VERTICAL);
        lp.setMarginStart(16);
        label.setLayoutParams(lp);
        eq("relative rule kept", a, lp.getRule(android.widget.RelativeLayout.END_OF));
        eq("rule TRUE", android.widget.RelativeLayout.TRUE, lp.getRule(android.widget.RelativeLayout.CENTER_VERTICAL));
        label.setOnClickListener(v -> box.setChecked(!box.isChecked()));
        row.addView(box);
        row.addView(label);
        final java.util.List<String> enabled = new java.util.ArrayList<>();
        box.setOnCheckedChangeListener((c, on) -> {
            if (on) enabled.add("Sports"); else enabled.remove("Sports");
        });
        final boolean[] saved = {false};
        save.setOnClickListener(v -> saved[0] = true);
        root.addView(save);
        root.addView(row);
        android.app.Dialog sheet = new android.app.Dialog(ctx);
        sheet.setContentView(root);
        sheet.show();
        String desc = MiniJson.encode(org.csweb.compat.ui.DialogProtocol.describe(sheet));
        eq("sheet described", "{\"title\":null,\"cancelable\":true,\"view\":{\"key\":\"v0\",\"type\":\"column\",\"children\":["
                + "{\"key\":\"v1\",\"type\":\"button\",\"text\":\"Save\"},"
                + "{\"key\":\"v2\",\"type\":\"column\",\"children\":["
                + "{\"key\":\"id" + a + "\",\"type\":\"checkbox\",\"text\":\"Sports\",\"checked\":false}]}]}}", desc);
        org.csweb.compat.ui.DialogProtocol.answer(sheet, answer("{\"fields\":{\"id" + a + "\":true},\"click\":\"v1\"}"));
        eq("checkbox answered through its listener", java.util.Collections.singletonList("Sports"), enabled);
        eq("image button clicked", true, saved[0]);
        org.csweb.compat.ui.DialogProtocol.answer(sheet, answer("{\"click\":\"v4\"}"));  // the label toggles the box
        eq("clickable label clicked", false, box.isChecked());
        sheet.dismiss();

        // AppCompat's builder: same API and types as androidx, shown through the dialog protocol.
        final String[] pressed = {null};
        androidx.appcompat.app.AlertDialog restart = new androidx.appcompat.app.AlertDialog.Builder(ctx)
                .setTitle("Restart Required").setMessage("Changes have been saved.")
                .setPositiveButton("Yes", (d, w) -> pressed[0] = "yes")
                .setNegativeButton("No", (d, w) -> pressed[0] = "no")
                .show();
        eq("appcompat dialog shown", true, restart.isShowing());
        eq("appcompat dialog described", "{\"title\":\"Restart Required\",\"cancelable\":true,\"message\":\"Changes have been saved.\","
                + "\"buttons\":{\"positive\":\"Yes\",\"negative\":\"No\"}}",
                MiniJson.encode(org.csweb.compat.ui.DialogProtocol.describe(restart)));
        org.csweb.compat.ui.DialogProtocol.answer(restart, answer("{\"button\":\"negative\"}"));
        eq("appcompat button", "no", pressed[0]);

        android.os.Bundle args = new android.os.Bundle();
        args.putInt("n", 3);
        args.putString("s", "x");
        eq("bundle int", 3, args.getInt("n"));
        eq("bundle wrong type gives default", 7, args.getInt("s", 7));
        eq("bundle missing string default", "d", args.getString("none", "d"));

        // No launcher: a plugin's "restart the app" finds no intent and so never calls Runtime.exit.
        eq("no launch intent", null, ctx.getPackageManager().getLaunchIntentForPackage(ctx.getPackageName()));
    }

    static void uri() {
        Uri u = Uri.parse("https://user@Example.com:8443/a%20b/c.m3u8?x=1&q=hello+world%21&x=2#frag%20x");
        eq("scheme", "https", u.getScheme());
        eq("host", "Example.com", u.getHost());
        eq("port", 8443, u.getPort());
        eq("userInfo", "user", u.getUserInfo());
        eq("path decoded", "/a b/c.m3u8", u.getPath());
        eq("encoded path", "/a%20b/c.m3u8", u.getEncodedPath());
        eq("last segment", "c.m3u8", u.getLastPathSegment());
        eq("segments", Arrays.asList("a b", "c.m3u8"), u.getPathSegments());
        eq("query param plus", "hello world!", u.getQueryParameter("q"));
        eq("first of repeated", "1", u.getQueryParameter("x"));
        eq("all repeated", Arrays.asList("1", "2"), u.getQueryParameters("x"));
        eq("missing param", null, u.getQueryParameter("nope"));
        eq("names", new LinkedHashSet<>(Arrays.asList("x", "q")), u.getQueryParameterNames());
        eq("fragment", "frag x", u.getFragment());
        eq("toString unchanged", "https://user@Example.com:8443/a%20b/c.m3u8?x=1&q=hello+world%21&x=2#frag%20x", u.toString());
        eq("no port", -1, Uri.parse("http://h/p").getPort());
        eq("relative", null, Uri.parse("/just/path").getScheme());
        eq("relative path", "/just/path", Uri.parse("/just/path").getPath());
        eq("opaque", true, Uri.parse("mailto:a@b.c").isOpaque());
        eq("opaque ssp", "a@b.c", Uri.parse("mailto:a@b.c").getSchemeSpecificPart());
        eq("lenient garbage", "not a uri at all", Uri.parse("not a uri at all").toString());
        eq("ipv6 host", "[::1]", Uri.parse("http://[::1]:80/").getHost());
        eq("ipv6 port", 80, Uri.parse("http://[::1]:80/").getPort());
        eq("encode", "a%20b%2Fc%3F%C3%A9_-!.~'()*", Uri.encode("a b/c?é_-!.~'()*"));
        eq("encode allow", "a/b%20c", Uri.encode("a/b c", "/"));
        eq("decode", "a b/é", Uri.decode("a%20b%2F%C3%A9"));
        eq("decode keeps plus", "a+b", Uri.decode("a+b"));
        eq("decode bad escape kept", "100%", Uri.decode("100%"));
        Uri built = new Uri.Builder().scheme("https").authority("api.example.com").appendPath("v1")
                .appendPath("search items").appendQueryParameter("q", "a&b c").appendQueryParameter("page", "2").build();
        eq("builder", "https://api.example.com/v1/search%20items?q=a%26b%20c&page=2", built.toString());
        eq("builder roundtrip", "a&b c", built.getQueryParameter("q"));
        eq("buildUpon keeps", "https://h/p?a=1&b=2", Uri.parse("https://h/p?a=1").buildUpon().appendQueryParameter("b", "2").build().toString());
        eq("equals", Uri.parse("x:y"), Uri.parse("x:y"));
    }

    @SuppressWarnings("unchecked")
    static void json() {
        Object v = MiniJson.decode("{\"a\":[1,2.5,\"x\\n\\u00e9\",true,null,{}],\"b\":-3}");
        Map<String, Object> m = (Map<String, Object>) v;
        eq("json array", Arrays.asList(1, 2.5, "x\né", true, null, Collections.emptyMap()), m.get("a"));
        eq("json int", -3, m.get("b"));
        eq("json roundtrip", "{\"k\":\"q\\\"\\\\\\n\",\"n\":[1,true]}",
                MiniJson.encode(new java.util.LinkedHashMap<String, Object>() {{ put("k", "q\"\\\n"); put("n", Arrays.asList(1, true)); }}));
        boolean threw = false;
        try {
            MiniJson.decode("{\"a\":1} trailing");
        } catch (IllegalArgumentException e) {
            threw = true;
        }
        eq("json rejects trailing", true, threw);
        StringBuilder deep = new StringBuilder();
        for (int i = 0; i < 200; i++) deep.append('[');
        threw = false;
        try {
            MiniJson.decode(deep.toString());
        } catch (IllegalArgumentException e) {
            threw = true;
        }
        eq("json rejects deep nesting", true, threw);
    }

    static void prefs() throws Exception {
        File dir = Files.createTempDirectory("csweb-prefs").toFile();
        HeadlessContext ctx = new HeadlessContext(dir, "test");
        SharedPreferences p = ctx.getSharedPreferences("my/prefs", 0);
        p.edit().putString("s", "v").putInt("i", 7).putLong("l", 1L << 40).putFloat("f", 1.5f)
                .putBoolean("b", true).putStringSet("set", new LinkedHashSet<>(Arrays.asList("x", "y"))).commit();
        eq("file name sanitised", true, new File(dir, "shared_prefs/my_prefs.json").isFile());
        // a fresh process sees the same values with the same types
        java.lang.reflect.Field open = org.csweb.compat.JsonSharedPreferences.class.getDeclaredField("OPEN");
        open.setAccessible(true);
        ((Map<?, ?>) open.get(null)).clear();
        SharedPreferences q = ctx.getSharedPreferences("my/prefs", 0);
        eq("string", "v", q.getString("s", null));
        eq("int", 7, q.getInt("i", 0));
        eq("long", 1L << 40, q.getLong("l", 0));
        eq("float", 1.5f, q.getFloat("f", 0));
        eq("bool", true, q.getBoolean("b", false));
        eq("set", new LinkedHashSet<>(Arrays.asList("x", "y")), q.getStringSet("set", null));
        eq("default", "d", q.getString("missing", "d"));
        boolean threw = false;
        try {
            q.getInt("s", 0);
        } catch (ClassCastException e) {
            threw = true;
        }
        eq("wrong type throws like Android", true, threw);
        q.edit().remove("s").apply();
        eq("removed", false, q.contains("s"));
        String[] seen = new String[1];
        q.registerOnSharedPreferenceChangeListener((sp, key) -> seen[0] = key);
        q.edit().putString("z", "1").apply();
        eq("listener", "z", seen[0]);
    }
}
