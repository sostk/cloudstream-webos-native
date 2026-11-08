// SPDX-License-Identifier: GPL-3.0-or-later
package org.csweb.compat.ui;

import android.app.AlertDialog;
import android.app.Dialog;
import android.content.DialogInterface;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.Switch;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.WeakHashMap;

/**
 * The dialog protocol between the runtime and the native app (docs/runtime/dialog-protocol.md).
 *
 * describe() turns a showing dialog into plain maps (title, message, buttons, list items and a view
 * tree of labels, text fields, checkboxes, switches and buttons, each with a stable "key"). The app
 * renders that with its own widgets and answers with a map:
 *
 *   {"dialog": id, "fields": {key: text | boolean}, "click": key, "item": n, "button": "positive" |
 *    "negative" | "neutral" | "cancel"}
 *
 * answer() applies it in that order, as the user's input would arrive on Android: field values first
 * (setText / setChecked, which fires OnCheckedChangeListener), then a click on a view button, then the
 * list pick, then the dialog button. Password field values are never included in a description.
 */
public final class DialogProtocol {
    private static final Map<View, String> NAMES = Collections.synchronizedMap(new WeakHashMap<View, String>());

    private DialogProtocol() {}

    /** Record the resource entry name of a view's id ("name_field"), used as its key. */
    public static void nameView(View v, String name) {
        if (name != null) NAMES.put(v, name);
    }

    // ---- describing ------------------------------------------------------------------------------

    public static Map<String, Object> describe(Dialog d) {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("title", str(d.getTitleText()));
        m.put("cancelable", d.isCancelable());
        View content = d.getContentView();
        if (d instanceof AlertDialog) {
            AlertDialog a = (AlertDialog) d;
            m.put("message", str(a.getMessage()));
            Map<String, Object> buttons = new LinkedHashMap<>();
            putButton(buttons, "positive", a, DialogInterface.BUTTON_POSITIVE);
            putButton(buttons, "negative", a, DialogInterface.BUTTON_NEGATIVE);
            putButton(buttons, "neutral", a, DialogInterface.BUTTON_NEUTRAL);
            m.put("buttons", buttons);
            CharSequence[] items = a.getItems();
            if (items != null) {
                List<String> list = new ArrayList<>();
                for (CharSequence s : items) list.add(str(s));
                m.put("items", list);
                switch (a.getChoiceMode()) {
                    case AlertDialog.CHOICE_SINGLE:
                        m.put("choice", "single");
                        m.put("checkedItem", a.getCheckedItem());
                        break;
                    case AlertDialog.CHOICE_MULTIPLE: {
                        m.put("choice", "multiple");
                        List<Integer> checked = new ArrayList<>();
                        boolean[] c = a.getCheckedItems();
                        for (int i = 0; i < c.length; i++) if (c[i]) checked.add(i);
                        m.put("checkedItems", checked);
                        break;
                    }
                    default:
                        m.put("choice", "items");
                }
            }
            if (a.getView() != null) content = a.getView();
        }
        if (content != null) {
            Object view = describeView(content, new int[]{0});
            if (view != null) m.put("view", view);
        }
        return m;
    }

    private static void putButton(Map<String, Object> buttons, String name, AlertDialog a, int which) {
        CharSequence t = a.getButtonText(which);
        if (t != null) buttons.put(name, str(t));
    }

    /** A view's key: its id's resource name, else "id<n>", else its position in the tree ("v<n>"). */
    static String key(View v, int index) {
        String name = NAMES.get(v);
        if (name != null) return name;
        return v.getId() != View.NO_ID ? "id" + v.getId() : "v" + index;
    }

    @SuppressWarnings("unchecked")
    private static Map<String, Object> describeView(View v, int[] counter) {
        int index = counter[0]++;
        if (v.getVisibility() != View.VISIBLE) return null;
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("key", key(v, index));
        if (!v.isEnabled()) m.put("enabled", false);
        if (v instanceof ViewGroup) {
            ViewGroup g = (ViewGroup) v;
            boolean row = v instanceof LinearLayout && ((LinearLayout) v).getOrientation() == LinearLayout.HORIZONTAL;
            m.put("type", row ? "row" : "column");
            List<Object> children = new ArrayList<>();
            for (int i = 0; i < g.getChildCount(); i++) {
                View child = g.getChildAt(i);
                Object c = describeView(child, counter);
                if (c == null) continue;
                // A check box or switch without text followed by a plain label (a RelativeLayout row of
                // CheckBox + TextView, the label toggling the box): one labelled control, as a TV shows it.
                View next = g.getChildAt(i + 1);
                if (child instanceof CompoundButton && isBlank(((TextView) child).getText()) && isLabel(next)) {
                    ((Map<String, Object>) c).put("text", str(((TextView) next).getText()));
                    counter[0]++;  // the label keeps its index, so keys stay those of index()
                    i++;
                }
                children.add(c);
            }
            m.put("children", children);
        } else if (v instanceof EditText) {
            EditText e = (EditText) v;
            m.put("type", "text-field");
            String kind = inputKind(e.getInputType());
            m.put("input", kind);
            if (e.getInputType() != 0 && (e.getInputType() & 0x00020000) != 0) m.put("multiline", true);
            if (e.getHint() != null) m.put("hint", str(e.getHint()));
            if (kind.equals("password")) m.put("hasValue", e.length() > 0);
            else m.put("value", e.getText().toString());
        } else if (v instanceof CompoundButton) {
            m.put("type", v instanceof Switch ? "switch" : v instanceof CheckBox ? "checkbox" : "toggle");
            m.put("text", str(((TextView) v).getText()));
            m.put("checked", ((CompoundButton) v).isChecked());
        } else if (v instanceof Button) {
            m.put("type", "button");
            m.put("text", str(((TextView) v).getText()));
        } else if (v.hasOnClickListeners()) {
            // Clickable labels and image buttons: buttons named by their text, else their content description.
            m.put("type", "button");
            CharSequence text = v instanceof TextView ? ((TextView) v).getText() : null;
            if (isBlank(text)) text = v.getContentDescription();
            m.put("text", isBlank(text) ? (v instanceof ImageView ? "Image button" : "Button") : str(text));
        } else if (v instanceof TextView) {
            m.put("type", "label");
            m.put("text", str(((TextView) v).getText()));
        } else {
            m.put("type", "space");
        }
        return m;
    }

    /** A label that may name the control before it: text only, not itself an input or a button. */
    private static boolean isLabel(View v) {
        return v instanceof TextView && !(v instanceof Button) && !(v instanceof EditText)
                && v.getVisibility() == View.VISIBLE && !isBlank(((TextView) v).getText());
    }

    private static boolean isBlank(CharSequence s) {
        return s == null || s.toString().trim().isEmpty();
    }

    /** InputType class and variation: text, number, phone, datetime, email, uri or password. */
    static String inputKind(int t) {
        int cls = t & 0x0000000f, variation = t & 0x00000ff0;
        if (cls == 0x1) {
            if (variation == 0x80 || variation == 0x90 || variation == 0xe0) return "password";
            if (variation == 0x20 || variation == 0xd0) return "email";
            if (variation == 0x10) return "uri";
            return "text";
        }
        if (cls == 0x2) return variation == 0x10 ? "password" : "number";
        if (cls == 0x3) return "phone";
        if (cls == 0x4) return "datetime";
        return "text";
    }

    // ---- answering -------------------------------------------------------------------------------

    /** Apply the user's answer. Unknown keys and values of the wrong type are errors, not ignored. */
    public static void answer(Dialog d, Map<String, Object> answer) {
        View content = d.getContentView();
        if (d instanceof AlertDialog && ((AlertDialog) d).getView() != null) content = ((AlertDialog) d).getView();
        Map<String, View> byKey = new LinkedHashMap<>();
        if (content != null) index(content, new int[]{0}, byKey);

        Object fields = answer.get("fields");
        if (fields instanceof Map) {
            for (Map.Entry<?, ?> e : ((Map<?, ?>) fields).entrySet()) {
                View v = byKey.get(String.valueOf(e.getKey()));
                Object value = e.getValue();
                if (v instanceof EditText && value instanceof String) {
                    ((EditText) v).setText((String) value);
                } else if (v instanceof CompoundButton && value instanceof Boolean) {
                    ((CompoundButton) v).setChecked((Boolean) value);
                } else {
                    throw new IllegalArgumentException("field " + e.getKey() + ": no such input or wrong value type");
                }
            }
        } else if (fields != null) {
            throw new IllegalArgumentException("fields must be an object");
        }
        Object click = answer.get("click");
        if (click != null) {
            View v = byKey.get(String.valueOf(click));
            boolean button = v instanceof Button || (v != null && v.hasOnClickListeners());
            if (!button || v instanceof CompoundButton) throw new IllegalArgumentException("click: no button " + click);
            v.performClick();
        }
        Object item = answer.get("item");
        if (item != null) {
            if (!(d instanceof AlertDialog) || !(item instanceof Integer)) throw new IllegalArgumentException("item must be an index into the dialog's list");
            ((AlertDialog) d).pickItem((Integer) item);
        }
        Object button = answer.get("button");
        if (button != null && d.isShowing()) {
            switch (String.valueOf(button)) {
                case "cancel":
                    if (d.isCancelable()) d.cancel();
                    break;
                case "positive": press(d, DialogInterface.BUTTON_POSITIVE); break;
                case "negative": press(d, DialogInterface.BUTTON_NEGATIVE); break;
                case "neutral": press(d, DialogInterface.BUTTON_NEUTRAL); break;
                default: throw new IllegalArgumentException("unknown button " + button);
            }
        }
    }

    private static void press(Dialog d, int which) {
        if (!(d instanceof AlertDialog)) throw new IllegalArgumentException("dialog has no buttons");
        ((AlertDialog) d).pressButton(which);
    }

    private static void index(View v, int[] counter, Map<String, View> out) {
        int i = counter[0]++;
        if (v.getVisibility() != View.VISIBLE) return;  // hidden views are not described, so not answerable
        if (v.isEnabled()) out.put(key(v, i), v);
        if (v instanceof ViewGroup) {
            ViewGroup g = (ViewGroup) v;
            for (int c = 0; c < g.getChildCount(); c++) index(g.getChildAt(c), counter, out);
        }
    }

    private static String str(CharSequence s) {
        return s == null ? null : s.toString();
    }
}
