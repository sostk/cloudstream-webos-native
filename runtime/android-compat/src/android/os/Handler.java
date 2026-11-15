// SPDX-License-Identifier: GPL-3.0-or-later
package android.os;

/**
 * Android's Handler: posts runnables and messages to a Looper's thread (see Looper). The no-argument
 * constructors use the calling thread's looper, as on Android, and fail without one.
 */
public class Handler {
    public interface Callback {
        boolean handleMessage(Message msg);
    }

    private final Looper looper;
    private final Callback callback;

    public Handler() {
        this((Callback) null);
    }

    public Handler(Callback callback) {
        Looper l = Looper.myLooper();
        if (l == null) {
            throw new RuntimeException("Can't create handler inside thread " + Thread.currentThread()
                    + " that has not called Looper.prepare()");
        }
        this.looper = l;
        this.callback = callback;
    }

    public Handler(Looper looper) {
        this(looper, null);
    }

    public Handler(Looper looper, Callback callback) {
        if (looper == null) throw new NullPointerException("looper");
        this.looper = looper;
        this.callback = callback;
    }

    /** The hidden (Looper, Callback, async) constructor kotlinx looks up below API 28. */
    public Handler(Looper looper, Callback callback, boolean async) {
        this(looper, callback);
    }

    public static Handler createAsync(Looper looper) {
        return new Handler(looper);
    }

    public static Handler createAsync(Looper looper, Callback callback) {
        return new Handler(looper, callback);
    }

    public final Looper getLooper() {
        return looper;
    }

    public void handleMessage(Message msg) {}

    public void dispatchMessage(Message msg) {
        if (msg.callback != null) {
            msg.callback.run();
        } else if (callback == null || !callback.handleMessage(msg)) {
            handleMessage(msg);
        }
    }

    // ---- runnables -------------------------------------------------------------------------------

    public final boolean post(Runnable r) {
        return postAtTime(r, SystemClock.uptimeMillis());
    }

    public final boolean postAtTime(Runnable r, long uptimeMillis) {
        return enqueue(Message.obtain(this, r), uptimeMillis);
    }

    public final boolean postAtTime(Runnable r, Object token, long uptimeMillis) {
        Message m = Message.obtain(this, r);
        m.token = token;
        return enqueue(m, uptimeMillis);
    }

    public final boolean postDelayed(Runnable r, long delayMillis) {
        return postAtTime(r, SystemClock.uptimeMillis() + Math.max(0, delayMillis));
    }

    public final boolean postDelayed(Runnable r, Object token, long delayMillis) {
        return postAtTime(r, token, SystemClock.uptimeMillis() + Math.max(0, delayMillis));
    }

    public final boolean postAtFrontOfQueue(Runnable r) {
        return enqueue(Message.obtain(this, r), Long.MIN_VALUE);
    }

    public final void removeCallbacks(Runnable r) {
        looper.remove(m -> m.target == this && m.callback == r);
    }

    public final void removeCallbacks(Runnable r, Object token) {
        looper.remove(m -> m.target == this && m.callback == r && (token == null || m.token == token));
    }

    public final boolean hasCallbacks(Runnable r) {
        return looper.has(m -> m.target == this && m.callback == r);
    }

    // ---- messages --------------------------------------------------------------------------------

    public final Message obtainMessage() {
        return Message.obtain(this);
    }

    public final Message obtainMessage(int what) {
        return Message.obtain(this, what);
    }

    public final Message obtainMessage(int what, Object obj) {
        return Message.obtain(this, what, obj);
    }

    public final Message obtainMessage(int what, int arg1, int arg2) {
        return Message.obtain(this, what, arg1, arg2);
    }

    public final Message obtainMessage(int what, int arg1, int arg2, Object obj) {
        return Message.obtain(this, what, arg1, arg2, obj);
    }

    public final boolean sendMessage(Message msg) {
        return sendMessageAtTime(msg, SystemClock.uptimeMillis());
    }

    public final boolean sendMessageDelayed(Message msg, long delayMillis) {
        return sendMessageAtTime(msg, SystemClock.uptimeMillis() + Math.max(0, delayMillis));
    }

    public boolean sendMessageAtTime(Message msg, long uptimeMillis) {
        msg.target = this;
        return enqueue(msg, uptimeMillis);
    }

    public final boolean sendMessageAtFrontOfQueue(Message msg) {
        msg.target = this;
        return enqueue(msg, Long.MIN_VALUE);
    }

    public final boolean sendEmptyMessage(int what) {
        return sendMessage(obtainMessage(what));
    }

    public final boolean sendEmptyMessageDelayed(int what, long delayMillis) {
        return sendMessageDelayed(obtainMessage(what), delayMillis);
    }

    public final boolean sendEmptyMessageAtTime(int what, long uptimeMillis) {
        return sendMessageAtTime(obtainMessage(what), uptimeMillis);
    }

    public final boolean hasMessages(int what) {
        return looper.has(m -> m.target == this && m.callback == null && m.what == what);
    }

    public final boolean hasMessages(int what, Object object) {
        return looper.has(m -> m.target == this && m.callback == null && m.what == what && (object == null || m.obj == object));
    }

    public final void removeMessages(int what) {
        looper.remove(m -> m.target == this && m.callback == null && m.what == what);
    }

    public final void removeMessages(int what, Object object) {
        looper.remove(m -> m.target == this && m.callback == null && m.what == what && (object == null || m.obj == object));
    }

    /** A null token removes every callback and message of this handler. */
    public final void removeCallbacksAndMessages(Object token) {
        looper.remove(m -> m.target == this && (token == null || m.token == token || m.obj == token));
    }

    private boolean enqueue(Message m, long when) {
        m.target = this;
        return looper.enqueue(m, when);
    }

    @Override
    public String toString() {
        return "Handler (" + getClass().getName() + ") {" + Integer.toHexString(System.identityHashCode(this)) + "}";
    }
}
