package com.muxiao.deskpet;

import android.app.AlertDialog;
import android.content.Context;

import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/**
 * Shared file and UI utilities used across multiple classes.
 */
public final class FileUtils {

    private FileUtils() {}

    // --- File I/O ---

    /** Read an entire file into a UTF-8 string. Returns null on failure. */
    public static String readFileAsString(File file) {
        try (FileInputStream fis = new FileInputStream(file)) {
            byte[] data = new byte[(int) file.length()];
            int offset = 0;
            while (offset < data.length) {
                int read = fis.read(data, offset, data.length - offset);
                if (read < 0) break;
                offset += read;
            }
            return new String(data, 0, offset, StandardCharsets.UTF_8);
        } catch (Exception e) {
            return null;
        }
    }

    /** Write raw bytes to a file, creating parent directories as needed. */
    public static void writeFile(File file, byte[] data) {
        try (FileOutputStream fos = new FileOutputStream(file)) {
            fos.write(data);
        } catch (Exception e) {
            android.util.Log.w("FileUtils", "Failed to write file: " + file, e);
        }
    }

    /** Copy all bytes from an InputStream to an OutputStream. */
    public static void copyStream(InputStream is, OutputStream os) throws Exception {
        byte[] buf = new byte[8192];
        int len;
        while ((len = is.read(buf)) > 0) {
            os.write(buf, 0, len);
        }
    }

    /** Recursively delete a file or directory. Returns true if all deletions succeeded. */
    public static boolean deleteRecursive(File file) {
        boolean success = true;
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) {
                for (File child : children) {
                    if (!deleteRecursive(child)) success = false;
                }
            }
        }
        if (!file.delete()) success = false;
        return success;
    }

    // --- JSON Helpers ---

    /** Get a string from JSON, trying key1 first then key2 (handles PascalCase/snake_case). */
    public static String optStringEither(JSONObject obj, String key1, String key2) {
        String v = obj.optString(key1, "");
        return v.isEmpty() ? obj.optString(key2, "") : v;
    }

    /** Get a double from JSON, trying key1 first then key2. */
    public static double optDoubleEither(JSONObject obj, String key1, String key2) {
        if (obj.has(key1)) return obj.optDouble(key1, 0);
        return obj.optDouble(key2, 0);
    }

    // --- MIME Type ---

    /** Guess MIME type from filename extension. */
    public static String guessMimeType(String fileName) {
        String lower = fileName.toLowerCase(Locale.ROOT);
        if (lower.endsWith(".json")) return "application/json";
        if (lower.endsWith(".moc3")) return "application/octet-stream";
        if (lower.endsWith(".moc")) return "application/octet-stream";
        if (lower.endsWith(".png")) return "image/png";
        if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) return "image/jpeg";
        if (lower.endsWith(".wav")) return "audio/wav";
        if (lower.endsWith(".mp3")) return "audio/mpeg";
        return "application/octet-stream";
    }

    // --- Preview Image ---

    /** Known preview image filenames, checked in priority order. */
    private static final String[] PREVIEW_NAMES = {
            "preview.png", "preview.jpg", "thumbnail.png", "icon.png", "thumb.png"
    };

    /**
     * Find a preview image in a model directory.
     * Checks known names first, then falls back to the first image file found.
     */
    public static File findPreviewImage(File dir) {
        for (String name : PREVIEW_NAMES) {
            File f = new File(dir, name);
            if (f.exists() && f.length() > 100) return f;
        }
        File[] images = dir.listFiles((d, name) -> {
            String lower = name.toLowerCase(Locale.ROOT);
            return lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg");
        });
        if (images != null && images.length > 0) return images[0];
        return new File(dir, "preview.png");
    }

    // --- UI Utilities ---

    /** Convert dp to pixels. */
    public static int dp(Context context, float dp) {
        return (int) (dp * context.getResources().getDisplayMetrics().density + 0.5f);
    }

    /**
     * Show a warning dialog when both motion and expression are being disabled.
     * @param context Activity or service context
     * @param onCancel Runnable to execute when user cancels (reverts toggle)
     */
    public static void showBothDisabledWarning(Context context, Runnable onCancel) {
        buildBothDisabledWarning(context, onCancel).show();
    }

    /**
     * Build a warning dialog for when both features are being disabled.
     * Callers that need to set the window type (e.g. overlay) should use this
     * instead of {@link #showBothDisabledWarning} and call {@code showDialog()} themselves.
     * @param context Activity or service context
     * @param onCancel Runnable to execute when user cancels (reverts toggle)
     */
    public static AlertDialog.Builder buildBothDisabledWarning(Context context, Runnable onCancel) {
        return new AlertDialog.Builder(context)
                .setTitle(R.string.dialog_both_disabled_title)
                .setMessage(R.string.dialog_both_disabled_message)
                .setPositiveButton(R.string.dialog_both_disabled_confirm, (d, w) -> d.dismiss())
                .setNegativeButton(R.string.dialog_cancel, (d, w) -> {
                    if (onCancel != null) onCancel.run();
                    d.dismiss();
                })
                .setOnCancelListener(d -> {
                    if (onCancel != null) onCancel.run();
                });
    }
}
