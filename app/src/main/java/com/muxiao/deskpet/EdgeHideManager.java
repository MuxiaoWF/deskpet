package com.muxiao.deskpet;

import android.animation.ValueAnimator;
import android.view.View;
import android.view.WindowManager;
import android.view.animation.DecelerateInterpolator;

/**
 * Edge-hide manager: snaps the floating pet to the nearest screen edge,
 * hiding most of it. Tap the exposed portion to expand.
 */
public class EdgeHideManager {

    public enum Edge { LEFT, RIGHT, TOP, BOTTOM }

    private boolean enabled = false;
    private Edge currentEdge = Edge.LEFT;
    private boolean isHidden = false;
    private float visiblePortion = 0.30f;
    private final int animDurationMs = 250;

    private ValueAnimator currentAnimator;
    private int targetX, targetY;
    private int savedX, savedY;

    public boolean isTouchOnExposedArea(float rawX, float rawY,
                                         WindowManager.LayoutParams params,
                                         int viewWidth, int viewHeight) {
        if (!enabled || !isHidden) return false;
        int exposedPx;
        switch (currentEdge) {
            case LEFT:
                exposedPx = (int) (viewWidth * visiblePortion);
                return rawX >= params.x && rawX <= params.x + exposedPx;
            case RIGHT:
                exposedPx = (int) (viewWidth * visiblePortion);
                return rawX >= params.x + viewWidth - exposedPx && rawX <= params.x + viewWidth;
            case TOP:
                exposedPx = (int) (viewHeight * visiblePortion);
                return rawY >= params.y && rawY <= params.y + exposedPx;
            case BOTTOM:
                exposedPx = (int) (viewHeight * visiblePortion);
                return rawY >= params.y + viewHeight - exposedPx && rawY <= params.y + viewHeight;
            default:
                return false;
        }
    }

    /**
     * Determine which screen edge is closest to the view center and calculate
     * the target position for hiding. The view will slide to that edge, leaving
     * only {@link #visiblePortion} of its width/height exposed.
     */
    public void snapToEdge(WindowManager.LayoutParams params,
                           int screenWidth, int screenHeight) {
        int w = params.width;
        int h = params.height;

        // Use edge distance for snap detection: only snap when the view edge
        // is actually close to the screen edge (within 15% of view dimension)
        int leftDist = params.x;
        int rightDist = screenWidth - (params.x + w);
        int topDist = params.y;
        int bottomDist = screenHeight - (params.y + h);

        int snapThresholdX = (int)(w * 0.15f);
        int snapThresholdY = (int)(h * 0.15f);

        boolean nearLeft = leftDist < snapThresholdX;
        boolean nearRight = rightDist < snapThresholdX;
        boolean nearTop = topDist < snapThresholdY;
        boolean nearBottom = bottomDist < snapThresholdY;

        // Determine which axis to snap on, then pick the closer edge
        if (nearLeft || nearRight) {
            if (nearTop || nearBottom) {
                // Corner: snap to the closer edge using edge distance
                int minHoriz = Math.min(leftDist, rightDist);
                int minVert = Math.min(topDist, bottomDist);
                if (minHoriz <= minVert) {
                    currentEdge = (leftDist <= rightDist) ? Edge.LEFT : Edge.RIGHT;
                } else {
                    currentEdge = (topDist <= bottomDist) ? Edge.TOP : Edge.BOTTOM;
                }
            } else {
                currentEdge = (leftDist <= rightDist) ? Edge.LEFT : Edge.RIGHT;
            }
        } else if (nearTop || nearBottom) {
            currentEdge = (topDist <= bottomDist) ? Edge.TOP : Edge.BOTTOM;
        } else {
            // Not near any edge — find the closest one via center distance
            int cx = params.x + w / 2;
            int cy = params.y + h / 2;
            int dL = cx, dR = screenWidth - cx, dT = cy, dB = screenHeight - cy;
            int min = Math.min(Math.min(dL, dR), Math.min(dT, dB));
            if (min == dL) currentEdge = Edge.LEFT;
            else if (min == dR) currentEdge = Edge.RIGHT;
            else if (min == dT) currentEdge = Edge.TOP;
            else currentEdge = Edge.BOTTOM;
        }

        switch (currentEdge) {
            case LEFT:
                targetX = -w + (int)(w * visiblePortion);
                targetY = params.y;
                break;
            case RIGHT:
                targetX = screenWidth - (int)(w * visiblePortion);
                targetY = params.y;
                break;
            case TOP:
                targetX = params.x;
                targetY = -h + (int)(h * visiblePortion);
                break;
            case BOTTOM:
                targetX = params.x;
                targetY = screenHeight - (int)(h * visiblePortion);
                break;
        }

        // Clamp so the visible strip is always on-screen
        int minVisiblePxX = (int)(w * visiblePortion);
        int minVisiblePxY = (int)(h * visiblePortion);
        targetX = Math.max(-w + minVisiblePxX, Math.min(targetX, screenWidth - minVisiblePxX));
        targetY = Math.max(-h + minVisiblePxY, Math.min(targetY, screenHeight - minVisiblePxY));
    }

    public void hide(View view, WindowManager wm, WindowManager.LayoutParams params) {
        if (currentAnimator != null && currentAnimator.isRunning()) {
            currentAnimator.cancel();
        }

        savedX = params.x;
        savedY = params.y;

        int startX = params.x;
        int startY = params.y;

        currentAnimator = ValueAnimator.ofFloat(0f, 1f);
        currentAnimator.setDuration(animDurationMs);
        currentAnimator.setInterpolator(new DecelerateInterpolator());
        currentAnimator.addUpdateListener(animation -> {
            float fraction = (float) animation.getAnimatedValue();
            params.x = (int) (startX + (targetX - startX) * fraction);
            params.y = (int) (startY + (targetY - startY) * fraction);
            try {
                wm.updateViewLayout(view, params);
            } catch (IllegalArgumentException ignored) {}
        });
        currentAnimator.addListener(new android.animation.AnimatorListenerAdapter() {
            @Override
            public void onAnimationEnd(android.animation.Animator animation) {
                isHidden = true;
            }
        });
        currentAnimator.start();
    }

    public void expand(View view, WindowManager wm, WindowManager.LayoutParams params,
                       int screenWidth, int screenHeight) {
        if (currentAnimator != null && currentAnimator.isRunning()) {
            currentAnimator.cancel();
        }

        // Restore previous position; fall back to center if out of bounds
        int targetExpandX = savedX;
        int targetExpandY = savedY;
        if (targetExpandX < -params.width || targetExpandX > screenWidth ||
            targetExpandY < -params.height || targetExpandY > screenHeight) {
            targetExpandX = (screenWidth - params.width) / 2;
            targetExpandY = (screenHeight - params.height) / 2;
        }
        final int expandX = targetExpandX;
        final int expandY = targetExpandY;

        int startX = params.x;
        int startY = params.y;

        isHidden = false;

        currentAnimator = ValueAnimator.ofFloat(0f, 1f);
        currentAnimator.setDuration(animDurationMs);
        currentAnimator.setInterpolator(new DecelerateInterpolator());
        currentAnimator.addUpdateListener(animation -> {
            float fraction = (float) animation.getAnimatedValue();
            params.x = (int) (startX + (expandX - startX) * fraction);
            params.y = (int) (startY + (expandY - startY) * fraction);
            try {
                wm.updateViewLayout(view, params);
            } catch (IllegalArgumentException ignored) {}
        });
        currentAnimator.start();
    }

    public void setEnabled(boolean enabled) { this.enabled = enabled; }
    public boolean isEnabled() { return enabled; }
    public boolean isHidden() { return isHidden; }
    public void setVisiblePortion(float portion) { this.visiblePortion = portion; }
}
