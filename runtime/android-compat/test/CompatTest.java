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
        base64();
        uri();
        json();
        prefs();
        missingPlatform();
        tlsSessions();  // last: it changes the JVM's provider list
        System.out.println("compat tests: " + passed + " passed, " + failed + " failed");
        if (failed > 0) System.exit(1);
    }

    /** Failures from Android parts the runtime lacks are named; other missing classes are not. */
    static void missingPlatform() {
        // NTV's stream sniffer: withContext(Dispatchers.Main) without android.os.Looper.
        Throwable ntv = new IllegalStateException("Module with the Main dispatcher had failed to initialize",
                new NoClassDefFoundError("Failed resolution of: Landroid/os/Looper;"));
        eq("looper", "android.os.Looper", org.csweb.compat.Unsupported.missingPlatformClass(ntv.getCause()));
        eq("webview", "android.webkit.CookieManager",
                org.csweb.compat.Unsupported.missingPlatformClass(new ClassNotFoundException("android.webkit.CookieManager")));
        eq("feature named", true, org.csweb.compat.Unsupported.missingFeature("android.webkit.WebView").contains("WebView"));
        eq("plugin class is not platform", null,
                org.csweb.compat.Unsupported.missingPlatformClass(new ClassNotFoundException("com.example.Missing")));
        eq("android class we have is not reported", null,
                org.csweb.compat.Unsupported.missingPlatformClass(new NoClassDefFoundError("Failed resolution of: Landroid/widget/Foo;")));
        eq("other errors ignored", null, org.csweb.compat.Unsupported.missingPlatformClass(new RuntimeException("Landroid/os/Looper;")));
    }

    /** TlsSessions: every SSLContext comes from the original provider, with its client session cache off. */
    static void tlsSessions() throws Exception {
        String base = javax.net.ssl.SSLContext.getInstance("TLS").getProvider().getName();
        javax.net.ssl.SSLContext plain = javax.net.ssl.SSLContext.getInstance("TLS");
        plain.init(null, null, null);
        eq("cache on before", true, plain.getClientSessionContext().getSessionCacheSize() != 0);
        eq("wraps the TLS provider", base, org.csweb.compat.TlsSessions.install());
        eq("installed once", base, org.csweb.compat.TlsSessions.install());
        eq("first provider", org.csweb.compat.TlsSessions.PROVIDER, java.security.Security.getProviders()[0].getName());
        javax.net.ssl.SSLContext c = javax.net.ssl.SSLContext.getInstance("TLS");
        eq("context from the wrapper", org.csweb.compat.TlsSessions.PROVIDER, c.getProvider().getName());
        c.init(null, null, null);
        eq("client cache off", 0, c.getClientSessionContext().getSessionCacheSize());
        eq("protocols of the original", java.util.Arrays.asList(plain.getSupportedSSLParameters().getProtocols()),
                java.util.Arrays.asList(c.getSupportedSSLParameters().getProtocols()));
        eq("socket factory works", true, c.getSocketFactory().getDefaultCipherSuites().length > 0);
        eq("TLSv1.3 wrapped too", 0, initialised("TLSv1.3").getClientSessionContext().getSessionCacheSize());
        eq("default context cache off", 0, javax.net.ssl.SSLContext.getDefault().getClientSessionContext().getSessionCacheSize());
    }

    static javax.net.ssl.SSLContext initialised(String protocol) throws Exception {
        javax.net.ssl.SSLContext c = javax.net.ssl.SSLContext.getInstance(protocol);
        c.init(null, null, null);
        return c;
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
