// SPDX-License-Identifier: GPL-3.0-or-later
// Builds view trees from compiled layout XML. Only the framework views of android.widget that the
// native app can render are created (see VIEWS); any other tag, including custom and AndroidX views,
// is reported as unsupported rather than approximated.
package android.view;

import android.content.Context;
import android.content.res.Resources;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;

import org.csweb.compat.Unsupported;
import org.csweb.compat.res.BinaryXmlParser;
import org.csweb.compat.res.ResourceTable;
import org.csweb.compat.res.TypedValues;
import org.csweb.compat.ui.DialogProtocol;
import org.xmlpull.v1.XmlPullParser;

import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

public class LayoutInflater {
    private static final String ANDROID = "http://schemas.android.com/apk/res/android";
    private static final Set<String> VIEWS = new HashSet<>(Arrays.asList(
            "View", "Space", "LinearLayout", "FrameLayout", "RelativeLayout", "ScrollView", "TextView", "EditText",
            "Button", "CheckBox", "Switch", "ImageView", "ImageButton"));

    /** Types only: AndroidX's FragmentManager builds a Factory2 for fragment tags; nothing installs one here. */
    public interface Factory {
        View onCreateView(String name, Context context, android.util.AttributeSet attrs);
    }

    public interface Factory2 extends Factory {
        View onCreateView(View parent, String name, Context context, android.util.AttributeSet attrs);
    }

    private final Context context;

    protected LayoutInflater(Context context) {
        this.context = context;
    }

    public static LayoutInflater from(Context context) {
        if (context == null) throw new NullPointerException("context");
        return new LayoutInflater(context);
    }

    public Context getContext() {
        return context;
    }

    public LayoutInflater cloneInContext(Context newContext) {
        return new LayoutInflater(newContext);
    }

    public View inflate(int resource, ViewGroup root) {
        return inflate(resource, root, root != null);
    }

    public View inflate(int resource, ViewGroup root, boolean attachToRoot) {
        Resources res = context.getResources();
        return inflate(res.getLayout(resource), root, attachToRoot);
    }

    public View inflate(XmlPullParser parser, ViewGroup root) {
        return inflate(parser, root, root != null);
    }

    public View inflate(XmlPullParser parser, ViewGroup root, boolean attachToRoot) {
        try {
            int type;
            while ((type = parser.next()) != XmlPullParser.START_TAG && type != XmlPullParser.END_DOCUMENT) {
                // skip to the root element
            }
            if (type != XmlPullParser.START_TAG) throw new InflateException(parser.getPositionDescription() + ": No start tag found!");
            if ("merge".equals(parser.getName())) {
                if (root == null || !attachToRoot) {
                    throw new InflateException("<merge /> can be used only with a valid ViewGroup root and attachToRoot=true");
                }
                inflateChildren(parser, root);
                return root;
            }
            View view = createView(parser);
            if (view instanceof ViewGroup) inflateChildren(parser, (ViewGroup) view);
            else skipChildren(parser);
            if (root != null && attachToRoot) {
                root.addView(view);
                return root;
            }
            return view;
        } catch (InflateException | Unsupported e) {
            throw e;
        } catch (Exception e) {
            throw new InflateException(parser.getPositionDescription() + ": " + e.getMessage(), e);
        }
    }

    private void inflateChildren(XmlPullParser parser, ViewGroup parent) throws Exception {
        int depth = parser.getDepth();
        int type;
        while (((type = parser.next()) != XmlPullParser.END_TAG || parser.getDepth() > depth) && type != XmlPullParser.END_DOCUMENT) {
            if (type != XmlPullParser.START_TAG) continue;
            String name = parser.getName();
            if ("requestFocus".equals(name) || "tag".equals(name)) {
                skipChildren(parser);
                continue;
            }
            if ("include".equals(name) || "merge".equals(name) || "fragment".equals(name)) {
                throw Unsupported.of("layout <" + name + "> (" + parser.getPositionDescription() + ")");
            }
            View child = createView(parser);
            if (child instanceof ViewGroup) inflateChildren(parser, (ViewGroup) child);
            else skipChildren(parser);
            parent.addView(child);
        }
    }

    private static void skipChildren(XmlPullParser parser) throws Exception {
        int depth = parser.getDepth();
        int type;
        while (((type = parser.next()) != XmlPullParser.END_TAG || parser.getDepth() > depth) && type != XmlPullParser.END_DOCUMENT) {
            if (type == XmlPullParser.START_TAG && parser.getDepth() == depth + 1 && !"requestFocus".equals(parser.getName())) {
                throw new InflateException(parser.getPositionDescription() + ": a " + parser.getName() + " inside a view that is not a ViewGroup");
            }
        }
    }

    private View createView(XmlPullParser parser) {
        String tag = parser.getName();
        if ("view".equals(tag)) tag = parser.getAttributeValue(null, "class");
        String simple = tag == null ? "" : tag;
        if (simple.startsWith("android.widget.") || simple.startsWith("android.view.")) {
            simple = simple.substring(simple.lastIndexOf('.') + 1);
        }
        if (!VIEWS.contains(simple)) throw Unsupported.of("view <" + tag + "> (only framework widgets are rendered on webOS)");
        View v;
        switch (simple) {
            case "LinearLayout": v = new LinearLayout(context); break;
            case "FrameLayout": v = new FrameLayout(context); break;
            case "RelativeLayout": v = new RelativeLayout(context); break;
            case "ImageView": v = new ImageView(context); break;
            case "ImageButton": v = new ImageButton(context); break;
            case "ScrollView": v = new ScrollView(context); break;
            case "TextView": v = new TextView(context); break;
            case "EditText": v = new EditText(context); break;
            case "Button": v = new Button(context); break;
            case "CheckBox": v = new CheckBox(context); break;
            case "Switch": v = new Switch(context); break;
            default: v = new View(context);
        }
        for (int i = 0; i < parser.getAttributeCount(); i++) {
            if (!ANDROID.equals(parser.getAttributeNamespace(i))) continue;
            apply(v, parser, i);
        }
        return v;
    }

    private void apply(View v, XmlPullParser parser, int i) {
        String name = parser.getAttributeName(i);
        switch (name) {
            case "id": {
                int id = parser instanceof android.util.AttributeSet
                        ? ((android.util.AttributeSet) parser).getAttributeResourceValue(i, View.NO_ID) : View.NO_ID;
                v.setId(id);
                if (parser instanceof BinaryXmlParser && ((BinaryXmlParser) parser).resourceTable() != null) {
                    DialogProtocol.nameView(v, ((BinaryXmlParser) parser).resourceTable().entryName(id));
                }
                break;
            }
            case "text":
                if (v instanceof TextView) ((TextView) v).setText(text(parser, i));
                break;
            case "contentDescription":
                v.setContentDescription(text(parser, i));
                break;
            case "hint":
                if (v instanceof TextView) ((TextView) v).setHint(text(parser, i));
                break;
            case "inputType":
                if (v instanceof TextView) ((TextView) v).setInputType(integer(parser, i, 0));
                break;
            case "checked":
                if (v instanceof CompoundButton) ((CompoundButton) v).setChecked("true".equals(String.valueOf(text(parser, i))));
                break;
            case "orientation":
                if (v instanceof LinearLayout) ((LinearLayout) v).setOrientation(integer(parser, i, LinearLayout.HORIZONTAL));
                break;
            case "visibility": {
                int x = integer(parser, i, 0);  // XML enum: visible=0, invisible=1, gone=2
                v.setVisibility(x == 2 ? View.GONE : x == 1 ? View.INVISIBLE : View.VISIBLE);
                break;
            }
            case "enabled":
                v.setEnabled(!"false".equals(String.valueOf(text(parser, i))));
                break;
            default:
                break;  // layout and styling attributes: the native app lays dialogs out itself
        }
    }

    /** An attribute's text with resource references resolved against the plugin's table, or its context. */
    private CharSequence text(XmlPullParser parser, int i) {
        if (parser instanceof BinaryXmlParser) {
            ResourceTable.Value v = ((BinaryXmlParser) parser).getAttributeTypedValue(i);
            if (v.type == TypedValues.TYPE_STRING) return v.string;
            if (v.type == TypedValues.TYPE_REFERENCE) {
                try {
                    return context.getText(v.data);
                } catch (Resources.NotFoundException e) {
                    throw new InflateException(parser.getPositionDescription() + ": unresolved reference @0x" + Integer.toHexString(v.data), e);
                }
            }
            return TypedValues.coerceToString(v.type, v.data, v.string);
        }
        return parser.getAttributeValue(i);
    }

    private static int integer(XmlPullParser parser, int i, int def) {
        if (parser instanceof BinaryXmlParser) {
            ResourceTable.Value v = ((BinaryXmlParser) parser).getAttributeTypedValue(i);
            return TypedValues.isInt(v.type) ? v.data : def;
        }
        String s = parser.getAttributeValue(i);
        try {
            return s.startsWith("0x") ? (int) Long.parseLong(s.substring(2), 16) : Integer.parseInt(s);
        } catch (RuntimeException e) {
            return def;
        }
    }
}
