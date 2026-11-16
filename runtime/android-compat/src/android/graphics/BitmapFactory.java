// SPDX-License-Identifier: GPL-3.0-or-later
package android.graphics;

import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;

/**
 * Android's BitmapFactory, headless (see Bitmap): recognises PNG, JPEG, GIF and WebP by their headers
 * and reads the size, without decoding pixels. Anything else gives null, as Android does for data it
 * cannot decode. Options.inJustDecodeBounds fills outWidth/outHeight/outMimeType and returns null.
 */
public final class BitmapFactory {
    /** Images larger than this are refused (null): the bytes travel to the app in a dialog description. */
    static final int MAX_BYTES = 4 << 20;

    private BitmapFactory() {}

    public static class Options {
        public boolean inJustDecodeBounds;
        public int inSampleSize;
        public Bitmap.Config inPreferredConfig = Bitmap.Config.ARGB_8888;
        public boolean inScaled = true;
        public boolean inMutable;
        public int inDensity;
        public int inTargetDensity;
        public int outWidth;
        public int outHeight;
        public String outMimeType;
    }

    public static Bitmap decodeByteArray(byte[] data, int offset, int length) {
        return decodeByteArray(data, offset, length, null);
    }

    public static Bitmap decodeByteArray(byte[] data, int offset, int length, Options opts) {
        if (data == null || offset < 0 || length < 0 || offset > data.length - length) {
            throw new ArrayIndexOutOfBoundsException();
        }
        byte[] bytes = new byte[length];
        System.arraycopy(data, offset, bytes, 0, length);
        return decode(bytes, opts);
    }

    public static Bitmap decodeStream(InputStream is) {
        return decodeStream(is, null, null);
    }

    public static Bitmap decodeStream(InputStream is, Rect outPadding, Options opts) {
        if (is == null) return null;
        try {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[16384];
            for (int n; (n = is.read(buf)) > 0; ) {
                if (out.size() + n > MAX_BYTES) return null;
                out.write(buf, 0, n);
            }
            return decode(out.toByteArray(), opts);
        } catch (IOException e) {
            return null;
        }
    }

    public static Bitmap decodeFile(String pathName) {
        return decodeFile(pathName, null);
    }

    public static Bitmap decodeFile(String pathName, Options opts) {
        try (InputStream in = new FileInputStream(pathName)) {
            return decodeStream(in, null, opts);
        } catch (IOException e) {
            return null;
        }
    }

    static Bitmap decode(byte[] b, Options opts) {
        if (b.length > MAX_BYTES) return null;
        int[] size = new int[2];
        String mime = sniff(b, size);
        if (opts != null) {
            opts.outWidth = mime == null ? -1 : size[0];
            opts.outHeight = mime == null ? -1 : size[1];
            opts.outMimeType = mime;
            if (opts.inJustDecodeBounds) return null;
        }
        if (mime == null || size[0] <= 0 || size[1] <= 0) return null;
        return new Bitmap(b, mime, size[0], size[1]);
    }

    /** The image type and, in size, width and height; null when the bytes are not a known image. */
    static String sniff(byte[] b, int[] size) {
        if (b.length >= 24 && u8(b, 0) == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G'
                && b[12] == 'I' && b[13] == 'H' && b[14] == 'D' && b[15] == 'R') {
            size[0] = be32(b, 16);
            size[1] = be32(b, 20);
            return "image/png";
        }
        if (b.length >= 10 && b[0] == 'G' && b[1] == 'I' && b[2] == 'F' && b[3] == '8') {
            size[0] = u8(b, 6) | u8(b, 7) << 8;
            size[1] = u8(b, 8) | u8(b, 9) << 8;
            return "image/gif";
        }
        if (b.length >= 30 && b[0] == 'R' && b[1] == 'I' && b[2] == 'F' && b[3] == 'F'
                && b[8] == 'W' && b[9] == 'E' && b[10] == 'B' && b[11] == 'P') {
            if (b[12] == 'V' && b[13] == 'P' && b[14] == '8' && b[15] == ' ') {
                size[0] = (u8(b, 26) | u8(b, 27) << 8) & 0x3fff;
                size[1] = (u8(b, 28) | u8(b, 29) << 8) & 0x3fff;
            } else if (b[12] == 'V' && b[13] == 'P' && b[14] == '8' && b[15] == 'L') {
                int bits = u8(b, 21) | u8(b, 22) << 8 | u8(b, 23) << 16 | u8(b, 24) << 24;
                size[0] = (bits & 0x3fff) + 1;
                size[1] = ((bits >>> 14) & 0x3fff) + 1;
            } else if (b[12] == 'V' && b[13] == 'P' && b[14] == '8' && b[15] == 'X') {
                size[0] = (u8(b, 24) | u8(b, 25) << 8 | u8(b, 26) << 16) + 1;
                size[1] = (u8(b, 27) | u8(b, 28) << 8 | u8(b, 29) << 16) + 1;
            } else {
                return null;
            }
            return "image/webp";
        }
        if (b.length >= 4 && u8(b, 0) == 0xFF && u8(b, 1) == 0xD8) {
            // Walk the segments to the first start-of-frame marker (SOF0..SOF15 but DHT, JPG, DAC).
            int i = 2;
            while (i + 9 < b.length) {
                if (u8(b, i) != 0xFF) return null;
                int marker = u8(b, i + 1);
                if (marker == 0xFF) {
                    i++;
                    continue;
                }
                if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
                    i += 2;
                    continue;
                }
                int len = u8(b, i + 2) << 8 | u8(b, i + 3);
                if (len < 2) return null;
                if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
                    size[1] = u8(b, i + 5) << 8 | u8(b, i + 6);
                    size[0] = u8(b, i + 7) << 8 | u8(b, i + 8);
                    return "image/jpeg";
                }
                i += 2 + len;
            }
            return null;
        }
        return null;
    }

    private static int u8(byte[] b, int i) {
        return b[i] & 0xff;
    }

    private static int be32(byte[] b, int i) {
        return u8(b, i) << 24 | u8(b, i + 1) << 16 | u8(b, i + 2) << 8 | u8(b, i + 3);
    }
}
