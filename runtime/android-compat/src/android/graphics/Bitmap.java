// SPDX-License-Identifier: GPL-3.0-or-later
package android.graphics;

/**
 * A decoded image, headless: it keeps the encoded bytes it came from (PNG, JPEG, GIF or WebP) and the
 * size read from their header, never pixels. ImageView.setImageBitmap passes those bytes on to the
 * native app through the dialog protocol, which draws them (a plugin's login QR code, issue #38).
 * Bitmaps built from pixels (createBitmap, Canvas) are not supported.
 */
public final class Bitmap {
    public enum Config { ALPHA_8, RGB_565, ARGB_4444, ARGB_8888, RGBA_F16, HARDWARE, RGBA_1010102 }

    public enum CompressFormat { JPEG, PNG, WEBP, WEBP_LOSSY, WEBP_LOSSLESS }

    private final byte[] encoded;
    private final String mime;
    private final int width;
    private final int height;
    private boolean recycled;

    Bitmap(byte[] encoded, String mime, int width, int height) {
        this.encoded = encoded;
        this.mime = mime;
        this.width = width;
        this.height = height;
    }

    public int getWidth() {
        return width;
    }

    public int getHeight() {
        return height;
    }

    public Config getConfig() {
        return Config.ARGB_8888;
    }

    public int getByteCount() {
        return width * height * 4;
    }

    public int getAllocationByteCount() {
        return getByteCount();
    }

    public boolean hasAlpha() {
        return !"image/jpeg".equals(mime);
    }

    public void recycle() {
        recycled = true;
    }

    public boolean isRecycled() {
        return recycled;
    }

    /** Writes the encoded bytes when they are already in `format`; re-encoding is not supported. */
    public boolean compress(CompressFormat format, int quality, java.io.OutputStream stream) {
        String want = format == CompressFormat.PNG ? "image/png" : format == CompressFormat.JPEG ? "image/jpeg" : "image/webp";
        if (!want.equals(mime)) throw org.csweb.compat.Unsupported.of("Bitmap.compress to another format");
        try {
            stream.write(encoded);
            return true;
        } catch (java.io.IOException e) {
            return false;
        }
    }

    public static Bitmap createBitmap(int width, int height, Config config) {
        throw org.csweb.compat.Unsupported.of("Bitmap.createBitmap (no pixel graphics)");
    }

    public static Bitmap createScaledBitmap(Bitmap src, int dstWidth, int dstHeight, boolean filter) {
        return src;  // the app scales images to fit; the encoded bytes stay as they are
    }

    // ---- runtime (not Android API) ---------------------------------------------------------------

    /** The encoded image (a copy), for the native app. */
    public byte[] encodedBytes() {
        return encoded.clone();
    }

    /** "image/png", "image/jpeg", "image/gif" or "image/webp". */
    public String mimeType() {
        return mime;
    }
}
