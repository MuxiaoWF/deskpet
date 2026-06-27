package com.muxiao.deskpet.live2d;

import android.opengl.GLSurfaceView;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * GLSurfaceView.Renderer that delegates all calls to native Live2D rendering code.
 */
public class Live2DRenderer implements GLSurfaceView.Renderer {

    private final Runnable onSurfaceReady;

    public Live2DRenderer(Runnable onSurfaceReady) {
        this.onSurfaceReady = onSurfaceReady;
    }

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        Live2DNativeBridge.nativeOnSurfaceCreated();
        // Notify that the surface is ready (runs on GL thread)
        if (onSurfaceReady != null) {
            onSurfaceReady.run();
        }
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        Live2DNativeBridge.nativeOnSurfaceChanged(width, height);
    }

    @Override
    public void onDrawFrame(GL10 gl) {
        Live2DNativeBridge.nativeOnDrawFrame();
    }
}
