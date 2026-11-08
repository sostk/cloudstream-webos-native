// SPDX-License-Identifier: GPL-3.0-or-later
// android.content.res.Resources. getSystem() describes the TV UI plane and has no resource table.
// Resources(AssetManager, DisplayMetrics, Configuration) is how CloudStream gives a plugin with
// requiresResources its own resources: the values come from the plugin's resources.arsc and res/ files.
// Drawables need a graphics stack and are reported unsupported.
package android.content.res;

import android.util.DisplayMetrics;

import org.csweb.compat.res.BinaryXmlParser;
import org.csweb.compat.res.ResourceTable;
import org.csweb.compat.res.TypedValues;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;

public class Resources {
    public static class NotFoundException extends RuntimeException {
        private static final long serialVersionUID = 1L;

        public NotFoundException() {}

        public NotFoundException(String name) {
            super(name);
        }
    }

    /** Themes carry no attributes in the headless runtime; methods taking one ignore it. */
    public final class Theme {
        public void applyStyle(int resId, boolean force) {}

        public Resources getResources() {
            return Resources.this;
        }
    }

    private static final Resources SYSTEM = new Resources(null, null, null, true);
    private static final int ATTR_OTHER = 0x01000004, ATTR_ONE = 0x01000005;

    private final DisplayMetrics metrics = new DisplayMetrics();
    private final Configuration configuration = new Configuration();
    private final AssetManager assets;
    private final ResourceTable table;

    private Resources(AssetManager assets, DisplayMetrics m, Configuration c, boolean system) {
        metrics.setToDefaults();
        configuration.setToDefaults();
        this.assets = assets;
        this.table = assets == null ? null : assets.resourceTable();
    }

    public Resources(AssetManager assets, DisplayMetrics metrics, Configuration config) {
        this(assets, metrics, config, false);
        if (metrics != null) this.metrics.setTo(metrics);
        if (assets == null || table == null) throw new NotFoundException("AssetManager without a resource table");
    }

    public static Resources getSystem() {
        return SYSTEM;
    }

    public final AssetManager getAssets() {
        return assets;
    }

    public DisplayMetrics getDisplayMetrics() {
        return metrics;
    }

    public Configuration getConfiguration() {
        return configuration;
    }

    public final Theme newTheme() {
        return new Theme();
    }

    // ---- names and identifiers -------------------------------------------------------------------

    /** Accepts "name", "type/name" and "package:type/name", like Android; 0 when not found. */
    public int getIdentifier(String name, String defType, String defPackage) {
        if (table == null || name == null) return 0;
        String pkg = defPackage, type = defType, entry = name;
        int colon = entry.indexOf(':');
        if (colon >= 0) {
            pkg = entry.substring(0, colon);
            entry = entry.substring(colon + 1);
        }
        int slash = entry.indexOf('/');
        if (slash >= 0) {
            type = entry.substring(0, slash);
            entry = entry.substring(slash + 1);
        }
        if (type == null || (pkg != null && !pkg.equals(table.packageName()))) return 0;
        return table.identifier(type, entry);
    }

    public String getResourceName(int id) {
        return getResourcePackageName(id) + ":" + getResourceTypeName(id) + "/" + getResourceEntryName(id);
    }

    public String getResourcePackageName(int id) {
        require(id);
        return table.packageName();
    }

    public String getResourceTypeName(int id) {
        require(id);
        return table.typeName(id);
    }

    public String getResourceEntryName(int id) {
        require(id);
        return table.entryName(id);
    }

    // ---- values ----------------------------------------------------------------------------------

    private void require(int id) {
        if (table == null || table.typeName(id) == null) throw notFound(id);
    }

    private static NotFoundException notFound(int id) {
        return new NotFoundException("Resource ID #0x" + Integer.toHexString(id));
    }

    private ResourceTable.Value value(int id) {
        ResourceTable.Value v = table == null ? null : table.value(id);
        if (v == null) throw notFound(id);
        return v;
    }

    public CharSequence getText(int id) {
        ResourceTable.Value v = value(id);
        if (v.type == TypedValues.TYPE_STRING) return v.string;
        throw new NotFoundException("Resource ID #0x" + Integer.toHexString(id) + " type #0x" + Integer.toHexString(v.type) + " is not valid");
    }

    public CharSequence getText(int id, CharSequence def) {
        try {
            return getText(id);
        } catch (NotFoundException e) {
            return def;
        }
    }

    public String getString(int id) {
        return getText(id).toString();
    }

    public String getString(int id, Object... formatArgs) {
        return String.format(Locale.getDefault(), getString(id), formatArgs);
    }

    public String getQuantityString(int id, int quantity) {
        ResourceTable.Bag bag = bag(id);
        ResourceTable.Value v = bag.items.get(quantity == 1 && bag.items.containsKey(ATTR_ONE) ? ATTR_ONE : ATTR_OTHER);
        v = table.resolve(v);
        if (v == null || v.type != TypedValues.TYPE_STRING) throw notFound(id);
        return v.string;
    }

    public String getQuantityString(int id, int quantity, Object... formatArgs) {
        return String.format(Locale.getDefault(), getQuantityString(id, quantity), formatArgs);
    }

    private ResourceTable.Bag bag(int id) {
        ResourceTable.Bag b = table == null ? null : table.bag(id);
        if (b == null) throw notFound(id);
        return b;
    }

    private List<ResourceTable.Value> arrayItems(int id) {
        List<ResourceTable.Value> out = new ArrayList<>();
        for (Map.Entry<Integer, ResourceTable.Value> e : bag(id).items.entrySet()) out.add(table.resolve(e.getValue()));
        return out;
    }

    public String[] getStringArray(int id) {
        List<ResourceTable.Value> items = arrayItems(id);
        String[] out = new String[items.size()];
        for (int i = 0; i < out.length; i++) {
            ResourceTable.Value v = items.get(i);
            out[i] = v.type == TypedValues.TYPE_STRING ? v.string : TypedValues.coerceToString(v.type, v.data, v.string);
        }
        return out;
    }

    public CharSequence[] getTextArray(int id) {
        return getStringArray(id);
    }

    public int[] getIntArray(int id) {
        List<ResourceTable.Value> items = arrayItems(id);
        int[] out = new int[items.size()];
        for (int i = 0; i < out.length; i++) out[i] = items.get(i).data;
        return out;
    }

    public int getColor(int id) {
        ResourceTable.Value v = value(id);
        if (TypedValues.isColor(v.type) || TypedValues.isInt(v.type)) return v.data;
        if (v.type == TypedValues.TYPE_STRING) throw org.csweb.compat.Unsupported.of("color state list resources");
        throw new NotFoundException("Resource ID #0x" + Integer.toHexString(id) + " is not a color");
    }

    public int getColor(int id, Theme theme) {
        return getColor(id);
    }

    public float getDimension(int id) {
        ResourceTable.Value v = value(id);
        if (v.type != TypedValues.TYPE_DIMENSION) throw new NotFoundException("Resource ID #0x" + Integer.toHexString(id) + " is not a dimension");
        return TypedValues.complexToDimension(v.data, metrics);
    }

    public int getDimensionPixelSize(int id) {
        ResourceTable.Value v = value(id);
        if (v.type != TypedValues.TYPE_DIMENSION) throw new NotFoundException("Resource ID #0x" + Integer.toHexString(id) + " is not a dimension");
        return TypedValues.complexToDimensionPixelSize(v.data, metrics);
    }

    public int getDimensionPixelOffset(int id) {
        return (int) getDimension(id);
    }

    public int getInteger(int id) {
        ResourceTable.Value v = value(id);
        if (!TypedValues.isInt(v.type)) throw new NotFoundException("Resource ID #0x" + Integer.toHexString(id) + " is not an integer");
        return v.data;
    }

    public boolean getBoolean(int id) {
        ResourceTable.Value v = value(id);
        if (v.type != TypedValues.TYPE_INT_BOOLEAN) throw new NotFoundException("Resource ID #0x" + Integer.toHexString(id) + " is not a boolean");
        return v.data != 0;
    }

    public float getFraction(int id, int base, int pbase) {
        ResourceTable.Value v = value(id);
        if (v.type != TypedValues.TYPE_FRACTION) throw new NotFoundException("Resource ID #0x" + Integer.toHexString(id) + " is not a fraction");
        float f = TypedValues.complexToFloat(v.data);
        return (v.data & 0xf) == 0 ? f * base : f * pbase;
    }

    // ---- files -----------------------------------------------------------------------------------

    private byte[] file(int id) {
        ResourceTable.Value v = value(id);
        if (v.type != TypedValues.TYPE_STRING || v.string == null || !v.string.startsWith("res/")) {
            throw new NotFoundException("Resource ID #0x" + Integer.toHexString(id) + " is not a file");
        }
        try {
            byte[] b = assets.readEntry(v.string);
            if (b == null) throw new NotFoundException("File " + v.string + " from resource ID #0x" + Integer.toHexString(id));
            return b;
        } catch (IOException e) {
            throw new NotFoundException("File " + v.string + ": " + e.getMessage());
        }
    }

    public XmlResourceParser getXml(int id) {
        return new BinaryXmlParser(file(id), table);
    }

    public XmlResourceParser getLayout(int id) {
        return getXml(id);
    }

    public XmlResourceParser getAnimation(int id) {
        return getXml(id);
    }

    public InputStream openRawResource(int id) {
        return new ByteArrayInputStream(file(id));
    }

    /**
     * A drawable resource as a named placeholder: there is no graphics stack, and plugin UI only hands
     * drawables to views (backgrounds, icons), which the native app does not draw. Unknown ids and
     * resources of other types throw NotFoundException, as on Android.
     */
    public android.graphics.drawable.Drawable getDrawable(int id) {
        require(id);
        String type = table.typeName(id);
        if (!"drawable".equals(type) && !"mipmap".equals(type) && !"color".equals(type)) {
            throw new NotFoundException("Resource ID #0x" + Integer.toHexString(id) + " is a " + type + ", not a drawable");
        }
        return new org.csweb.compat.res.ResourceDrawable(type + "/" + table.entryName(id));
    }

    public android.graphics.drawable.Drawable getDrawable(int id, Theme theme) {
        return getDrawable(id);
    }
}
