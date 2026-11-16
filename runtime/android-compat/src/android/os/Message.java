// SPDX-License-Identifier: GPL-3.0-or-later
package android.os;

/** Android's Message: what, arg1, arg2, obj and a callback, delivered by a Handler on its Looper. */
public final class Message {
    public int what;
    public int arg1;
    public int arg2;
    public Object obj;
    Handler target;
    Runnable callback;
    Object token;
    long when;
    long seq;
    private Bundle data;

    public Message() {}

    public static Message obtain() {
        return new Message();
    }

    public static Message obtain(Handler h) {
        Message m = new Message();
        m.target = h;
        return m;
    }

    public static Message obtain(Handler h, int what) {
        Message m = obtain(h);
        m.what = what;
        return m;
    }

    public static Message obtain(Handler h, int what, Object obj) {
        Message m = obtain(h, what);
        m.obj = obj;
        return m;
    }

    public static Message obtain(Handler h, int what, int arg1, int arg2) {
        Message m = obtain(h, what);
        m.arg1 = arg1;
        m.arg2 = arg2;
        return m;
    }

    public static Message obtain(Handler h, int what, int arg1, int arg2, Object obj) {
        Message m = obtain(h, what, arg1, arg2);
        m.obj = obj;
        return m;
    }

    public static Message obtain(Handler h, Runnable callback) {
        Message m = obtain(h);
        m.callback = callback;
        return m;
    }

    public static Message obtain(Message orig) {
        Message m = new Message();
        m.what = orig.what;
        m.arg1 = orig.arg1;
        m.arg2 = orig.arg2;
        m.obj = orig.obj;
        m.target = orig.target;
        m.callback = orig.callback;
        m.data = orig.data;
        return m;
    }

    public Handler getTarget() {
        return target;
    }

    public void setTarget(Handler target) {
        this.target = target;
    }

    public Runnable getCallback() {
        return callback;
    }

    public long getWhen() {
        return when;
    }

    public Bundle getData() {
        if (data == null) data = new Bundle();
        return data;
    }

    public Bundle peekData() {
        return data;
    }

    public void setData(Bundle data) {
        this.data = data;
    }

    public void sendToTarget() {
        target.sendMessage(this);
    }

    public void recycle() {}

    public void setAsynchronous(boolean async) {}

    public boolean isAsynchronous() {
        return false;
    }

    @Override
    public String toString() {
        return "{ when=" + when + " what=" + what + " target=" + target + " }";
    }
}
