// SPDX-License-Identifier: GPL-3.0-or-later
package android.os;

/** Android's clocks. Both count from the runtime's start, as they count from boot on Android. */
public final class SystemClock {
    private static final long START = System.nanoTime();

    private SystemClock() {}

    public static long uptimeMillis() {
        return (System.nanoTime() - START) / 1_000_000L;
    }

    public static long elapsedRealtime() {
        return uptimeMillis();
    }

    public static long elapsedRealtimeNanos() {
        return System.nanoTime() - START;
    }

    public static long currentThreadTimeMillis() {
        return uptimeMillis();
    }

    public static void sleep(long ms) {
        long end = uptimeMillis() + ms;
        boolean interrupted = false;
        for (long left = ms; left > 0; left = end - uptimeMillis()) {
            try {
                Thread.sleep(left);
            } catch (InterruptedException e) {
                interrupted = true;  // as Android's: keep sleeping, keep the interrupt
            }
        }
        if (interrupted) Thread.currentThread().interrupt();
    }
}
