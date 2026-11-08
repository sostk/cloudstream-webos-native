// SPDX-License-Identifier: GPL-3.0-or-later
package org.csweb.loader;

import com.lagradost.cloudstream3.APIHolder;
import com.lagradost.cloudstream3.MainAPI;
import com.lagradost.cloudstream3.MainAPIKt;
import com.lagradost.cloudstream3.plugins.BasePlugin;
import com.lagradost.cloudstream3.plugins.Plugin;

import dalvik.system.PathClassLoader;

import org.csweb.compat.HeadlessActivities;
import org.csweb.compat.HeadlessContext;
import org.csweb.compat.MiniJson;
import org.csweb.compat.Unsupported;
import org.csweb.compat.ui.DialogProtocol;
import org.csweb.compat.ui.UiHost;

import java.io.BufferedReader;
import java.io.InputStreamReader;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.PrintStream;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import kotlin.Unit;
import kotlin.coroutines.EmptyCoroutineContext;
import kotlinx.coroutines.BuildersKt;

/**
 * Loads one unmodified CloudStream plugin (.cs3) the way CloudStream's PluginManager.loadPlugin does,
 * runs one provider operation, and reports on stdout as JSON lines:
 *
 *   csloader --plugin F.cs3 --data DIR --op load-only|search|mainpage|details|links|settings [--query Q]
 *            [--url U] [--provider NAME] [--timeout SECONDS] [--limit N] [--sha256 HEX] [--ui stdin]
 *
 * Events: plugin, provider, result, link, subtitle, dialog, toast, unsupported, error, done. Diagnostics
 * (plugin logging) go to stderr. With --ui stdin, every dialog the plugin shows is described in a
 * "dialog" event and the runtime waits for the app's answer as one JSON line on stdin
 * (org.csweb.compat.ui.DialogProtocol); without it dialogs are reported and left unanswered.
 *
 *   csloader --serve 1 --data DIR      the long-lived service instead (see Service)
 *
 * The process ends with Runtime.halt, because ART's shutdown thread aborts after main returns in this
 * runtime build.
 */
public final class Main {
    private static final PrintStream OUT = System.out;
    static final Map<String, String> args = new LinkedHashMap<>();
    static HeadlessContext appContext;
    /** The service request the current thread works for; its id tags every event the thread emits. */
    static final ThreadLocal<String> REQUEST = new ThreadLocal<>();
    /** The coroutine of each running service request, for cancellation. */
    static final Map<String, kotlinx.coroutines.Job> JOBS = new java.util.concurrent.ConcurrentHashMap<>();
    /** Service requests that already reported done (cancelled or timed out) while their thread still runs. */
    static final java.util.Set<String> ENDED = java.util.concurrent.ConcurrentHashMap.newKeySet();

    /** Where dialog answers come from; null when no app is attached (dialogs are reported only). */
    interface Answers {
        /** The next answer for dialog {@code id}, or null when the app closed the channel. */
        Map<String, Object> next(int id) throws Exception;
    }

    static Answers answers;

    public static void main(String[] argv) {
        long start = System.currentTimeMillis();
        int code = 1;
        try {
            for (int i = 0; i + 1 < argv.length; i += 2) {
                if (!argv[i].startsWith("--")) throw new IllegalArgumentException("bad argument " + argv[i]);
                args.put(argv[i].substring(2), argv[i + 1]);
            }
            if (args.containsKey("serve")) {
                Service.run(args);  // never returns
            }
            if ("stdin".equals(args.get("ui"))) answers = Main::readAnswer;
            startWatchdog(Integer.parseInt(args.getOrDefault("timeout", "120")));
            // As on Android an uncaught exception on any thread ends the process; report it first.
            Thread.setDefaultUncaughtExceptionHandler((thread, t) -> {
                error("thread:" + thread.getName(), t);
                emit(event("done", "ok", false));
                Runtime.getRuntime().halt(4);
            });
            UiHost.install(new LoaderUi());
            code = run() ? 0 : 2;
        } catch (Throwable t) {
            error("startup", t);
        }
        emit(event("done", "ok", code == 0, "elapsed_ms", System.currentTimeMillis() - start));
        OUT.flush();
        System.err.flush();
        Runtime.getRuntime().halt(code);
    }

    private static boolean run() throws Exception {
        File file = new File(required("plugin"));
        File data = new File(required("data"));
        String op = args.getOrDefault("op", "load-only");
        String hashBefore = sha256(file);
        String expected = args.get("sha256");
        if (expected != null && !expected.equalsIgnoreCase(hashBefore)) {
            // A supplied hash proves only that the bytes are the ones the caller vetted, not who made them.
            emit(event("error", "stage", "integrity", "type", "HashMismatch", "message",
                    "plugin SHA-256 " + hashBefore + " does not match the supplied " + expected));
            return false;
        }

        initApp();
        // --- PluginManager.loadPlugin, step by step -------------------------------------------------
        BasePlugin plugin = loadPlugin(file, data, hashBefore, null);
        if (plugin == null) return false;
        if (!answerDialogs("load")) return false;
        if (op.equals("settings")) {
            boolean ok = settings(plugin);
            if (!sha256(file).equals(hashBefore)) {
                emit(event("error", "stage", "integrity", "type", "PluginChanged", "message", "plugin file hash changed"));
                return false;
            }
            return ok;
        }

        List<MainAPI> providers = new ArrayList<>();
        for (MainAPI api : APIHolder.INSTANCE.getAllProviders()) {
            if (file.getAbsolutePath().equals(api.getSourcePlugin())) providers.add(api);
        }
        for (MainAPI api : providers) {
            emit(event("provider", "name", api.getName(), "mainUrl", api.getMainUrl(), "lang", api.getLang(),
                    "hasMainPage", api.getHasMainPage(), "hasQuickSearch", api.getHasQuickSearch(),
                    "supportedTypes", String.valueOf(api.getSupportedTypes())));
        }
        if (providers.isEmpty()) {
            emit(event("error", "stage", "register", "type", "NoProvider", "message", "plugin registered no MainAPI"));
            return false;
        }
        MainAPI api = providers.get(0);
        if (args.containsKey("provider")) {
            api = null;
            for (MainAPI p : providers) if (p.getName().equals(args.get("provider"))) api = p;
            if (api == null) throw new IllegalArgumentException("no provider named " + args.get("provider"));
        }

        boolean ok = op.equals("load-only") || operation(api, op, args);
        ok &= answerDialogs("operation:" + op);
        String hashAfter = sha256(file);
        if (!hashAfter.equals(hashBefore)) {
            emit(event("error", "stage", "integrity", "type", "PluginChanged", "message", "plugin file hash changed"));
            return false;
        }
        return ok;
    }

    /**
     * The parts of CommonActivity.init that plugins rely on: NewPipe gets CloudStream's OkHttp downloader
     * (YouTube plugins search and load through NewPipe). Reflection, as these classes are not on the
     * loader's compile classpath.
     */
    static void initApp() {
        try {
            Class<?> impl = Class.forName("com.lagradost.cloudstream3.DownloaderTestImpl");
            Object companion = impl.getField("Companion").get(null);
            Object downloader = companion.getClass().getMethod("getInstance").invoke(companion);
            Class<?> type = Class.forName("org.schabi.newpipe.extractor.downloader.Downloader");
            Class.forName("org.schabi.newpipe.extractor.NewPipe").getMethod("init", type).invoke(null, downloader);
        } catch (Throwable t) {
            error("init:newpipe", t);
        }
    }

    /**
     * One pass of CloudStream's loadPlugin. Plugins get the headless application context, as plugins that
     * check `context is Activity` before showing UI must not see an Activity. A plugin whose load() casts the
     * context to an Activity type is unloaded the way PluginManager.unloadPlugin does it and loaded again,
     * in a fresh class loader, with a headless instance of exactly that type (CloudStream passes its
     * MainActivity on Android).
     */
    static BasePlugin loadPlugin(File file, File data, String hash, Class<?> activityType) throws Exception {
        PathClassLoader loader = new PathClassLoader(file.getAbsolutePath(), Main.class.getClassLoader());
        String manifestText;
        try (InputStream in = loader.getResourceAsStream("manifest.json")) {
            if (in == null) {
                emit(event("error", "stage", "manifest", "type", "MissingManifest", "message", "No manifest found"));
                return null;
            }
            manifestText = new String(readAll(in, 64 * 1024), StandardCharsets.UTF_8);
        }
        BasePlugin.Manifest manifest = MainAPIKt.getMapper().readValue(manifestText, BasePlugin.Manifest.class);
        Class<?> pluginClass = loader.loadClass(manifest.getPluginClassName());
        HeadlessContext app = new HeadlessContext(data, "com.lagradost.cloudstream3");
        appContext = app;
        // A plugin that needs an Activity also finds it where CloudStream keeps one (CommonActivity.activity,
        // set by MainActivity), from its constructor on: PlayFy reads its SharedPreferences through it there.
        // Only for this load; plugins that run without an Activity keep seeing none.
        android.app.Activity activity = activityType == null ? null : HeadlessActivities.create(activityType, app);
        Object previousActivity = activity == null ? null : commonActivity(new java.lang.ref.WeakReference<>(activity));
        try {
            return loadPluginWith(file, data, hash, activityType, loader, manifest, pluginClass, app, activity);
        } finally {
            if (activity != null) commonActivity(previousActivity);
        }
    }

    private static BasePlugin loadPluginWith(File file, File data, String hash, Class<?> activityType, ClassLoader loader,
            BasePlugin.Manifest manifest, Class<?> pluginClass, HeadlessContext app, android.app.Activity activity)
            throws Exception {
        BasePlugin plugin = (BasePlugin) pluginClass.getDeclaredConstructor().newInstance();
        plugin.setFilename(file.getAbsolutePath());
        if (activityType == null) {
            emit(event("plugin", "name", manifest.getName(), "version", manifest.getVersion(),
                    "class", manifest.getPluginClassName(), "base", plugin instanceof Plugin ? "Plugin" : "BasePlugin",
                    "requiresResources", manifest.getRequiresResources(), "sha256", hash));
        }
        if (manifest.getRequiresResources()) {
            // PluginManager.loadPlugin: AssetManager by reflection, addAssetPath(<.cs3>), then Resources over it.
            try {
                android.content.res.AssetManager assets =
                        android.content.res.AssetManager.class.getDeclaredConstructor().newInstance();
                java.lang.reflect.Method addAssetPath =
                        android.content.res.AssetManager.class.getMethod("addAssetPath", String.class);
                addAssetPath.invoke(assets, file.getAbsolutePath());
                if (plugin instanceof Plugin) {
                    ((Plugin) plugin).setResources(new android.content.res.Resources(assets,
                            app.getResources().getDisplayMetrics(), app.getResources().getConfiguration()));
                }
                if (activityType == null) {
                    emit(event("resources", "package", assets.resourceTable() == null ? null : assets.resourceTable().packageName()));
                }
            } catch (Throwable t) {
                error("resources", t);
                return null;
            }
        }
        try {
            if (plugin instanceof Plugin) {
                ((Plugin) plugin).load(activity == null ? app : activity);
            } else {
                plugin.load();
            }
        } catch (ClassCastException e) {
            Class<?> wanted = activityTypeFrom(e, loader);
            if (activityType != null || wanted == null) {
                error("lifecycle", e);
                return null;
            }
            unload(plugin);
            emit(event("compat", "context", "headless-activity", "type", wanted.getName(),
                    "reason", "load(Context) casts its context to an Activity"));
            return loadPlugin(file, data, hash, wanted);
        } catch (Throwable t) {
            error("lifecycle", t);
            return null;
        }
        if (plugin instanceof Plugin && ((Plugin) plugin).getOpenSettings() != null) {
            emit(event("settings", "available", true));
        }
        return plugin;
    }

    /**
     * Replaces CloudStream's CommonActivity.activity (the static WeakReference `_activity` behind
     * CommonActivity.getActivity()) with `reference` and returns the field's previous value. The plugin
     * keeps its own strong reference to the activity it was given.
     */
    private static Object commonActivity(Object reference) {
        try {
            java.lang.reflect.Field f = com.lagradost.cloudstream3.CommonActivity.class.getDeclaredField("_activity");
            f.setAccessible(true);
            Object previous = f.get(null);
            f.set(null, reference);
            return previous;
        } catch (ReflectiveOperationException | LinkageError e) {
            System.err.println("csweb: cannot set CommonActivity.activity: " + e);
            return null;
        }
    }

    /** The Activity class named by "X cannot be cast to <class>", if it is one. */
    private static Class<?> activityTypeFrom(ClassCastException e, ClassLoader loader) {
        String m = String.valueOf(e.getMessage());
        int at = m.indexOf("cannot be cast to ");
        if (at < 0 || !m.startsWith(HeadlessContext.class.getName())) return null;
        try {
            Class<?> c = Class.forName(m.substring(at + "cannot be cast to ".length()).trim(), false, loader);
            return android.app.Activity.class.isAssignableFrom(c) ? c : null;
        } catch (ClassNotFoundException notFound) {
            return null;
        }
    }

    /** PluginManager.unloadPlugin: beforeUnload, then drop the APIs and extractors the plugin registered. */
    static void unload(BasePlugin plugin) {
        try {
            plugin.beforeUnload();
        } catch (Throwable t) {
            error("beforeUnload", t);
        }
        String source = plugin.getFilename();
        List<MainAPI> mine = new ArrayList<>();
        for (MainAPI api : APIHolder.INSTANCE.getAllProviders()) if (source.equals(api.getSourcePlugin())) mine.add(api);
        for (MainAPI api : mine) APIHolder.INSTANCE.removePluginMapping(api);
        APIHolder.INSTANCE.getAllProviders().removeAll(mine);
        List<com.lagradost.cloudstream3.utils.ExtractorApi> ext = new ArrayList<>();
        for (com.lagradost.cloudstream3.utils.ExtractorApi x : com.lagradost.cloudstream3.utils.ExtractorApiKt.getExtractorApis()) {
            if (source.equals(x.getSourcePlugin())) ext.add(x);
        }
        com.lagradost.cloudstream3.utils.ExtractorApiKt.getExtractorApis().removeAll(ext);
    }

    // --- plugin UI: settings and dialogs ---------------------------------------------------------

    /**
     * CloudStream's settings button: openSettings(context), then the dialogs it shows. CloudStream passes
     * its Activity; the headless runtime passes the application context, so plugins that need an
     * Activity's fragment manager or custom views fail with an unsupported report.
     */
    static boolean settings(BasePlugin plugin) {
        kotlin.jvm.functions.Function1<android.content.Context, Unit> open =
                plugin instanceof Plugin ? ((Plugin) plugin).getOpenSettings() : null;
        if (open == null) {
            emit(event("unsupported", "api", "plugin has no settings (openSettings is null)", "stage", "settings"));
            return false;
        }
        try {
            open.invoke(appContext);
        } catch (Throwable t) {
            error("settings", t);
            return false;
        }
        return answerDialogs("settings");
    }

    static final class LoaderUi extends UiHost {
        final List<android.app.Dialog> showing = new ArrayList<>();
        final Map<android.app.Dialog, Integer> ids = new java.util.IdentityHashMap<>();
        final Map<android.app.Dialog, String> owners = new java.util.IdentityHashMap<>();
        final java.util.Set<android.app.Dialog> reported =
                java.util.Collections.newSetFromMap(new java.util.IdentityHashMap<android.app.Dialog, Boolean>());
        int next = 1;

        @Override public synchronized void shown(android.app.Dialog d) {
            if (!ids.containsKey(d)) ids.put(d, next++);
            String owner = REQUEST.get();
            if (owner != null) owners.put(d, owner);
            showing.add(d);
        }

        @Override public void dismissed(android.app.Dialog d) {
            Integer id;
            synchronized (this) {
                showing.remove(d);
                id = ids.get(d);
            }
            emit(event("dialog_closed", "dialog", id));
        }

        @Override public void toast(CharSequence text, int duration) {
            emit(event("toast", "text", String.valueOf(text), "long", duration != 0));
        }

        /** The newest showing dialog shown by {@code owner} (null: by the one-shot loader or any thread). */
        synchronized android.app.Dialog top(String owner) {
            for (int i = showing.size() - 1; i >= 0; i--) {
                android.app.Dialog d = showing.get(i);
                if (owner == null || owner.equals(owners.get(d))) return d;
            }
            return null;
        }

        synchronized int id(android.app.Dialog d) {
            return ids.get(d);
        }
    }

    private static BufferedReader uiInput;

    /** One-shot mode (--ui stdin): the next stdin line is the answer to the dialog just described. */
    private static Map<String, Object> readAnswer(int id) throws IOException {
        if (uiInput == null) uiInput = new BufferedReader(new InputStreamReader(System.in, StandardCharsets.UTF_8));
        String line = uiInput.readLine();
        return line == null ? null : parseAnswer(line);
    }

    static Map<String, Object> parseAnswer(String line) {
        if (line.length() > 1 << 20) throw new IllegalArgumentException("answer longer than 1 MiB");
        Object parsed = MiniJson.decode(line);
        if (!(parsed instanceof Map)) throw new IllegalArgumentException("answer is not a JSON object");
        @SuppressWarnings("unchecked") Map<String, Object> answer = (Map<String, Object>) parsed;
        return answer;
    }

    /**
     * Describe the newest dialog the current request showed, wait for the app's answer, apply it; repeat
     * until none is showing. Answers run on this thread (the one that showed the dialog), as dialog
     * callbacks run on the thread that owns the dialog on Android.
     */
    static boolean answerDialogs(String stage) {
        LoaderUi ui = (LoaderUi) UiHost.get("dialogs");
        String owner = REQUEST.get();
        for (int round = 0; round < 200; round++) {
            android.app.Dialog d = ui.top(owner);
            if (d == null) return true;
            int id = ui.id(d);
            if (answers == null) {
                // Without an app attached each dialog is reported once and stays open, unanswered.
                synchronized (ui) {
                    for (android.app.Dialog open : ui.showing) {
                        if (!ui.reported.add(open)) continue;
                        Map<String, Object> o = event("dialog", "dialog", ui.ids.get(open), "stage", stage, "answerable", false);
                        o.putAll(DialogProtocol.describe(open));
                        emit(o);
                    }
                }
                return true;
            }
            try {
                Map<String, Object> m = event("dialog", "dialog", id, "stage", stage);
                m.putAll(DialogProtocol.describe(d));
                emit(m);
                Map<String, Object> answer = answers.next(id);
                if (answer == null) {
                    emit(event("error", "stage", "dialog", "type", "EndOfInput", "message", "the app closed the dialog channel"));
                    return false;
                }
                if (!Integer.valueOf(id).equals(answer.get("dialog"))) {
                    throw new IllegalArgumentException("answer for dialog " + answer.get("dialog") + ", expected " + id);
                }
                DialogProtocol.answer(d, answer);  // answer contents (typed text, passwords) are never logged
            } catch (Throwable t) {
                error("dialog", t);
                return false;
            }
        }
        emit(event("error", "stage", "dialog", "type", "TooManyDialogs", "message", "more than 200 dialog rounds"));
        return false;
    }

    // --- provider operations --------------------------------------------------------------------

    private interface Suspend {
        Object call(kotlin.coroutines.Continuation<? super Object> c) throws Exception;
    }

    private static Object await(Suspend s) throws Exception {
        String id = REQUEST.get();
        return BuildersKt.runBlocking(EmptyCoroutineContext.INSTANCE, (scope, cont) -> {
            if (id != null) JOBS.put(id, kotlinx.coroutines.JobKt.getJob(scope.getCoroutineContext()));
            try {
                return s.call(cont);
            } catch (RuntimeException e) {
                throw e;
            } catch (Exception e) {
                throw new RuntimeException(e);
            }
        });
    }

    static boolean operation(MainAPI api, String op, Map<String, String> p) {
        int limit = Integer.parseInt(p.getOrDefault("limit", "20"));
        try {
            switch (op) {
                case "search": {
                    String q = required(p, "query");
                    Object r = await(c -> api.search(q, 1, c));
                    return items(r, "result", limit);
                }
                case "mainpage": {
                    if (api.getMainPage().isEmpty()) {
                        emit(event("unsupported", "api", api.getName() + ".getMainPage", "stage", "operation"));
                        return false;
                    }
                    // One declared section (index into MainAPI.mainPage, as the provider event lists them), one page.
                    int index = Integer.parseInt(p.getOrDefault("section", "0"));
                    int page = Integer.parseInt(p.getOrDefault("page", "1"));
                    if (index < 0 || index >= api.getMainPage().size()) throw new IllegalArgumentException("no main page section " + index);
                    com.lagradost.cloudstream3.MainPageData first = api.getMainPage().get(index);
                    com.lagradost.cloudstream3.MainPageRequest req = new com.lagradost.cloudstream3.MainPageRequest(
                            first.getName(), first.getData(), first.getHorizontalImages());
                    Object r = await(c -> api.getMainPage(page, req, c));
                    if (r == null) return items(null, "result", limit);
                    boolean any = false;
                    for (Object list : (List<?>) bean(r, "getItems")) {
                        emit(event("section", "name", bean(list, "getName"), "index", index, "page", page,
                                "horizontal", tryBean(list, "isHorizontalImages"), "hasNext", tryBean(r, "getHasNext")));
                        any |= items(bean(list, "getList"), "result", limit);
                    }
                    return any;
                }
                case "details": {
                    String url = required(p, "url");
                    Object r = await(c -> api.load(url, c));
                    if (r == null) return items(null, "details", 1);
                    emit(details(r));
                    Object eps = tryBean(r, "getEpisodes");
                    if (eps instanceof List) items(eps, "episode", limit);
                    if (eps instanceof Map) {  // AnimeLoadResponse: episodes per DubStatus
                        for (Map.Entry<?, ?> e : ((Map<?, ?>) eps).entrySet()) {
                            if (!(e.getValue() instanceof List)) continue;
                            int i = 0;
                            for (Object ep : (List<?>) e.getValue()) {
                                if (i++ >= limit) break;
                                Map<String, Object> m = describe("episode", ep);
                                m.put("dub", String.valueOf(e.getKey()));
                                emit(m);
                            }
                        }
                    }
                    return true;
                }
                case "links": {
                    String data = required(p, "url");
                    int[] n = {0};
                    // Plugins call these callbacks from their own coroutine threads (extractors run in parallel),
                    // where REQUEST is unset: tag the events with this request's id explicitly.
                    final String rid = REQUEST.get();
                    Object r = await(c -> api.loadLinks(data, false,
                            sub -> { emitFor(rid, subtitle(sub, "true".equals(p.get("withHeaders")))); return Unit.INSTANCE; },
                            link -> { n[0]++; emitFor(rid, link(link, "true".equals(p.get("withHeaders")))); return Unit.INSTANCE; }, c));
                    emit(event("links_returned", "value", String.valueOf(r), "count", n[0]));
                    return n[0] > 0;
                }
                default:
                    throw new IllegalArgumentException("unknown op " + op);
            }
        } catch (Throwable t) {
            error("operation:" + op, t);
            return false;
        }
    }

    private static boolean items(Object r, String kind, int limit) {
        Object list = r;
        if (r != null && !(r instanceof List)) list = tryBean(r, "getItems");
        if (!(list instanceof List) || ((List<?>) list).isEmpty()) {
            emit(event("empty", "kind", kind));
            return false;
        }
        int i = 0;
        for (Object o : (List<?>) list) {
            if (i++ >= limit) break;
            emit(describe(kind, o));
        }
        emit(event("count", "kind", kind, "value", ((List<?>) list).size()));
        return true;
    }

    // Plain properties of a result object (SearchResponse, LoadResponse, ExtractorLink, SubtitleFile...).
    private static final String[] PROPS = {"getName", "getUrl", "getApiName", "getType", "getPosterUrl", "getYear",
            "getPlot", "getDataUrl", "getSource", "getQuality", "getReferer", "isM3u8", "isDash", "getLang", "getEpisode",
            "getSeason", "getData", "getDescription", "getRunTime", "getDate", "getScore"};

    /** A LoadResponse: the plain properties plus what the details screen shows. */
    private static Map<String, Object> details(Object r) {
        Map<String, Object> m = describe("details", r);
        for (String p : new String[]{"getBackgroundPosterUrl", "getLogoUrl", "getDuration", "getContentRating",
                "getComingSoon", "getShowStatus"}) {
            Object v = tryBean(r, p);
            if (v != null) m.put(Character.toLowerCase(p.charAt(3)) + p.substring(4), v instanceof Number || v instanceof Boolean ? v : String.valueOf(v));
        }
        Object tags = tryBean(r, "getTags");
        if (tags instanceof List) {
            List<String> t = new ArrayList<>();
            for (Object o : (List<?>) tags) t.add(String.valueOf(o));
            m.put("tags", t);
        }
        Object actors = tryBean(r, "getActors");
        if (actors instanceof List) {
            List<Object> a = new ArrayList<>();
            for (Object o : (List<?>) actors) {
                Object actor = tryBean(o, "getActor");
                if (actor == null) continue;
                Map<String, Object> x = new LinkedHashMap<>();
                x.put("name", tryBean(actor, "getName"));
                x.put("image", tryBean(actor, "getImage"));
                Object role = tryBean(o, "getRoleString");
                if (role == null) role = tryBean(o, "getRole");
                if (role != null) x.put("role", String.valueOf(role));
                a.add(x);
            }
            m.put("actors", a);
        }
        Object recs = tryBean(r, "getRecommendations");
        if (recs instanceof List) {
            List<Object> a = new ArrayList<>();
            for (Object o : (List<?>) recs) {
                Map<String, Object> x = describe("result", o);
                x.remove("event");
                a.add(x);
            }
            m.put("recommendations", a);
        }
        Object trailers = tryBean(r, "getTrailers");
        if (trailers instanceof List) {
            List<Object> a = new ArrayList<>();
            for (Object o : (List<?>) trailers) a.add(tryBean(o, "getExtractorUrl"));
            m.put("trailers", a);
        }
        Object seasons = tryBean(r, "getSeasonNames");
        if (seasons instanceof List) {
            List<Object> a = new ArrayList<>();
            for (Object o : (List<?>) seasons) {
                Map<String, Object> x = new LinkedHashMap<>();
                x.put("season", tryBean(o, "getSeason"));
                x.put("name", tryBean(o, "getName"));
                x.put("displaySeason", tryBean(o, "getDisplaySeason"));
                a.add(x);
            }
            m.put("seasons", a);
        }
        return m;
    }

    /**
     * An ExtractorLink. Header values are needed to play some links (Referer, User-Agent, cookies) and go
     * to the app only when it asks (withHeaders, service mode); the one-shot mode used by the probe logs
     * its output and gets header names only.
     */
    private static Map<String, Object> link(Object l, boolean withHeaders) {
        Map<String, Object> m = describe("link", l);
        Object type = tryBean(l, "getType");
        if (type != null) m.put("linkType", String.valueOf(type));
        Object headers = tryBean(l, "getHeaders");
        if (withHeaders && headers instanceof Map) {
            Map<String, Object> h = new LinkedHashMap<>();
            for (Map.Entry<?, ?> e : ((Map<?, ?>) headers).entrySet()) h.put(String.valueOf(e.getKey()), String.valueOf(e.getValue()));
            m.remove("headerNames");
            m.put("headers", h);
        }
        // DrmExtractorLink: the DRM system and, for ClearKey, the content key (CS3IPlayer builds a local key
        // set from kid/key). Key values and licence parameters are secrets: like header values they go
        // only to the app (withHeaders), never to logged output.
        Object uuid = tryBean(l, "getUuid");
        if (uuid != null) {
            Map<String, Object> drm = new LinkedHashMap<>();
            drm.put("uuid", String.valueOf(uuid));
            Object kty = tryBean(l, "getKty");
            if (kty != null) drm.put("kty", String.valueOf(kty));
            Object license = tryBean(l, "getLicenseUrl");
            if (license != null) drm.put("licenseUrl", String.valueOf(license));
            Object kid = tryBean(l, "getKid");
            Object key = tryBean(l, "getKey");
            if (withHeaders) {
                if (kid != null) drm.put("kid", String.valueOf(kid));
                if (key != null) drm.put("key", String.valueOf(key));
                Object params = tryBean(l, "getKeyRequestParameters");
                if (params instanceof Map) {
                    Map<String, Object> p = new LinkedHashMap<>();
                    for (Map.Entry<?, ?> e : ((Map<?, ?>) params).entrySet()) p.put(String.valueOf(e.getKey()), String.valueOf(e.getValue()));
                    drm.put("keyRequestParameters", p);
                }
            } else {
                drm.put("hasKey", kid != null && key != null);
            }
            m.put("drm", drm);
        }
        // ExtractorLink.audioTracks: separate audio streams the player merges with the video (CS3IPlayer).
        Object tracks = tryBean(l, "getAudioTracks");
        if (tracks instanceof List && !((List<?>) tracks).isEmpty()) {
            List<Object> a = new ArrayList<>();
            for (Object t : (List<?>) tracks) {
                Object url = tryBean(t, "getUrl");
                if (url == null) continue;
                Map<String, Object> x = new LinkedHashMap<>();
                x.put("url", String.valueOf(url));
                Object th = tryBean(t, "getHeaders");
                if (th instanceof Map) {
                    if (withHeaders) {
                        Map<String, Object> h = new LinkedHashMap<>();
                        for (Map.Entry<?, ?> e : ((Map<?, ?>) th).entrySet()) h.put(String.valueOf(e.getKey()), String.valueOf(e.getValue()));
                        x.put("headers", h);
                    } else {
                        x.put("headerNames", new ArrayList<>(((Map<?, ?>) th).keySet()));
                    }
                }
                a.add(x);
            }
            m.put("audioTracks", a);
        }
        return m;
    }

    /** A SubtitleFile; its header values (newSubtitleFile headers) go only to the app, as for links. */
    private static Map<String, Object> subtitle(Object sub, boolean withHeaders) {
        Map<String, Object> m = describe("subtitle", sub);
        Object headers = tryBean(sub, "getHeaders");
        if (withHeaders && headers instanceof Map) {
            Map<String, Object> h = new LinkedHashMap<>();
            for (Map.Entry<?, ?> e : ((Map<?, ?>) headers).entrySet()) h.put(String.valueOf(e.getKey()), String.valueOf(e.getValue()));
            m.remove("headerNames");
            m.put("headers", h);
        }
        return m;
    }

    private static Map<String, Object> describe(String kind, Object o) {
        Map<String, Object> m = event(kind, "class", o.getClass().getName());
        for (String p : PROPS) {
            Object v = tryBean(o, p);
            if (v == null) continue;
            String key = p.startsWith("is") ? p : Character.toLowerCase(p.charAt(3)) + p.substring(4);
            m.put(key, v instanceof Number || v instanceof Boolean ? v : String.valueOf(v));
        }
        Object headers = tryBean(o, "getHeaders");
        if (headers instanceof Map) m.put("headerNames", new ArrayList<>(((Map<?, ?>) headers).keySet()));  // names only
        return m;
    }

    private static Object bean(Object o, String getter) throws Exception {
        Method m = o.getClass().getMethod(getter);
        return m.invoke(o);
    }

    static Object tryBean(Object o, String getter) {
        try {
            return bean(o, getter);
        } catch (Throwable t) {
            return null;
        }
    }

    // --- output -----------------------------------------------------------------------------------

    static Map<String, Object> event(String name, Object... kv) {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("event", name);
        for (int i = 0; i + 1 < kv.length; i += 2) m.put(String.valueOf(kv[i]), kv[i + 1]);
        return m;
    }

    /** emit() for a known request id, from any thread. */
    static void emitFor(String rid, Map<String, Object> m) {
        if (rid != null && !m.containsKey("id")) {
            if (ENDED.contains(rid)) return;
            Map<String, Object> tagged = new LinkedHashMap<>();
            tagged.put("id", rid);
            tagged.putAll(m);
            m = tagged;
        }
        emit(m);
    }

    static synchronized void emit(Map<String, Object> m) {
        String id = REQUEST.get();
        if (id != null && ENDED.contains(id)) return;  // a cancelled request's thread reports nothing after its done
        if (id != null && !m.containsKey("id")) {
            Map<String, Object> tagged = new LinkedHashMap<>();
            tagged.put("id", id);
            tagged.putAll(m);
            m = tagged;
        }
        OUT.println(MiniJson.encode(m));
        OUT.flush();
    }

    static void error(String stage, Throwable t) {
        for (Throwable c = t; c != null; c = c.getCause()) {
            if (c instanceof Unsupported) {
                emit(event("unsupported", "api", c.getMessage(), "stage", stage));
                break;
            }
        }
        Throwable root = t;
        while (root.getCause() != null && root.getCause() != root) root = root.getCause();
        emit(event("error", "stage", stage, "type", t.getClass().getName(), "message", String.valueOf(t.getMessage()),
                "rootType", root.getClass().getName(), "rootMessage", String.valueOf(root.getMessage())));
        t.printStackTrace(System.err);
    }

    private static void startWatchdog(int seconds) {
        Thread w = new Thread(() -> {
            try {
                Thread.sleep(seconds * 1000L);
            } catch (InterruptedException e) {
                return;
            }
            emit(event("error", "stage", "timeout", "type", "Timeout", "message", "no result after " + seconds + " s"));
            emit(event("done", "ok", false));
            Runtime.getRuntime().halt(3);
        }, "csloader-watchdog");
        w.setDaemon(true);
        w.start();
    }

    // --- helpers ----------------------------------------------------------------------------------

    private static String required(String key) {
        return required(args, key);
    }

    static String required(Map<String, String> p, String key) {
        String v = p.get(key);
        if (v == null) throw new IllegalArgumentException("missing --" + key);
        return v;
    }

    static byte[] readAll(InputStream in, int max) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[8192];
        for (int n; (n = in.read(buf)) > 0; ) {
            out.write(buf, 0, n);
            if (out.size() > max) throw new IOException("entry larger than " + max + " bytes");
        }
        return out.toByteArray();
    }

    static String sha256(File f) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        try (InputStream in = new FileInputStream(f)) {
            byte[] buf = new byte[65536];
            for (int n; (n = in.read(buf)) > 0; ) md.update(buf, 0, n);
        }
        StringBuilder sb = new StringBuilder();
        for (byte b : md.digest()) sb.append(String.format("%02x", b));
        return sb.toString();
    }
}
