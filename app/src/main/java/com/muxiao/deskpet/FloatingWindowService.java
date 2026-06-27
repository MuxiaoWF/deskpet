package com.muxiao.deskpet;

import android.annotation.SuppressLint;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.view.Display;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.core.app.NotificationCompat;
import com.muxiao.deskpet.live2d.Live2DGLSurfaceView;
import com.muxiao.deskpet.live2d.Live2DNativeBridge;

import org.json.JSONObject;

import java.io.File;

/**
 * Foreground service that manages the floating Live2D pet overlay window.
 * Handles touch events (drag, tap, long-press), pinch-to-scale, click-through mode,
 * and model loading via JNI to native Live2D code.
 */
public class FloatingWindowService extends Service {

    public interface LoadingStateListener {
        void onLoadingStateChanged(boolean isLoading, String modelPath);
        void onServiceStopped();
    }

    private static LoadingStateListener loadingStateListener;
    private static volatile boolean serviceRunning = false;

    public static void setLoadingStateListener(LoadingStateListener listener) {
        loadingStateListener = listener;
    }

    public static boolean isRunning() {
        return serviceRunning;
    }

    private static final String CHANNEL_ID = "deskpet_channel";
    private static final int NOTIFICATION_ID = 1;
    private static final float DRAG_THRESHOLD = 10f;
    private static final long LONG_PRESS_MS = 500;
    private static final long DOUBLE_TAP_MS = 300;
    private static final int BASE_SIZE = 300;

    static final String PREF_NAME = "deskpet";

    private static final String ACTION_STOP = "STOP";
    private static final String ACTION_TOGGLE_CLICK_THROUGH = "TOGGLE_CLICK_THROUGH";

    public static final String EXTRA_MODEL_PATH = "model_path";
    private static final String EXTRA_HIT_AREA_CONFIG = "hit_area_config";
    private static final String EXTRA_LOOK_AT_CONFIG = "look_at_config";
    private static final String EXTRA_MODEL_CONFIG = "model_config";

    private WindowManager windowManager;
    private Live2DGLSurfaceView glSurfaceView;
    private WindowManager.LayoutParams layoutParams;

    private float touchOffsetX, touchOffsetY;
    private boolean isDragging;
    private long touchDownTime;

    private float scaleFactor = 1.0f;

    private final ActionMenuManager actionMenuManager = new ActionMenuManager();
    private final MotionSoundPlayer motionSoundPlayer = new MotionSoundPlayer();
    private String currentModelDir;

    private boolean clickThrough = false;
    private volatile boolean isLoadingModel = false;
    private long lastTapTime = 0;
    private boolean dragEnabled = true;
    private boolean surfaceReady = false;
    private Runnable pendingModelLoad = null;
    private EdgeHideManager edgeHideManager;
    private TextBubbleOverlay textBubbleOverlay;
    private int screenWidth, screenHeight;

    // Pinch gesture detection
    private float lastPinchDistance = 0;
    private boolean isPinching = false;
    private boolean wasPinching = false;  // tracks if a pinch occurred during this touch sequence
    private static final float PINCH_THRESHOLD = 1.2f;

    // Shake gesture detection
    private SensorManager sensorManager;
    private Sensor accelerometer;
    private long lastShakeTime = 0;
    private static final float SHAKE_THRESHOLD = 20.0f;  // raised from 15 to reduce false positives from tilting
    private static final long SHAKE_COOLDOWN_MS = 1000;
    private float lastAccelX = 0, lastAccelY = 0, lastAccelZ = 0;
    private boolean firstAccelReading = true;


    private final SharedPreferences.OnSharedPreferenceChangeListener prefsListener =
            (prefs, key) -> {
                if ("drag_enable".equals(key)) {
                    dragEnabled = prefs.getBoolean("drag_enable", true);
                } else if ("scale".equals(key)) {
                    float newScale = prefs.getFloat("scale", 1.0f);
                    if (Math.abs(newScale - scaleFactor) > 0.001f) {
                        scaleFactor = newScale;
                        if (glSurfaceView != null && layoutParams != null) {
                            applyWindowSize();
                        }
                    }
                } else if ("edge_hide_visible_portion".equals(key)) {
                    if (edgeHideManager != null) {
                        edgeHideManager.setVisiblePortion(prefs.getFloat("edge_hide_visible_portion", 0.30f));
                    }
                } else if ("edge_hide_enabled".equals(key)) {
                    if (edgeHideManager != null) {
                        boolean enabled = prefs.getBoolean("edge_hide_enabled", false);
                        edgeHideManager.setEnabled(enabled);
                        if (!enabled && edgeHideManager.isHidden()) {
                            refreshScreenSize();
                            edgeHideManager.expand(glSurfaceView, windowManager, layoutParams,
                                    screenWidth, screenHeight);
                        }
                    }
                } else if ("motion_enabled".equals(key)) {
                    boolean motionEnabled = prefs.getBoolean("motion_enabled", true);
                    if (!motionEnabled) {
                        Live2DNativeBridge.nativeSetRandomSpeakEnabled(false);
                    } else if (prefs.getBoolean("random_speak_enable", false)) {
                        Live2DNativeBridge.nativeSetRandomSpeakEnabled(true);
                        Live2DNativeBridge.nativeSetRandomSpeakInterval(prefs.getInt("random_speak_interval", 30));
                    }
                }
            };

    public static void start(Context context, String modelPath) {
        start(context, modelPath, null, null, null);
    }

    public static void start(Context context, String modelPath,
                              String hitAreaConfig, String lookAtConfig, String modelConfig) {
        Intent intent = new Intent(context, FloatingWindowService.class);
        if (modelPath != null) {
            intent.putExtra(EXTRA_MODEL_PATH, modelPath);
        }
        if (hitAreaConfig != null) {
            intent.putExtra(EXTRA_HIT_AREA_CONFIG, hitAreaConfig);
        }
        if (lookAtConfig != null) {
            intent.putExtra(EXTRA_LOOK_AT_CONFIG, lookAtConfig);
        }
        if (modelConfig != null) {
            intent.putExtra(EXTRA_MODEL_CONFIG, modelConfig);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.startForegroundService(intent);
        } else {
            context.startService(intent);
        }
    }

    @Override
    @SuppressLint("deprecation")
    public void onCreate() {
        super.onCreate();
        serviceRunning = true;
        windowManager = (WindowManager) getSystemService(WINDOW_SERVICE);
        createNotificationChannel();
        startForeground(NOTIFICATION_ID, buildNotification());

        SharedPreferences prefs = getSharedPreferences(PREF_NAME, MODE_PRIVATE);
        scaleFactor = prefs.getFloat("scale", 1.0f);
        dragEnabled = prefs.getBoolean("drag_enable", true);

        edgeHideManager = new EdgeHideManager();
        edgeHideManager.setEnabled(prefs.getBoolean("edge_hide_enabled", false));
        edgeHideManager.setVisiblePortion(prefs.getFloat("edge_hide_visible_portion", 0.30f));

        // Sound is now triggered from native side (works for menu, hit area, and auto motions)
        Live2DNativeBridge.setMotionSoundCallback((soundPath, delayMs) ->
                motionSoundPlayer.play(soundPath, delayMs));

        // VarFloat change callback: refresh menu when model state changes from native side.
        // Debounced: a single costume change can trigger 5-10 VarFloat updates in rapid succession.
        final android.os.Handler refreshHandler = new android.os.Handler(android.os.Looper.getMainLooper());
        final Runnable[] refreshRunnable = {null};
        Live2DNativeBridge.setVarFloatChangedCallback((name, value) -> {
            if (actionMenuManager != null) {
                if (refreshRunnable[0] != null) {
                    refreshHandler.removeCallbacks(refreshRunnable[0]);
                }
                refreshRunnable[0] = () -> {
                    if (actionMenuManager != null) {
                        actionMenuManager.refresh();
                    }
                };
                refreshHandler.postDelayed(refreshRunnable[0], 100);
            }
        });

        prefs.registerOnSharedPreferenceChangeListener(prefsListener);

        // Initialize shake detection via accelerometer
        sensorManager = (SensorManager) getSystemService(SENSOR_SERVICE);
        if (sensorManager != null) {
            accelerometer = sensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
            if (accelerometer != null) {
                firstAccelReading = true;  // reset to avoid stale delta on re-registration
                sensorManager.registerListener(sensorListener, accelerometer, SensorManager.SENSOR_DELAY_NORMAL);
            }
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null) {
            String action = intent.getAction();
            if (ACTION_STOP.equals(action)) {
                stopSelf();
                return START_NOT_STICKY;
            } else if (ACTION_TOGGLE_CLICK_THROUGH.equals(action)) {
                setClickThrough(!clickThrough);
                return START_STICKY;
            }
        }

        if (glSurfaceView == null) {
            Live2DNativeBridge.SetContext(this);
            Live2DNativeBridge.nativeInit();

            glSurfaceView = new Live2DGLSurfaceView(this);

            int size = (int) (BASE_SIZE * scaleFactor);
            int windowType = getOverlayWindowType();
            layoutParams = new WindowManager.LayoutParams(
                    size, size,
                    windowType,
                    WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                            | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
                    PixelFormat.TRANSLUCENT
            );
            layoutParams.gravity = Gravity.TOP | Gravity.START;

            initScreenSize();

            SharedPreferences prefs = getSharedPreferences(PREF_NAME, MODE_PRIVATE);
            layoutParams.x = prefs.getInt("window_x", 0);
            layoutParams.y = prefs.getInt("window_y", 200);

            // Clamp initial position so the window is at least partially visible
            int maxX = screenWidth - size / 2;
            int maxY = screenHeight - size / 2;
            int minX = -size / 2;
            int minY = -size / 2;
            layoutParams.x = Math.max(minX, Math.min(layoutParams.x, maxX));
            layoutParams.y = Math.max(minY, Math.min(layoutParams.y, maxY));

            glSurfaceView.setOnTouchListener(this::onTouch);
            glSurfaceView.setOnSurfaceReadyListener(() -> {
                surfaceReady = true;
                if (pendingModelLoad != null) {
                    glSurfaceView.queueEvent(pendingModelLoad);
                    pendingModelLoad = null;
                }
            });
            windowManager.addView(glSurfaceView, layoutParams);

            textBubbleOverlay = new TextBubbleOverlay();
            Live2DNativeBridge.setMotionTextCallback((text, durationMs, delayMs) -> {
                if (textBubbleOverlay != null && glSurfaceView != null) {
                    glSurfaceView.postDelayed(() -> textBubbleOverlay.show(glSurfaceView, text, durationMs), delayMs);
                }
            });
        }

        if (intent != null) {
            String modelPath = intent.getStringExtra(EXTRA_MODEL_PATH);
            // On crash restart (START_STICKY), intent has no model path.
            // Reload the last model from SharedPreferences, but limit retries
            // to prevent crash loops.
            if (modelPath == null) {
                // Crash restart (START_STICKY) — don't retry model load, just notify
                android.util.Log.w("DeskPet", "[Service]Crash restart detected, skipping model reload");
                Toast.makeText(this, "模型加载异常，请重新选择模型", Toast.LENGTH_LONG).show();
                return START_STICKY;
            } else {
                // Normal start with explicit model path — reset crash counter
                getSharedPreferences(PREF_NAME, MODE_PRIVATE)
                        .edit().putInt("crash_restart_count", 0).apply();
            }
            if (isLoadingModel) {
                Toast.makeText(this, R.string.toast_model_loading, Toast.LENGTH_SHORT).show();
                return START_STICKY;
            }
            isLoadingModel = true;
            currentModelDir = new File(modelPath).getParent();
            motionSoundPlayer.setModelDir(currentModelDir);

            broadcastLoadingState(true, modelPath);

            getSharedPreferences(PREF_NAME, MODE_PRIVATE)
                    .edit().putString("last_model_path", modelPath).apply();

            final String path = modelPath;
            final String hitAreaConfig = intent.getStringExtra(EXTRA_HIT_AREA_CONFIG);
            final String lookAtConfig = intent.getStringExtra(EXTRA_LOOK_AT_CONFIG);
            final String modelConfig = intent.getStringExtra(EXTRA_MODEL_CONFIG);
            final String savedExpression = getSharedPreferences(PREF_NAME, MODE_PRIVATE)
                    .getString("last_expression", null);

            // Model loading must happen on the GL thread. If the surface isn't ready yet,
            // defer loading via pendingModelLoad (executed when onSurfaceReady fires).
            Runnable modelLoadTask = () -> {
                try {
                    Live2DNativeBridge.nativeLoadModel(path);
                    if (hitAreaConfig != null) {
                        Live2DNativeBridge.nativeSetHitAreaConfig(hitAreaConfig);
                    }
                    if (lookAtConfig != null) {
                        Live2DNativeBridge.nativeSetLookAtConfig(lookAtConfig);
                    }
                    if (modelConfig != null) {
                        Live2DNativeBridge.nativeSetModelConfig(modelConfig);
                    }
                    // Apply runtime settings from SharedPreferences
                    SharedPreferences sp = getSharedPreferences(PREF_NAME, MODE_PRIVATE);
                    Live2DNativeBridge.nativeSetFollowMode(sp.getInt("follow_mode", 0));
                    if (sp.getBoolean("random_speak_enable", false)
                            && sp.getBoolean("motion_enabled", true)) {
                        Live2DNativeBridge.nativeSetRandomSpeakEnabled(true);
                        Live2DNativeBridge.nativeSetRandomSpeakInterval(sp.getInt("random_speak_interval", 30));
                    }
                    if (sp.getBoolean("debug_hit_areas", false)) {
                        Live2DNativeBridge.nativeShowHitAreas(true);
                    }
                    Live2DNativeBridge.nativeSetExpressionBlendMode(sp.getInt("expression_blend_mode", 0));
                    Live2DNativeBridge.nativeSetPhysicsWeight(sp.getFloat("physics_weight", 1.0f));
                    Live2DNativeBridge.nativeSetMirror(sp.getBoolean("model_mirror", false));
                    Live2DNativeBridge.nativeSetRotation(sp.getFloat("model_rotation", 0f));
                    if (savedExpression != null && !savedExpression.isEmpty()) {
                        Live2DNativeBridge.nativeSetExpression(savedExpression);
                    }
                } finally {
                    isLoadingModel = false;
                    broadcastLoadingState(false, path);
                }
            };

            if (surfaceReady) {
                glSurfaceView.queueEvent(modelLoadTask);
            } else {
                pendingModelLoad = modelLoadTask;
            }
        }

        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        serviceRunning = false;
        surfaceReady = false;
        pendingModelLoad = null;

        if (loadingStateListener != null) {
            loadingStateListener.onServiceStopped();
        }

        Live2DNativeBridge.setMotionTextCallback(null);
        Live2DNativeBridge.setVarFloatChangedCallback(null);
        Live2DNativeBridge.setCostumeChangedCallback(null);
        Live2DNativeBridge.setMotionSoundCallback(null);
        motionSoundPlayer.release();
        if (textBubbleOverlay != null) {
            textBubbleOverlay.dismiss();
            textBubbleOverlay = null;
        }

        SharedPreferences prefs = getSharedPreferences(PREF_NAME, MODE_PRIVATE);
        prefs.unregisterOnSharedPreferenceChangeListener(prefsListener);

        // Unregister shake sensor
        if (sensorManager != null) {
            sensorManager.unregisterListener(sensorListener);
            sensorManager = null;
        }

        try {
            String currentExpr = Live2DNativeBridge.nativeGetCurrentExpression();
            if (currentExpr != null && !currentExpr.isEmpty()) {
                prefs.edit().putString("last_expression", currentExpr).apply();
            }
        } catch (Exception ignored) {}

        SharedPreferences.Editor editor = prefs.edit().putFloat("scale", scaleFactor);
        if (layoutParams != null) {
            editor.putInt("window_x", layoutParams.x);
            editor.putInt("window_y", layoutParams.y);
        }
        editor.apply();

        if (glSurfaceView != null) {
            glSurfaceView.setOnTouchListener(null);
            final java.util.concurrent.CountDownLatch latch = new java.util.concurrent.CountDownLatch(1);
            glSurfaceView.queueEvent(() -> {
                try {
                    Live2DNativeBridge.nativeRelease();
                } finally {
                    latch.countDown();
                }
            });
            try {
                latch.await(5000, java.util.concurrent.TimeUnit.MILLISECONDS);
            } catch (InterruptedException ignored) {}
            // Pause GL rendering before removing from window to prevent
            // SurfaceView.performDrawFinished NPE on pending frames
            glSurfaceView.onPause();
            try {
                windowManager.removeView(glSurfaceView);
            } catch (Exception ignored) {}
            glSurfaceView = null;
        }
        super.onDestroy();
    }

    private void refreshScreenSize() {
        screenWidth = 0;
        screenHeight = 0;
        initScreenSize();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    /**
     * Main touch handler for the floating pet window.
     * Handles: edge-hide expand on tap, drag-to-move, long-press for menu, double-tap gesture.
     * Touch events are also forwarded to native Live2D for model interaction (head tracking, etc.).
     */
    @SuppressLint("ClickableViewAccessibility")
    private boolean onTouch(View v, MotionEvent event) {
        // Edge-hide: only exposed area is touchable when hidden
        if (edgeHideManager != null && edgeHideManager.isEnabled() && edgeHideManager.isHidden()) {
            if (event.getAction() == MotionEvent.ACTION_DOWN) {
                if (edgeHideManager.isTouchOnExposedArea(event.getRawX(), event.getRawY(),
                        layoutParams, glSurfaceView.getWidth(), glSurfaceView.getHeight())) {
                    edgeHideManager.expand(glSurfaceView, windowManager, layoutParams,
                            screenWidth, screenHeight);
                    return true;
                }
            }
            return false;
        }

        float localX = event.getX();
        float localY = event.getY();

        // Use getActionMasked() to correctly handle multi-touch pointer indices.
        // getAction() encodes pointer index in upper bits, breaking ACTION_POINTER_DOWN/UP.
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                touchOffsetX = event.getRawX() - layoutParams.x;
                touchOffsetY = event.getRawY() - layoutParams.y;
                isDragging = false;
                wasPinching = false;
                isPinching = false;
                lastPinchDistance = 0;
                touchDownTime = System.currentTimeMillis();
                final float downX = localX;
                final float downY = localY;
                glSurfaceView.queueEvent(() ->
                        Live2DNativeBridge.nativeOnTouchesBegan(downX, downY));
                glSurfaceView.requestRender();
                return true;

            case MotionEvent.ACTION_MOVE:
                // Pinch detection with 2 fingers
                if (event.getPointerCount() >= 2) {
                    isPinching = true;
                    wasPinching = true;
                    float currentDist = getFingerDistance(event);
                    if (lastPinchDistance > 0) {
                        float ratio = currentDist / lastPinchDistance;
                        if (ratio > PINCH_THRESHOLD) {
                            // Pinch out
                            glSurfaceView.queueEvent(() ->
                                    Live2DNativeBridge.nativeOnGestureEvent(8, true));
                            lastPinchDistance = currentDist;
                        } else if (ratio < 1.0f / PINCH_THRESHOLD) {
                            // Pinch in
                            glSurfaceView.queueEvent(() ->
                                    Live2DNativeBridge.nativeOnGestureEvent(8, false));
                            lastPinchDistance = currentDist;
                        }
                    }
                    return true;
                }

                if (wasPinching) {
                    // Ignore single-finger moves after a pinch occurred
                    return true;
                }

                // Update localX/localY so ACTION_UP uses the current finger position
                localX = event.getX();
                localY = event.getY();

                if (dragEnabled) {
                    if (!isDragging) {
                        float dx = event.getRawX() - (layoutParams.x + touchOffsetX);
                        float dy = event.getRawY() - (layoutParams.y + touchOffsetY);
                        if (Math.hypot(dx, dy) > DRAG_THRESHOLD) {
                            isDragging = true;
                        }
                    }
                    if (isDragging) {
                        layoutParams.x = (int) (event.getRawX() - touchOffsetX);
                        layoutParams.y = (int) (event.getRawY() - touchOffsetY);
                        windowManager.updateViewLayout(glSurfaceView, layoutParams);
                    }
                }
                if (!isDragging) {
                    // Before drag threshold: forward to native for model interaction
                    final float moveX = localX;
                    final float moveY = localY;
                    glSurfaceView.queueEvent(() ->
                            Live2DNativeBridge.nativeOnTouchesMoved(moveX, moveY));
                    glSurfaceView.requestRender();
                } else {
                    // During drag: forward converted coordinates to native
                    final float rawX = event.getRawX();
                    final float rawY = event.getRawY();
                    final int winX = layoutParams.x;
                    final int winY = layoutParams.y;
                    glSurfaceView.queueEvent(() ->
                            Live2DNativeBridge.nativeOnTouchesMoved(rawX - winX, rawY - winY));
                    glSurfaceView.requestRender();
                }
                return true;

            case MotionEvent.ACTION_POINTER_DOWN:
                // Second finger down: start pinch tracking
                if (event.getPointerCount() == 2) {
                    lastPinchDistance = getFingerDistance(event);
                    isPinching = true;
                    wasPinching = true;
                }
                return true;

            case MotionEvent.ACTION_POINTER_UP:
                // Finger lifted during multi-touch
                if (event.getPointerCount() <= 2) {
                    isPinching = false;
                    lastPinchDistance = 0;
                }
                return true;

            case MotionEvent.ACTION_CANCEL:
                // System cancelled touch (incoming call, system gesture, etc.)
                isPinching = false;
                wasPinching = false;
                lastPinchDistance = 0;
                glSurfaceView.queueEvent(() ->
                        Live2DNativeBridge.nativeOnTouchesCancelled());
                return true;

            case MotionEvent.ACTION_UP:
                isPinching = false;
                lastPinchDistance = 0;
                // Skip tap/long-press/double-tap if a pinch just occurred
                if (wasPinching) {
                    wasPinching = false;
                    glSurfaceView.queueEvent(() ->
                            Live2DNativeBridge.nativeOnTouchesCancelled());
                    return true;
                }
                wasPinching = false;
                long duration = System.currentTimeMillis() - touchDownTime;
                if (!isDragging) {
                    v.performClick();
                    if (duration >= LONG_PRESS_MS) {
                        // Long-press: cancel touch tracking (without triggering hit/release
                        // behavior), then fire gesture event + menu.
                        glSurfaceView.queueEvent(() -> {
                            Live2DNativeBridge.nativeOnTouchesCancelled();
                            Live2DNativeBridge.nativeOnGestureEvent(1, true);
                        });
                        boolean menuEnabled = getSharedPreferences(PREF_NAME, MODE_PRIVATE)
                                .getBoolean("long_press_menu_enable", true);
                        if (menuEnabled && currentModelDir != null) {
                            actionMenuManager.show(FloatingWindowService.this, glSurfaceView, currentModelDir);
                        }
                        return true;
                    }
                    long now = System.currentTimeMillis();
                    if (now - lastTapTime < DOUBLE_TAP_MS) {
                        // Double-tap: cancel the ongoing touch to reset native hit area state
                        // (_pressedHitAreaIndex, ParamHit, etc.), then fire gesture event.
                        glSurfaceView.queueEvent(() -> {
                            Live2DNativeBridge.nativeOnTouchesCancelled();
                            Live2DNativeBridge.nativeOnGestureEvent(0, true);
                        });
                        lastTapTime = 0; // Reset to prevent next single tap from being misdetected as double-tap
                        return true;
                    }
                    lastTapTime = now;

                    // Single tap: fire touchesEnded for model hit detection
                    final float tapX = localX;
                    final float tapY = localY;
                    glSurfaceView.queueEvent(() ->
                            Live2DNativeBridge.nativeOnTouchesEnded(tapX, tapY, false));
                    glSurfaceView.requestRender();
                } else {
                    // Drag ended — must notify native so _captured and _touchActive reset.
                    final float endX = localX;
                    final float endY = localY;
                    glSurfaceView.queueEvent(() ->
                            Live2DNativeBridge.nativeOnTouchesEnded(endX, endY, true));
                    getSharedPreferences(PREF_NAME, MODE_PRIVATE)
                            .edit()
                            .putInt("window_x", layoutParams.x)
                            .putInt("window_y", layoutParams.y)
                            .apply();
                }
                return true;
        }
        return false;
    }

    private void setClickThrough(boolean enabled) {
        clickThrough = enabled;
        if (glSurfaceView == null || layoutParams == null) return;

        if (enabled) {
            float ctAlpha = getSharedPreferences(PREF_NAME, MODE_PRIVATE)
                    .getFloat("click_through_alpha", 0.4f);
            layoutParams.flags |= WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE;
            glSurfaceView.setAlpha(ctAlpha);
        } else {
            layoutParams.flags &= ~WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE;
            glSurfaceView.setAlpha(1.0f);
        }
        windowManager.updateViewLayout(glSurfaceView, layoutParams);

        NotificationManager nm = getSystemService(NotificationManager.class);
        nm.notify(NOTIFICATION_ID, buildNotification());

        Toast.makeText(this,
                enabled ? R.string.toast_ct_on : R.string.toast_ct_off,
                Toast.LENGTH_SHORT).show();
    }

    private void broadcastLoadingState(boolean isLoading, String modelPath) {
        new android.os.Handler(android.os.Looper.getMainLooper()).post(() -> {
            if (loadingStateListener != null) {
                loadingStateListener.onLoadingStateChanged(isLoading, modelPath);
            }

            NotificationManager nm = getSystemService(NotificationManager.class);
            nm.notify(NOTIFICATION_ID, buildNotification());
        });
    }

    private void applyWindowSize() {
        int size = (int) (BASE_SIZE * scaleFactor);
        layoutParams.width = size;
        layoutParams.height = size;
        windowManager.updateViewLayout(glSurfaceView, layoutParams);
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID,
                    getString(R.string.notif_channel_name),
                    NotificationManager.IMPORTANCE_LOW);
            channel.setDescription(getString(R.string.notif_channel_desc));
            getSystemService(NotificationManager.class).createNotificationChannel(channel);
        }
    }

    private Notification buildNotification() {
        Intent stopIntent = new Intent(this, FloatingWindowService.class);
        stopIntent.setAction(ACTION_STOP);
        PendingIntent stopPi = PendingIntent.getService(this, 0, stopIntent,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);

        Intent mainIntent = new Intent(this, MainActivity.class);
        PendingIntent mainPi = PendingIntent.getActivity(this, 0, mainIntent,
                PendingIntent.FLAG_IMMUTABLE);

        Intent clickThroughIntent = new Intent(this, FloatingWindowService.class);
        clickThroughIntent.setAction(ACTION_TOGGLE_CLICK_THROUGH);
        PendingIntent clickThroughPi = PendingIntent.getService(this, 1, clickThroughIntent,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);

        String ctLabel = getString(clickThrough ? R.string.notif_action_ct_off : R.string.notif_action_ct_on);
        int ctIcon = clickThrough
                ? android.R.drawable.ic_menu_revert
                : android.R.drawable.ic_menu_view;

        String statusText;
        if (isLoadingModel) {
            statusText = getString(R.string.notif_loading);
        } else if (clickThrough) {
            statusText = getString(R.string.notif_click_through);
        } else {
            statusText = getString(R.string.notif_running);
        }

        NotificationCompat.Builder builder = new NotificationCompat.Builder(this, CHANNEL_ID)
                .setContentTitle(getString(R.string.notif_title))
                .setContentText(statusText)
                .setSmallIcon(android.R.drawable.ic_menu_compass)
                .setContentIntent(mainPi)
                .addAction(ctIcon, ctLabel, clickThroughPi)
                .addAction(android.R.drawable.ic_menu_close_clear_cancel,
                        getString(R.string.notif_action_stop), stopPi)
                .setOngoing(true);

        return builder.build();
    }

    @SuppressWarnings("deprecation")
    private static int getOverlayWindowType() {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                : WindowManager.LayoutParams.TYPE_PHONE;
    }

    @SuppressWarnings("deprecation")
    private void initScreenSize() {
        if (screenWidth > 0) return;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            android.graphics.Rect bounds = windowManager.getCurrentWindowMetrics().getBounds();
            screenWidth = bounds.width();
            screenHeight = bounds.height();
        } else {
            @SuppressWarnings("deprecation")
            Display display = windowManager.getDefaultDisplay();
            android.graphics.Point p = new android.graphics.Point();
            display.getRealSize(p);
            screenWidth = p.x;
            screenHeight = p.y;
        }
    }

    /**
     * Calculate distance between two fingers for pinch detection.
     */
    private float getFingerDistance(MotionEvent event) {
        if (event.getPointerCount() < 2) return 0;
        float dx = event.getX(0) - event.getX(1);
        float dy = event.getY(0) - event.getY(1);
        return (float) Math.sqrt(dx * dx + dy * dy);
    }

    /**
     * Shake detection via accelerometer.
     * Fires nativeOnGestureEvent(9, true) when shake magnitude exceeds threshold.
     */
    private final SensorEventListener sensorListener = new SensorEventListener() {
        @Override
        public void onSensorChanged(SensorEvent event) {
            if (event.sensor.getType() != Sensor.TYPE_ACCELEROMETER) return;

            float x = event.values[0];
            float y = event.values[1];
            float z = event.values[2];

            if (firstAccelReading) {
                lastAccelX = x;
                lastAccelY = y;
                lastAccelZ = z;
                firstAccelReading = false;
                return;
            }

            float deltaX = x - lastAccelX;
            float deltaY = y - lastAccelY;
            float deltaZ = z - lastAccelZ;
            float magnitude = (float) Math.sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);

            long now = System.currentTimeMillis();
            if (magnitude > SHAKE_THRESHOLD && now - lastShakeTime > SHAKE_COOLDOWN_MS) {
                lastShakeTime = now;
                if (glSurfaceView != null) {
                    glSurfaceView.queueEvent(() ->
                            Live2DNativeBridge.nativeOnGestureEvent(9, true));
                }
            }

            lastAccelX = x;
            lastAccelY = y;
            lastAccelZ = z;
        }

        @Override
        public void onAccuracyChanged(Sensor sensor, int accuracy) {}
    };
}
