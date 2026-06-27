package com.muxiao.deskpet;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.opengl.GLSurfaceView;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;

import com.muxiao.deskpet.live2d.Live2DNativeBridge;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Long-press popup menu for the floating Live2D pet.
 * Reads motion metadata from native layer (config.mlve) and optional actions.json
 * to build expression/motion/action menus with meaningful labels.
 */
public class ActionMenuManager {

    private static final String TAG = "ActionMenuManager";

    // Colors updated per dark mode
    private int colorBg = 0xFCFCFCFC;
    private int colorBgAlt = 0xF0F0F0;
    private int colorBorder = 0xFFDDDDDD;
    private int colorHeader = 0xFF666666;
    private int colorDivider = 0xFFE0E0E0;
    private int colorText = 0xFF333333;
    private int colorTextDim = 0xFFAAAAAA;
    private int colorRipple = 0x15000000;
    private int colorSwitchThumb = 0xFF4A4A6A;
    private int colorSwitchTrack = 0xFFDDDDDD;

    private void updateColors(Context context) {
        boolean dark = context.getSharedPreferences(
                FloatingWindowService.PREF_NAME, Context.MODE_PRIVATE)
                .getBoolean("dark_mode", false);
        if (dark) {
            colorBg = 0xF2202030;
            colorBgAlt = 0xFF2A2A3A;
            colorBorder = 0xFF3A3A48;
            colorHeader = 0xFF9898A0;
            colorDivider = 0xFF2C2C36;
            colorText = 0xFFE0E0E6;
            colorTextDim = 0xFF787880;
            colorRipple = 0x20FFFFFF;
            colorSwitchThumb = 0xFF8B8BC0;
            colorSwitchTrack = 0xFF3A3A48;
        } else {
            colorBg = 0xFCFCFCFC;
            colorBgAlt = 0xF0F0F0;
            colorBorder = 0xFFDDDDDD;
            colorHeader = 0xFF666666;
            colorDivider = 0xFFE0E0E0;
            colorText = 0xFF333333;
            colorTextDim = 0xFFAAAAAA;
            colorRipple = 0x15000000;
            colorSwitchThumb = 0xFF4A4A6A;
            colorSwitchTrack = 0xFFDDDDDD;
        }
    }

    private PopupWindow popupWindow;

    /** Simple toggle callback, avoids java.util.function.Consumer (API 24+). */
    private interface ToggleCallback {
        void onToggle(boolean value);
    }

    /**
     * Show an AlertDialog that works from a Service context.
     * Uses TYPE_APPLICATION_OVERLAY so the dialog appears over the floating window.
     * Silently ignores BadTokenException if the window token is invalid.
     */
    private static void showDialog(Context context, android.app.AlertDialog.Builder builder) {
        if (context instanceof android.app.Activity) {
            android.app.Activity activity = (android.app.Activity) context;
            if (activity.isFinishing() || activity.isDestroyed()) return;
        }
        try {
            android.app.AlertDialog dialog = builder.create();
            dialog.getWindow().setType(android.view.WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY);
            dialog.show();
        } catch (Exception ignored) {
        }
    }

    public void show(Context context, View anchor, String modelDir) {
        Log.d(TAG, "show() called: context=" + context.getClass().getSimpleName() + " anchor=" + anchor.getClass().getSimpleName() + " modelDir=" + modelDir);
        if (popupWindow != null && popupWindow.isShowing()) {
            Log.d(TAG, "show() dismissing existing popup");
            popupWindow.dismiss();
            return;
        }
        _refreshContext = context;
        _refreshAnchor = anchor;
        _refreshModelDir = modelDir;

        updateColors(context);

        LinearLayout menuLayout = new LinearLayout(context);
        menuLayout.setOrientation(LinearLayout.VERTICAL);
        android.graphics.drawable.GradientDrawable menuBg = new android.graphics.drawable.GradientDrawable();
        menuBg.setColor(colorBg);
        menuBg.setStroke(dp(context, 1), colorBorder);
        menuBg.setCornerRadius(dp(context, 8));
        menuLayout.setBackground(menuBg);
        menuLayout.setPadding(0, dp(context, 4), 0, dp(context, 4));

        // Settings section (at top)
        SharedPreferences prefs = context.getSharedPreferences(
                FloatingWindowService.PREF_NAME, Context.MODE_PRIVATE);
        addSectionHeader(menuLayout, "  " + context.getString(R.string.menu_settings));

        // Long-press menu toggle
        {
            androidx.appcompat.widget.SwitchCompat lpSwitch = new androidx.appcompat.widget.SwitchCompat(
                    new android.view.ContextThemeWrapper(context, androidx.appcompat.R.style.Theme_AppCompat));
            addToggleItem(menuLayout, context.getString(R.string.menu_long_press_menu), prefs.getBoolean("long_press_menu_enable", true), enabled -> {
                Log.d(TAG, "[Toggle:long_press_menu] enabled=" + enabled + " drag_enable=" + prefs.getBoolean("drag_enable", true));
                if (!enabled && !prefs.getBoolean("drag_enable", true)) {
                    Log.w(TAG, "[Toggle:long_press_menu] Both would be disabled, showing warning");
                    showBothDisabledWarning(context, prefs, "long_press_menu_enable", false, lpSwitch);
                } else {
                    boolean ok = prefs.edit().putBoolean("long_press_menu_enable", enabled).commit();
                    Log.d(TAG, "[Toggle:long_press_menu] commit result=" + ok);
                }
            }, lpSwitch);
        }

        // Edge hide toggle
        addToggleItem(menuLayout, context.getString(R.string.menu_edge_hide), prefs.getBoolean("edge_hide_enabled", false), enabled -> {
            Log.d(TAG, "[Toggle:edge_hide] enabled=" + enabled);
            prefs.edit().putBoolean("edge_hide_enabled", enabled).apply();
            Log.d(TAG, "[Toggle:edge_hide] apply done");
        });

        // Follow mode
        addToggleItem(menuLayout, context.getString(R.string.menu_auto_follow), prefs.getInt("follow_mode", 0) == 0, enabled -> {
            int mode = enabled ? 0 : 2;
            Log.d(TAG, "[Toggle:follow_mode] enabled=" + enabled + " -> mode=" + mode);
            prefs.edit().putInt("follow_mode", mode).apply();
            Live2DNativeBridge.nativeSetFollowMode(mode);
            Log.d(TAG, "[Toggle:follow_mode] nativeSetFollowMode called");
        });

        // Debug hit areas
        addToggleItem(menuLayout, context.getString(R.string.menu_show_hit_areas), prefs.getBoolean("debug_hit_areas", false), enabled -> {
            Log.d(TAG, "[Toggle:debug_hit_areas] enabled=" + enabled);
            prefs.edit().putBoolean("debug_hit_areas", enabled).apply();
            Live2DNativeBridge.nativeShowHitAreas(enabled);
            Log.d(TAG, "[Toggle:debug_hit_areas] nativeShowHitAreas called");
        });

        // AABB-only hit test mode (debug)
        addToggleItem(menuLayout, context.getString(R.string.menu_aabb_hit_test), prefs.getBoolean("aabb_hit_test", false), enabled -> {
            Log.d(TAG, "[Toggle:aabb_hit_test] enabled=" + enabled);
            prefs.edit().putBoolean("aabb_hit_test", enabled).apply();
            Live2DNativeBridge.nativeSetAabbHitTestMode(enabled);
            Log.d(TAG, "[Toggle:aabb_hit_test] nativeSetAabbHitTestMode called");
        });

        // Click-through toggle
        addToggleItem(menuLayout, context.getString(R.string.menu_click_through), prefs.getBoolean("click_through_enabled", false), enabled -> {
            Log.d(TAG, "[Toggle:click_through] enabled=" + enabled);
            prefs.edit().putBoolean("click_through_enabled", enabled).apply();
            Intent ctIntent = new Intent(context, FloatingWindowService.class);
            ctIntent.setAction("TOGGLE_CLICK_THROUGH");
            Log.d(TAG, "[Toggle:click_through] starting service TOGGLE_CLICK_THROUGH");
            context.startService(ctIntent);
        });

        // Random speak toggle
        addToggleItem(menuLayout, context.getString(R.string.menu_random_speak), prefs.getBoolean("random_speak_enable", false), enabled -> {
            Log.d(TAG, "[Toggle:random_speak] enabled=" + enabled);
            prefs.edit().putBoolean("random_speak_enable", enabled).apply();
            Live2DNativeBridge.nativeSetRandomSpeakEnabled(enabled);
            Log.d(TAG, "[Toggle:random_speak] nativeSetRandomSpeakEnabled(" + enabled + ") called");
            if (enabled) {
                int interval = prefs.getInt("random_speak_interval", 30);
                Log.d(TAG, "[Toggle:random_speak] setting interval=" + interval);
                Live2DNativeBridge.nativeSetRandomSpeakInterval(interval);
            }
        });

        // Random speak interval slider (10–120s)
        addSliderItem(menuLayout, context.getString(R.string.menu_random_speak_interval),
                prefs.getInt("random_speak_interval", 30), 10, 120, "s",
                value -> {
                    Log.d(TAG, "[Slider:random_speak_interval] value=" + value);
                    prefs.edit().putInt("random_speak_interval", value).apply();
                    Live2DNativeBridge.nativeSetRandomSpeakInterval(value);
                });

        // Motion enable toggle
        addToggleItem(menuLayout, context.getString(R.string.menu_motion_enabled), prefs.getBoolean("motion_enabled", true), enabled -> {
            Log.d(TAG, "[Toggle:motion_enabled] enabled=" + enabled);
            prefs.edit().putBoolean("motion_enabled", enabled).apply();
            if (!enabled) {
                Log.d(TAG, "[Toggle:motion_enabled] disabling random speak");
                Live2DNativeBridge.nativeSetRandomSpeakEnabled(false);
            } else if (prefs.getBoolean("random_speak_enable", false)) {
                Log.d(TAG, "[Toggle:motion_enabled] re-enabling random speak");
                Live2DNativeBridge.nativeSetRandomSpeakEnabled(true);
                Live2DNativeBridge.nativeSetRandomSpeakInterval(prefs.getInt("random_speak_interval", 30));
            }
            Log.d(TAG, "[Toggle:motion_enabled] done");
        });

        // Drag enable toggle
        {
            androidx.appcompat.widget.SwitchCompat dragSwitch = new androidx.appcompat.widget.SwitchCompat(
                    new android.view.ContextThemeWrapper(context, androidx.appcompat.R.style.Theme_AppCompat));
            addToggleItem(menuLayout, context.getString(R.string.menu_drag_enabled), prefs.getBoolean("drag_enable", true), enabled -> {
                Log.d(TAG, "[Toggle:drag_enable] enabled=" + enabled + " long_press_menu_enable=" + prefs.getBoolean("long_press_menu_enable", true));
                if (!enabled && !prefs.getBoolean("long_press_menu_enable", true)) {
                    Log.w(TAG, "[Toggle:drag_enable] Both would be disabled, showing warning");
                    showBothDisabledWarning(context, prefs, "drag_enable", false, dragSwitch);
                } else {
                    boolean ok = prefs.edit().putBoolean("drag_enable", enabled).commit();
                    Log.d(TAG, "[Toggle:drag_enable] commit result=" + ok);
                }
            }, dragSwitch);
        }

        // Click-through alpha slider (0.1–0.8)
        addSliderItem(menuLayout, context.getString(R.string.menu_ct_alpha),
                Math.round(prefs.getFloat("click_through_alpha", 0.4f) * 100), 10, 80, "%",
                value -> {
                    Log.d(TAG, "[Slider:ct_alpha] value=" + value + " -> " + (value / 100f));
                    prefs.edit().putFloat("click_through_alpha", value / 100f).apply();
                });

        // Edge hide visible portion slider (5%–50%)
        addSliderItem(menuLayout, context.getString(R.string.menu_edge_visible),
                Math.round(prefs.getFloat("edge_hide_visible_portion", 0.15f) * 100), 5, 50, "%",
                value -> {
                    Log.d(TAG, "[Slider:edge_visible] value=" + value + " -> " + (value / 100f));
                    prefs.edit().putFloat("edge_hide_visible_portion", value / 100f).apply();
                });

        // Expression blend mode (Cubism2 only)
        int currentBlendMode = prefs.getInt("expression_blend_mode", 0);
        String[] blendModeLabels = context.getResources().getStringArray(R.array.blend_mode_entries);
        addPickerItem(menuLayout, context.getString(R.string.menu_blend_mode),
                currentBlendMode < blendModeLabels.length ? blendModeLabels[currentBlendMode] : String.valueOf(currentBlendMode),
                blendModeLabels, currentBlendMode, selectedIndex -> {
                    Log.d(TAG, "[Picker:blend_mode] selectedIndex=" + selectedIndex);
                    prefs.edit().putInt("expression_blend_mode", selectedIndex).apply();
                    Live2DNativeBridge.nativeSetExpressionBlendMode(selectedIndex);
                    Log.d(TAG, "[Picker:blend_mode] nativeSetExpressionBlendMode called");
                });

        // Physics weight slider (0–100 → 0.0–1.0)
        addSliderItem(menuLayout, context.getString(R.string.menu_physics_weight),
                Math.round(prefs.getFloat("physics_weight", 1.0f) * 100), 0, 100, "%",
                value -> {
                    float weight = value / 100f;
                    Log.d(TAG, "[Slider:physics_weight] value=" + value + " weight=" + weight);
                    prefs.edit().putFloat("physics_weight", weight).apply();
                    Live2DNativeBridge.nativeSetPhysicsWeight(weight);
                });

        // Model mirror toggle
        addToggleItem(menuLayout, context.getString(R.string.menu_mirror),
                prefs.getBoolean("model_mirror", false),
                checked -> {
                    Log.d(TAG, "[Toggle:model_mirror] checked=" + checked);
                    prefs.edit().putBoolean("model_mirror", checked).apply();
                    Live2DNativeBridge.nativeSetMirror(checked);
                    Log.d(TAG, "[Toggle:model_mirror] nativeSetMirror called");
                });

        // Model rotation slider (-180° to 180°)
        addSliderItem(menuLayout, context.getString(R.string.menu_rotation),
                Math.round(prefs.getFloat("model_rotation", 0f)), -180, 180, "°",
                value -> {
                    Log.d(TAG, "[Slider:rotation] value=" + value);
                    prefs.edit().putFloat("model_rotation", (float) value).apply();
                    Live2DNativeBridge.nativeSetRotation((float) value);
                });

        // Menu data loading priority:
        // 1. Native metadata (from config.mlve via C++ layer) — richest data (names, priorities, choices, time limits)
        // 2. File-based fallback (from .model3.json / .model.json) — basic motion/expression lists
        // 3. actions.json overlay — user-defined custom actions, merged on top of either source
        MenuData menuData = loadMenuDataFromNative(context);
        Log.d(TAG, "MenuData from native: empty=" + menuData.isEmpty() + " expressions=" + menuData.expressions.size() + " groups=" + menuData.motionGroups.size());
        if (menuData.isEmpty()) {
            menuData = loadMenuDataFromFiles(modelDir);
            Log.d(TAG, "MenuData from files: empty=" + menuData.isEmpty() + " expressions=" + menuData.expressions.size() + " groups=" + menuData.motionGroups.size());
        }
        // Fill in sound paths from model_config.json if native metadata didn't include them
        fillMissingSounds(modelDir, menuData);
        mergeActionsJson(modelDir, menuData);

        if (!menuData.isEmpty()) {
            // Expressions section
            if (!menuData.expressions.isEmpty()) {
                addDivider(menuLayout);
                addSectionHeader(menuLayout, "  " + context.getString(R.string.menu_expressions));
                for (MenuItem item : menuData.expressions) {
                    addMenuItem(menuLayout, item, anchor);
                }
            }

            // Motion groups — merge by category prefix to reduce fragmentation.
            // Groups like "开关-皮带", "开关-衬衫" share the "开关" prefix and should
            // appear under a single section header instead of one header each.
            LinkedHashMap<String, List<Map.Entry<String, List<MenuItem>>>> categories = new LinkedHashMap<>();
            for (Map.Entry<String, List<MenuItem>> entry : menuData.motionGroups.entrySet()) {
                String cat = getCategoryPrefix(entry.getKey());
                if (!categories.containsKey(cat)) {
                    categories.put(cat, new ArrayList<>());
                }
                categories.get(cat).add(entry);
            }

            for (Map.Entry<String, List<Map.Entry<String, List<MenuItem>>>> catEntry : categories.entrySet()) {
                String category = catEntry.getKey();
                List<Map.Entry<String, List<MenuItem>>> groups = catEntry.getValue();

                addDivider(menuLayout);
                addSectionHeader(menuLayout, "  " + category);

                // Single group in category → show its entries directly (full list).
                // Multiple groups → each group is a compact item (tap to play via VarFloat selection).
                if (groups.size() == 1) {
                    for (MenuItem item : groups.get(0).getValue()) {
                        addMenuItem(menuLayout, item, anchor);
                    }
                } else {
                    for (Map.Entry<String, List<MenuItem>> groupEntry : groups) {
                        String subLabel = stripCategoryPrefix(groupEntry.getKey(), category);
                        if (subLabel.isEmpty()) subLabel = groupEntry.getKey();
                        addCompactItem(menuLayout, subLabel, groupEntry.getKey(), anchor);
                    }
                }
            }

            // Slot/component toggle section (aligned with Live2DViewerEX slot system)
            try {
                String slotJson = Live2DNativeBridge.nativeGetSlotState();
                Log.d(TAG, "Slot state JSON: " + (slotJson == null ? "null" : slotJson.length() + " chars"));
                if (slotJson != null && !slotJson.isEmpty()) {
                    JSONArray slotArray = new JSONArray(slotJson);
                    Log.d(TAG, "Slot array length=" + slotArray.length());
                    if (slotArray.length() > 0) {
                        addDivider(menuLayout);
                        addSectionHeader(menuLayout, "  " + context.getString(R.string.menu_slots));
                        for (int i = 0; i < slotArray.length(); i++) {
                            JSONObject slot = slotArray.getJSONObject(i);
                            Log.d(TAG, "Adding slot item[" + i + "]: name=" + slot.optString("name") + " target=" + slot.optString("target"));
                            addSlotItem(menuLayout, slot, anchor);
                        }
                    }
                }
            } catch (Exception e) {
                Log.w(TAG, "Failed to load slot state", e);
            }
        }

        // Wrap in ScrollView with max height
        ScrollView scrollView = new ScrollView(context);
        DisplayMetrics dm = context.getResources().getDisplayMetrics();
        int maxHeight = (int) (dm.heightPixels * 0.6f);
        scrollView.addView(menuLayout);

        popupWindow = new PopupWindow(scrollView,
                Math.min(dp(context, 240), dm.widthPixels - dp(context, 40)),
                maxHeight, true);
        popupWindow.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        popupWindow.setOutsideTouchable(true);
        popupWindow.setWindowLayoutType(android.view.WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY);
        popupWindow.setOnDismissListener(() -> {
            Log.d(TAG, "PopupWindow dismissed");
            popupWindow = null;
            Live2DNativeBridge.nativeSetMenuVisible(false);
        });
        popupWindow.setElevation(dp(context, 4));

        int[] location = new int[2];
        anchor.getLocationOnScreen(location);
        Live2DNativeBridge.nativeSetMenuVisible(true);
        popupWindow.showAtLocation(anchor, Gravity.NO_GRAVITY,
                location[0] + anchor.getWidth() / 2,
                location[1] - 10);
        Log.d(TAG, "show() popup displayed at (" + location[0] + ", " + location[1] + ") menuLayout childCount=" + menuLayout.getChildCount());
    }

    public void dismiss() {
        Log.d(TAG, "dismiss() called: popupWindow=" + (popupWindow != null) + " isShowing=" + (popupWindow != null && popupWindow.isShowing()));
        if (popupWindow != null && popupWindow.isShowing()) {
            popupWindow.dismiss();
            // popupWindow.dismiss() triggers onDismissListener which calls nativeSetMenuVisible(false)
        }
    }

    // Stored params for refresh (set by show(), cleared by dismiss)
    private Context _refreshContext;
    private View _refreshAnchor;
    private String _refreshModelDir;

    /**
     * Refresh the menu if it's currently showing. Dismisses and reopens with fresh state.
     * Called when VarFloat changes from native side (model click) to sync menu display.
     */
    public void refresh() {
        if (popupWindow != null && popupWindow.isShowing() && _refreshContext != null) {
            Log.d(TAG, "refresh() called: reopening menu");
            popupWindow.dismiss();
            show(_refreshContext, _refreshAnchor, _refreshModelDir);
        }
    }

    // --- Native Metadata Loading ---

    /**
     * Load motion metadata from C++ native layer (from config.mlve).
     * Returns rich metadata with names, priorities, expressions, etc.
     */
    private MenuData loadMenuDataFromNative(Context context) {
        MenuData data = new MenuData();
        try {
            // Get motion metadata JSON from native
            String motionJson = Live2DNativeBridge.nativeGetMotionMetaList();
            Log.d(TAG, "loadMenuDataFromNative: motionJson=" + (motionJson == null ? "null" : motionJson.length() + " chars"));
            if (motionJson == null || motionJson.isEmpty()) return data;

            JSONObject motions = new JSONObject(motionJson);
            Log.d(TAG, "loadMenuDataFromNative: groups count=" + motions.length());
            if (motions.length() == 0) return data;

            Iterator<String> groups = motions.keys();
            while (groups.hasNext()) {
                String groupName = groups.next();
                JSONArray motionArray = motions.optJSONArray(groupName);
                if (motionArray == null) continue;

                List<MenuItem> groupItems = new ArrayList<>();
                for (int i = 0; i < motionArray.length(); i++) {
                    JSONObject m = motionArray.getJSONObject(i);
                    String name = m.optString("name", "");
                    boolean enabled = m.optBoolean("enabled", true);
                    if (!enabled) continue;  // skip disabled motions

                    // Build label: use name if available, otherwise fallback to #index
                    String label;
                    if (!name.isEmpty()) {
                        label = "  " + name;
                    } else {
                        label = "  #" + i;
                    }

                    int priority = m.optInt("priority", 2);
                    int fadeIn = m.optInt("fade_in", 0);
                    int fadeOut = m.optInt("fade_out", 0);
                    String expression = m.optString("expression", "");
                    int weight = m.optInt("weight", 1);
                    String sound = m.optString("sound", null);
                    if (sound != null && sound.isEmpty()) sound = null;

                    // Parse choices
                    JSONArray choicesArr = m.optJSONArray("choices");
                    List<ChoiceItem> choices = null;
                    if (choicesArr != null && choicesArr.length() > 0) {
                        choices = new ArrayList<>();
                        for (int c = 0; c < choicesArr.length(); c++) {
                            JSONObject ch = choicesArr.getJSONObject(c);
                            choices.add(new ChoiceItem(
                                    ch.optString("text", ""),
                                    ch.optString("group", ""),
                                    ch.optString("motion", ""),
                                    ch.optString("next_mtn", "")));
                        }
                    }

                    // Parse time_limit
                    JSONObject timeLimitObj = m.optJSONObject("time_limit");
                    TimeLimitItem timeLimit = null;
                    if (timeLimitObj != null) {
                        timeLimit = new TimeLimitItem(
                                timeLimitObj.optInt("hour", -1),
                                timeLimitObj.optInt("minute", -1),
                                timeLimitObj.optInt("month", -1),
                                timeLimitObj.optInt("day", -1),
                                timeLimitObj.optInt("begin", -1),
                                timeLimitObj.optInt("end", -1),
                                timeLimitObj.optBoolean("birthday", false));
                    }

                    // Filter by time limit
                    if (timeLimit != null && !timeLimit.isAvailableNow(context)) continue;

                    groupItems.add(new MenuItem(label, groupName, i,
                            expression.isEmpty() ? null : expression,
                            null, priority, fadeIn, fadeOut, weight,
                            choices, timeLimit, sound));
                }
                if (!groupItems.isEmpty()) {
                    data.motionGroups.put(groupName, groupItems);
                }
            }

            // Get group display names
            String groupJson = Live2DNativeBridge.nativeGetGroupList();
            if (groupJson != null && !groupJson.isEmpty()) {
                JSONArray groupArray = new JSONArray(groupJson);
                for (int i = 0; i < groupArray.length(); i++) {
                    JSONObject g = groupArray.getJSONObject(i);
                    String name = g.optString("name", "");
                    String text = g.optString("text", "");
                    if (!name.isEmpty() && !text.isEmpty()) {
                        data.groupDisplayNames.put(name, text);
                    }
                }
            }


        } catch (Exception e) {
            Log.w(TAG, "Failed to load native motion metadata", e);
        }
        return data;
    }

    // --- File-Based Loading (Fallback) ---

    private MenuData loadMenuDataFromFiles(String modelDir) {
        MenuData data = new MenuData();
        if (modelDir == null) {
            Log.w(TAG, "loadMenuDataFromFiles: modelDir is null");
            return data;
        }
        Log.d(TAG, "loadMenuDataFromFiles: modelDir=" + modelDir);

        // Load from model JSON
        File[] cubism3Files = new File(modelDir).listFiles(
                (d, name) -> name.endsWith(".model3.json"));
        if (cubism3Files != null && cubism3Files.length > 0) {
            Log.d(TAG, "loadMenuDataFromFiles: found cubism3 model: " + cubism3Files[0].getName());
            loadCubism3ModelJson(cubism3Files[0], data);
        } else {
            File[] cubism2Files = new File(modelDir).listFiles(
                    (d, name) -> name.endsWith(".model.json") && !name.endsWith(".model3.json"));
            if (cubism2Files != null && cubism2Files.length > 0) {
                Log.d(TAG, "loadMenuDataFromFiles: found cubism2 model: " + cubism2Files[0].getName());
                loadCubism2ModelJson(cubism2Files[0], data);
            } else {
                Log.w(TAG, "loadMenuDataFromFiles: no model files found");
            }
        }
        Log.d(TAG, "loadMenuDataFromFiles: result expressions=" + data.expressions.size() + " groups=" + data.motionGroups.size());
        return data;
    }

    // --- Actions.json Merging ---

    private void mergeActionsJson(String modelDir, MenuData data) {
        List<MenuItem> customItems = loadFromActionsJson(modelDir);
        Log.d(TAG, "mergeActionsJson: customItems=" + (customItems == null ? "null" : customItems.size()));
        if (customItems == null || customItems.isEmpty()) return;

        for (MenuItem item : customItems) {
            if (item.param != null) {
                String controlGroup = "Controls";
                List<MenuItem> groupList = data.motionGroups.get(controlGroup);
                if (groupList == null) {
                    groupList = new ArrayList<>();
                    data.motionGroups.put(controlGroup, groupList);
                }
                groupList.add(item);
            } else if (item.expression != null) {
                boolean exists = false;
                for (MenuItem existing : data.expressions) {
                    if (item.expression.equals(existing.expression)) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    data.expressions.add(item);
                }
            } else if (item.motionGroup != null) {
                List<MenuItem> groupList = data.motionGroups.get(item.motionGroup);
                if (groupList == null) {
                    groupList = new ArrayList<>();
                    data.motionGroups.put(item.motionGroup, groupList);
                }
                boolean exists = false;
                for (MenuItem existing : groupList) {
                    if (existing.motionGroup != null
                            && existing.motionGroup.equals(item.motionGroup)
                            && existing.motionIndex == item.motionIndex) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    groupList.add(item);
                }
            }
        }
    }

    private List<MenuItem> loadFromActionsJson(String modelDir) {
        File actionsFile = new File(modelDir, "actions.json");
        if (!actionsFile.exists()) return null;
        try {
            String jsonStr = FileUtils.readFileAsString(actionsFile);
            if (jsonStr == null) return null;
            JSONObject json = new JSONObject(jsonStr);
            JSONArray menuItems = json.optJSONArray("menuItems");
            if (menuItems == null) return null;

            List<MenuItem> items = new ArrayList<>();
            for (int i = 0; i < menuItems.length(); i++) {
                JSONObject obj = menuItems.getJSONObject(i);
                String label = obj.optString("label", "");
                String motionGroup = obj.optString("motionGroup", null);
                int motionIndex = obj.optInt("motionIndex", 0);
                String expression = obj.optString("expression", null);
                String param = obj.optString("param", null);
                int priority = obj.optInt("priority", 3);  // default Force for menu items
                int fadeIn = obj.optInt("fade_in", 0);
                int fadeOut = obj.optInt("fade_out", 0);
                items.add(new MenuItem(label, motionGroup, motionIndex, expression, param, priority, fadeIn, fadeOut));
            }
            return items;
        } catch (Exception e) {
            return null;
        }
    }

    // --- Sound Path Fallback ---

    /**
     * Load sound paths from model_config.json and fill in missing sound fields
     * on MenuItems that don't already have one.
     */
    private void fillMissingSounds(String modelDir, MenuData data) {
        if (modelDir == null) return;
        try {
            File configFile = new File(modelDir, "model_config.json");
            if (!configFile.exists()) return;
            String json = FileUtils.readFileAsString(configFile);
            if (json == null || json.isEmpty()) return;
            JSONObject config = new JSONObject(json);
            JSONObject motions = config.optJSONObject("motions");
            if (motions == null) return;

            for (List<MenuItem> groupItems : data.motionGroups.values()) {
                for (int idx = 0; idx < groupItems.size(); idx++) {
                    MenuItem item = groupItems.get(idx);
                    if (item.sound != null || item.motionGroup == null) continue;
                    JSONArray motionArray = motions.optJSONArray(item.motionGroup);
                    if (motionArray == null || item.motionIndex >= motionArray.length()) continue;
                    JSONObject m = motionArray.optJSONObject(item.motionIndex);
                    if (m == null) continue;
                    String sound = m.optString("sound", m.optString("Sound", null));
                    if (sound != null && !sound.isEmpty()) {
                        groupItems.set(idx, new MenuItem(item.label, item.motionGroup,
                                item.motionIndex, item.expression, item.param,
                                item.priority, item.fadeInTime, item.fadeOutTime,
                                item.weight, item.choices, item.timeLimit, sound));
                    }
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "fillMissingSounds failed", e);
        }
    }

    // --- Model JSON Loading (Fallback) ---

    private void loadCubism3ModelJson(File jsonFile, MenuData data) {
        try {
            String jsonStr = FileUtils.readFileAsString(jsonFile);
            if (jsonStr == null) return;
            JSONObject json = new JSONObject(jsonStr);
            JSONObject fileRefs = json.optJSONObject("FileReferences");
            if (fileRefs == null) return;

            // Expressions
            JSONArray expressions = fileRefs.optJSONArray("Expressions");
            if (expressions != null) {
                for (int i = 0; i < expressions.length(); i++) {
                    JSONObject expr = expressions.getJSONObject(i);
                    String name = expr.optString("Name", expr.optString("name", "Expression " + i));
                    data.expressions.add(new MenuItem("  " + name, null, 0, name, null, 3, 0, 0));
                }
            }

            // Motions
            JSONObject motions = fileRefs.optJSONObject("Motions");
            if (motions != null) {
                Iterator<String> groups = motions.keys();
                while (groups.hasNext()) {
                    String group = groups.next();
                    JSONArray motionList = motions.getJSONArray(group);
                    List<MenuItem> groupItems = new ArrayList<>();
                    for (int i = 0; i < motionList.length(); i++) {
                        JSONObject motion = motionList.optJSONObject(i);
                        String label = "  #" + i;
                        String sound = null;
                        if (motion != null) {
                            String file = motion.optString("File", motion.optString("file", ""));
                            if (!file.isEmpty()) {
                                String baseName = file;
                                int lastSlash = baseName.lastIndexOf('/');
                                if (lastSlash >= 0) baseName = baseName.substring(lastSlash + 1);
                                int lastDot = baseName.lastIndexOf('.');
                                if (lastDot > 0) baseName = baseName.substring(0, lastDot);
                                baseName = baseName.replaceFirst("(?i)^" + group.toLowerCase(Locale.ROOT) + "_?", "");
                                if (!baseName.isEmpty() && !baseName.equals(String.valueOf(i))) {
                                    label = "  " + baseName;
                                }
                            }
                            sound = motion.optString("Sound", null);
                            if (sound != null && sound.isEmpty()) sound = null;
                        }
                        groupItems.add(new MenuItem(label, group, i, null, null, 2, 0, 0, 1, null, null, sound));
                    }
                    data.motionGroups.put(group, groupItems);
                }
            }
        } catch (Exception e) {
            // Ignore
        }
    }

    private void loadCubism2ModelJson(File jsonFile, MenuData data) {
        try {
            String jsonStr = FileUtils.readFileAsString(jsonFile);
            if (jsonStr == null) return;
            JSONObject json = new JSONObject(jsonStr);

            JSONArray expressions = json.optJSONArray("expressions");
            if (expressions != null) {
                for (int i = 0; i < expressions.length(); i++) {
                    JSONObject expr = expressions.getJSONObject(i);
                    String name = expr.optString("name", "Expression " + i);
                    data.expressions.add(new MenuItem("  " + name, null, 0, name, null, 3, 0, 0));
                }
            }

            JSONObject motions = json.optJSONObject("motions");
            if (motions != null) {
                Iterator<String> groups = motions.keys();
                while (groups.hasNext()) {
                    String group = groups.next();
                    JSONArray motionList = motions.getJSONArray(group);
                    List<MenuItem> groupItems = new ArrayList<>();
                    for (int i = 0; i < motionList.length(); i++) {
                        groupItems.add(new MenuItem("  #" + i, group, i, null, null, 2, 0, 0));
                    }
                    data.motionGroups.put(group, groupItems);
                }
            }
        } catch (Exception e) {
            // Ignore
        }
    }

    // --- UI Helpers ---

    private void addSectionHeader(LinearLayout layout, String text) {
        TextView tv = new TextView(layout.getContext());
        tv.setText(text);
        tv.setTextColor(colorHeader);
        tv.setTextSize(12);
        tv.setTypeface(null, Typeface.BOLD);
        tv.setPadding(dp(layout.getContext(), 14), dp(layout.getContext(), 10),
                dp(layout.getContext(), 14), dp(layout.getContext(), 4));
        layout.addView(tv);
    }

    private void addDivider(LinearLayout layout) {
        View divider = new View(layout.getContext());
        divider.setBackgroundColor(colorDivider);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 1);
        lp.setMargins(dp(layout.getContext(), 14), dp(layout.getContext(), 4),
                dp(layout.getContext(), 14), dp(layout.getContext(), 4));
        divider.setLayoutParams(lp);
        layout.addView(divider);
    }

    /**
     * Extract category prefix from a motion group name.
     * E.g. "开关-皮带" → "开关", "动画_2.打飞机" → "动画", "触摸-头" → "触摸"
     * Groups without a recognized prefix go into "其他".
     */
    private static String getCategoryPrefix(String groupName) {
        // Try Chinese dash separator: "prefix-remainder"
        int dash = groupName.indexOf('-');
        if (dash > 0) return groupName.substring(0, dash);

        // Try underscore separator for "动画_N.xxx" patterns
        int under = groupName.indexOf('_');
        if (under > 0) return groupName.substring(0, under);

        // Bracket groups like "【工程标记用】" → use as-is
        if (groupName.startsWith("【")) return groupName;

        return "其他";
    }

    /**
     * Strip category prefix from group name to get the sub-label.
     * E.g. ("开关-皮带", "开关") → "皮带"
     * E.g. ("动画_2.打飞机", "动画") → "2.打飞机"
     */
    private static String stripCategoryPrefix(String groupName, String category) {
        if (groupName.startsWith(category)) {
            String rest = groupName.substring(category.length());
            if (rest.startsWith("-") || rest.startsWith("_")) rest = rest.substring(1);
            return rest;
        }
        return groupName;
    }

    /**
     * Add a compact item for a small motion group.
     * Tapping uses VarFloat-aware selection on the native side to play
     * the correct entry based on the current state.
     */
    private void addCompactItem(LinearLayout layout, String label, String motionGroup, View anchor) {
        Context context = layout.getContext();
        TextView tv = new TextView(context);
        tv.setText("  " + label);
        tv.setTextColor(colorText);
        tv.setTextSize(14);
        tv.setPadding(dp(context, 16), dp(context, 10), dp(context, 16), dp(context, 10));
        tv.setBackground(createItemRipple(context));
        tv.setOnClickListener(v -> {
            SharedPreferences prefs = context.getSharedPreferences(
                    FloatingWindowService.PREF_NAME, Context.MODE_PRIVATE);
            boolean motionEnabled = prefs.getBoolean("motion_enabled", true);
            boolean isGL = anchor instanceof GLSurfaceView;
            Log.d(TAG, "addCompactItem click: group=[" + motionGroup + "] label=[" + label + "] motionEnabled=" + motionEnabled + " isGL=" + isGL + " anchor=" + anchor.getClass().getSimpleName());
            if (isGL && motionEnabled) {
                ((GLSurfaceView) anchor).queueEvent(() -> {
                    Log.d(TAG, "addCompactItem queueEvent: calling nativeLoadMotionByGroup [" + motionGroup + "]");
                    Live2DNativeBridge.nativeLoadMotionByGroup(motionGroup);
                    Log.d(TAG, "addCompactItem queueEvent: nativeLoadMotionByGroup returned");
                });
            } else if (!motionEnabled) {
                Log.w(TAG, "addCompactItem: SKIPPED motion is disabled");
            } else {
                Log.w(TAG, "addCompactItem: SKIPPED anchor is NOT GLSurfaceView");
            }
            if (popupWindow != null) popupWindow.dismiss();
        });
        layout.addView(tv);
    }

    private void addMenuItem(LinearLayout layout, MenuItem item, View anchor) {
        Context context = layout.getContext();
        TextView tv = new TextView(context);
        tv.setText(item.label);
        tv.setTextColor(colorText);
        tv.setTextSize(14);
        tv.setPadding(dp(context, 16), dp(context, 10), dp(context, 16), dp(context, 10));
        tv.setBackground(createItemRipple(context));
        tv.setOnClickListener(v -> {
            // Check if motions are enabled
            SharedPreferences prefs = context.getSharedPreferences(
                    FloatingWindowService.PREF_NAME, Context.MODE_PRIVATE);
            boolean motionEnabled = prefs.getBoolean("motion_enabled", true);
            Log.d(TAG, "addMenuItem click: label=[" + item.label + "] param=[" + item.param + "] group=[" + item.motionGroup + "] motionEnabled=" + motionEnabled + " anchor=" + anchor.getClass().getSimpleName());

            // If motion has choices, show sub-popup instead of playing directly
            if (item.choices != null && !item.choices.isEmpty()) {
                if (!motionEnabled) return;
                Log.d(TAG, "addMenuItem: showing choices popup for [" + item.label + "]");
                showChoicesPopup(context, anchor, item);
                return;
            }

            if (anchor instanceof GLSurfaceView && motionEnabled) {
                ((GLSurfaceView) anchor).queueEvent(() -> {
                    // Set expression first if the motion has one linked
                    if (item.expression != null && !item.expression.isEmpty()) {
                        Log.d(TAG, "addMenuItem queueEvent: setExpression [" + item.expression + "]");
                        Live2DNativeBridge.nativeSetExpression(item.expression);
                    }
                    if (item.param != null && !item.param.isEmpty()) {
                        Log.d(TAG, "addMenuItem queueEvent: nativeToggleParam [" + item.param + "]");
                        Live2DNativeBridge.nativeToggleParam(item.param);
                    }
                    if (item.motionGroup != null && !item.motionGroup.isEmpty()) {
                        // Use metadata-driven priority and fade times
                        float fadeInSec = item.fadeInTime / 1000.0f;
                        float fadeOutSec = item.fadeOutTime / 1000.0f;
                        Log.d(TAG, "addMenuItem queueEvent: nativeLoadMotion group=[" + item.motionGroup + "] index=" + item.motionIndex + " pri=" + item.priority + " fade=" + fadeInSec + "/" + fadeOutSec);
                        Live2DNativeBridge.nativeLoadMotion(
                                item.motionGroup, item.motionIndex,
                                item.priority, fadeInSec, fadeOutSec);
                    }
                });
            } else {
                Log.w(TAG, "addMenuItem: SKIPPED - anchor not GLSurfaceView(" + (anchor instanceof GLSurfaceView) + ") or motionEnabled=" + motionEnabled);
            }
            if (popupWindow != null) popupWindow.dismiss();
        });
        layout.addView(tv);
    }

    /**
     * Add a slot toggle item that cycles through the slot's values on tap.
     * Each slot has ids[] (parts) and values[] (parameter values), displayed as "name: currentValue".
     */
    @SuppressLint("SetTextI18n")
    private void addSlotItem(LinearLayout layout, JSONObject slot, View anchor) {
        Context context = layout.getContext();
        String name = slot.optString("name", "");
        String target = slot.optString("target", "");
        int currentIndex = slot.optInt("currentIndex", -1);
        JSONArray values = slot.optJSONArray("values");
        JSONArray ids = slot.optJSONArray("ids");

        // Build label: "name: currentValue" or "name: id[currentIndex]"
        String valueText = "";
        if (ids != null && currentIndex >= 0 && currentIndex < ids.length()) {
            valueText = ids.optString(currentIndex, "");
        } else if (values != null && currentIndex >= 0 && currentIndex < values.length()) {
            valueText = String.valueOf(values.optDouble(currentIndex, 0));
        }
        String label = name.isEmpty() ? target : name;
        if (!valueText.isEmpty()) {
            label += ": " + valueText;
        }

        TextView tv = new TextView(context);
        tv.setText(label);
        tv.setTextColor(colorText);
        tv.setTextSize(14);
        tv.setPadding(dp(context, 16), dp(context, 10), dp(context, 16), dp(context, 10));
        tv.setBackground(createItemRipple(context));
        tv.setOnClickListener(v -> {
            boolean isGL = anchor instanceof GLSurfaceView;
            Log.d(TAG, "addSlotItem click: name=[" + name + "] target=[" + target + "] idx=" + currentIndex + " isGL=" + isGL + " anchor=" + anchor.getClass().getSimpleName());
            if (isGL) {
                ((GLSurfaceView) anchor).queueEvent(() -> {
                    Log.d(TAG, "addSlotItem queueEvent: calling nativeToggleParam [" + target + "]");
                    Live2DNativeBridge.nativeToggleParam(target);
                    Log.d(TAG, "addSlotItem queueEvent: nativeToggleParam returned");
                });
            } else {
                Log.w(TAG, "addSlotItem: anchor is NOT GLSurfaceView, skipping nativeToggleParam");
            }
            if (popupWindow != null) popupWindow.dismiss();
        });
        layout.addView(tv);
    }

    /**
     * Show a sub-popup with choices for a motion that has branching dialogue.
     */
    @SuppressLint("SetTextI18n")
    private void showChoicesPopup(Context context, View anchor, MenuItem item) {
        LinearLayout choiceLayout = new LinearLayout(context);
        choiceLayout.setOrientation(LinearLayout.VERTICAL);
        android.graphics.drawable.GradientDrawable choiceBg = new android.graphics.drawable.GradientDrawable();
        choiceBg.setColor(colorBg);
        choiceBg.setStroke(dp(context, 1), colorBorder);
        choiceBg.setCornerRadius(dp(context, 8));
        choiceLayout.setBackground(choiceBg);
        choiceLayout.setPadding(0, dp(context, 4), 0, dp(context, 4));

        // Title
        addTextItem(choiceLayout, "  " + item.label.trim(), dp(context, 12));

        for (ChoiceItem choice : item.choices) {
            TextView choiceTv = new TextView(context);
            choiceTv.setText("  " + (choice.text.isEmpty() ? "..." : choice.text));
            choiceTv.setTextColor(colorText);
            choiceTv.setTextSize(14);
            choiceTv.setPadding(dp(context, 16), dp(context, 10), dp(context, 16), dp(context, 10));
            choiceTv.setBackground(createItemRipple(context));
            choiceTv.setOnClickListener(v -> {
                Log.d(TAG, "showChoicesPopup choice click: text=[" + choice.text + "] motion=[" + choice.motion + "] group=[" + choice.group + "]");
                if (anchor instanceof GLSurfaceView) {
                    ((GLSurfaceView) anchor).queueEvent(() -> {
                        // Set expression from parent motion
                        if (item.expression != null && !item.expression.isEmpty()) {
                            Log.d(TAG, "showChoicesPopup queueEvent: setExpression [" + item.expression + "]");
                            Live2DNativeBridge.nativeSetExpression(item.expression);
                        }
                        // Play the choice's motion
                        if (choice.motion != null && !choice.motion.isEmpty()) {
                            String[] parts = choice.motion.split(":");
                            String group = parts[0];
                            int index = 0;
                            if (parts.length > 1) {
                                try { index = Integer.parseInt(parts[1]); } catch (NumberFormatException ignored) {}
                            }
                            Log.d(TAG, "showChoicesPopup queueEvent: nativeLoadMotion group=[" + group + "] index=" + index);
                            Live2DNativeBridge.nativeLoadMotion(group, index, 3, 0, 0);
                        } else if (choice.group != null && !choice.group.isEmpty()) {
                            // Random from group
                            Log.d(TAG, "showChoicesPopup queueEvent: nativeLoadMotion group=[" + choice.group + "] index=0");
                            Live2DNativeBridge.nativeLoadMotion(choice.group, 0, 3, 0, 0);
                        }
                    });
                }
                // Dismiss both popups
                dismiss();
            });
            choiceLayout.addView(choiceTv);
        }

        // Cancel button — listener wired after choicePopup is created
        TextView cancelTv = new TextView(context);
        cancelTv.setText("  " + context.getString(R.string.menu_cancel));
        cancelTv.setTextColor(colorTextDim);
        cancelTv.setTextSize(13);
        cancelTv.setPadding(dp(context, 16), dp(context, 10), dp(context, 16), dp(context, 10));
        cancelTv.setBackground(createItemRipple(context));
        choiceLayout.addView(cancelTv);

        ScrollView choiceScroll = new ScrollView(context);
        DisplayMetrics dm = context.getResources().getDisplayMetrics();
        int maxHeight = (int) (dm.heightPixels * 0.4f);
        choiceScroll.addView(choiceLayout);

        PopupWindow choicePopup = new PopupWindow(choiceScroll,
                Math.min(dp(context, 200), dm.widthPixels - dp(context, 60)),
                maxHeight, true);
        choicePopup.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        choicePopup.setOutsideTouchable(true);
        choicePopup.setWindowLayoutType(android.view.WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY);
        choicePopup.setElevation(dp(context, 4));

        cancelTv.setOnClickListener(v -> choicePopup.dismiss());

        int[] location = new int[2];
        anchor.getLocationOnScreen(location);
        choicePopup.showAtLocation(anchor, Gravity.NO_GRAVITY,
                location[0] + anchor.getWidth() / 2 + dp(context, 10),
                location[1] - 10);
    }

    private void addToggleItem(LinearLayout layout, String label, boolean initialState,
                                ToggleCallback onToggle) {
        Context context = layout.getContext();
        LinearLayout row = new LinearLayout(context);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(android.view.Gravity.CENTER_VERTICAL);
        row.setPadding(dp(context, 16), dp(context, 8), dp(context, 16), dp(context, 8));
        row.setBackground(createItemRipple(context));

        TextView tv = new TextView(context);
        tv.setText(label);
        tv.setTextColor(colorText);
        tv.setTextSize(14);
        LinearLayout.LayoutParams tvLp = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        row.addView(tv, tvLp);

        android.view.ContextThemeWrapper themedCtx = new android.view.ContextThemeWrapper(
                context, androidx.appcompat.R.style.Theme_AppCompat);
        androidx.appcompat.widget.SwitchCompat toggle = new androidx.appcompat.widget.SwitchCompat(themedCtx);
        toggle.setChecked(initialState);
        toggle.setShowText(false);
        toggle.setTextOn("");
        toggle.setTextOff("");
        toggle.setThumbTintList(android.content.res.ColorStateList.valueOf(colorSwitchThumb));
        toggle.setTrackTintList(android.content.res.ColorStateList.valueOf(colorSwitchTrack));
        toggle.setClickable(false);
        toggle.setFocusable(false);
        row.addView(toggle);

        row.setOnClickListener(v -> {
            boolean oldState = toggle.isChecked();
            boolean newState = !oldState;
            Log.d(TAG, "Toggle row clicked: label=[" + label + "] oldState=" + oldState + " -> newState=" + newState + " view=" + v);
            toggle.setChecked(newState);
            Log.d(TAG, "Toggle setChecked done: label=[" + label + "] isChecked=" + toggle.isChecked());
            Log.d(TAG, "Invoking ToggleCallback: label=[" + label + "] value=" + newState);
            onToggle.onToggle(newState);
            Log.d(TAG, "ToggleCallback returned: label=[" + label + "]");
        });

        layout.addView(row);
    }

    private void addToggleItem(LinearLayout layout, String label, boolean initialState,
                                ToggleCallback onToggle, androidx.appcompat.widget.SwitchCompat existingToggle) {
        Context context = layout.getContext();
        LinearLayout row = new LinearLayout(context);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(android.view.Gravity.CENTER_VERTICAL);
        row.setPadding(dp(context, 16), dp(context, 8), dp(context, 16), dp(context, 8));
        row.setBackground(createItemRipple(context));

        TextView tv = new TextView(context);
        tv.setText(label);
        tv.setTextColor(colorText);
        tv.setTextSize(14);
        LinearLayout.LayoutParams tvLp = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        row.addView(tv, tvLp);

        existingToggle.setChecked(initialState);
        existingToggle.setShowText(false);
        existingToggle.setTextOn("");
        existingToggle.setTextOff("");
        existingToggle.setThumbTintList(android.content.res.ColorStateList.valueOf(colorSwitchThumb));
        existingToggle.setTrackTintList(android.content.res.ColorStateList.valueOf(colorSwitchTrack));
        existingToggle.setClickable(false);
        existingToggle.setFocusable(false);
        row.addView(existingToggle);

        row.setOnClickListener(v -> {
            boolean oldState = existingToggle.isChecked();
            boolean newState = !oldState;
            Log.d(TAG, "Toggle row clicked (existing): label=[" + label + "] oldState=" + oldState + " -> newState=" + newState + " view=" + v);
            existingToggle.setChecked(newState);
            Log.d(TAG, "Toggle setChecked done (existing): label=[" + label + "] isChecked=" + existingToggle.isChecked());
            Log.d(TAG, "Invoking ToggleCallback (existing): label=[" + label + "] value=" + newState);
            onToggle.onToggle(newState);
            Log.d(TAG, "ToggleCallback returned (existing): label=[" + label + "]");
        });

        layout.addView(row);
    }

    private void showBothDisabledWarning(Context context, SharedPreferences prefs,
                                          String key, boolean value,
                                          androidx.appcompat.widget.SwitchCompat toggle) {
        Log.w(TAG, "showBothDisabledWarning: key=[" + key + "] value=" + value + " toggle.isChecked=" + toggle.isChecked());
        showDialog(context, FileUtils.buildBothDisabledWarning(context, () -> {
            Log.d(TAG, "BothDisabled revert: key=[" + key + "] = " + !value);
            toggle.setChecked(!value);
            prefs.edit().putBoolean(key, !value).commit();
        }));
    }

    private interface SliderCallback {
        void onChanged(int value);
    }

    private void addSliderItem(LinearLayout layout, String label, int initialValue,
                                int min, int max, String suffix, SliderCallback onChanged) {
        Context context = layout.getContext();
        LinearLayout container = new LinearLayout(context);
        container.setOrientation(LinearLayout.VERTICAL);
        container.setPadding(dp(context, 16), dp(context, 6), dp(context, 16), dp(context, 6));

        LinearLayout headerRow = new LinearLayout(context);
        headerRow.setOrientation(LinearLayout.HORIZONTAL);
        headerRow.setGravity(android.view.Gravity.CENTER_VERTICAL);

        TextView labelTv = new TextView(context);
        labelTv.setText(label);
        labelTv.setTextColor(colorText);
        labelTv.setTextSize(13);
        headerRow.addView(labelTv, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        TextView valueTv = new TextView(context);
        valueTv.setText(initialValue + suffix);
        valueTv.setTextColor(colorTextDim);
        valueTv.setTextSize(13);
        headerRow.addView(valueTv);

        container.addView(headerRow);

        SeekBar seekBar = new SeekBar(context);
        seekBar.setMax(max - min);
        seekBar.setProgress(initialValue - min);
        seekBar.setPadding(dp(context, 8), 0, dp(context, 8), 0);
        container.addView(seekBar);

        seekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
                valueTv.setText((progress + min) + suffix);
            }
            @Override public void onStartTrackingTouch(SeekBar sb) {}
            @Override
            public void onStopTrackingTouch(SeekBar sb) {
                int val = sb.getProgress() + min;
                Log.d(TAG, "Slider onStopTrackingTouch: label=[" + label + "] value=" + val);
                onChanged.onChanged(val);
            }
        });

        layout.addView(container);
    }

    private interface PickerCallback {
        void onSelected(int index);
    }

    private void addPickerItem(LinearLayout layout, String label, String currentValue,
                                String[] options, int selectedIndex, PickerCallback onSelected) {
        Context context = layout.getContext();
        LinearLayout row = new LinearLayout(context);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(android.view.Gravity.CENTER_VERTICAL);
        row.setPadding(dp(context, 16), dp(context, 8), dp(context, 16), dp(context, 8));
        row.setBackground(createItemRipple(context));

        TextView tv = new TextView(context);
        tv.setText(label);
        tv.setTextColor(colorText);
        tv.setTextSize(14);
        row.addView(tv, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        TextView valueTv = new TextView(context);
        valueTv.setText(currentValue);
        valueTv.setTextColor(colorTextDim);
        valueTv.setTextSize(13);
        valueTv.setPadding(dp(context, 8), 0, 0, 0);
        row.addView(valueTv);

        final int[] current = {selectedIndex};
        row.setOnClickListener(v -> showDialog(context, new android.app.AlertDialog.Builder(context)
                .setTitle(label)
                .setSingleChoiceItems(options, current[0], (dialog, which) -> {
                    Log.d(TAG, "Picker selected: label=[" + label + "] index=" + which + " option=[" + options[which] + "]");
                    current[0] = which;
                    valueTv.setText(options[which]);
                    onSelected.onSelected(which);
                    dialog.dismiss();
                })
                .setNegativeButton(R.string.dialog_cancel, null)));

        layout.addView(row);
    }

    private void addTextItem(LinearLayout layout, String text, int padding) {
        TextView tv = new TextView(layout.getContext());
        tv.setText(text);
        tv.setTextColor(colorHeader);
        tv.setTextSize(13);
        tv.setPadding(padding, dp(layout.getContext(), 12), padding, dp(layout.getContext(), 12));
        layout.addView(tv);
    }

    private android.graphics.drawable.Drawable createItemRipple(Context context) {
        android.content.res.ColorStateList rippleColor = android.content.res.ColorStateList.valueOf(colorRipple);
        android.graphics.drawable.GradientDrawable mask = new android.graphics.drawable.GradientDrawable();
        mask.setColor(colorBgAlt);
        mask.setCornerRadius(dp(context, 4));
        return new android.graphics.drawable.RippleDrawable(rippleColor, null, mask);
    }

    private static int dp(Context context, int value) {
        return FileUtils.dp(context, value);
    }

    // --- Data Classes ---

    private static class MenuData {
        final List<MenuItem> expressions = new ArrayList<>();
        final LinkedHashMap<String, List<MenuItem>> motionGroups = new LinkedHashMap<>();
        final LinkedHashMap<String, String> groupDisplayNames = new LinkedHashMap<>();

        boolean isEmpty() {
            return expressions.isEmpty() && motionGroups.isEmpty();
        }
    }

    private static class MenuItem {
        final String label;
        final String motionGroup;
        final int motionIndex;
        final String expression;
        final String param;
        final int priority;
        final int fadeInTime;   // milliseconds
        final int fadeOutTime;  // milliseconds
        final int weight;
        final List<ChoiceItem> choices;
        final TimeLimitItem timeLimit;
        final String sound;     // sound file path relative to model dir

        MenuItem(String label, String motionGroup, int motionIndex, String expression,
                 String param, int priority, int fadeInTime, int fadeOutTime) {
            this(label, motionGroup, motionIndex, expression, param, priority, fadeInTime, fadeOutTime, 1, null, null, null);
        }

        MenuItem(String label, String motionGroup, int motionIndex, String expression,
                 String param, int priority, int fadeInTime, int fadeOutTime, int weight,
                 List<ChoiceItem> choices, TimeLimitItem timeLimit) {
            this(label, motionGroup, motionIndex, expression, param, priority, fadeInTime, fadeOutTime, weight, choices, timeLimit, null);
        }

        MenuItem(String label, String motionGroup, int motionIndex, String expression,
                 String param, int priority, int fadeInTime, int fadeOutTime, int weight,
                 List<ChoiceItem> choices, TimeLimitItem timeLimit, String sound) {
            this.label = label;
            this.motionGroup = motionGroup;
            this.motionIndex = motionIndex;
            this.expression = expression;
            this.param = param;
            this.priority = priority;
            this.fadeInTime = fadeInTime;
            this.fadeOutTime = fadeOutTime;
            this.weight = weight;
            this.choices = choices;
            this.timeLimit = timeLimit;
            this.sound = sound;
        }
    }

    private static class ChoiceItem {
        final String text;
        final String group;
        final String motion;
        final String nextMtn;

        ChoiceItem(String text, String group, String motion, String nextMtn) {
            this.text = text;
            this.group = group;
            this.motion = motion;
            this.nextMtn = nextMtn;
        }
    }

    private static class TimeLimitItem {
        final int hour;
        final int minute;
        final int month;
        final int day;
        final int begin;
        final int end;
        final boolean birthday;

        TimeLimitItem(int hour, int minute, int month, int day, int begin, int end, boolean birthday) {
            this.hour = hour;
            this.minute = minute;
            this.month = month;
            this.day = day;
            this.begin = begin;
            this.end = end;
            this.birthday = birthday;
        }

        /**
         * Check if the current time matches the time limit constraints.
         * Requires a Context to read birthday from SharedPreferences.
         */
        boolean isAvailableNow(Context context) {
            Calendar now = Calendar.getInstance();

            if (month >= 0 && (now.get(Calendar.MONTH) + 1) != month) return false;
            if (day >= 0 && now.get(Calendar.DAY_OF_MONTH) != day) return false;
            if (hour >= 0 && now.get(Calendar.HOUR_OF_DAY) != hour) return false;
            if (minute >= 0 && now.get(Calendar.MINUTE) != minute) return false;

            // begin/end: hour range
            if (begin >= 0 && end >= 0) {
                int currentHour = now.get(Calendar.HOUR_OF_DAY);
                if (currentHour < begin || currentHour >= end) return false;
            }

            // birthday: check if today matches stored birthday
            if (birthday) {
                SharedPreferences prefs = context.getSharedPreferences(
                        FloatingWindowService.PREF_NAME, Context.MODE_PRIVATE);
                int bMonth = prefs.getInt("birthday_month", -1);
                int bDay = prefs.getInt("birthday_day", -1);
                if (bMonth < 0 || bDay < 0) return false;
                return (now.get(Calendar.MONTH) + 1) == bMonth
                        && now.get(Calendar.DAY_OF_MONTH) == bDay;
            }
            return true;
        }
    }
}
