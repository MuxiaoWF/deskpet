package com.muxiao.deskpet;

import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Imports Live2D models from SAF (Storage Access Framework) directory URIs.
 * Recursively copies all files preserving directory structure into app internal storage.
 */
public class ModelImporter {

    private static final String TAG = "ModelImporter";

    // Failure reasons exposed via getLastError() so the UI can show a
    // specific message instead of a generic "import failed"
    public static final String ERR_EMPTY = "empty";
    public static final String ERR_NO_MODEL_JSON = "no_model_json";
    public static final String ERR_IO = "io";
    public static final String ERR_CANCELLED = "cancelled";

    private final Context context;
    private final List<String> errors = new ArrayList<>();
    private volatile AtomicBoolean cancelFlag;
    private volatile String lastError;

    public ModelImporter(Context context) {
        this.context = context;
    }

    /** Reason of the last failure, or null if the last import succeeded. */
    public String getLastError() {
        return lastError;
    }

    public String importModel(Uri treeUri, String customName) {
        return importModel(treeUri, customName, null, null);
    }

    /**
     * Import a model from a SAF directory URI.
     * Copies all files, finds the model JSON, and extracts config.mlve configs if present.
     *
     * @param treeUri    SAF tree URI of the source directory
     * @param customName optional custom name for the model directory
     * @param cancelFlag when set to true, aborts the import and cleans up (nullable)
     * @param progress   progress callback, null-safe (nullable)
     * @return absolute path to the model JSON file, or null on failure (see getLastError)
     */
    public String importModel(Uri treeUri, String customName,
                              AtomicBoolean cancelFlag, ImportProgressListener progress) {
        this.cancelFlag = cancelFlag;
        this.lastError = null;
        File modelDir = null;
        try {
            notifyScan(progress);

            List<UriFile> files = listFiles(treeUri);
            if (files.isEmpty()) {
                lastError = listHadError ? ERR_IO : ERR_EMPTY;
                return null;
            }

            // Find the model settings file (.model3.json preferred over .model.json)
            // Prioritize top-level files to avoid picking up sub-models
            String modelJsonName = findModelJsonName(files);
            if (modelJsonName == null) {
                lastError = ERR_NO_MODEL_JSON;
                return null;
            }

            // Determine directory name: custom > derived from model JSON filename
            String modelName = resolveModelName(customName, modelJsonName);
            modelDir = createUniqueModelDir(modelName);
            modelDir.mkdirs();

            // Copy all files preserving directory structure
            int copied = 0;
            for (UriFile f : files) {
                if (isCancelled()) {
                    lastError = ERR_CANCELLED;
                    return null;
                }
                String relativePath = f.relativePath;
                if (relativePath == null || relativePath.isEmpty()) continue;

                File destFile = new File(modelDir, relativePath);
                Objects.requireNonNull(destFile.getParentFile()).mkdirs();

                try (InputStream is = context.getContentResolver().openInputStream(f.uri)) {
                    if (is != null) {
                        try (OutputStream fos = new FileOutputStream(destFile)) {
                            byte[] buf = new byte[8192];
                            int len;
                            while ((len = is.read(buf)) > 0) {
                                if (isCancelled()) {
                                    lastError = ERR_CANCELLED;
                                    return null;
                                }
                                fos.write(buf, 0, len);
                            }
                        }
                    }
                } catch (Exception e) {
                    Log.e(TAG, "Failed to copy file: " + f.relativePath, e);
                    errors.add(f.relativePath);
                }
                copied++;
                notifyCopy(progress, copied, files.size());
            }

            // The model JSON is the one file that must have been copied successfully
            File modelJsonFile = new File(modelDir, modelJsonName);
            if (!modelJsonFile.exists() || modelJsonFile.length() == 0) {
                Log.e(TAG, "Model JSON was not copied: " + modelJsonName
                        + (errors.isEmpty() ? "" : " (failed: " + errors + ")"));
                lastError = ERR_IO;
                return null;
            }

            // Extract configs from config.mlve if present in the imported folder
            extractConfigsFromMlve(modelDir);

            return modelJsonFile.getAbsolutePath();
        } catch (Exception e) {
            Log.e(TAG, "Failed to import model from URI", e);
            lastError = ERR_IO;
            return null;
        } finally {
            if (lastError != null && modelDir != null && modelDir.exists()) {
                FileUtils.deleteRecursive(modelDir);
            }
        }
    }

    private boolean isCancelled() {
        return cancelFlag != null && cancelFlag.get();
    }

    private static void notifyScan(ImportProgressListener progress) {
        if (progress != null) progress.onProgress(ImportProgressListener.STAGE_SCAN, -1, -1);
    }

    private static void notifyCopy(ImportProgressListener progress, int done, int total) {
        if (progress != null) progress.onProgress(ImportProgressListener.STAGE_COPY, done, total);
    }

    /**
     * Find the model JSON filename from the file list.
     * Prefers .model3.json over .model.json, and top-level files over subdirectory files.
     */
    private String findModelJsonName(List<UriFile> files) {
        // Priority 1: top-level .model3.json
        for (UriFile f : files) {
            if (f.name.endsWith(".model3.json") && !f.relativePath.contains("/")) {
                return f.name;
            }
        }
        // Priority 2: top-level .model.json
        for (UriFile f : files) {
            if (f.name.endsWith(".model.json") && !f.relativePath.contains("/")
                    && !f.name.endsWith(".model3.json")) {
                return f.name;
            }
        }
        // Priority 3: any .model3.json (subdirectory)
        for (UriFile f : files) {
            if (f.name.endsWith(".model3.json")) {
                return f.name;
            }
        }
        // Priority 4: any .model.json (subdirectory)
        for (UriFile f : files) {
            if (f.name.endsWith(".model.json") && !f.name.endsWith(".model3.json")) {
                return f.name;
            }
        }
        return null;
    }

    /** Resolve directory name from custom name or model JSON filename. */
    private String resolveModelName(String customName, String modelJsonName) {
        if (customName != null && !customName.isEmpty()) {
            return customName.replaceAll("[<>:\"|?*\\\\/]", "_").replaceAll("^[\\s.]+|[\\s.]+$", "");
        }
        if (modelJsonName.endsWith(".model3.json")) {
            return modelJsonName.replace(".model3.json", "");
        }
        return modelJsonName.replace(".model.json", "");
    }

    /** Create a unique model directory, appending _2, _3, etc. if the name already exists. */
    private File createUniqueModelDir(String modelName) {
        File modelDir = new File(context.getFilesDir(), "models/" + modelName);
        if (modelDir.exists()) {
            int suffix = 2;
            while (new File(context.getFilesDir(), "models/" + modelName + "_" + suffix).exists()) {
                suffix++;
            }
            modelName = modelName + "_" + suffix;
            modelDir = new File(context.getFilesDir(), "models/" + modelName);
        }
        return modelDir;
    }

    // --- SAF Directory Listing ---

    private boolean listHadError;

    private List<UriFile> listFiles(Uri treeUri) {
        List<UriFile> result = new ArrayList<>();
        listHadError = false;
        listFilesRecursive(treeUri, "", result);
        return result;
    }

    private void listFilesRecursive(Uri dirUri, String prefix, List<UriFile> result) {
        if (isCancelled()) return;
        String docId;
        try {
            docId = DocumentsContract.getDocumentId(dirUri);
        } catch (IllegalArgumentException e) {
            // Fallback: extract document ID from the URI path manually
            // Handles encoded Chinese characters that confuse getDocumentId()
            String path = dirUri.getPath();
            if (path == null) return;
            int treeIdx = path.indexOf("/tree/");
            if (treeIdx >= 0) {
                docId = path.substring(treeIdx + 6);
            } else {
                int docIdx = path.indexOf("/document/");
                if (docIdx >= 0) {
                    docId = path.substring(docIdx + 10);
                } else {
                    return;
                }
            }
        }
        Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(dirUri, docId);
        try (Cursor cursor = context.getContentResolver().query(childrenUri,
                new String[]{
                        DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                        DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                        DocumentsContract.Document.COLUMN_MIME_TYPE
                }, null, null, null)) {
            if (cursor == null) return;

            while (cursor.moveToNext()) {
                String childDocId = cursor.getString(0);
                String name = cursor.getString(1);
                String mimeType = cursor.getString(2);
                Uri docUri = DocumentsContract.buildDocumentUriUsingTree(dirUri, childDocId);

                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mimeType)) {
                    listFilesRecursive(docUri, prefix.isEmpty() ? name : prefix + "/" + name, result);
                } else {
                    String relPath = prefix.isEmpty() ? name : prefix + "/" + name;
                    result.add(new UriFile(docUri, name, relPath));
                }
            }
        } catch (Exception e) {
            listHadError = true;
            Log.e(TAG, "Failed to list directory contents", e);
        }
    }

    private static class UriFile {
        final Uri uri;
        final String name;
        final String relativePath;

        UriFile(Uri uri, String name, String relativePath) {
            this.uri = uri;
            this.name = name;
            this.relativePath = relativePath;
        }
    }

    // --- Config Extraction from config.mlve ---

    /**
     * If config.mlve exists in the model directory, extract configs from it
     * and save as separate JSON files (hit_area_config.json, model_config.json).
     * This allows the "Load" button to restore LPK-style config data for folder imports.
     */
    private void extractConfigsFromMlve(File modelDir) {
        File mlveFile = new File(modelDir, "config.mlve");
        if (!mlveFile.exists()) return;

        try {
            String mlveStr = FileUtils.readFileAsString(mlveFile);
            if (mlveStr == null) return;
            JSONObject mlve = new JSONObject(mlveStr);
            extractAndSaveConfigs(mlve, modelDir);
        } catch (Exception e) {
            Log.w(TAG, "Failed to extract configs from config.mlve", e);
        }
    }

    /**
     * Extract hit area and model config from mlve JSON and persist to files.
     * Mirrors LpkUnpacker.extractHitAreaConfig / extractModelConfig logic.
     */
    private static void extractAndSaveConfigs(JSONObject mlve, File modelDir) {
        try {
            // Extract hit areas
            JSONArray hitAreas = mlve.optJSONArray("HitAreas");
            if (hitAreas == null) hitAreas = mlve.optJSONArray("hit_areas");
            if (hitAreas != null && hitAreas.length() > 0) {
                JSONArray result = new JSONArray();
                for (int i = 0; i < hitAreas.length(); i++) {
                    JSONObject area = hitAreas.optJSONObject(i);
                    if (area == null) continue;
                    JSONObject entry = new JSONObject();
                    entry.put("name", FileUtils.optStringEither(area, "Name", "name"));
                    entry.put("id", FileUtils.optStringEither(area, "Id", "id"));
                    entry.put("motion", FileUtils.optStringEither(area, "Motion", "motion"));
                    entry.put("enter_mtn", FileUtils.optStringEither(area, "EnterMtn", "enter_mtn"));
                    entry.put("exit_mtn", FileUtils.optStringEither(area, "ExitMtn", "exit_mtn"));
                    entry.put("enabled", area.optBoolean("enabled", true));
                    result.put(entry);
                }
                if (result.length() > 0) {
                    FileUtils.writeFile(new File(modelDir, "hit_area_config.json"),
                            result.toString().getBytes(StandardCharsets.UTF_8));
                }
            }

            // Extract model config (motions, controllers, groups, etc.)
            JSONObject modelConfig = new JSONObject();
            JSONObject motions = mlve.optJSONObject("Motions");
            if (motions == null) motions = mlve.optJSONObject("motions");
            if (motions != null) modelConfig.put("motions", motions);

            JSONArray groups = mlve.optJSONArray("Groups");
            if (groups == null) groups = mlve.optJSONArray("groups");
            if (groups != null) modelConfig.put("groups", groups);

            JSONObject controllers = mlve.optJSONObject("controllers");
            if (controllers == null) controllers = mlve.optJSONObject("Controllers");
            if (controllers != null) modelConfig.put("controllers", controllers);

            if (modelConfig.length() > 0) {
                FileUtils.writeFile(new File(modelDir, "model_config.json"),
                        modelConfig.toString().getBytes(StandardCharsets.UTF_8));
            }
        } catch (Exception e) {
            Log.w(TAG, "Failed to extract/save configs", e);
        }
    }
}
