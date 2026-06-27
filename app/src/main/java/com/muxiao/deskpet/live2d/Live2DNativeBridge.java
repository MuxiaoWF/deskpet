package com.muxiao.deskpet.live2d;

import android.annotation.SuppressLint;
import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;

/**
 * JNI bridge between Java and native Live2D rendering code.
 * Declares native methods and provides the LoadFile() callback for native code
 * to load files from assets or filesystem.
 */
public class Live2DNativeBridge {

    public static native void nativeInit();
    public static native void nativeOnSurfaceCreated();
    public static native void nativeOnSurfaceChanged(int width, int height);
    public static native void nativeOnDrawFrame();
    public static native void nativeOnTouchesBegan(float pointX, float pointY);
    public static native void nativeOnTouchesEnded(float pointX, float pointY, boolean wasDragging);
    public static native void nativeOnTouchesCancelled();
    public static native void nativeOnTouchesMoved(float pointX, float pointY);
    public static native void nativeLoadModel(String modelPath);
    public static native void nativeLoadMotion(String motionGroup, int index, int priority, float fadeInTime, float fadeOutTime);
    public static native void nativeLoadMotionByGroup(String motionGroup);
    public static native void nativeSetExpression(String expressionName);
    public static native void nativeToggleParam(String paramName);
    public static native void nativeRelease();

    // Config-driven features
    public static native void nativeSetHitAreaConfig(String hitAreaJson);
    public static native void nativeSetAabbHitTestMode(boolean enabled);
    public static native void nativeSetLookAtConfig(String lookAtJson);
    public static native void nativeSetExpressionBlend(String expressionsJson);

    // Expression control
    public static native void nativeLastExpression();
    public static native String nativeGetCurrentExpression();
    public static native void nativeSetExpressionBlendMode(int mode);  // 0=Overwrite, 1=Add, 2=Multiply

    // Physics control
    public static native void nativeSetPhysicsWeight(float weight);
    public static native void nativeEnablePhysicsWithFade(float targetWeight, float fadeTime);
    public static native void nativeDisablePhysicsWithFade(float fadeTime);

    // Full model config (motion metadata, groups, controllers)
    public static native void nativeSetModelConfig(String configJson);
    public static native String nativeGetMotionMetaList();
    public static native String nativeGetGroupList();

    // Gesture events for KeyTrigger controller
    public static native void nativeOnGestureEvent(int gestureType, boolean isDown);

    // Face tracking follow mode
    public static native void nativeSetFollowMode(int mode);  // 0=AUTO_FOLLOW, 1=CLICK_FOLLOW, 2=DISABLE

    // Debug: show/hide hit area rectangles
    public static native void nativeShowHitAreas(boolean show);

    // Menu visibility state (synced from ActionMenuManager to prevent tap-through)
    public static native void nativeSetMenuVisible(boolean visible);

    // Multi-model support
    public static native void nativeLoadModelAt(String modelPath, int slot);
    public static native void nativeRemoveModel(int slot);
    public static native void nativeSelectModel(int slot);

    // Slot state (component/clothing visibility toggle)
    public static native String nativeGetSlotState();
    public static native void nativeSetSlotState(String groupName, int slotIndex);

    // Random speak control
    public static native void nativeSetRandomSpeakEnabled(boolean enabled);
    public static native void nativeSetRandomSpeakInterval(int seconds);

    // Model transform
    public static native void nativeSetMirror(boolean mirror);
    public static native void nativeSetRotation(float degrees);
    public static native void nativeSetTouchOffset(float dx, float dy);
    public static native void nativeSetTouchScale(float scale);

    public interface MotionTextCallback {
        void onMotionText(String text, int durationMs, int delayMs);
    }

    private static volatile MotionTextCallback motionTextCallback;

    public static void setMotionTextCallback(MotionTextCallback callback) {
        motionTextCallback = callback;
    }

    /**
     * Called from native code when a motion with a text field is triggered.
     */
    public static void onMotionText(String text, int durationMs, int delayMs) {
        if (motionTextCallback != null) {
            motionTextCallback.onMotionText(text, durationMs, delayMs);
        }
    }

    public interface MotionSoundCallback {
        void onMotionSound(String soundPath, int delayMs);
    }

    private static volatile MotionSoundCallback motionSoundCallback;

    public static void setMotionSoundCallback(MotionSoundCallback callback) {
        motionSoundCallback = callback;
    }

    /**
     * Called from native code when a motion with a sound field is triggered.
     */
    public static void onMotionSound(String soundPath, int delayMs) {
        if (motionSoundCallback != null) {
            motionSoundCallback.onMotionSound(soundPath, delayMs);
        }
    }

    public interface CostumeChangedCallback {
        void onCostumeChanged(String costumeName, String groupName);
    }

    private static volatile CostumeChangedCallback costumeChangedCallback;

    public static void setCostumeChangedCallback(CostumeChangedCallback callback) {
        costumeChangedCallback = callback;
    }

    /**
     * Called from native code when a costume change occurs.
     * @param costumeName The costume set name (empty for single group toggle)
     * @param groupName The group target that was toggled (empty for set-level changes)
     */
    public static void onCostumeChanged(String costumeName, String groupName) {
        if (costumeChangedCallback != null) {
            costumeChangedCallback.onCostumeChanged(costumeName, groupName);
        }
    }

    public interface VarFloatChangedCallback {
        void onVarFloatChanged(String name, float value);
    }

    private static volatile VarFloatChangedCallback varFloatChangedCallback;

    public static void setVarFloatChangedCallback(VarFloatChangedCallback callback) {
        varFloatChangedCallback = callback;
    }

    /**
     * Called from native code when a VarFloat value changes.
     */
    public static void onVarFloatChanged(String name, float value) {
        if (varFloatChangedCallback != null) {
            varFloatChangedCallback.onVarFloatChanged(name, value);
        }
    }

    public static void SetContext(Context ctx) {
        context = ctx.getApplicationContext();
    }

    private static final String TAG = "Live2DNativeBridge";

    /**
     * Called from native code (JNI) to load a file as byte array.
     * <p>
     * Search order:
     *   Absolute paths ("/..."): filesystem only (imported models).
     *   Relative paths: assets > assets/Shaders/ > filesystem.
     * <p>
     * The Shaders/ fallback handles GLSL source files that the native renderer
     * requests by bare filename but are stored under assets/Shaders/.
     */
    public static byte[] LoadFile(String filePath) {
        InputStream fileData = null;
        String source = "none";
        try {
            boolean isAbsolute = filePath.startsWith("/");

            // For absolute paths, check filesystem first (imported models)
            if (isAbsolute) {
                File file = new File(filePath);
                if (file.exists()) {
                    fileData = new FileInputStream(file);
                    source = "filesystem";
                }
            }

            // For relative paths, try assets first
            if (fileData == null && context != null) {
                try {
                    fileData = context.getAssets().open(filePath);
                    source = "assets";
                } catch (IOException ignored) {
                }

                // Fallback: try Shaders/ subdirectory (for shader source files)
                if (fileData == null) {
                    try {
                        fileData = context.getAssets().open("Shaders/" + filePath);
                        source = "assets/Shaders/";
                    } catch (IOException ignored) {
                    }
                }
            }

            // Final fallback: filesystem (for relative paths that weren't in assets)
            if (fileData == null) {
                File file = new File(filePath);
                if (file.exists()) {
                    fileData = new FileInputStream(file);
                    source = "filesystem";
                } else {
                    Log.w(TAG, "LoadFile: file not found: " + filePath);
                    return null;
                }
            }

            java.io.ByteArrayOutputStream bos = new java.io.ByteArrayOutputStream();
            byte[] buf = new byte[32768];
            int len;
            while ((len = fileData.read(buf)) != -1) {
                bos.write(buf, 0, len);
            }
            byte[] result = bos.toByteArray();
            return result;
        } catch (IOException e) {
            Log.e(TAG, "LoadFile IOException: " + filePath, e);
            return null;
        } finally {
            try {
                if (fileData != null) {
                    fileData.close();
                }
            } catch (IOException ignored) {
            }
        }
    }

    @SuppressLint("StaticFieldLeak")
    private static Context context;
    private static final String LIBRARY_NAME = "live2d_native";

    static {
        System.loadLibrary(LIBRARY_NAME);
    }
}
