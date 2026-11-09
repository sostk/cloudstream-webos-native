// SPDX-License-Identifier: GPL-3.0-or-later
package org.csweb.loader;

import com.lagradost.cloudstream3.APIHolder;
import com.lagradost.cloudstream3.MainAPI;
import com.lagradost.cloudstream3.plugins.BasePlugin;

import org.csweb.compat.ui.UiHost;

import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * The long-lived runtime: one ART process that keeps plugins loaded and serves JSON requests, one per
 * stdin line, so the app pays the runtime start once (docs/runtime/service-protocol.md).
 *
 *   csloader --serve 1 --data DIR [--workers N]
 *
 * Every event a request causes carries its "id"; each request ends with exactly one "done" event.
 * Ordering: load, unload and shutdown run one at a time, after every request received before them;
 * other requests run concurrently (up to --workers), after every load/unload received before them.
 * Dialog answers ({"op":"answer","dialog":n,...}) are routed to the request that showed the dialog.
 */
final class Service {
    private static final Map<String, BasePlugin> plugins = new ConcurrentHashMap<>();
    private static final Map<String, Running> running = new ConcurrentHashMap<>();
    private static final Map<Integer, BlockingQueue<Map<String, Object>>> answerQueues = new ConcurrentHashMap<>();
    private static volatile boolean inputClosed;
    private static File data;

    private static final class Running {
        final AtomicBoolean finished = new AtomicBoolean();
        volatile Thread thread;
    }

    private Service() {}

    static void run(Map<String, String> args) {
        data = new File(Main.required(args, "data"));
        int workers = Integer.parseInt(args.getOrDefault("workers", "4"));
        // An uncaught exception in a plugin's own thread is reported; the service keeps serving.
        Thread.setDefaultUncaughtExceptionHandler((thread, t) -> Main.error("thread:" + thread.getName(), t));
        UiHost.install(new Main.LoaderUi());
        Main.answers = Service::nextAnswer;
        Main.initApp();
        int loadWorkers = Integer.parseInt(args.getOrDefault("load-workers", "3"));
        ExecutorService admin = Executors.newSingleThreadExecutor(daemon("csweb-admin"));
        ExecutorService ops = Executors.newFixedThreadPool(workers, daemon("csweb-op"));
        // Plugins load in parallel, as CloudStream's PluginManager does (amap): one slow plugin (its own
        // network requests in load()) no longer holds up the others.
        ExecutorService loads = Executors.newFixedThreadPool(loadWorkers, daemon("csweb-load"));
        ScheduledExecutorService timers = Executors.newSingleThreadScheduledExecutor(daemon("csweb-timer"));
        Future<?> lastAdmin = null;
        List<Future<?>> opsSinceAdmin = new ArrayList<>();    // ops and loads an unload/shutdown waits for
        List<Future<?>> loadsSinceAdmin = new ArrayList<>();  // loads an op for a missing provider waits for
        Manifests.warmUp();  // the Jackson mapper, built while plugins load
        Main.emit(Main.event("ready", "protocol", 1));
        BufferedReader in = new BufferedReader(new InputStreamReader(System.in, StandardCharsets.UTF_8));
        while (true) {
            Map<String, Object> req;
            try {
                String line = in.readLine();
                if (line == null) {
                    inputClosed = true;  // dialogs still waiting for an answer end with EndOfInput
                    req = new LinkedHashMap<>();
                    req.put("op", "shutdown");
                    req.put("id", "eof");
                } else if (line.trim().isEmpty()) {
                    continue;
                } else {
                    req = Main.parseAnswer(line);
                }
            } catch (Throwable t) {
                Main.error("request", t);
                continue;
            }
            String op = String.valueOf(req.get("op"));
            if (op.equals("answer")) {
                Object dialog = req.get("dialog");
                if (dialog instanceof Integer) queue((Integer) dialog).add(req);
                else Main.emit(Main.event("error", "stage", "request", "type", "BadAnswer", "message", "answer without a dialog number"));
                continue;
            }
            Object rawId = req.get("id");
            if (!(rawId instanceof String) || ((String) rawId).isEmpty()) {
                Main.emit(Main.event("error", "stage", "request", "type", "BadRequest", "message", "request without a string id"));
                continue;
            }
            String id = (String) rawId;
            if (op.equals("cancel")) {
                cancel(String.valueOf(req.get("target")), "cancelled");
                Main.emit(Main.event("done", "id", id, "ok", true));
                continue;
            }
            if (running.containsKey(id)) {
                Main.emit(Main.event("error", "id", id, "stage", "request", "type", "DuplicateId", "message", "a request with this id is running"));
                continue;
            }
            Map<String, String> p = new LinkedHashMap<>();
            for (Map.Entry<String, Object> e : req.entrySet()) {
                if (e.getValue() != null) p.put(e.getKey(), String.valueOf(e.getValue()));
            }
            Running r = new Running();
            running.put(id, r);
            // Scheduling: unload and shutdown run alone, after everything sent before them. Loads run in
            // parallel with each other and with operations. An operation waits for the loads sent before it
            // only when what it names is not loaded yet (Home's request for its source must not wait for
            // an unrelated plugin's slow load).
            boolean exclusive = op.equals("unload") || op.equals("shutdown");
            if (op.equals("load")) {
                Future<?> barrier = lastAdmin;
                Future<?> f = loads.submit(() -> {
                    if (barrier != null) waitFor(barrier);
                    execute(id, op, p, r);
                });
                opsSinceAdmin.add(f);
                loadsSinceAdmin.add(f);
            } else if (exclusive) {
                List<Future<?>> before = opsSinceAdmin;
                opsSinceAdmin = new ArrayList<>();
                loadsSinceAdmin = new ArrayList<>();
                lastAdmin = admin.submit(() -> {
                    for (Future<?> f : before) waitFor(f);
                    execute(id, op, p, r);
                    if (op.equals("shutdown")) {
                        System.out.flush();
                        Runtime.getRuntime().halt(0);
                    }
                });
                if (op.equals("shutdown")) {
                    while (true) waitFor(lastAdmin);  // halts inside the task
                }
            } else {
                Future<?> barrier = lastAdmin;
                List<Future<?>> loadsBefore = new ArrayList<>(loadsSinceAdmin);
                opsSinceAdmin.add(ops.submit(() -> {
                    if (barrier != null) waitFor(barrier);
                    if (needsPendingLoads(op, p)) {
                        for (Future<?> f : loadsBefore) waitFor(f);
                    }
                    execute(id, op, p, r);
                }));
            }
            int seconds = Integer.parseInt(p.getOrDefault("timeout", op.equals("settings") ? "600" : "120"));
            timers.schedule(() -> cancel(id, "timeout"), seconds, TimeUnit.SECONDS);
        }
    }

    /** Whether `op` names something that may still be loading (then it waits for the loads sent before it). */
    private static boolean needsPendingLoads(String op, Map<String, String> p) {
        switch (op) {
            case "providers":
                return true;  // the complete list
            case "settings":
                return p.get("plugin") == null || !plugins.containsKey(new File(p.get("plugin")).getAbsolutePath());
            case "search":
            case "mainpage":
            case "details":
            case "links":
                try {
                    findProvider(p);
                    return false;
                } catch (RuntimeException notYet) {
                    return true;
                }
            default:
                return false;
        }
    }

    private static void execute(String id, String op, Map<String, String> p, Running r) {
        if (r.finished.get()) {  // cancelled before it started
            Main.ENDED.remove(id);
            return;
        }
        r.thread = Thread.currentThread();
        Main.REQUEST.set(id);
        long start = System.currentTimeMillis();
        boolean ok = false;
        try {
            ok = handle(op, p);
        } catch (Throwable t) {
            Main.error("request:" + op, t);
        } finally {
            Main.JOBS.remove(id);
            Main.REQUEST.remove();
            Main.ENDED.remove(id);
            r.thread = null;
            if (r.finished.compareAndSet(false, true)) {
                running.remove(id);
                Map<String, Object> m = Main.event("done", "id", id, "ok", ok, "elapsed_ms", System.currentTimeMillis() - start);
                Main.emit(m);
            }
            Thread.interrupted();  // a late cancel must not leak into the next request on this thread
        }
    }

    private static boolean handle(String op, Map<String, String> p) throws Exception {
        switch (op) {
            case "load": return load(p);
            case "unload": {
                String path = new File(Main.required(p, "plugin")).getAbsolutePath();
                BasePlugin plugin = plugins.remove(path);
                if (plugin == null) throw new IllegalArgumentException("plugin not loaded: " + path);
                Main.unload(plugin);
                return true;
            }
            case "providers":
                for (MainAPI api : Main.providers()) provider(api);
                return true;
            case "settings": {
                BasePlugin plugin = plugins.get(new File(Main.required(p, "plugin")).getAbsolutePath());
                if (plugin == null) throw new IllegalArgumentException("plugin not loaded: " + p.get("plugin"));
                return Main.settings(plugin);
            }
            case "shutdown":
                return true;
            case "search":
            case "mainpage":
            case "details":
            case "links": {
                MainAPI api = findProvider(p);
                boolean ok = Main.operation(api, op, p);
                return Main.answerDialogs("operation:" + op) && ok;
            }
            default:
                throw new IllegalArgumentException("unknown op " + op);
        }
    }

    private static boolean load(Map<String, String> p) throws Exception {
        File file = new File(Main.required(p, "plugin")).getAbsoluteFile();
        String expected = Main.required(p, "sha256");
        if (plugins.containsKey(file.getPath())) throw new IllegalStateException("plugin already loaded: " + file);
        LoadTiming.begin();
        String hash = Main.sha256(file);
        LoadTiming.mark("integrity");
        if (!expected.equalsIgnoreCase(hash)) {
            Main.emit(Main.event("error", "stage", "integrity", "type", "HashMismatch", "message",
                    "plugin SHA-256 " + hash + " does not match the supplied " + expected));
            return false;
        }
        BasePlugin plugin = Main.loadPlugin(file, data, hash, null, p.get("activity"));
        Map<String, Object> timing = LoadTiming.end();
        if (plugin == null) return false;
        Map<String, Object> timingEvent = Main.event("timing");
        timingEvent.putAll(timing);
        Main.emit(timingEvent);
        plugins.put(file.getPath(), plugin);
        if (!Main.answerDialogs("load")) return false;
        int n = 0;
        for (MainAPI api : Main.providers()) {
            if (file.getPath().equals(api.getSourcePlugin())) {
                provider(api);
                n++;
            }
        }
        Main.emit(Main.event("loaded", "plugin", file.getPath(), "providers", n));  // 0: settings-only plugins
        return true;
    }

    private static void provider(MainAPI api) {
        List<String> sections = new ArrayList<>();
        for (com.lagradost.cloudstream3.MainPageData d : api.getMainPage()) sections.add(d.getName());
        List<String> types = new ArrayList<>();
        for (Object t : api.getSupportedTypes()) types.add(String.valueOf(t));
        Main.emit(Main.event("provider", "name", api.getName(), "plugin", api.getSourcePlugin(), "mainUrl", api.getMainUrl(),
                "lang", api.getLang(), "hasMainPage", api.getHasMainPage(), "hasQuickSearch", api.getHasQuickSearch(),
                "supportedTypes", types, "mainPage", sections,
                // How CloudStream schedules this provider's main page (APIRepository.getMainPage) and
                // what it needs; read by reflection so an older API without a field still loads.
                "sequentialMainPage", Main.tryBean(api, "getSequentialMainPage"),
                "sequentialMainPageDelay", Main.tryBean(api, "getSequentialMainPageDelay"),
                "sequentialMainPageScrollDelay", Main.tryBean(api, "getSequentialMainPageScrollDelay"),
                "mainPageTimeoutMs", Main.tryBean(api, "getGetMainPageTimeoutMs"),
                "usesWebView", Main.tryBean(api, "getUsesWebView"),
                "hasDownloadSupport", Main.tryBean(api, "getHasDownloadSupport"),
                "instantLinkLoading", Main.tryBean(api, "getInstantLinkLoading")));
    }

    /** By provider name (optionally within a plugin), or the plugin's only provider when no name is given. */
    private static MainAPI findProvider(Map<String, String> p) {
        String name = p.get("provider");
        String plugin = p.containsKey("plugin") ? new File(p.get("plugin")).getAbsolutePath() : null;
        if (name == null && plugin == null) throw new IllegalArgumentException("request names neither a provider nor a plugin");
        List<MainAPI> found = new ArrayList<>();
        for (MainAPI api : Main.providers()) {
            if ((name == null || api.getName().equals(name)) && (plugin == null || plugin.equals(api.getSourcePlugin()))) found.add(api);
        }
        if (found.size() == 1 || (name != null && !found.isEmpty())) return found.get(0);
        throw new IllegalArgumentException(found.isEmpty() ? "no loaded provider " + (name != null ? "named " + name : "in " + plugin)
                : plugin + " has " + found.size() + " providers; name one");
    }

    /** Cancel a request: its coroutine is cancelled and its thread interrupted; it ends with done ok=false. */
    private static void cancel(String id, String why) {
        Running r = running.get(id);
        if (r == null || r.finished.get()) return;
        kotlinx.coroutines.Job job = Main.JOBS.get(id);
        if (job != null) job.cancel((java.util.concurrent.CancellationException) null);
        Thread t = r.thread;
        if (t != null) t.interrupt();
        if (r.finished.compareAndSet(false, true)) {
            Main.ENDED.add(id);  // removed when its thread finishes (or skips) the request
            running.remove(id);
            Main.emit(Main.event("error", "id", id, "stage", why, "type", why.equals("timeout") ? "Timeout" : "Cancelled",
                    "message", why.equals("timeout") ? "request did not finish in time" : "cancelled by the app"));
            Main.emit(Main.event("done", "id", id, "ok", false));
        }
    }

    private static BlockingQueue<Map<String, Object>> queue(int dialog) {
        return answerQueues.computeIfAbsent(dialog, k -> new LinkedBlockingQueue<>());
    }

    private static Map<String, Object> nextAnswer(int dialog) throws InterruptedException {
        BlockingQueue<Map<String, Object>> q = queue(dialog);
        while (true) {
            Map<String, Object> a = q.poll(500, TimeUnit.MILLISECONDS);
            if (a != null) return a;
            if (inputClosed) return null;
        }
    }

    private static void waitFor(Future<?> f) {
        try {
            f.get();
        } catch (Exception e) {
            // the request reported its own failure
        }
    }

    private static java.util.concurrent.ThreadFactory daemon(String name) {
        int[] n = {0};
        return r -> {
            Thread t = new Thread(r, name + "-" + (++n[0]));
            t.setDaemon(true);
            return t;
        };
    }
}
