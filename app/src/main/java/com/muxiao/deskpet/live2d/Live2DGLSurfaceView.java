package com.muxiao.deskpet.live2d;

import android.content.Context;
import android.graphics.PixelFormat;
import android.opengl.GLSurfaceView;
import android.util.AttributeSet;
import android.view.Choreographer;

/**
 * Translucent GLSurfaceView for Live2D rendering.
 * <p>
 * Uses RENDERMODE_WHEN_DIRTY + Choreographer.FrameCallback to render at display
 * refresh rate without busy-waiting. Each vsync triggers requestRender(), which
 * tells the GL thread to call nativeOnDrawFrame(). This keeps CPU/GPU usage low
 * when the pet is idle while still responding immediately to touch events.
 */
public class Live2DGLSurfaceView extends GLSurfaceView implements Choreographer.FrameCallback {

    private boolean animating = true;
    private Runnable onSurfaceReady;

    public Live2DGLSurfaceView(Context context) {
        super(context);
        init();
    }

    public Live2DGLSurfaceView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    /**
     * Set a callback to be invoked on the GL thread once the surface is created
     * and the rendering context is ready. Used to defer model loading until the
     * surface is confirmed ready.
     */
    public void setOnSurfaceReadyListener(Runnable listener) {
        this.onSurfaceReady = listener;
    }

    private void init() {
        setZOrderOnTop(true);
        getHolder().setFormat(PixelFormat.TRANSLUCENT);
        setEGLContextClientVersion(2);
        setEGLConfigChooser(8, 8, 8, 8, 0, 0);
        setRenderer(new Live2DRenderer(() -> {
            if (onSurfaceReady != null) {
                onSurfaceReady.run();
            }
        }));
        setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
    }

    @Override
    public void doFrame(long frameTimeNanos) {
        if (animating) {
            requestRender();
            // Re-register for next frame
            Choreographer.getInstance().postFrameCallback(this);
        }
    }

    @Override
    public void onAttachedToWindow() {
        super.onAttachedToWindow();
        animating = true;
        Choreographer.getInstance().postFrameCallback(this);
    }

    @Override
    public void onDetachedFromWindow() {
        animating = false;
        try {
            Choreographer.getInstance().removeFrameCallback(this);
        } catch (Exception ignored) {}
        try {
            super.onDetachedFromWindow();
        } catch (Exception ignored) {
            // Suppress framework-level crashes during view detachment
        }
    }

    @Override
    protected void onWindowVisibilityChanged(int visibility) {
        try {
            super.onWindowVisibilityChanged(visibility);
        } catch (NullPointerException ignored) {
            // Suppress SurfaceView.performDrawFinished NPE when parent is null
            // during window removal (race between GL frame completion and view detach)
        }
    }

    @Override
    public void onPause() {
        animating = false;
        Choreographer.getInstance().removeFrameCallback(this);
        super.onPause();
    }

    @Override
    public void onResume() {
        super.onResume();
        animating = true;
        Choreographer.getInstance().postFrameCallback(this);
    }

    @Override
    public boolean performClick() {
        return super.performClick();
    }
}
