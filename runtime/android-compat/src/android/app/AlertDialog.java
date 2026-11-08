// SPDX-License-Identifier: GPL-3.0-or-later
package android.app;

import android.content.Context;
import android.content.DialogInterface;
import android.view.View;
import android.widget.Button;

/** android.app.AlertDialog: title, message, optional view, up to three buttons, or a list of choices. */
public class AlertDialog extends Dialog {
    public static final int CHOICE_NONE = 0, CHOICE_ITEMS = 1, CHOICE_SINGLE = 2, CHOICE_MULTIPLE = 3;

    private CharSequence message;
    private View view;
    private final CharSequence[] buttonText = new CharSequence[3];
    private final DialogInterface.OnClickListener[] buttonListener = new DialogInterface.OnClickListener[3];
    private final Button[] buttons = new Button[3];
    private CharSequence[] items;
    private int choiceMode = CHOICE_NONE;
    private int checkedItem = -1;
    private boolean[] checkedItems;
    private DialogInterface.OnClickListener itemListener;
    private DialogInterface.OnMultiChoiceClickListener multiListener;

    protected AlertDialog(Context context) {
        super(context);
    }

    protected AlertDialog(Context context, int themeResId) {
        super(context, themeResId);
    }

    private static int slot(int whichButton) {
        switch (whichButton) {
            case BUTTON_POSITIVE: return 0;
            case BUTTON_NEGATIVE: return 1;
            case BUTTON_NEUTRAL: return 2;
            default: throw new IllegalArgumentException("Button does not exist");
        }
    }

    public void setMessage(CharSequence message) {
        this.message = message;
    }

    public CharSequence getMessage() {
        return message;
    }

    public void setView(View view) {
        this.view = view;
    }

    public View getView() {
        return view;
    }

    public void setButton(int whichButton, CharSequence text, DialogInterface.OnClickListener listener) {
        buttonText[slot(whichButton)] = text;
        buttonListener[slot(whichButton)] = listener;
    }

    @Override
    void build() {
        for (int i = 0; i < 3; i++) {
            if (buttonText[i] == null) continue;
            Button b = new Button(getContext());
            b.setText(buttonText[i]);
            buttons[i] = b;
        }
    }

    /** As on Android, null until the dialog is created (shown); never null for a defined button after that. */
    public Button getButton(int whichButton) {
        return buttons[slot(whichButton)];
    }

    // ---- state the UI host describes and drives ------------------------------------------------

    public CharSequence getButtonText(int whichButton) {
        return buttonText[slot(whichButton)];
    }

    public CharSequence[] getItems() {
        return items;
    }

    public int getChoiceMode() {
        return choiceMode;
    }

    public int getCheckedItem() {
        return checkedItem;
    }

    public boolean[] getCheckedItems() {
        return checkedItems;
    }

    /**
     * The user pressed a dialog button. A View.OnClickListener the plugin set on getButton() replaces the
     * default handling (and the dialog stays open); otherwise the listener runs and the dialog dismisses.
     */
    public void pressButton(int whichButton) {
        int i = slot(whichButton);
        if (buttonText[i] == null) throw new IllegalArgumentException("button not defined");
        if (buttons[i] != null && buttons[i].hasOnClickListeners()) {
            buttons[i].performClick();
            return;
        }
        if (buttonListener[i] != null) buttonListener[i].onClick(this, whichButton);
        dismiss();
    }

    /** The user picked a list item (plain list: dismisses; single choice: marks it checked). */
    public void pickItem(int which) {
        if (items == null || which < 0 || which >= items.length) throw new IllegalArgumentException("no item " + which);
        if (choiceMode == CHOICE_ITEMS) {
            if (itemListener != null) itemListener.onClick(this, which);
            dismiss();
        } else if (choiceMode == CHOICE_SINGLE) {
            checkedItem = which;
            if (itemListener != null) itemListener.onClick(this, which);
        } else if (choiceMode == CHOICE_MULTIPLE) {
            checkedItems[which] = !checkedItems[which];
            if (multiListener != null) multiListener.onClick(this, which, checkedItems[which]);
        } else {
            throw new IllegalArgumentException("dialog has no list");
        }
    }

    public static class Builder {
        private final Context context;
        private CharSequence title, message;
        private View view;
        private int viewLayout;
        private final CharSequence[] buttonText = new CharSequence[3];
        private final DialogInterface.OnClickListener[] buttonListener = new DialogInterface.OnClickListener[3];
        private CharSequence[] items;
        private int choiceMode = CHOICE_NONE;
        private int checkedItem = -1;
        private boolean[] checkedItems;
        private DialogInterface.OnClickListener itemListener;
        private DialogInterface.OnMultiChoiceClickListener multiListener;
        private boolean cancelable = true;
        private DialogInterface.OnCancelListener onCancel;
        private DialogInterface.OnDismissListener onDismiss;

        public Builder(Context context) {
            this(context, 0);
        }

        public Builder(Context context, int themeResId) {
            if (context == null) throw new NullPointerException("context");
            this.context = context;
        }

        public Context getContext() {
            return context;
        }

        public Builder setTitle(CharSequence title) {
            this.title = title;
            return this;
        }

        public Builder setTitle(int titleId) {
            return setTitle(context.getText(titleId));
        }

        public Builder setMessage(CharSequence message) {
            this.message = message;
            return this;
        }

        public Builder setMessage(int messageId) {
            return setMessage(context.getText(messageId));
        }

        public Builder setIcon(int iconId) {
            return this;  // no graphics stack: icons are not shown
        }

        public Builder setIcon(android.graphics.drawable.Drawable icon) {
            return this;
        }

        public Builder setView(View view) {
            this.view = view;
            this.viewLayout = 0;
            return this;
        }

        public Builder setView(int layoutResId) {
            this.view = null;
            this.viewLayout = layoutResId;
            return this;
        }

        private Builder button(int slot, CharSequence text, DialogInterface.OnClickListener listener) {
            buttonText[slot] = text;
            buttonListener[slot] = listener;
            return this;
        }

        public Builder setPositiveButton(CharSequence text, DialogInterface.OnClickListener listener) {
            return button(0, text, listener);
        }

        public Builder setPositiveButton(int textId, DialogInterface.OnClickListener listener) {
            return button(0, context.getText(textId), listener);
        }

        public Builder setNegativeButton(CharSequence text, DialogInterface.OnClickListener listener) {
            return button(1, text, listener);
        }

        public Builder setNegativeButton(int textId, DialogInterface.OnClickListener listener) {
            return button(1, context.getText(textId), listener);
        }

        public Builder setNeutralButton(CharSequence text, DialogInterface.OnClickListener listener) {
            return button(2, text, listener);
        }

        public Builder setNeutralButton(int textId, DialogInterface.OnClickListener listener) {
            return button(2, context.getText(textId), listener);
        }

        public Builder setItems(CharSequence[] items, DialogInterface.OnClickListener listener) {
            this.items = items;
            this.itemListener = listener;
            this.choiceMode = CHOICE_ITEMS;
            return this;
        }

        public Builder setItems(int itemsId, DialogInterface.OnClickListener listener) {
            return setItems(context.getResources().getTextArray(itemsId), listener);
        }

        public Builder setSingleChoiceItems(CharSequence[] items, int checkedItem, DialogInterface.OnClickListener listener) {
            this.items = items;
            this.checkedItem = checkedItem;
            this.itemListener = listener;
            this.choiceMode = CHOICE_SINGLE;
            return this;
        }

        public Builder setSingleChoiceItems(int itemsId, int checkedItem, DialogInterface.OnClickListener listener) {
            return setSingleChoiceItems(context.getResources().getTextArray(itemsId), checkedItem, listener);
        }

        public Builder setMultiChoiceItems(CharSequence[] items, boolean[] checkedItems,
                DialogInterface.OnMultiChoiceClickListener listener) {
            this.items = items;
            this.checkedItems = checkedItems;
            this.multiListener = listener;
            this.choiceMode = CHOICE_MULTIPLE;
            return this;
        }

        public Builder setMultiChoiceItems(int itemsId, boolean[] checkedItems,
                DialogInterface.OnMultiChoiceClickListener listener) {
            return setMultiChoiceItems(context.getResources().getTextArray(itemsId), checkedItems, listener);
        }

        public Builder setCancelable(boolean cancelable) {
            this.cancelable = cancelable;
            return this;
        }

        public Builder setOnCancelListener(DialogInterface.OnCancelListener listener) {
            this.onCancel = listener;
            return this;
        }

        public Builder setOnDismissListener(DialogInterface.OnDismissListener listener) {
            this.onDismiss = listener;
            return this;
        }

        /** The dialog create() fills in; androidx.appcompat.app.AlertDialog.Builder makes its own type. */
        protected AlertDialog newDialog(Context context) {
            return new AlertDialog(context);
        }

        public AlertDialog create() {
            AlertDialog d = newDialog(context);
            d.setTitle(title);
            d.message = message;
            d.view = viewLayout != 0 ? android.view.LayoutInflater.from(context).inflate(viewLayout, null) : view;
            for (int i = 0; i < 3; i++) {
                d.buttonText[i] = buttonText[i];
                d.buttonListener[i] = buttonListener[i];
            }
            d.items = items;
            d.choiceMode = choiceMode;
            d.checkedItem = checkedItem;
            d.checkedItems = checkedItems != null ? checkedItems
                    : choiceMode == CHOICE_MULTIPLE ? new boolean[items == null ? 0 : items.length] : null;
            d.itemListener = itemListener;
            d.multiListener = multiListener;
            d.setCancelable(cancelable);
            d.setOnCancelListener(onCancel);
            d.setOnDismissListener(onDismiss);
            return d;
        }

        public AlertDialog show() {
            AlertDialog d = create();
            d.show();
            return d;
        }
    }
}
