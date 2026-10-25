package com.muxiao.deskpet;

import android.content.Context;
import android.graphics.drawable.GradientDrawable;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.PopupWindow;
import android.widget.TextView;

/**
 * Text bubble overlay that appears near the floating pet when a motion triggers text.
 */
public class TextBubbleOverlay {

    private PopupWindow bubbleWindow;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private Runnable dismissRunnable;

    public void show(View anchor, String text, int durationMs) {
        if (text == null || text.isEmpty()) return;

        dismiss();

        Context context = anchor.getContext();

        TextView textView = new TextView(context);
        textView.setText(text);
        textView.setTextColor(0xFF333333);
        textView.setTextSize(13);
        textView.setMaxWidth(dp(context, 200));
        textView.setPadding(dp(context, 12), dp(context, 8), dp(context, 12), dp(context, 8));

        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0xF5F5F5F5);
        bg.setCornerRadius(dp(context, 12));
        bg.setStroke(dp(context, 1), 0xFFDDDDDD);
        textView.setBackground(bg);

        bubbleWindow = new PopupWindow(textView,
                WindowManager.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.WRAP_CONTENT, false);
        bubbleWindow.setBackgroundDrawable(null);
        bubbleWindow.setElevation(dp(context, 4));

        int[] location = new int[2];
        anchor.getLocationOnScreen(location);

        // Measure so the bubble can be centered above the anchor and clamped
        // on screen (it used to open with its left edge at the anchor center,
        // overflowing the screen when the pet was near the right/top edge).
        textView.measure(View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED),
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
        android.util.DisplayMetrics dm = context.getResources().getDisplayMetrics();
        int bubbleW = Math.min(textView.getMeasuredWidth(), dm.widthPixels);
        int x = location[0] + anchor.getWidth() / 2 - bubbleW / 2;
        int y = location[1] - textView.getMeasuredHeight() - dp(context, 8);
        x = Math.max(dp(context, 4), Math.min(x, dm.widthPixels - bubbleW - dp(context, 4)));
        y = Math.max(dp(context, 4), y);

        try {
            bubbleWindow.showAtLocation(anchor, Gravity.NO_GRAVITY, x, y);
        } catch (Exception e) {
            bubbleWindow = null;
            return;
        }

        // Auto-dismiss
        if (durationMs <= 0) durationMs = 3000;
        dismissRunnable = this::dismiss;
        handler.postDelayed(dismissRunnable, durationMs);
    }

    public void dismiss() {
        if (dismissRunnable != null) {
            handler.removeCallbacks(dismissRunnable);
            dismissRunnable = null;
        }
        if (bubbleWindow != null && bubbleWindow.isShowing()) {
            try {
                bubbleWindow.dismiss();
            } catch (Exception ignored) {}
        }
        bubbleWindow = null;
    }

    private static int dp(Context context, int value) {
        return FileUtils.dp(context, value);
    }
}
