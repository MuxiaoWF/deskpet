package com.muxiao.deskpet;

import android.content.Context;
import android.net.Uri;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Enumeration;
import java.util.Locale;
import java.util.Objects;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * Unpacks Live2D LPK/WPK package files.
 * LPK is a ZIP archive containing:
 *   - config.mlve (or MD5-hashed filename) — JSON manifest
 *   - Encrypted model files (.moc, .moc3, .png, .json, etc.)
 * Supports types: STD_1_0, STD2_0, STM_1_0
 */
public class LpkUnpacker {

    private static final String TAG = "LpkUnpacker";

    // Encrypted file names are 32 hex chars + .bin or .bin3 (case-insensitive)
    private static final java.util.regex.Pattern ENC_FILE_PATTERN =
            java.util.regex.Pattern.compile("[0-9a-fA-F]{32}\\.bin3?");

    private final Context context;

    /**
     * Result of unpacking an LPK/WPK file.
     * Contains the model path and extracted configuration data.
     */
    public static class UnpackResult {
        public final String modelPath;
        public final String hitAreaConfig;  // JSON array of hit area definitions
        public final String lookAtConfig;   // JSON object of look-at parameters
        public final String modelConfig;    // JSON object of model-level config (motions, expressions, etc.)
        public final String previewPath;    // Path to extracted preview image

        public UnpackResult(String modelPath, String hitAreaConfig, String lookAtConfig, String modelConfig, String previewPath) {
            this.modelPath = modelPath;
            this.hitAreaConfig = hitAreaConfig;
            this.lookAtConfig = lookAtConfig;
            this.modelConfig = modelConfig;
            this.previewPath = previewPath;
        }
    }

    /** Tracks the preview path extracted during unpack(). */
    private String lastExtractedPreviewPath;
    /** Tracks the raw config.mlve JSON string for config extraction. */
    private String lastMlveRaw;

    public LpkUnpacker(Context context) {
        this.context = context;
    }

    /**
     * Unpack an LPK/WPK file with an optional custom model name.
     *
     * @param lpkUri     URI of the .lpk or .wpk file
     * @param customName custom name for the model directory, or null to auto-detect
     * @return absolute path to the extracted .model.json or .model3.json, or null on failure
     */
    public String unpack(Uri lpkUri, String customName) {
        lastExtractedPreviewPath = null;
        lastMlveRaw = null;
        File tempLpk = null;
        try {
            tempLpk = new File(context.getCacheDir(), "temp_lpk_" + System.currentTimeMillis() + ".lpk");
            copyUriToFile(lpkUri, tempLpk);
            Log.i(TAG, "LPK file copied to: " + tempLpk.getAbsolutePath() + " size=" + tempLpk.length());

            try (ZipFile zip = new ZipFile(tempLpk)) {
                Log.i(TAG, "ZIP entries count: " + zip.size());

                byte[] outerPreview = findPreviewInZip(zip);
                String mlveRaw = readMlveConfig(zip);

                if (mlveRaw == null) {
                    Log.i(TAG, "No config.mlve found, checking for nested LPK...");
                    String nestedLpkPath = findNestedLpk(zip);
                    if (nestedLpkPath != null) {
                        Log.i(TAG, "Found nested LPK: " + nestedLpkPath);
                        JSONObject configJson = readConfigJson(zip);
                        // Extract title from outer WPK config.json for auto-naming
                        String wpkTitle = null;
                        if (configJson != null) {
                            String t = configJson.optString("title", "");
                            if (!t.isEmpty()) wpkTitle = t;
                        }
                        return extractNestedLpk(zip, nestedLpkPath, configJson, outerPreview, customName, wpkTitle);
                    }
                    Log.e(TAG, "Failed to find config.mlve or nested LPK");
                    return null;
                }

                JSONObject mlve = new JSONObject(mlveRaw);
                String lpkType = mlve.optString("type", "STD_1_0");
                String lpkId = mlve.optString("id", "");
                String keyId = mlve.optString("keyId", lpkId);
                boolean encrypted = parseEncryptField(mlve);

                Log.i(TAG, "LPK type=" + lpkType + " id=" + lpkId + " keyId=" + keyId + " encrypted=" + encrypted);

                int sdkVersion = detectSdkVersion(mlve);
                Log.i(TAG, "Detected SDK version: " + sdkVersion);

                lastMlveRaw = mlveRaw;

                // Extract fileId/metaData for STM_1_0 key generation
                String fileId = mlve.optString("fileId", null);
                String metaData = mlve.optString("metaData", null);

                String modelJsonPath;

                if ("STD2_0".equals(lpkType) || "STM_1_0".equals(lpkType)) {
                    modelJsonPath = extractStructured(zip, mlve, lpkType, keyId, fileId, metaData, customName);
                } else {
                    String modelName = (customName != null && !customName.isEmpty())
                            ? customName : "lpk_model_" + System.currentTimeMillis();
                    File outDir = new File(context.getFilesDir(), "models/" + sanitizeForDirName(modelName));
                    outDir.mkdirs();
                    modelJsonPath = extractFlat(zip, lpkType, keyId, null, null, encrypted, outDir);
                }

                if (modelJsonPath != null) {
                    File modelDir = new File(modelJsonPath).getParentFile();
                    if (modelDir != null) {
                        File mlveFile = new File(modelDir, "config.mlve");
                        if (!mlveFile.exists()) {
                            FileUtils.writeFile(mlveFile, mlveRaw.getBytes(StandardCharsets.UTF_8));
                            Log.i(TAG, "Saved config.mlve to: " + mlveFile.getAbsolutePath());
                        }
                    }
                }

                if (modelJsonPath != null && outerPreview != null) {
                    File modelDir = new File(modelJsonPath).getParentFile();
                    if (modelDir != null) {
                        File previewFile = new File(modelDir, "preview.png");
                        FileUtils.writeFile(previewFile, outerPreview);
                lastExtractedPreviewPath = previewFile.getAbsolutePath();
                Log.i(TAG, "Saved preview: " + previewFile.getAbsolutePath());
                    }
                }

                if (modelJsonPath != null) {
                    Log.i(TAG, "LPK extracted to: " + modelJsonPath);
                } else {
                    Log.e(TAG, "Failed to extract model JSON from LPK");
                }
                return modelJsonPath;
            }
        } catch (Exception e) {
            Log.e(TAG, "Failed to unpack LPK", e);
            return null;
        } finally {
            if (tempLpk != null) tempLpk.delete();
        }
    }

    /**
     * Unpack an LPK/WPK file and return full config data.
     */
    public UnpackResult unpackWithConfig(Uri lpkUri, String customName) {
        // Step 1: Unpack model (handles decryption, SDK conversion, preview extraction)
        String modelPath = unpack(lpkUri, customName);
        if (modelPath == null) return null;

        File modelDir = new File(modelPath).getParentFile();

        // Step 2: Extract config from config.mlve (not the model JSON)
        // config.mlve contains all metadata: hit areas, controllers, motion metadata, etc.
        // The model JSON (.model3.json) only has standard Cubism SDK file references.
        String hitAreaConfig = null;
        String lookAtConfig = null;
        String modelConfig = null;
        try {
            JSONObject mlveJson = null;

            // Try to read config.mlve from model directory (saved during unpack)

            if (modelDir != null) {
                File mlveFile = new File(modelDir, "config.mlve");
                if (mlveFile.exists()) {
                    String mlveStr = FileUtils.readFileAsString(mlveFile);
                    if (mlveStr != null) {
                        mlveJson = new JSONObject(mlveStr);
                        Log.i(TAG, "Extracting configs from saved config.mlve");
                    }
                }
            }

            // Fallback: use lastMlveRaw from unpack() if file not found
            if (mlveJson == null && lastMlveRaw != null) {
                mlveJson = new JSONObject(lastMlveRaw);
                Log.i(TAG, "Extracting configs from in-memory config.mlve");
            }

            if (mlveJson != null) {
                hitAreaConfig = extractHitAreaConfig(mlveJson);
                lookAtConfig = extractLookAtConfig(mlveJson);
                modelConfig = extractModelConfig(mlveJson);
            } else {
                Log.w(TAG, "No config.mlve available for config extraction");
            }
        } catch (Exception e) {
            Log.w(TAG, "Failed to extract config from config.mlve", e);
        }

        // Step 3: Persist extracted configs to files for later reuse (Load button, app restart)
        if (modelDir != null) {
            try {
                if (hitAreaConfig != null) {
                    FileUtils.writeFile(new File(modelDir, "hit_area_config.json"), hitAreaConfig.getBytes(StandardCharsets.UTF_8));
                }
                if (lookAtConfig != null) {
                    FileUtils.writeFile(new File(modelDir, "look_at_config.json"), lookAtConfig.getBytes(StandardCharsets.UTF_8));
                }
                if (modelConfig != null) {
                    FileUtils.writeFile(new File(modelDir, "model_config.json"), modelConfig.getBytes(StandardCharsets.UTF_8));
                }
            } catch (Exception e) {
                Log.w(TAG, "Failed to persist config files", e);
            }
        }

        // Step 4: Preview was already extracted during unpack()
        String previewPath = lastExtractedPreviewPath;
        if (previewPath == null) {
            // Fallback: check if preview exists in model directory
            if (modelDir != null) {
                File previewFile = new File(modelDir, "preview.png");
                if (previewFile.exists()) previewPath = previewFile.getAbsolutePath();
            }
        }

        Log.i(TAG, "unpackWithConfig result - modelPath: " + modelPath
                + " hitAreas: " + (hitAreaConfig != null ? "yes" : "no")
                + " lookAt: " + (lookAtConfig != null ? "yes" : "no")
                + " modelConfig: " + (modelConfig != null ? "yes" : "no")
                + " preview: " + (previewPath != null ? "yes" : "no"));

        return new UnpackResult(modelPath, hitAreaConfig, lookAtConfig, modelConfig, previewPath);
    }

    /**
     * Search a ZIP file for a preview image.
     * Returns the image bytes or null if not found.
     * Searches in order: specific names > preview-like names > first root-level image.
     */
    private byte[] findPreviewInZip(ZipFile zip) {
        // 1. Try common preview image names (exact match)
        String[] previewNames = {"preview.png", "Preview.png", "preview.jpg", "thumbnail.png", "icon.png", "thumb.png"};
        for (String name : previewNames) {
            byte[] data = readEntry(zip, name);
            if (data != null && data.length > 100) {
                return data;
            }
        }

        // 2. Search for entries with preview/thumbnail in the name (any path)
        Enumeration<? extends ZipEntry> entries = zip.entries();
        while (entries.hasMoreElements()) {
            ZipEntry entry = entries.nextElement();
            if (entry.isDirectory()) continue;
            String lower = entry.getName().toLowerCase(Locale.ROOT);
            if ((lower.contains("preview") || lower.contains("thumbnail") || lower.contains("thumb"))
                    && (lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg"))) {
                byte[] data = readEntry(zip, entry);
                if (data != null && data.length > 100) {
                    return data;
                }
            }
        }

        // 3. Fallback: first image file in root directory (no subdirectory)
        entries = zip.entries();
        while (entries.hasMoreElements()) {
            ZipEntry entry = entries.nextElement();
            if (entry.isDirectory()) continue;
            String name = entry.getName();
            // Root-level only (no '/' in name after trimming)
            if (name.contains("/")) continue;
            String lower = name.toLowerCase(Locale.ROOT);
            if (lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg")) {
                byte[] data = readEntry(zip, entry);
                if (data != null && data.length > 100) {
                    return data;
                }
            }
        }

        return null;
    }

    /**
     * Extract hit area definitions from config.mlve.
     * Format: [{name, id, motion, width, height, center_x, center_y, enabled}]
     */
    private String extractHitAreaConfig(JSONObject mlve) {
        try {
            // Try PascalCase first (config.mlve standard), then snake_case
            JSONArray hitAreas = mlve.optJSONArray("HitAreas");
            if (hitAreas == null || hitAreas.length() == 0) {
                hitAreas = mlve.optJSONArray("hit_areas");
            }
            if (hitAreas == null || hitAreas.length() == 0) {
                // Try nested in list[0] for STD2_0/STM_1_0
                JSONArray list = mlve.optJSONArray("list");
                if (list != null && list.length() > 0) {
                    JSONObject chara = list.optJSONObject(0);
                    if (chara != null) {
                        hitAreas = chara.optJSONArray("HitAreas");
                        if (hitAreas == null) hitAreas = chara.optJSONArray("hit_areas");
                    }
                }
                // Try under FileReferences (model3.json format)
                if (hitAreas == null || hitAreas.length() == 0) {
                    JSONObject fileRefs = mlve.optJSONObject("FileReferences");
                    if (fileRefs != null) {
                        hitAreas = fileRefs.optJSONArray("HitAreas");
                        if (hitAreas == null) hitAreas = fileRefs.optJSONArray("hit_areas");
                    }
                }
            }
            if (hitAreas == null || hitAreas.length() == 0) {
                return null;
            }

            JSONArray result = new JSONArray();
            for (int i = 0; i < hitAreas.length(); i++) {
                JSONObject area = hitAreas.optJSONObject(i);
                if (area == null) continue;

                JSONObject entry = new JSONObject();
                // PascalCase (config.mlve) or snake_case fallback
                entry.put("name", FileUtils.optStringEither(area, "Name", "name"));
                entry.put("id", FileUtils.optStringEither(area, "Id", "id"));
                entry.put("motion", FileUtils.optStringEither(area, "Motion", "motion"));
                entry.put("enter_mtn", FileUtils.optStringEither(area, "EnterMtn", "enter_mtn"));
                entry.put("exit_mtn", FileUtils.optStringEither(area, "ExitMtn", "exit_mtn"));
                entry.put("enabled", area.optBoolean("enabled", true));
                entry.put("width", FileUtils.optDoubleEither(area, "Width", "width"));
                entry.put("height", FileUtils.optDoubleEither(area, "Height", "height"));
                entry.put("center_x", FileUtils.optDoubleEither(area, "CenterX", "center_x"));
                entry.put("center_y", FileUtils.optDoubleEither(area, "CenterY", "center_y"));
                entry.put("order", area.optInt("order", i));

                // Parameter toggle support (for model control panels)
                String param = FileUtils.optStringEither(area, "Param", "param");
                if (!param.isEmpty()) {
                    entry.put("param", param);
                    JSONArray values = area.optJSONArray("Values");
                    if (values == null) values = area.optJSONArray("values");
                    if (values != null) {
                        entry.put("values", values);
                    }
                }
                result.put(entry);
            }

            Log.i(TAG, "Extracted " + result.length() + " hit areas from config.mlve");
            return result.toString();
        } catch (Exception e) {
            Log.w(TAG, "Failed to extract hit areas", e);
            return null;
        }
    }

    /**
     * Extract look-at parameters from config.mlve.
     * Format: {mouseTracking, mouseTrackingAutoReset, angleXFactor, angleYFactor, bodyAngleXFactor, eyeBallXFactor, eyeBallYFactor}
     */
    private String extractLookAtConfig(JSONObject mlve) {
        try {
            JSONObject result = new JSONObject();

            // Check top-level fields
            if (mlve.has("mouseTracking")) {
                result.put("mouseTracking", mlve.optInt("mouseTracking", 1));
            }
            if (mlve.has("mouseTrackingAutoReset")) {
                result.put("mouseTrackingAutoReset", mlve.optBoolean("mouseTrackingAutoReset", true));
            }

            // Check for custom parameter factors
            if (mlve.has("angleXFactor")) result.put("angleXFactor", mlve.optDouble("angleXFactor", 30.0));
            if (mlve.has("angleYFactor")) result.put("angleYFactor", mlve.optDouble("angleYFactor", 30.0));
            if (mlve.has("bodyAngleXFactor")) result.put("bodyAngleXFactor", mlve.optDouble("bodyAngleXFactor", 10.0));
            if (mlve.has("eyeBallXFactor")) result.put("eyeBallXFactor", mlve.optDouble("eyeBallXFactor", 1.0));
            if (mlve.has("eyeBallYFactor")) result.put("eyeBallYFactor", mlve.optDouble("eyeBallYFactor", 1.0));
            if (mlve.has("damping")) result.put("damping", mlve.optDouble("damping", 0.5));

            // Also check nested controllers section
            JSONObject controllers = mlve.optJSONObject("controllers");
            if (controllers != null) {
                if (controllers.has("mouseTracking")) result.put("mouseTracking", controllers.optInt("mouseTracking", 1));
                if (controllers.has("mouseTrackingAutoReset")) result.put("mouseTrackingAutoReset", controllers.optBoolean("mouseTrackingAutoReset", true));
            }

            // Check for look params in model config
            JSONObject lookParams = mlve.optJSONObject("look_params");
            if (lookParams != null) {
                java.util.Iterator<String> keys = lookParams.keys();
                while (keys.hasNext()) {
                    String key = keys.next();
                    result.put(key, lookParams.get(key));
                }
            }

            if (result.length() == 0) {
                return null;
            }

            Log.i(TAG, "Extracted look-at config: " + result);
            return result.toString();
        } catch (Exception e) {
            Log.w(TAG, "Failed to extract look-at config", e);
            return null;
        }
    }

    /**
     * Extract full model config from config.mlve for C++ ModelConfigParser.
     * Format: {motions: {group: [{name, file, priority, ...}]}, groups: [...], controllers: {...}, ...}
     */
    private String extractModelConfig(JSONObject mlve) {
        try {
            JSONObject result = new JSONObject();

            // --- Motions dictionary ---
            // Try top-level, then FileReferences
            JSONObject motions = mlve.optJSONObject("Motions");
            if (motions == null) motions = mlve.optJSONObject("motions");
            if (motions == null) {
                JSONObject fileRefs = mlve.optJSONObject("FileReferences");
                if (fileRefs != null) {
                    motions = fileRefs.optJSONObject("Motions");
                }
            }
            if (motions != null) {
                // Enrich each motion entry with full metadata
                JSONObject enrichedMotions = new JSONObject();
                java.util.Iterator<String> groups = motions.keys();
                while (groups.hasNext()) {
                    String groupName = groups.next();
                    JSONArray motionArray = motions.optJSONArray(groupName);
                    if (motionArray == null) continue;
                    JSONArray enrichedArray = new JSONArray();
                    for (int i = 0; i < motionArray.length(); i++) {
                        JSONObject motion = motionArray.optJSONObject(i);
                        if (motion == null) continue;
                        // Pass through all fields from the original motion object
                        // The C++ parser will handle PascalCase/snake_case normalization
                        enrichedArray.put(motion);
                    }
                    enrichedMotions.put(groupName, enrichedArray);
                }
                result.put("motions", enrichedMotions);
            }

            // --- Expressions array ---
            JSONArray expressions = mlve.optJSONArray("Expressions");
            if (expressions == null) expressions = mlve.optJSONArray("expressions");
            if (expressions == null) {
                JSONObject fileRefs = mlve.optJSONObject("FileReferences");
                if (fileRefs != null) {
                    expressions = fileRefs.optJSONArray("Expressions");
                }
            }
            if (expressions != null) {
                result.put("expressions", expressions);
            }

            // --- Groups ---
            JSONArray groupsArr = mlve.optJSONArray("Groups");
            if (groupsArr == null) groupsArr = mlve.optJSONArray("groups");
            if (groupsArr != null) {
                result.put("groups", groupsArr);
            }

            // --- Controllers ---
            JSONObject controllers = mlve.optJSONObject("controllers");
            if (controllers == null) controllers = mlve.optJSONObject("Controllers");
            if (controllers != null) {
                result.put("controllers", controllers);
            }

            // --- Top-level hit_params ---
            JSONArray hitParams = mlve.optJSONArray("hit_params");
            if (hitParams == null) hitParams = mlve.optJSONArray("HitParams");
            if (hitParams != null) {
                result.put("hit_params", hitParams);
            }

            // --- Top-level loop_params ---
            JSONArray loopParams = mlve.optJSONArray("loop_params");
            if (loopParams == null) loopParams = mlve.optJSONArray("LoopParams");
            if (loopParams != null) {
                result.put("loop_params", loopParams);
            }

            // --- Scale factor ---
            if (mlve.has("scale_factor")) {
                result.put("scale_factor", mlve.optDouble("scale_factor", 1.0));
            }
            if (mlve.has("ScaleFactor")) {
                result.put("scale_factor", mlve.optDouble("ScaleFactor", 1.0));
            }

            // --- Allow mod ---
            if (mlve.has("allow_mod")) {
                result.put("allow_mod", mlve.optBoolean("allow_mod", false));
            }

            // --- Physics ---
            String physics = FileUtils.optStringEither(mlve, "Physics", "physics");
            if (!physics.isEmpty()) {
                result.put("physics", physics);
            }

            if (result.length() == 0) {
                return null;
            }

            Log.i(TAG, "Extracted model config: " + result);
            return result.toString();
        } catch (Exception e) {
            Log.w(TAG, "Failed to extract model config", e);
            return null;
        }
    }

    /**
     * Find nested LPK file in Steam Workshop format.
     * Structure: {id}/{id}.lpk + config.json + preview.png
     */
    private String findNestedLpk(ZipFile zip) {
        Enumeration<? extends ZipEntry> entries = zip.entries();
        while (entries.hasMoreElements()) {
            ZipEntry entry = entries.nextElement();
            String name = entry.getName();
            if (name.endsWith(".lpk") && !entry.isDirectory()) {
                return name;
            }
        }
        return null;
    }

    /**
     * Read config.json from Steam Workshop LPK.
     */
    private JSONObject readConfigJson(ZipFile zip) {
        try {
            String configStr = readEntryAsString(zip, "config.json");
            if (configStr == null) {
                // Try with directory prefix
                Enumeration<? extends ZipEntry> entries = zip.entries();
                while (entries.hasMoreElements()) {
                    ZipEntry entry = entries.nextElement();
                    if (entry.getName().endsWith("config.json")) {
                        configStr = readEntryAsString(zip, entry);
                        break;
                    }
                }
            }
            if (configStr != null) {
                return new JSONObject(configStr);
            }
        } catch (Exception e) {
            Log.w(TAG, "Failed to read config.json", e);
        }
        return null;
    }

    /**
     * Extract nested LPK from Steam Workshop wrapper.
     */
    private String extractNestedLpk(ZipFile zip, String nestedLpkPath,
                                     JSONObject configJson, byte[] outerPreview, String customName,
                                     String wpkTitle) throws Exception {
        // If generic search didn't find outer preview, try explicit path based on WPK structure:
        // WPK = {id}/config.json + {id}/preview.png(or random name) + {id}/{id}.lpk
        if (outerPreview == null && nestedLpkPath.contains("/")) {
            String dir = nestedLpkPath.substring(0, nestedLpkPath.lastIndexOf('/') + 1);
            // 1. Try known preview filenames
            for (String name : new String[]{"preview.png", "Preview.png", "preview.jpg", "thumbnail.png"}) {
                byte[] data = readEntry(zip, dir + name);
                if (data != null && data.length > 100) {
                    outerPreview = data;
                    Log.i(TAG, "Found outer WPK preview at: " + dir + name);
                    break;
                }
            }
            // 2. Fallback: any image in the same directory as the nested LPK
            if (outerPreview == null) {
                Enumeration<? extends ZipEntry> entries = zip.entries();
                while (entries.hasMoreElements()) {
                    ZipEntry entry = entries.nextElement();
                    if (entry.isDirectory()) continue;
                    String entryName = entry.getName();
                    if (!entryName.startsWith(dir) || entryName.equals(nestedLpkPath)) continue;
                    String lower = entryName.toLowerCase(Locale.ROOT);
                    if (lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg")) {
                        byte[] data = readEntry(zip, entry);
                        if (data != null && data.length > 100) {
                            outerPreview = data;
                            Log.i(TAG, "Found outer WPK preview (fallback) at: " + entryName);
                            break;
                        }
                    }
                }
            }
        }
        Log.i(TAG, "extractNestedLpk: outerPreview=" + (outerPreview != null ? outerPreview.length + " bytes" : "null")
                + " nestedLpkPath=" + nestedLpkPath);

        // Read nested LPK data
        byte[] nestedLpkData = readEntry(zip, nestedLpkPath);
        if (nestedLpkData == null) {
            Log.e(TAG, "Failed to read nested LPK: " + nestedLpkPath);
            return null;
        }

        // Write nested LPK to temp file
        File nestedLpkFile = new File(context.getCacheDir(), "nested_lpk_" + System.currentTimeMillis() + ".lpk");
        FileUtils.writeFile(nestedLpkFile, nestedLpkData);
        Log.i(TAG, "Nested LPK extracted to: " + nestedLpkFile.getAbsolutePath()
                + " size=" + nestedLpkData.length);

        // Now process the nested LPK as a standard LPK
        String modelJsonPath;
        try (ZipFile nestedZip = new ZipFile(nestedLpkFile)) {
            // Read config.mlve from nested LPK
            String mlveRaw = readMlveConfig(nestedZip);
            if (mlveRaw == null) {
                Log.e(TAG, "Failed to find config.mlve in nested LPK");
                nestedLpkFile.delete();
                return null;
            }

            JSONObject mlve = new JSONObject(mlveRaw);
            String lpkType = mlve.optString("type", "STD_1_0");
            String lpkId = mlve.optString("id", "");
            String keyId = mlve.optString("keyId", lpkId);
            boolean encrypted = parseEncryptField(mlve);

            Log.i(TAG, "Nested LPK type=" + lpkType + " id=" + lpkId + " keyId=" + keyId + " encrypted=" + encrypted);

            // For STM_1_0, use fileId from config.json if available
            String fileId = null;
            String metaData = null;
            if ("STM_1_0".equals(lpkType) && configJson != null) {
                fileId = configJson.optString("fileId", null);
                metaData = configJson.optString("metaData", null);
                Log.i(TAG, "STM_1_0 config: fileId=" + fileId + " metaData=" + metaData);
            }

            int sdkVersion = detectSdkVersion(mlve);
            Log.i(TAG, "Nested LPK SDK version: " + sdkVersion);

            // Resolve model name: customName > wpkTitle > timestamp
            String resolvedName = (customName != null && !customName.isEmpty()) ? customName : wpkTitle;

            if ("STD2_0".equals(lpkType) || "STM_1_0".equals(lpkType)) {
                modelJsonPath = extractStructured(nestedZip, mlve, lpkType, keyId, fileId, metaData, resolvedName);
            } else {
                // STD_1_0 or unknown: extract all files
                String modelName = (resolvedName != null && !resolvedName.isEmpty())
                        ? resolvedName : "lpk_model_" + System.currentTimeMillis();
                File outDir = new File(context.getFilesDir(), "models/" + sanitizeForDirName(modelName));
                outDir.mkdirs();
                modelJsonPath = extractFlat(nestedZip, lpkType, keyId, null, null, encrypted, outDir);
            }

            // Save preview: prefer outer WPK preview, fall back to inner LPK preview
            if (modelJsonPath != null) {
                File modelDir = new File(modelJsonPath).getParentFile();
                if (modelDir != null) {
                    File previewFile = new File(modelDir, "preview.png");
                    if (outerPreview != null) {
                        FileUtils.writeFile(previewFile, outerPreview);
                        lastExtractedPreviewPath = previewFile.getAbsolutePath();
                        Log.i(TAG, "Saved outer WPK preview: " + previewFile.getAbsolutePath());
                    } else if (previewFile.exists()) {
                        lastExtractedPreviewPath = previewFile.getAbsolutePath();
                        Log.i(TAG, "Using existing inner LPK preview: " + previewFile.getAbsolutePath());
                    } else {
                        byte[] innerPreview = findPreviewInZip(nestedZip);
                        if (innerPreview != null) {
                            FileUtils.writeFile(previewFile, innerPreview);
                            lastExtractedPreviewPath = previewFile.getAbsolutePath();
                            Log.i(TAG, "Saved inner LPK preview: " + previewFile.getAbsolutePath());
                        }
                    }
                }
            }
        }
        nestedLpkFile.delete();

        if (modelJsonPath != null) {
            Log.i(TAG, "Nested LPK extracted to: " + modelJsonPath);
        } else {
            Log.e(TAG, "Failed to extract model JSON from nested LPK");
        }
        return modelJsonPath;
    }

    /**
     * Extract STD2_0 / STM_1_0 format with character and costume list.
     */
    private String extractStructured(ZipFile zip, JSONObject mlve, String lpkType,
                                      String keyId, String fileId, String metaData,
                                      String customName) throws Exception {
        String modelName = (customName != null && !customName.isEmpty())
                ? customName : "lpk_model_" + System.currentTimeMillis();
        File outDir = new File(context.getFilesDir(), "models/" + sanitizeForDirName(modelName));
        outDir.mkdirs();

        JSONArray list = mlve.optJSONArray("list");
        if (list == null || list.length() == 0) {
            Log.e(TAG, "No characters in LPK config");
            return null;
        }

        // Use the first character's first costume
        JSONObject chara = list.getJSONObject(0);

        JSONArray costumes = chara.optJSONArray("costume");
        if (costumes == null || costumes.length() == 0) {
            Log.e(TAG, "No costumes in LPK config");
            return null;
        }

        JSONObject costume = costumes.getJSONObject(0);
        String costumePath = costume.optString("path", "");
        if (costumePath.isEmpty()) {
            Log.e(TAG, "Empty costume path");
            return null;
        }

        Log.i(TAG, "Extracting costume: " + costumePath);

        // Decrypt the model JSON (use keyId for decryption)
        byte[] modelJsonBytes = decryptFile(zip, lpkType, keyId, fileId, metaData, costumePath);
        if (modelJsonBytes == null) {
            Log.e(TAG, "Failed to decrypt model JSON: " + costumePath);
            return null;
        }

        String modelJsonStr = new String(modelJsonBytes, StandardCharsets.UTF_8);
        JSONObject modelJson;
        try {
            modelJson = new JSONObject(modelJsonStr);
        } catch (Exception e) {
            Log.e(TAG, "Failed to parse model JSON: " + e.getMessage());
            Log.e(TAG, "Raw content: " + modelJsonStr.substring(0, Math.min(500, modelJsonStr.length())));
            return null;
        }

        int sdkVersion = detectSdkVersion(mlve);
        if (sdkVersion == 2) {
            Log.i(TAG, "Detected SDK2 model, converting to SDK3 format");
            modelJson = convertSdk2ToSdk3(modelJson);
        }

        // Find model JSON name (look for .model3.json or .model.json keys)
        String modelFileName = findModelFileName(modelJson, outDir);
        Log.i(TAG, "Model file name: " + modelFileName);

        // Extract all referenced files from the model JSON and update references
        java.util.Map<String, String> nameMapping = new java.util.HashMap<>();
        java.util.Set<String> extractedEntries = new java.util.HashSet<>();
        extractReferencedFiles(zip, lpkType, keyId, fileId, metaData, modelJson, outDir, nameMapping, extractedEntries);

        Log.i(TAG, "Extracted " + nameMapping.size() + " encrypted files");

        // Extract any remaining ZIP entries not referenced by the model JSON
        int skippedCount = 0;
        Enumeration<? extends ZipEntry> allEntries = zip.entries();
        while (allEntries.hasMoreElements()) {
            ZipEntry entry = allEntries.nextElement();
            if (entry.isDirectory()) continue;
            String entryName = entry.getName();

            // Skip already-extracted entries by ZIP entry name
            if (extractedEntries.contains(entryName)) continue;

            String basename = entryName;
            int slash = basename.lastIndexOf('/');
            if (slash >= 0) basename = basename.substring(slash + 1);

            // config.mlve and config.json are handled separately by unpack()
            if ("config.mlve".equals(basename) || "config.json".equals(basename)) continue;

            byte[] data = readEntry(zip, entry);
            if (data == null) continue;

            byte[] content;
            String outName;
            if (isEncryptedFileName(basename)) {
                content = decryptData(lpkType, keyId, fileId, metaData, entryName, data);
                // Replace .bin3/.bin with the actual file extension
                String ext = guessExtension(content);
                String base = basename.contains(".") ? basename.substring(0, basename.lastIndexOf('.')) : basename;
                outName = sanitizeFileName(base + ext);
            } else {
                content = data;
                outName = sanitizeFileName(basename);
            }


            File outFile = new File(outDir, outName);
            Objects.requireNonNull(outFile.getParentFile()).mkdirs();
            FileUtils.writeFile(outFile, content);
            nameMapping.put(basename, outName);
            skippedCount++;
        }
        if (skippedCount > 0) {
            Log.i(TAG, "Extracted " + skippedCount + " additional files not referenced in model JSON");
        }

        // Write the model JSON (already updated with new file references)
        String updatedJson = modelJson.toString(2);  // Pretty print with indent

        File modelFile = new File(outDir, modelFileName);
        FileUtils.writeFile(modelFile, updatedJson.getBytes(StandardCharsets.UTF_8));

        // Log output directory contents
        File[] outFiles = outDir.listFiles();
        if (outFiles != null) {
            Log.i(TAG, "Output directory contains " + outFiles.length + " files");
        }

        // Log key sections of the model JSON
        try {
            JSONObject fileRefs = modelJson.optJSONObject("FileReferences");
            if (fileRefs != null) {
                Object textures = fileRefs.opt("Textures");
                if (textures != null) {
                    Log.i(TAG, "Textures in JSON: " + textures);
                }
                Object moc = fileRefs.opt("Moc");
                if (moc != null) {
                    Log.i(TAG, "Moc in JSON: " + moc);
                }
                Object motions = fileRefs.opt("Motions");
                if (motions != null) {
                    String motionsStr = motions.toString();
                    Log.i(TAG, "Motions in JSON: " + motionsStr.substring(0, Math.min(500, motionsStr.length())));
                }
                Object expressions = fileRefs.opt("Expressions");
                if (expressions != null) {
                    Log.i(TAG, "Expressions in JSON: " + expressions);
                }
                Object physics = fileRefs.opt("Physics");
                if (physics != null) {
                    Log.i(TAG, "Physics in JSON: " + physics);
                }
            }
            // Log HitAreas
            Object hitAreas = modelJson.opt("HitAreas");
            if (hitAreas != null) {
                Log.i(TAG, "HitAreas in JSON: " + hitAreas);
            }
            // Log Groups
            Object groups = modelJson.opt("Groups");
            if (groups != null) {
                Log.i(TAG, "Groups in JSON: " + groups);
            }
        } catch (Exception e) {
            Log.w(TAG, "Failed to log JSON sections", e);
        }

        return modelFile.getAbsolutePath();
    }

    /**
     * Extract STD_1_0 or unknown format — flat file extraction.
     */
    private String extractFlat(ZipFile zip, String lpkType, String keyId,
                                String fileId, String metaData, boolean encrypted,
                                File outDir) throws Exception {
        String modelJsonPath = null;

        Log.i(TAG, "Extracting flat format, encrypted=" + encrypted);

        Enumeration<? extends ZipEntry> entries = zip.entries();
        while (entries.hasMoreElements()) {
            ZipEntry entry = entries.nextElement();
            if (entry.isDirectory()) continue;

            String name = entry.getName();
            String ext = getExtension(name);

            File outFile = new File(outDir, sanitizeFileName(name));
            Objects.requireNonNull(outFile.getParentFile()).mkdirs();

            if (!encrypted || ext.equals(".json") || ext.equals(".mlve") || ext.equals(".txt")) {
                // Extract as-is
                try (InputStream is = zip.getInputStream(entry);
                     FileOutputStream fos = new FileOutputStream(outFile)) {
                    FileUtils.copyStream(is, fos);
                }
            } else {
                byte[] data = readEntry(zip, entry);
                byte[] decrypted = decryptData(lpkType, keyId, fileId, metaData, name, data);

                if (!verifyDecryptedData(decrypted, name)) {
                    Log.w(TAG, "Decryption may have failed for: " + name);
                }

                try (FileOutputStream fos = new FileOutputStream(outFile)) {
                    fos.write(decrypted);
                }
            }

            // Track model JSON files
            if (name.endsWith(".model3.json") || name.endsWith(".model.json")) {
                modelJsonPath = outFile.getAbsolutePath();
                Log.i(TAG, "Found model JSON: " + name);
            }
        }

        // If no model JSON found, search in extracted files
        if (modelJsonPath == null) {
            modelJsonPath = findModelJsonInDir(outDir);
            if (modelJsonPath != null) {
                Log.i(TAG, "Found model JSON in directory: " + modelJsonPath);
            }
        }

        return modelJsonPath;
    }

    /**
     * Recursively extract files referenced in a model JSON.
     * Also updates the JSON object with new file names.
     */
    private void extractReferencedFiles(ZipFile zip, String lpkType, String keyId,
                                         String fileId, String metaData,
                                         JSONObject json, File outDir,
                                         java.util.Map<String, String> nameMapping,
                                         java.util.Set<String> extractedEntries) throws Exception {
        java.util.Iterator<String> keys = json.keys();
        while (keys.hasNext()) {
            String key = keys.next();
            Object val = json.get(key);

            if (val instanceof JSONObject) {
                extractReferencedFiles(zip, lpkType, keyId, fileId, metaData,
                        (JSONObject) val, outDir, nameMapping, extractedEntries);
            } else if (val instanceof JSONArray) {
                JSONArray arr = (JSONArray) val;
                for (int i = 0; i < arr.length(); i++) {
                    Object item = arr.get(i);
                    if (item instanceof JSONObject) {
                        extractReferencedFiles(zip, lpkType, keyId, fileId, metaData,
                                (JSONObject) item, outDir, nameMapping, extractedEntries);
                    } else if (item instanceof String) {
                        String itemStr = (String) item;
                        String newVal = extractIfEncrypted(zip, lpkType, keyId, fileId, metaData,
                                itemStr, outDir, nameMapping, extractedEntries);
                        if (newVal != null) {
                            arr.put(i, newVal);  // Update the array element
                        }
                    }
                }
            } else if (val instanceof String) {
                String valStr = (String) val;
                String newVal = extractIfEncrypted(zip, lpkType, keyId, fileId, metaData,
                        valStr, outDir, nameMapping, extractedEntries);
                if (newVal != null) {
                    json.put(key, newVal);  // Update the JSON object
                }
            }
        }
    }

    /**
     * Extract a referenced file from the ZIP archive.
     * Returns the output filename if extraction was performed, null otherwise.
     * <p>
     * Handles:
     * 1. Encrypted filenames (hex-hash .bin3/.bin) — decrypts and extracts
     * 2. Non-encrypted filenames — extracts as-is if found in ZIP
     * <p>
     * When decrypted data is JSON, recursively extracts any nested encrypted
     * references (handles _command/_postcommand and nested model JSONs).
     */
    private String extractIfEncrypted(ZipFile zip, String lpkType, String keyId,
                                       String fileId, String metaData,
                                       String value, File outDir,
                                       java.util.Map<String, String> nameMapping,
                                       java.util.Set<String> extractedEntries) throws Exception {
        if (nameMapping.containsKey(value)) return nameMapping.get(value); // already extracted

        // Extract the basename for encrypted filename detection
        String basename = value;
        int lastSlash = value.lastIndexOf('/');
        if (lastSlash >= 0) {
            basename = value.substring(lastSlash + 1);
        }
        int lastBackslash = basename.lastIndexOf('\\');
        if (lastBackslash >= 0) {
            basename = basename.substring(lastBackslash + 1);
        }

        boolean encrypted = isEncryptedFileName(basename);

        // Try finding the ZIP entry with the full path first, then basename
        ZipEntry zipEntry = findEntry(zip, value);
        if (zipEntry == null && !value.equals(basename)) {
            zipEntry = findEntry(zip, basename);
        }
        if (zipEntry == null) {
            // Not found — for encrypted names this is a warning, for non-encrypted just skip
            if (encrypted) {
                Log.w(TAG, "Failed to find ZIP entry: " + value + " (also tried: " + basename + ")");
            }
            return null;
        }

        // Use the actual ZIP entry name for key generation (may differ in casing)
        String zipEntryName = zipEntry.getName();
        byte[] data = readEntry(zip, zipEntry);
        if (data == null) {
            Log.w(TAG, "Failed to read ZIP entry data: " + zipEntryName);
            return null;
        }

        // Track this ZIP entry as processed
        extractedEntries.add(zipEntryName);

        byte[] content;
        String outName;

        if (encrypted) {
            // Encrypted file: decrypt using the actual ZIP entry name for key generation
            content = decryptData(lpkType, keyId, fileId, metaData, zipEntryName, data);
            String ext = guessExtension(content);
            outName = sanitizeFileName(basename) + ext;
        } else {
            // Non-encrypted file: use as-is
            content = data;
            outName = sanitizeFileName(value);
        }

        // If content is JSON, recursively extract nested encrypted references.
        // This handles _command/_postcommand fields and nested model JSONs.
        if (outName.endsWith(".json")) {
            try {
                String jsonStr = new String(content, StandardCharsets.UTF_8).trim();
                if (jsonStr.startsWith("{")) {
                    JSONObject nestedJson = new JSONObject(jsonStr);
                    java.util.Map<String, String> nestedMapping = new java.util.HashMap<>();
                    extractReferencedFiles(zip, lpkType, keyId, fileId, metaData,
                            nestedJson, outDir, nestedMapping, extractedEntries);

                    if (!nestedMapping.isEmpty()) {
                        // Save the updated JSON with resolved references
                        content = nestedJson.toString(2).getBytes(StandardCharsets.UTF_8);
                    }
                }
            } catch (Exception ignored) {
            }
        }

        File outFile = new File(outDir, outName);
        try (FileOutputStream fos = new FileOutputStream(outFile)) {
            fos.write(content);
        }

        nameMapping.put(value, outName);
        // Update JSON reference if the output name differs from the original value
        return outName.equals(value) ? null : outName;
    }

    /**
     * Read config.mlve from the ZIP, trying MD5-hashed filename first.
     */
    private String readMlveConfig(ZipFile zip) {
        // Try MD5-hashed filename first
        String hashedName = md5();
        String raw = readEntryAsString(zip, hashedName);
        if (raw != null) return raw;

        // Try plain filename
        raw = readEntryAsString(zip, "config.mlve");
        if (raw != null) return raw;

        // Search all entries for .mlve files
        Enumeration<? extends ZipEntry> entries = zip.entries();
        while (entries.hasMoreElements()) {
            ZipEntry entry = entries.nextElement();
            if (entry.getName().endsWith(".mlve")) {
                raw = readEntryAsString(zip, entry);
                if (raw != null) return raw;
            }
        }

        return null;
    }

    // --- Decryption ---
    // LPK files use a custom XOR encryption scheme with per-file keys.
    // Key generation: hash(keyId + filename) using a multiplicative hash (genkey).
    // Decryption: XOR with a pseudo-random keystream, processed in 1024-byte blocks.
    // Three LPK types exist with different key compositions:
    //   STD_1_0: genkey(keyId + filename)
    //   STD2_0:  genkey(keyId + filename)
    //   STM_1_0: genkey(keyId + fileId + filename + metaData)  [Steam Workshop]

    /**
     * Generate XOR key from a string (same as Python genkey).
     * Python: ret = (ret * 31 + ord(i)) & 0xffffffff
     *         if ret & 0x80000000: ret = ret | 0xffffffff00000000
     */
    private static long genkey(String s) {
        long ret = 0;
        for (int i = 0; i < s.length(); i++) {
            ret = (ret * 31 + s.charAt(i)) & 0xFFFFFFFFL;
        }
        // Convert to signed 64-bit if highest bit is set (matching Python behavior)
        if ((ret & 0x80000000L) != 0) {
            ret = ret | 0xFFFFFFFF00000000L;
        }
        return ret;
    }

    /**
     * XOR decrypt data using the LPK encryption scheme.
     * Processes data in 1024-byte blocks, each block resets the key.
     * <p>
     * Python reference:
     *   tmpkey = (65535 & 2531011 + 214013 * tmpkey >> 16) & 0xffffffff
     *   Operator precedence: * > + > >> > &
     *   So: tmpkey = (65535 & ((2531011 + 214013 * tmpkey) >> 16)) & 0xffffffff
     * <p>
     * Uses long arithmetic to avoid int overflow (matching Python's arbitrary precision).
     */
    private static byte[] decrypt(long key, byte[] data) {
        byte[] result = new byte[data.length];
        int len = data.length;

        for (int blockStart = 0; blockStart < len; blockStart += 1024) {
            long tmpkey = key;
            int blockEnd = Math.min(blockStart + 1024, len);

            for (int i = blockStart; i < blockEnd; i++) {
                tmpkey = (65535 & ((2531011 + 214013 * tmpkey) >> 16)) & 0xFFFFFFFFL;
                result[i] = (byte) ((tmpkey & 0xFF) ^ (data[i] & 0xFF));
            }
        }

        return result;
    }

    /**
     * Decrypt a file from the ZIP archive.
     * Includes verification to detect incorrect decryption.
     * Tries primary decryption first, falls back to offset-based variant if verification fails.
     */
    private byte[] decryptFile(ZipFile zip, String lpkType, String keyId,
                                String fileId, String metaData, String filename) {
        byte[] data = readEntry(zip, filename);
        if (data == null) {
            Log.e(TAG, "Failed to read entry: " + filename);
            return null;
        }

        byte[] decrypted = decryptData(lpkType, keyId, fileId, metaData, filename, data);

        // Verify decryption
        if (!verifyDecryptedData(decrypted, filename)) {
            Log.w(TAG, "Primary decryption failed verification for: " + filename
                    + ", trying offset-based variant...");
            byte[] altDecrypted = decryptDataWithOffset(lpkType, keyId, fileId, metaData, filename, data);
            if (verifyDecryptedData(altDecrypted, filename)) {
                Log.i(TAG, "Offset-based decryption succeeded for: " + filename);
                return altDecrypted;
            }
            Log.w(TAG, "All decryption variants failed for: " + filename
                    + " (data doesn't look like a valid file)");
            // Still return the primary result - let caller decide what to do
            return decrypted;
        }

        return decrypted;
    }

    /**
     * Decrypt data with the appropriate key for the LPK type.
     * Uses keyId (not lpkId) for key generation.
     * <p>
     * Key generation (from Python reference):
     *   - STM_1_0: genkey(keyId + fileId + filename + metaData)
     *   - STD2_0:  genkey(keyId + filename)
     *   - STD_1_0: genkey(keyId + filename)
     */
    private byte[] decryptData(String lpkType, String keyId, String fileId,
                                String metaData, String filename, byte[] data) {
        long key;
        String keySource;
        if ("STM_1_0".equals(lpkType)) {
            // STM_1_0 (Steam Workshop) uses all four components
            String fid = fileId != null ? fileId : "";
            String md = metaData != null ? metaData : "";
            keySource = keyId + fid + filename + md;
        } else {
            keySource = keyId + filename;
        }
        key = genkey(keySource);

        return decrypt(key, data);
    }

    /**
     * Offset-based decryption variant.
     * Some LPK versions use a different keystream that incorporates the file's position/offset.
     * This is the alternative decryption path seen in IL2CPP (IMBEEAHJKBG, HCJJJLNJOOM etc.).
     * <p>
     * The offset variant XORs the initial key with the data length before generating the keystream.
     */
    private byte[] decryptDataWithOffset(String lpkType, String keyId, String fileId,
                                          String metaData, String filename, byte[] data) {
        long key;
        String keySource;
        if ("STM_1_0".equals(lpkType)) {
            String fid = fileId != null ? fileId : "";
            String md = metaData != null ? metaData : "";
            keySource = keyId + fid + filename + md;
        } else {
            keySource = keyId + filename;
        }
        key = genkey(keySource);

        // Offset variant: XOR key with data length
        key = key ^ (long) data.length;

        return decrypt(key, data);
    }

    // --- Encrypt field parsing ---

    /**
     * Parse the encrypt field from config.mlve with support for various formats.
     * Handles: true/false, "true"/"false", "yes"/"no", 1/0, "1"/"0"
     * Default: true (encrypted) for safety.
     */
    private static boolean parseEncryptField(JSONObject mlve) {
        Object encryptVal = mlve.opt("encrypt");
        if (encryptVal == null) {
            return true;
        }

        if (encryptVal instanceof Boolean) {
            return (Boolean) encryptVal;
        }

        if (encryptVal instanceof Number) {
            return ((Number) encryptVal).intValue() != 0;
        }

        String str = encryptVal.toString().trim().toLowerCase(Locale.ROOT);
        switch (str) {
            case "false":
            case "no":
            case "0":
            case "off":
            case "none":
                return false;
            case "true":
            case "yes":
            case "1":
            case "on":
                return true;
            default:
                Log.w(TAG, "Unknown encrypt value: \"" + str + "\", defaulting to true");
                return true;
        }
    }

    // --- SDK version detection and conversion ---

    /**
     * Detect the Live2D SDK version from config.mlve.
     * Returns 2 for Cubism 2, 3 for Cubism 3/4/5, 0 if unknown.
     */
    private static int detectSdkVersion(JSONObject mlve) {
        // Check explicit version field
        String version = mlve.optString("version", "");
        if (!version.isEmpty()) {
            // version like "2.0", "3.0", "1.0" etc.
            if (version.startsWith("2")) return 2;
            if (version.startsWith("3") || version.startsWith("4") || version.startsWith("5")) return 3;
        }

        // Check type field for hints
        String type = mlve.optString("type", "");
        if (type.contains("2")) return 2;
        if (type.contains("3")) return 3;

        // Check model file extension in the list
        JSONArray list = mlve.optJSONArray("list");
        if (list != null && list.length() > 0) {
            JSONObject chara = list.optJSONObject(0);
            if (chara != null) {
                JSONArray costumes = chara.optJSONArray("costume");
                if (costumes != null && costumes.length() > 0) {
                    JSONObject costume = costumes.optJSONObject(0);
                    if (costume != null) {
                        String path = costume.optString("path", "");
                        if (path.endsWith(".model.json")) return 2;
                        if (path.endsWith(".model3.json")) return 3;
                    }
                }
            }
        }

        // Check for SDK2-specific fields at top level
        if (mlve.has("model") || mlve.has("textures") || mlve.has("motions")) {
            return 2;
        }

        Log.w(TAG, "Could not detect SDK version from config.mlve");
        return 0;
    }

    /**
     * Convert a Cubism 2 model config to Cubism 3/4/5 format.
     * <p>
     * SDK2 format (config.mlve style):
     * {
     *   "type": "Live2D Model Setting",
     *   "model": "model.moc",
     *   "textures": ["texture_00.png"],
     *   "motions": { "Idle": [{ "file": "idle_00.mtn" }] },
     *   "hit_areas": [{ "name": "Head", "id": "D_REF.HEAD" }],
     *   ...
     * }
     * <p>
     * SDK3 format (model3.json):
     * {
     *   "Version": 3,
     *   "FileReferences": {
     *     "Moc": "model.moc3",
     *     "Textures": ["texture_00.png"],
     *     "Motions": { "Idle": [{ "File": "idle_00.motion3.json" }] },
     *     "HitAreas": [{ "Name": "Head", "Id": "D_REF.HEAD" }]
     *   }
     * }
     */
    private static JSONObject convertSdk2ToSdk3(JSONObject sdk2) throws Exception {
        JSONObject sdk3 = new JSONObject();
        sdk3.put("Version", 3);

        JSONObject fileRefs = new JSONObject();

        // Model file: .moc -> .moc3 (just update the reference, actual conversion is not needed
        // since the native code handles both formats)
        String model = sdk2.optString("model", "");
        if (!model.isEmpty()) {
            fileRefs.put("Moc", model);
        }

        // Textures
        Object textures = sdk2.opt("textures");
        if (textures != null) {
            fileRefs.put("Textures", textures);
        }

        // Motions: convert "file" key to "File"
        JSONObject motions = sdk2.optJSONObject("motions");
        if (motions != null) {
            JSONObject sdk3Motions = new JSONObject();
            java.util.Iterator<String> groups = motions.keys();
            while (groups.hasNext()) {
                String group = groups.next();
                JSONArray groupMotions = motions.getJSONArray(group);
                JSONArray sdk3GroupMotions = new JSONArray();
                for (int i = 0; i < groupMotions.length(); i++) {
                    JSONObject motion = groupMotions.getJSONObject(i);
                    JSONObject sdk3Motion = new JSONObject();
                    // SDK2 uses "file", SDK3 uses "File"
                    String file = motion.optString("file", motion.optString("File", ""));
                    if (!file.isEmpty()) {
                        sdk3Motion.put("File", file);
                    }
                    // Copy other fields
                    if (motion.has("fade_in_time")) sdk3Motion.put("FadeInTime", motion.get("fade_in_time"));
                    if (motion.has("fade_out_time")) sdk3Motion.put("FadeOutTime", motion.get("fade_out_time"));
                    if (motion.has("sound")) sdk3Motion.put("Sound", motion.get("sound"));
                    sdk3GroupMotions.put(sdk3Motion);
                }
                sdk3Motions.put(group, sdk3GroupMotions);
            }
            fileRefs.put("Motions", sdk3Motions);
        }

        // Expressions: convert format
        JSONArray expressions = sdk2.optJSONArray("expressions");
        if (expressions != null) {
            JSONArray sdk3Expressions = new JSONArray();
            for (int i = 0; i < expressions.length(); i++) {
                JSONObject expr = expressions.getJSONObject(i);
                JSONObject sdk3Expr = new JSONObject();
                if (expr.has("name")) sdk3Expr.put("Name", expr.get("name"));
                if (expr.has("file")) sdk3Expr.put("File", expr.get("file"));
                sdk3Expressions.put(sdk3Expr);
            }
            fileRefs.put("Expressions", sdk3Expressions);
        }

        // Physics
        String physics = sdk2.optString("physics", "");
        if (!physics.isEmpty()) {
            fileRefs.put("Physics", physics);
        }

        // Pose
        String pose = sdk2.optString("pose", "");
        if (!pose.isEmpty()) {
            fileRefs.put("Pose", pose);
        }

        sdk3.put("FileReferences", fileRefs);

        // Hit areas
        JSONArray hitAreas = sdk2.optJSONArray("hit_areas");
        if (hitAreas != null) {
            JSONArray sdk3HitAreas = new JSONArray();
            for (int i = 0; i < hitAreas.length(); i++) {
                JSONObject area = hitAreas.getJSONObject(i);
                JSONObject sdk3Area = new JSONObject();
                if (area.has("name")) sdk3Area.put("Name", area.get("name"));
                if (area.has("id")) sdk3Area.put("Id", area.get("id"));
                if (area.has("motion")) sdk3Area.put("Motion", area.get("motion"));
                if (area.has("enter_mtn")) sdk3Area.put("EnterMtn", area.get("enter_mtn"));
                if (area.has("exit_mtn")) sdk3Area.put("ExitMtn", area.get("exit_mtn"));
                sdk3HitAreas.put(sdk3Area);
            }
            sdk3.put("HitAreas", sdk3HitAreas);
        }

        // Groups (optional)
        JSONArray groups = sdk2.optJSONArray("groups");
        if (groups != null) {
            sdk3.put("Groups", groups);
        }

        Log.i(TAG, "SDK2->SDK3 conversion complete. Keys: " + sdk3.keys());
        return sdk3;
    }

    // --- Utility methods ---

    /**
     * Sanitize a string for use as a directory name.
     * Preserves Chinese characters and other Unicode, removes only problematic characters.
     */
    private static String sanitizeForDirName(String name) {
        // Replace characters that are problematic in file paths
        String result = name.replaceAll("[<>:\"|?*\\\\/]", "_");
        // Trim whitespace and dots from ends
        result = result.replaceAll("^[\\s.]+|[\\s.]+$", "");
        // Collapse multiple underscores/spaces
        result = result.replaceAll("[_\\s]+", "_");
        if (result.isEmpty()) {
            result = "unnamed";
        }
        return result;
    }

    private static boolean isEncryptedFileName(String s) {
        return s != null && ENC_FILE_PATTERN.matcher(s).matches();
    }

    private static String findModelFileName(JSONObject json, File outDir) {
        // Look for FileReferences -> Moc or similar (Cubism 3+)
        JSONObject refs = json.optJSONObject("FileReferences");
        if (refs == null) refs = json.optJSONObject("file_references");

        if (refs != null) {
            String moc = refs.optString("Moc", refs.optString("moc", ""));
            if (moc.contains(".moc3")) return "model0.model3.json";
            if (moc.contains(".moc")) return "model0.model.json";
        }

        // Check for Cubism 2 format (top-level "model" key)
        String model = json.optString("model", "");
        if (!model.isEmpty()) {
            if (model.contains(".moc3")) return "model0.model3.json";
            if (model.contains(".moc")) return "model0.model.json";
        }

        // Fallback: detect by looking for .moc/.moc3 files already in outDir
        File[] files = outDir.listFiles();
        if (files != null) {
            for (File f : files) {
                if (f.getName().endsWith(".moc3")) return "model0.model3.json";
                if (f.getName().endsWith(".moc")) return "model0.model.json";
            }
        }

        // Last resort: default to .model3.json (more common)
        return "model0.model3.json";
    }

    private String findModelJsonInDir(File dir) {
        File[] files = dir.listFiles();
        if (files == null) return null;

        for (File f : files) {
            if (f.getName().endsWith(".model3.json")) return f.getAbsolutePath();
        }
        for (File f : files) {
            if (f.getName().endsWith(".model.json") && !f.getName().endsWith(".model3.json"))
                return f.getAbsolutePath();
        }
        return null;
    }

    /**
     * Verify if decrypted data looks valid.
     * Returns true if data appears to be a valid file (JSON, PNG, MOC, etc.)
     */
    private static boolean verifyDecryptedData(byte[] data, String filename) {
        if (data == null || data.length == 0) return false;

        // Check for common file signatures
        if (data.length >= 4) {
            // MOC3
            if (data[0] == 'M' && data[1] == 'O' && data[2] == 'C' && data[3] == '3') return true;
            // MOC
            if (data[0] == 'm' && data[1] == 'o' && data[2] == 'c') return true;
            // PNG
            if (data[0] == (byte) 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') return true;
            // JPG
            if (data[0] == (byte) 0xFF && data[1] == (byte) 0xD8) return true;
        }

        // Check if it looks like text (no null bytes in first 1024 bytes)
        int checkLen = Math.min(data.length, 1024);
        boolean hasNull = false;
        for (int i = 0; i < checkLen; i++) {
            if (data[i] == 0) {
                hasNull = true;
                break;
            }
        }

        // If it has null bytes and isn't a known binary format, it's likely corrupt
        if (hasNull) return false;

        // Check if it's valid JSON (only for text-like content)
        if (data[0] == '{' || data[0] == '[') {
            try {
                String s = new String(data, 0, Math.min(data.length, 8192), StandardCharsets.UTF_8).trim();
                if (s.startsWith("{") || s.startsWith("[")) {
                    new org.json.JSONObject(s);
                    return true;
                }
            } catch (Exception ignored) {}
        }

        // No null bytes and filename suggests text, probably valid
        return filename.endsWith(".json") || filename.endsWith(".txt");
    }

    private static String guessExtension(byte[] data) {
        // Check magic bytes
        if (data.length >= 4) {
            if (data[0] == 'M' && data[1] == 'O' && data[2] == 'C' && data[3] == '3') return ".moc3";
            if (data[0] == 'm' && data[1] == 'o' && data[2] == 'c') return ".moc";
            if (data[0] == (byte) 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') return ".png";
            if (data[0] == (byte) 0xFF && data[1] == (byte) 0xD8) return ".jpg";
        }

        // Try JSON
        try {
            String s = new String(data, StandardCharsets.UTF_8).trim();
            if (s.startsWith("{") || s.startsWith("[")) return ".json";
        } catch (Exception ignored) {}

        return "";
    }

    private static String getExtension(String name) {
        int dot = name.lastIndexOf('.');
        return dot >= 0 ? name.substring(dot) : "";
    }

    private static String sanitizeFileName(String name) {
        // Remove path separators, keep only the filename
        name = name.replace('\\', '/');
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        // Remove characters not safe for Android filesystem
        return name.replaceAll("[<>:\"|?*]", "_");
    }

    private static String md5() {
        try {
            MessageDigest md = MessageDigest.getInstance("MD5");
            byte[] digest = md.digest("config.mlve".getBytes(StandardCharsets.UTF_8));
            StringBuilder sb = new StringBuilder();
            for (byte b : digest) {
                sb.append(String.format("%02x", b & 0xFF));
            }
            return sb.toString();
        } catch (Exception e) {
            return "config.mlve";
        }
    }

    private byte[] readEntry(ZipFile zip, String name) {
        ZipEntry entry = zip.getEntry(name);
        if (entry != null) return readEntry(zip, entry);

        // Fallback: case-insensitive search through all entries
        String nameLower = name.toLowerCase(Locale.ROOT);
        Enumeration<? extends ZipEntry> entries = zip.entries();
        while (entries.hasMoreElements()) {
            ZipEntry e = entries.nextElement();
            if (e.getName().toLowerCase(Locale.ROOT).equals(nameLower)) {
                return readEntry(zip, e);
            }
        }

        return null;
    }

    /**
     * Find a ZIP entry by name, trying exact match first, then case-insensitive.
     * Returns the actual ZipEntry (which has the correct name) or null.
     */
    private ZipEntry findEntry(ZipFile zip, String name) {
        ZipEntry entry = zip.getEntry(name);
        if (entry != null) return entry;

        // Fallback: case-insensitive search through all entries
        String nameLower = name.toLowerCase(Locale.ROOT);
        Enumeration<? extends ZipEntry> entries = zip.entries();
        while (entries.hasMoreElements()) {
            ZipEntry e = entries.nextElement();
            if (e.getName().toLowerCase(Locale.ROOT).equals(nameLower)) {
                return e;
            }
        }

        return null;
    }

    private byte[] readEntry(ZipFile zip, ZipEntry entry) {
        try (InputStream is = zip.getInputStream(entry)) {
            ByteArrayOutputStream bos = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int len;
            while ((len = is.read(buf)) > 0) {
                bos.write(buf, 0, len);
            }
            return bos.toByteArray();
        } catch (Exception e) {
            Log.e(TAG, "Failed to read entry: " + entry.getName(), e);
            return null;
        }
    }

    private String readEntryAsString(ZipFile zip, String name) {
        byte[] data = readEntry(zip, name);
        if (data == null) return null;
        try {
            // Try UTF-8 with BOM handling
            String s = new String(data, StandardCharsets.UTF_8);
            if (s.startsWith("\uFEFF")) s = s.substring(1);
            return s;
        } catch (Exception e) {
            return null;
        }
    }

    private String readEntryAsString(ZipFile zip, ZipEntry entry) {
        byte[] data = readEntry(zip, entry);
        if (data == null) return null;
        try {
            String s = new String(data, StandardCharsets.UTF_8);
            if (s.startsWith("\uFEFF")) s = s.substring(1);
            return s;
        } catch (Exception e) {
            return null;
        }
    }

    private void copyUriToFile(Uri uri, File dest) throws Exception {
        try (InputStream is = context.getContentResolver().openInputStream(uri);
             FileOutputStream fos = new FileOutputStream(dest)) {
            FileUtils.copyStream(Objects.requireNonNull(is), fos);
        }
    }

    /**
     * Read persisted config JSON files from a model directory.
     * Returns a UnpackResult with config data if files exist, or null configs if not.
     * This allows the "Load" button and auto-load to restore LPK config data.
     */
    public static UnpackResult loadPersistedConfigs(String modelPath) {
        File modelDir = new File(modelPath).getParentFile();
        if (modelDir == null) return new UnpackResult(modelPath, null, null, null, null);

        String hitAreaConfig = FileUtils.readFileAsString(new File(modelDir, "hit_area_config.json"));
        String lookAtConfig = FileUtils.readFileAsString(new File(modelDir, "look_at_config.json"));
        String modelConfig = FileUtils.readFileAsString(new File(modelDir, "model_config.json"));
        String previewPath = null;
        File previewFile = new File(modelDir, "preview.png");
        if (previewFile.exists()) previewPath = previewFile.getAbsolutePath();

        return new UnpackResult(modelPath, hitAreaConfig, lookAtConfig, modelConfig, previewPath);
    }

}
