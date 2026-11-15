// SPDX-License-Identifier: GPL-3.0-or-later
package android.os;

import java.util.PriorityQueue;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.FutureTask;

/**
 * A message loop on one thread, as Android's. The main looper runs on a daemon thread of its own
 * ("csweb-main"), started the first time anyone asks for it: plugin UI (settings dialogs, their
 * listeners), Handler(Looper.getMainLooper()) callbacks and kotlinx's Dispatchers.Main (the APK's
 * AndroidDispatcherFactory, built on Handler) all run there, one at a time, as on Android's UI thread.
 *
 * Other threads may prepare() and loop() their own. Messages run in order of their time
 * (SystemClock.uptimeMillis), then of posting.
 */
public final class Looper {
    private static final ThreadLocal<Looper> MINE = new ThreadLocal<>();
    private static Looper main;

    final Object lock = new Object();
    private final PriorityQueue<Message> queue = new PriorityQueue<>(16, (a, b) -> {
        int c = Long.compare(a.when, b.when);
        return c != 0 ? c : Long.compare(a.seq, b.seq);
    });
    private final Thread thread;
    private long seq;
    private boolean quitting;

    private Looper(Thread thread) {
        this.thread = thread;
    }

    public static void prepare() {
        if (MINE.get() != null) throw new RuntimeException("Only one Looper may be created per thread");
        MINE.set(new Looper(Thread.currentThread()));
    }

    /** Android's apps call this only from the framework; the runtime's main looper starts by itself. */
    public static void prepareMainLooper() {
        throw new IllegalStateException("The main Looper has already been prepared.");
    }

    public static Looper myLooper() {
        return MINE.get();
    }

    public static synchronized Looper getMainLooper() {
        if (main == null) {
            final Looper[] made = new Looper[1];
            final Object ready = new Object();
            Thread t = new Thread(() -> {
                prepare();
                synchronized (ready) {
                    made[0] = myLooper();
                    ready.notifyAll();
                }
                loop();
            }, "csweb-main");
            t.setDaemon(true);
            t.start();
            synchronized (ready) {
                while (made[0] == null) {
                    try {
                        ready.wait();
                    } catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                        throw new IllegalStateException("interrupted while starting the main looper", e);
                    }
                }
            }
            main = made[0];
        }
        return main;
    }

    /** Runs this thread's messages until quit(). A callback that throws is reported and the loop goes on. */
    public static void loop() {
        Looper me = myLooper();
        if (me == null) throw new RuntimeException("No Looper; Looper.prepare() wasn't called on this thread.");
        while (true) {
            Message m = me.next();
            if (m == null) return;
            try {
                m.target.dispatchMessage(m);
            } catch (Throwable t) {
                // On Android an uncaught exception here ends the app; the runtime serves other plugins, so
                // it is reported and the loop continues.
                System.err.println("csweb: uncaught exception on " + me.thread.getName() + ": " + t);
                t.printStackTrace();
            }
        }
    }

    public Thread getThread() {
        return thread;
    }

    public boolean isCurrentThread() {
        return Thread.currentThread() == thread;
    }

    public void quit() {
        synchronized (lock) {
            quitting = true;
            queue.clear();
            lock.notifyAll();
        }
    }

    public void quitSafely() {
        quit();
    }

    public MessageQueue getQueue() {
        return new MessageQueue();
    }

    boolean enqueue(Message m, long when) {
        synchronized (lock) {
            if (quitting) return false;
            m.when = when;
            m.seq = seq++;
            queue.add(m);
            lock.notifyAll();
            return true;
        }
    }

    /** Removes the queued messages `match` accepts; true when one was. */
    boolean remove(java.util.function.Predicate<Message> match) {
        synchronized (lock) {
            return queue.removeIf(match);
        }
    }

    boolean has(java.util.function.Predicate<Message> match) {
        synchronized (lock) {
            for (Message m : queue) if (match.test(m)) return true;
            return false;
        }
    }

    private Message next() {
        synchronized (lock) {
            while (true) {
                if (quitting) return null;
                Message head = queue.peek();
                long now = SystemClock.uptimeMillis();
                if (head != null && head.when <= now) return queue.poll();
                try {
                    if (head == null) lock.wait();
                    else lock.wait(Math.max(1, head.when - now));
                } catch (InterruptedException e) {
                    // the main thread is never interrupted on purpose; keep serving
                }
            }
        }
    }

    // ---- runtime helpers (not Android API) -------------------------------------------------------

    /**
     * Runs `task` on the main looper and returns its result, as the runtime's own code needs to act on
     * plugin UI from another thread. On the main thread itself it runs at once. Exceptions from the task
     * are rethrown unwrapped when unchecked.
     */
    public static <T> T runOnMain(Callable<T> task) throws Exception {
        Looper m = getMainLooper();
        if (m.isCurrentThread()) return task.call();
        FutureTask<T> f = new FutureTask<>(task);
        new Handler(m).post(f);
        try {
            return f.get();
        } catch (ExecutionException e) {
            Throwable c = e.getCause();
            if (c instanceof Exception) throw (Exception) c;
            if (c instanceof Error) throw (Error) c;
            throw e;
        }
    }

    @Override
    public String toString() {
        return "Looper (" + thread.getName() + ", tid " + thread.getId() + ")";
    }
}
