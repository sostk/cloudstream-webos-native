// SPDX-License-Identifier: GPL-3.0-or-later
package android.os;

/** Type only: Looper.getQueue() returns one; idle handlers are not supported. */
public final class MessageQueue {
    MessageQueue() {}

    public interface IdleHandler {
        boolean queueIdle();
    }

    public void addIdleHandler(IdleHandler handler) {
        throw org.csweb.compat.Unsupported.of("MessageQueue.addIdleHandler");
    }

    public void removeIdleHandler(IdleHandler handler) {}
}
