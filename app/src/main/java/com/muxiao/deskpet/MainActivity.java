package com.muxiao.deskpet;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.AlertDialog;
import android.app.DatePickerDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.util.Log;
import android.util.LruCache;
import android.view.Gravity;
import android.view.View;
import android.view.ViewOutlineProvider;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Space;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;

import org.json.JSONObject;

import java.io.File;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

import androidx.documentfile.provider.DocumentFile;

import androidx.appcompat.app.AppCompatDelegate;
import androidx.core.os.LocaleListCompat;

public class MainActivity extends AppCompatActivity {

    // Color palette (updated dynamically for dark mode)
    private static int C_PRIMARY = 0xFF4A4A6A;
    private static int C_BG = 0xFFF5F5F7;
    private static int C_CARD = 0xFFFFFFFF;
    private static int C_TEXT = 0xFF1A1A1A;
    private static int C_TEXT_SECONDARY = 0xFF888888;
    private static int C_BORDER = 0xFFE8E8E8;

    private static void applyDarkMode(boolean dark) {
        if (dark) {
            C_PRIMARY = 0xFF8B8BC0;
            C_BG = 0xFF121218;
            C_CARD = 0xFF1E1E28;
            C_TEXT = 0xFFE8E8EC;
            C_TEXT_SECONDARY = 0xFF9898A0;
            C_BORDER = 0xFF2C2C36;
        } else {
            C_PRIMARY = 0xFF4A4A6A;
            C_BG = 0xFFF5F5F7;
            C_CARD = 0xFFFFFFFF;
            C_TEXT = 0xFF1A1A1A;
            C_TEXT_SECONDARY = 0xFF888888;
            C_BORDER = 0xFFE8E8E8;
        }
    }

    private LinearLayout modelListLayout;
    private ProgressBar importProgressBar;
    private boolean isDarkMode;

    // Loading state tracking
    private final Set<String> loadingPaths = new HashSet<>();
    private boolean isImporting = false;
    private boolean isExporting = false;
    private final Set<String> deletingPaths = new HashSet<>();
    private Button btnStartPet;
    private Button btnImportFolder;
    private Button btnImportLpk;

    // Bitmap cache for model preview thumbnails (max 4MB)
    private final LruCache<String, Bitmap> thumbnailCache = new LruCache<>(4 * 1024 * 1024) {
        @Override
        protected int sizeOf(String key, Bitmap value) {
            return value.getByteCount();
        }
    };

    // Activity Result API launchers
    private final ActivityResultLauncher<Intent> overlayPermissionLauncher =
            registerForActivityResult(new ActivityResultContracts.StartActivityForResult(), result -> {
                if (Settings.canDrawOverlays(this)) {
                    startPet();
                } else {
                    Toast.makeText(this, R.string.toast_overlay_permission, Toast.LENGTH_SHORT).show();
                }
            });

    private final ActivityResultLauncher<Intent> pickModelLauncher =
            registerForActivityResult(new ActivityResultContracts.StartActivityForResult(), result -> {
                if (result.getResultCode() == RESULT_OK && result.getData() != null) {
                    Uri uri = result.getData().getData();
                    if (uri != null) {
                        showNameInputDialog(
                                getString(R.string.dialog_import_model),
                                getString(R.string.dialog_name_hint),
                                null,
                                name -> doImportModel(uri, name));
                    }
                }
            });

    private final ActivityResultLauncher<Intent> pickLpkLauncher =
            registerForActivityResult(new ActivityResultContracts.StartActivityForResult(), result -> {
                if (result.getResultCode() == RESULT_OK && result.getData() != null) {
                    Uri uri = result.getData().getData();
                    if (uri != null) {
                        String defaultName = guessNameFromUri(uri);
                        showNameInputDialog(
                                getString(R.string.dialog_import_lpk),
                                getString(R.string.dialog_name_hint),
                                defaultName,
                                name -> doImportLpk(uri, name));
                    }
                }
            });

    private final ActivityResultLauncher<String> notificationPermLauncher =
            registerForActivityResult(new ActivityResultContracts.RequestPermission(), granted -> startPet());

    // Export model state
    private File exportSourceDir;
    private String exportSourceName;

    private final ActivityResultLauncher<Intent> pickExportDirLauncher =
            registerForActivityResult(new ActivityResultContracts.StartActivityForResult(), result -> {
                if (result.getResultCode() == RESULT_OK && result.getData() != null) {
                    Uri treeUri = result.getData().getData();
                    if (treeUri != null && exportSourceDir != null) {
                        doExportModel(treeUri);
                    }
                }
            });

    private final FloatingWindowService.LoadingStateListener loadingStateListener =
            new FloatingWindowService.LoadingStateListener() {
                @Override
                public void onLoadingStateChanged(boolean isLoading, String modelPath) {
                    runOnUiThread(() -> {
                        if (isLoading && modelPath != null) {
                            loadingPaths.add(modelPath);
                        } else if (modelPath != null) {
                            loadingPaths.remove(modelPath);
                        }
                        updateButtonStates();
                        refreshModelList();
                    });
                }

                @Override
                public void onServiceStopped() {
                    runOnUiThread(() -> updateButtonStates());
                }
            };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        SharedPreferences prefs0 = getSharedPreferences(FloatingWindowService.PREF_NAME, MODE_PRIVATE);
        isDarkMode = prefs0.getBoolean("dark_mode", false);
        applyDarkMode(isDarkMode);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(C_BG);

        root.addView(createTopBar());

        ScrollView scrollView = new ScrollView(this);
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(14), dp(14), dp(14), dp(20));

        // Control section
        btnStartPet = createPrimaryButton(getString(R.string.btn_start_pet), v -> startPet());
        content.addView(createSectionCard(getString(R.string.section_control), new View[]{
                btnStartPet,
                spacer(dp(8)),
                createSecondaryButton(getString(R.string.btn_stop_pet), v -> {
                    stopService(new Intent(this, FloatingWindowService.class));
                    loadingPaths.clear();
                })
        }));

        content.addView(spacer(dp(12)));

        // Import section
        btnImportFolder = createAccentButton(getString(R.string.btn_import_folder), v -> importModel());
        btnImportLpk = createAccentButton(getString(R.string.btn_import_lpk), v -> importLpk());
        LinearLayout importRow = new LinearLayout(this);
        importRow.setOrientation(LinearLayout.HORIZONTAL);
        importRow.addView(btnImportFolder, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        Space hSpace = new Space(this);
        hSpace.setLayoutParams(new LinearLayout.LayoutParams(dp(8), LinearLayout.LayoutParams.WRAP_CONTENT));
        importRow.addView(hSpace);
        importRow.addView(btnImportLpk, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        content.addView(createSectionCard(getString(R.string.section_import), new View[]{importRow}));

        content.addView(spacer(dp(12)));

        SharedPreferences prefs = getSharedPreferences(FloatingWindowService.PREF_NAME, MODE_PRIVATE);

        // Settings section (size + toggles)
        float savedScale = prefs.getFloat("scale", 1.0f);

        // Use holder arrays to avoid forward-reference errors in lambdas
        final View[] longPressHolder = new View[1];
        final View[] dragHolder = new View[1];

        View longPressToggle = createToggleRow(getString(R.string.menu_long_press_menu),
                prefs.getBoolean("long_press_menu_enable", true),
                checked -> {
                    boolean dragOn = prefs.getBoolean("drag_enable", true);
                    if (!checked && !dragOn) {
                        showBothDisabledWarning(prefs, "long_press_menu_enable", false,
                                (androidx.appcompat.widget.SwitchCompat) ((LinearLayout) longPressHolder[0]).getChildAt(1));
                    } else {
                        prefs.edit().putBoolean("long_press_menu_enable", checked).commit();
                    }
                });
        longPressHolder[0] = longPressToggle;
        View dragToggle = createToggleRow(getString(R.string.menu_drag_enabled),
                prefs.getBoolean("drag_enable", true),
                checked -> {
                    boolean menuOn = prefs.getBoolean("long_press_menu_enable", true);
                    if (!checked && !menuOn) {
                        showBothDisabledWarning(prefs, "drag_enable", false,
                                (androidx.appcompat.widget.SwitchCompat) ((LinearLayout) dragHolder[0]).getChildAt(1));
                    } else {
                        prefs.edit().putBoolean("drag_enable", checked).commit();
                    }
                });
        dragHolder[0] = dragToggle;

        content.addView(createSectionCard(getString(R.string.menu_settings), new View[]{
                createSeekBarSection(savedScale),
                spacer(dp(6)),
                longPressToggle,
                spacer(dp(6)),
                dragToggle
        }));

        content.addView(spacer(dp(12)));

        // Model list section
        LinearLayout modelSection = createSectionCard(getString(R.string.section_models), null);
        modelListLayout = new LinearLayout(this);
        modelListLayout.setOrientation(LinearLayout.VERTICAL);
        modelSection.addView(modelListLayout);
        content.addView(modelSection);

        content.addView(spacer(dp(12)));

        // Birthday section
        {
            LinearLayout birthdayContainer = new LinearLayout(this);
            birthdayContainer.setOrientation(LinearLayout.VERTICAL);
            birthdayContainer.addView(createBirthdaySection(prefs));
            TextView birthdayNote = new TextView(this);
            birthdayNote.setText(R.string.birthday_note);
            birthdayNote.setTextSize(11);
            birthdayNote.setTextColor(C_TEXT_SECONDARY);
            birthdayNote.setPadding(0, dp(6), 0, 0);
            birthdayContainer.addView(birthdayNote);
            content.addView(createSectionCard(getString(R.string.section_birthday), new View[]{
                    birthdayContainer
            }));
        }

        scrollView.addView(content);
        root.addView(scrollView, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1));

        // Import progress bar (hidden by default)
        importProgressBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        importProgressBar.setIndeterminate(true);
        importProgressBar.setVisibility(View.GONE);
        LinearLayout.LayoutParams progressLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        root.addView(importProgressBar, progressLp);

        setContentView(root);
        setupStatusBar();
        refreshModelList();
        handleIncomingIntent(getIntent());

        FloatingWindowService.setLoadingStateListener(loadingStateListener);
    }

    @Override
    protected void onResume() {
        super.onResume();
        updateButtonStates();
    }

    @Override
    protected void onDestroy() {
        FloatingWindowService.setLoadingStateListener(null);
        super.onDestroy();
    }

    @SuppressWarnings("deprecation")
    private void setupStatusBar() {
        SharedPreferences sp = getSharedPreferences(FloatingWindowService.PREF_NAME, MODE_PRIVATE);
        boolean dark = sp.getBoolean("dark_mode", false);
        getWindow().setStatusBarColor(C_CARD);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            android.view.WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                if (dark) {
                    controller.setSystemBarsAppearance(0,
                            android.view.WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS);
                } else {
                    controller.setSystemBarsAppearance(
                            android.view.WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS,
                            android.view.WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS);
                }
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(dark ? 0
                    : View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR);
        }
    }

    // --- UI Builder Methods ---

    private View createTopBar() {
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.VERTICAL);
        bar.setBackgroundColor(C_CARD);
        bar.setPadding(dp(16), dp(32), dp(16), dp(14));
        bar.setElevation(dp(3));

        // Title row with language selector
        LinearLayout titleRow = new LinearLayout(this);
        titleRow.setOrientation(LinearLayout.HORIZONTAL);
        titleRow.setGravity(Gravity.CENTER_VERTICAL);

        TextView title = new TextView(this);
        title.setText(R.string.title_live2d_pet);
        title.setTextSize(18);
        title.setTextColor(C_TEXT);
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        title.setLayoutParams(new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        titleRow.addView(title);

        // Language buttons
        String currentLang = Locale.getDefault().getLanguage();
        String currentCountry = Locale.getDefault().getCountry();
        boolean isZhCN = "zh".equals(currentLang) && "CN".equals(currentCountry);
        boolean isZhTW = "zh".equals(currentLang) && "TW".equals(currentCountry);

        titleRow.addView(createLangButton("EN", !isZhCN && !isZhTW, () -> switchLocale("en")));
        titleRow.addView(createLangButton("中文", isZhCN, () -> switchLocale("zh-CN")));
        titleRow.addView(createLangButton("繁體", isZhTW, () -> switchLocale("zh-TW")));

        // Dark mode toggle
        View spacer = new View(this);
        spacer.setLayoutParams(new LinearLayout.LayoutParams(dp(4), 1));
        titleRow.addView(spacer);

        androidx.appcompat.widget.SwitchCompat darkSwitch = new androidx.appcompat.widget.SwitchCompat(this);
        darkSwitch.setChecked(isDarkMode);
        darkSwitch.setThumbTintList(android.content.res.ColorStateList.valueOf(C_PRIMARY));
        darkSwitch.setTrackTintList(android.content.res.ColorStateList.valueOf(C_BORDER));
        darkSwitch.setSwitchPadding(dp(4));
        darkSwitch.setOnCheckedChangeListener((btn, checked) -> {
            getSharedPreferences(FloatingWindowService.PREF_NAME, MODE_PRIVATE)
                    .edit().putBoolean("dark_mode", checked).apply();
            AppCompatDelegate.setDefaultNightMode(checked
                    ? AppCompatDelegate.MODE_NIGHT_YES
                    : AppCompatDelegate.MODE_NIGHT_NO);
        });
        titleRow.addView(darkSwitch);

        bar.addView(titleRow);

        return bar;
    }

    private View createLangButton(String text, boolean selected, Runnable onClick) {
        TextView btn = new TextView(this);
        btn.setText(text);
        btn.setTextSize(12);
        btn.setPadding(dp(8), dp(4), dp(8), dp(4));
        if (selected) {
            btn.setTextColor(C_PRIMARY);
            btn.setTypeface(null, android.graphics.Typeface.BOLD);
            GradientDrawable bg = new GradientDrawable();
            bg.setColor(0x15000000);
            bg.setCornerRadius(dp(4));
            btn.setBackground(bg);
        } else {
            btn.setTextColor(C_TEXT_SECONDARY);
            btn.setBackground(textBtnBg());
        }
        btn.setOnClickListener(v -> onClick.run());
        return btn;
    }

    private void switchLocale(String localeTag) {
        LocaleListCompat locales = LocaleListCompat.forLanguageTags(localeTag);
        AppCompatDelegate.setApplicationLocales(locales);
    }

    private LinearLayout createSectionCard(String title, View[] children) {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        GradientDrawable cardBg = new GradientDrawable();
        cardBg.setColor(C_CARD);
        cardBg.setCornerRadius(dp(10));
        card.setBackground(cardBg);
        card.setElevation(dp(2));
        card.setOutlineProvider(new ViewOutlineProvider() {
            @Override
            public void getOutline(View view, android.graphics.Outline outline) {
                outline.setRoundRect(0, 0, view.getWidth(), view.getHeight(), dp(10));
            }
        });
        card.setClipToOutline(true);
        card.setPadding(dp(16), dp(14), dp(16), dp(14));

        if (title != null) {
            TextView tv = new TextView(this);
            tv.setText(title);
            tv.setTextSize(12);
            tv.setTextColor(C_TEXT_SECONDARY);
            tv.setTypeface(null, android.graphics.Typeface.BOLD);
            tv.setLetterSpacing(0.03f);
            card.addView(tv);

            if (children != null && children.length > 0) {
                card.addView(spacer(dp(10)));
            }
        }

        if (children != null) {
            for (View child : children) {
                card.addView(child);
            }
        }
        return card;
    }

    private Button createPrimaryButton(String text, View.OnClickListener listener) {
        Button btn = new Button(this);
        btn.setText(text);
        btn.setTextColor(Color.WHITE);
        btn.setTextSize(14);
        btn.setTypeface(null, android.graphics.Typeface.BOLD);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(C_PRIMARY);
        bg.setCornerRadius(dp(8));
        btn.setBackground(bg);
        btn.setAllCaps(false);
        btn.setGravity(Gravity.CENTER);
        btn.setPadding(dp(16), dp(12), dp(16), dp(12));
        btn.setStateListAnimator(null);
        btn.setElevation(dp(1));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        btn.setLayoutParams(lp);
        btn.setOnClickListener(listener);
        return btn;
    }

    private Button createSecondaryButton(String text, View.OnClickListener listener) {
        Button btn = new Button(this);
        btn.setText(text);
        btn.setTextColor(C_TEXT_SECONDARY);
        btn.setTextSize(13);
        btn.setBackground(textBtnBg());
        btn.setAllCaps(false);
        btn.setGravity(Gravity.CENTER);
        btn.setPadding(dp(14), dp(8), dp(14), dp(8));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        btn.setLayoutParams(lp);
        btn.setOnClickListener(listener);
        return btn;
    }

    private Button createAccentButton(String text, View.OnClickListener listener) {
        Button btn = new Button(this);
        btn.setText(text);
        btn.setTextColor(C_PRIMARY);
        btn.setTextSize(13);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0x0D4A4A6A);
        bg.setCornerRadius(dp(8));
        bg.setStroke(dp(1), 0x1A4A4A6A);
        btn.setBackground(bg);
        btn.setAllCaps(false);
        btn.setGravity(Gravity.CENTER);
        btn.setPadding(dp(14), dp(10), dp(14), dp(10));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        btn.setLayoutParams(lp);
        btn.setOnClickListener(listener);
        return btn;
    }

    private interface ToggleCallback {
        void onToggle(boolean checked);
    }

    private View createToggleRow(String label, boolean initialState,
                                 ToggleCallback onToggle) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);

        TextView tv = new TextView(this);
        tv.setText(label);
        tv.setTextSize(14);
        tv.setTextColor(C_TEXT);
        tv.setLayoutParams(new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        row.addView(tv);

        androidx.appcompat.widget.SwitchCompat toggle = new androidx.appcompat.widget.SwitchCompat(this);
        toggle.setChecked(initialState);
        toggle.setThumbTintList(android.content.res.ColorStateList.valueOf(C_PRIMARY));
        toggle.setTrackTintList(android.content.res.ColorStateList.valueOf(C_BORDER));
        toggle.setSwitchPadding(dp(4));
        toggle.setOnCheckedChangeListener((btn, checked) -> onToggle.onToggle(checked));
        row.addView(toggle);

        return row;
    }

    private void showBothDisabledWarning(SharedPreferences prefs, String key,
                                         boolean value, androidx.appcompat.widget.SwitchCompat toggle) {
        FileUtils.showBothDisabledWarning(this, () -> {
            toggle.setChecked(!value);
            prefs.edit().putBoolean(key, !value).commit();
        });
    }

    @SuppressLint("SetTextI18n")
    private View createSeekBarSection(float savedScale) {
        LinearLayout section = new LinearLayout(this);
        section.setOrientation(LinearLayout.HORIZONTAL);
        section.setGravity(Gravity.CENTER_VERTICAL);

        TextView sizeLabel = new TextView(this);
        sizeLabel.setText(Math.round(savedScale * 100) + "%");
        sizeLabel.setTextSize(13);
        sizeLabel.setTextColor(C_TEXT);
        sizeLabel.setPadding(0, 0, dp(10), 0);
        section.addView(sizeLabel);

        SeekBar seekBar = new SeekBar(this);
        seekBar.setMax(470);
        seekBar.setProgress(Math.round(savedScale * 100) - 30);
        seekBar.setPadding(dp(8), 0, dp(8), 0);
        LinearLayout.LayoutParams seekLp = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        seekBar.setLayoutParams(seekLp);
        seekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
                float scale = (progress + 30) / 100f;
                sizeLabel.setText(Math.round(scale * 100) + "%");
                getSharedPreferences(FloatingWindowService.PREF_NAME, MODE_PRIVATE)
                        .edit().putFloat("scale", scale).apply();
            }
            @Override public void onStartTrackingTouch(SeekBar sb) {}
            @Override public void onStopTrackingTouch(SeekBar sb) {}
        });
        section.addView(seekBar);

        return section;
    }

    private View createBirthdaySection(SharedPreferences prefs) {
        LinearLayout section = new LinearLayout(this);
        section.setOrientation(LinearLayout.HORIZONTAL);
        section.setGravity(Gravity.CENTER_VERTICAL);

        int savedMonth = prefs.getInt("birthday_month", -1);
        int savedDay = prefs.getInt("birthday_day", -1);
        boolean hasSaved = savedMonth > 0;

        TextView label = new TextView(this);
        label.setText(hasSaved
                ? getString(R.string.birthday_set, savedMonth, savedDay)
                : getString(R.string.birthday_not_set));
        label.setTextSize(13);
        label.setTextColor(C_TEXT);
        section.addView(label, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        // Use holder arrays to resolve circular lambda references
        final Button[] btnHolder = new Button[1];
        final Button[] clearHolder = new Button[1];

        Button clearBtn = new Button(this);
        clearBtn.setText(R.string.birthday_clear);
        clearBtn.setTextSize(12);
        clearBtn.setTextColor(C_TEXT_SECONDARY);
        clearBtn.setBackground(textBtnBg());
        clearBtn.setAllCaps(false);
        clearBtn.setPadding(dp(8), dp(4), dp(8), dp(4));
        clearBtn.setVisibility(hasSaved ? View.VISIBLE : View.GONE);
        clearBtn.setOnClickListener(v -> {
            getSharedPreferences(FloatingWindowService.PREF_NAME, MODE_PRIVATE)
                    .edit()
                    .remove("birthday_month")
                    .remove("birthday_day")
                    .apply();
            label.setText(getString(R.string.birthday_not_set));
            btnHolder[0].setText(R.string.birthday_set_btn);
            clearHolder[0].setVisibility(View.GONE);
        });
        clearHolder[0] = clearBtn;

        Button btn = new Button(this);
        btn.setText(hasSaved ? R.string.birthday_change : R.string.birthday_set_btn);
        btn.setTextSize(12);
        btn.setTextColor(C_PRIMARY);
        btn.setBackground(outlinedBtnBg());
        btn.setAllCaps(false);
        btn.setPadding(dp(12), dp(4), dp(12), dp(4));
        btn.setOnClickListener(v -> {
            SharedPreferences sp = getSharedPreferences(FloatingWindowService.PREF_NAME, MODE_PRIVATE);
            int curMonth = sp.getInt("birthday_month", -1);
            int curDay = sp.getInt("birthday_day", -1);
            new DatePickerDialog(this, (view, year, month, day) -> {
                getSharedPreferences(FloatingWindowService.PREF_NAME, MODE_PRIVATE)
                        .edit()
                        .putInt("birthday_month", month + 1)
                        .putInt("birthday_day", day)
                        .apply();
                label.setText(getString(R.string.birthday_set, month + 1, day));
                btnHolder[0].setText(R.string.birthday_change);
                clearHolder[0].setVisibility(View.VISIBLE);
            }, 2000, curMonth > 0 ? curMonth - 1 : 0, curDay > 0 ? curDay : 1).show();
        });
        btnHolder[0] = btn;

        section.addView(btn);
        section.addView(clearBtn);

        return section;
    }

    private View spacer(int height) {
        Space s = new Space(this);
        s.setLayoutParams(new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, height));
        return s;
    }

    // --- Model List ---

    private void refreshModelList() {
        modelListLayout.removeAllViews();
        File modelsDir = new File(getFilesDir(), "models");
        if (!modelsDir.exists()) {
            showEmptyHint();
            return;
        }

        File[] modelDirs = modelsDir.listFiles(File::isDirectory);
        if (modelDirs == null || modelDirs.length == 0) {
            showEmptyHint();
            return;
        }

        for (File dir : modelDirs) {
            File[] cubism3Files = dir.listFiles((d, name) ->
                    name.endsWith(".model3.json") && new File(d, name).isFile());
            File[] cubism2Files = dir.listFiles((d, name) ->
                    name.endsWith(".model.json") && !name.endsWith(".model3.json")
                            && new File(d, name).isFile());

            if (cubism3Files != null && cubism3Files.length > 0) {
                addModelCard(cubism3Files[0], dir);
            } else if (cubism2Files != null && cubism2Files.length > 0) {
                addModelCard(cubism2Files[0], dir);
            }
        }
    }

    private void updateButtonStates() {
        boolean anyLoading = !loadingPaths.isEmpty();
        boolean running = FloatingWindowService.isRunning();

        if (btnStartPet != null) {
            btnStartPet.setEnabled(!anyLoading && !isImporting);
            if (anyLoading) {
                btnStartPet.setText(getString(R.string.btn_loading));
                btnStartPet.setTextColor(C_TEXT_SECONDARY);
            } else if (running) {
                btnStartPet.setText(getString(R.string.btn_pet_running));
                btnStartPet.setTextColor(C_TEXT_SECONDARY);
            } else {
                btnStartPet.setText(getString(R.string.btn_start_pet));
                btnStartPet.setTextColor(Color.WHITE);
            }
        }
        if (btnImportFolder != null) {
            btnImportFolder.setEnabled(!isImporting && !isExporting);
            btnImportFolder.setText(isImporting ? getString(R.string.btn_importing) : getString(R.string.btn_import_folder));
            btnImportFolder.setTextColor(isImporting ? C_TEXT_SECONDARY : C_PRIMARY);
        }
        if (btnImportLpk != null) {
            btnImportLpk.setEnabled(!isImporting && !isExporting);
            btnImportLpk.setText(isImporting ? getString(R.string.btn_importing) : getString(R.string.btn_import_lpk));
            btnImportLpk.setTextColor(isImporting ? C_TEXT_SECONDARY : C_PRIMARY);
        }
    }

    private void showEmptyHint() {
        TextView hint = new TextView(this);
        hint.setText(R.string.hint_empty_models);
        hint.setTextSize(13);
        hint.setTextColor(C_TEXT_SECONDARY);
        hint.setGravity(Gravity.CENTER);
        hint.setPadding(dp(16), dp(20), dp(16), dp(20));
        modelListLayout.addView(hint);
    }

    private void addModelCard(File jsonFile, File dir) {
        String fileName = jsonFile.getName();
        String versionTag = fileName.endsWith(".model3.json") ? "v3+" : "v2";

        String parsedName = parseModelDisplayName(jsonFile);
        String modelName = (parsedName != null) ? parsedName
                : fileName.replace(".model3.json", "").replace(".model.json", "");

        final String modelPath = jsonFile.getAbsolutePath();
        final String dirPath = dir.getAbsolutePath();
        final boolean isLoading = loadingPaths.contains(modelPath);
        final boolean isDeleting = deletingPaths.contains(dirPath);

        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackground(roundedBorderBg(dp(10)));
        card.setElevation(dp(1));
        card.setOutlineProvider(new ViewOutlineProvider() {
            @Override
            public void getOutline(View view, android.graphics.Outline outline) {
                outline.setRoundRect(0, 0, view.getWidth(), view.getHeight(), dp(10));
            }
        });
        card.setClipToOutline(true);
        card.setPadding(dp(12), dp(10), dp(12), dp(10));
        LinearLayout.LayoutParams cardLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        cardLp.setMargins(0, dp(6), 0, 0);
        card.setLayoutParams(cardLp);

        // Row 1: thumbnail + info + load button
        LinearLayout row1 = new LinearLayout(this);
        row1.setOrientation(LinearLayout.HORIZONTAL);
        row1.setGravity(Gravity.CENTER_VERTICAL);
        row1.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        // Preview thumbnail with caching and contentDescription
        File previewFile = FileUtils.findPreviewImage(dir);
        if (previewFile.exists()) {
            try {
                String cacheKey = previewFile.getAbsolutePath();
                Bitmap bmp = thumbnailCache.get(cacheKey);
                if (bmp == null || bmp.isRecycled()) {
                    BitmapFactory.Options opts = new BitmapFactory.Options();
                    opts.inSampleSize = calculateInSampleSize(previewFile, dp(48), dp(48));
                    bmp = BitmapFactory.decodeFile(previewFile.getAbsolutePath(), opts);
                    if (bmp != null) {
                        thumbnailCache.put(cacheKey, bmp);
                    }
                }
                if (bmp != null && !bmp.isRecycled()) {
                    ImageView thumb = new ImageView(this);
                    thumb.setImageBitmap(bmp);
                    thumb.setScaleType(ImageView.ScaleType.CENTER_CROP);
                    thumb.setContentDescription(getString(R.string.cd_model_preview));
                    int size = dp(40);
                    LinearLayout.LayoutParams thumbLp = new LinearLayout.LayoutParams(size, size);
                    thumbLp.setMargins(0, 0, dp(10), 0);
                    thumb.setLayoutParams(thumbLp);
                    thumb.setClipToOutline(true);
                    thumb.setOutlineProvider(new ViewOutlineProvider() {
                        @Override
                        public void getOutline(View view, android.graphics.Outline outline) {
                            outline.setRoundRect(0, 0, view.getWidth(), view.getHeight(), dp(6));
                        }
                    });
                    row1.addView(thumb);
                }
            } catch (Exception e) {
                Log.w("MainActivity", "Failed to load preview: " + previewFile, e);
            }
        }

        // Model info
        LinearLayout info = new LinearLayout(this);
        info.setOrientation(LinearLayout.VERTICAL);
        info.setLayoutParams(new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        LinearLayout nameRow = new LinearLayout(this);
        nameRow.setOrientation(LinearLayout.HORIZONTAL);
        nameRow.setGravity(Gravity.CENTER_VERTICAL);

        TextView nameTv = new TextView(this);
        nameTv.setText(dir.getName());
        nameTv.setTextSize(13);
        nameTv.setTextColor(C_TEXT);
        nameTv.setTypeface(null, android.graphics.Typeface.BOLD);
        nameTv.setMaxLines(1);
        nameRow.addView(nameTv, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));

        TextView tag = new TextView(this);
        tag.setText(versionTag);
        tag.setTextSize(9);
        tag.setTextColor(C_TEXT_SECONDARY);
        tag.setPadding(dp(4), 0, 0, 0);
        nameRow.addView(tag);
        info.addView(nameRow);

        TextView statusTv = new TextView(this);
        if (isLoading) {
            statusTv.setText(R.string.btn_loading);
            statusTv.setTextColor(C_TEXT_SECONDARY);
        } else if (isDeleting) {
            statusTv.setText(R.string.status_deleting);
            statusTv.setTextColor(C_TEXT_SECONDARY);
        } else {
            statusTv.setText(modelName);
            statusTv.setTextColor(C_TEXT_SECONDARY);
        }
        statusTv.setTextSize(11);
        statusTv.setPadding(0, dp(2), 0, 0);
        statusTv.setMaxLines(1);
        info.addView(statusTv);

        row1.addView(info);

        // Load button
        Button btnLoad = new Button(this);
        if (isLoading) {
            btnLoad.setText(R.string.btn_loading);
            btnLoad.setTextColor(C_TEXT_SECONDARY);
            btnLoad.setBackground(textBtnBg());
            btnLoad.setEnabled(false);
        } else {
            btnLoad.setText(R.string.btn_load);
            btnLoad.setTextColor(Color.WHITE);
            GradientDrawable loadBg = new GradientDrawable();
            loadBg.setColor(C_PRIMARY);
            loadBg.setCornerRadius(dp(8));
            btnLoad.setBackground(loadBg);
            btnLoad.setEnabled(!isDeleting);
        }
        btnLoad.setTextSize(12);
        btnLoad.setAllCaps(false);
        btnLoad.setStateListAnimator(null);
        btnLoad.setMinWidth(0);
        btnLoad.setMinimumWidth(0);
        btnLoad.setMinimumHeight(0);
        btnLoad.setPadding(dp(10), dp(6), dp(10), dp(6));
        LinearLayout.LayoutParams loadLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        loadLp.setMargins(dp(8), 0, 0, 0);
        btnLoad.setLayoutParams(loadLp);
        btnLoad.setOnClickListener(v -> {
            if (!Settings.canDrawOverlays(this)) {
                Toast.makeText(this, R.string.toast_overlay_permission, Toast.LENGTH_SHORT).show();
                return;
            }
            loadingPaths.add(modelPath);
            updateButtonStates();
            refreshModelList();
            LpkUnpacker.UnpackResult persisted = LpkUnpacker.loadPersistedConfigs(modelPath);
            FloatingWindowService.start(this, modelPath,
                    persisted.hitAreaConfig, persisted.lookAtConfig, persisted.modelConfig);
        });
        row1.addView(btnLoad);
        card.addView(row1);

        // Row 2: action buttons (export, rename, delete)
        LinearLayout row2 = new LinearLayout(this);
        row2.setOrientation(LinearLayout.HORIZONTAL);
        row2.setGravity(Gravity.END);
        row2.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        row2.setPadding(0, dp(2), 0, 0);

        // Export button
        Button btnExport = new Button(this);
        btnExport.setText(R.string.btn_export);
        btnExport.setContentDescription(getString(R.string.cd_export_model));
        btnExport.setTextColor(C_TEXT_SECONDARY);
        btnExport.setTextSize(11);
        btnExport.setBackground(textBtnBg());
        btnExport.setEnabled(!isLoading && !isDeleting);
        btnExport.setMinWidth(0);
        btnExport.setMinimumWidth(0);
        btnExport.setMinimumHeight(0);
        btnExport.setPadding(dp(8), dp(4), dp(8), dp(4));
        btnExport.setOnClickListener(v -> {
            exportSourceDir = dir;
            exportSourceName = dir.getName();
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            pickExportDirLauncher.launch(intent);
        });
        row2.addView(btnExport);

        // Rename button with contentDescription
        Button btnRename = new Button(this);
        btnRename.setText(R.string.btn_rename);
        btnRename.setContentDescription(getString(R.string.cd_rename_model));
        btnRename.setTextColor(C_TEXT_SECONDARY);
        btnRename.setTextSize(11);
        btnRename.setBackground(textBtnBg());
        btnRename.setEnabled(!isLoading && !isDeleting);
        btnRename.setMinWidth(0);
        btnRename.setMinimumWidth(0);
        btnRename.setMinimumHeight(0);
        btnRename.setPadding(dp(8), dp(4), dp(8), dp(4));
        btnRename.setOnClickListener(v -> {
            EditText input = new EditText(this);
            input.setText(dir.getName());
            input.selectAll();
            input.setSingleLine(true);
            input.setPadding(dp(16), dp(12), dp(16), dp(4));

            new AlertDialog.Builder(this)
                    .setTitle(R.string.dialog_rename_title)
                    .setView(input)
                    .setPositiveButton(R.string.dialog_confirm, (dialog, which) -> {
                        String newName = input.getText().toString().trim();
                        if (newName.isEmpty() || newName.equals(dir.getName())) return;
                        File newDir = new File(dir.getParentFile(), newName);
                        if (newDir.exists()) {
                            Toast.makeText(this, R.string.toast_name_exists, Toast.LENGTH_SHORT).show();
                            return;
                        }
                        if (dir.renameTo(newDir)) {
                            SharedPreferences sp = getSharedPreferences(FloatingWindowService.PREF_NAME, MODE_PRIVATE);
                            String lastPath = sp.getString("last_model_path", null);
                            if (lastPath != null && lastPath.startsWith(dirPath)) {
                                String updated = lastPath.replace(dirPath, newDir.getAbsolutePath());
                                sp.edit().putString("last_model_path", updated).apply();
                            }
                            Toast.makeText(this, getString(R.string.toast_renamed, newName), Toast.LENGTH_SHORT).show();
                            refreshModelList();
                        } else {
                            Toast.makeText(this, R.string.toast_rename_failed, Toast.LENGTH_SHORT).show();
                        }
                    })
                    .setNegativeButton(R.string.dialog_cancel, null)
                    .show();
        });
        row2.addView(btnRename);

        // Delete button with contentDescription
        Button btnDel = new Button(this);
        btnDel.setText(isDeleting ? getString(R.string.status_deleting) : getString(R.string.btn_delete));
        btnDel.setContentDescription(getString(R.string.cd_delete_model));
        btnDel.setTextColor(C_TEXT_SECONDARY);
        btnDel.setTextSize(11);
        btnDel.setBackground(textBtnBg());
        btnDel.setEnabled(!isLoading && !isDeleting);
        btnDel.setMinWidth(0);
        btnDel.setMinimumWidth(0);
        btnDel.setMinimumHeight(0);
        btnDel.setPadding(dp(8), dp(4), dp(8), dp(4));
        btnDel.setOnClickListener(v -> new AlertDialog.Builder(this)
                .setTitle(R.string.dialog_delete_title)
                .setMessage(getString(R.string.dialog_delete_message, dir.getName()))
                .setPositiveButton(R.string.dialog_delete_confirm, (dialog, which) -> {
                    stopService(new Intent(this, FloatingWindowService.class));
                    deletingPaths.add(dirPath);
                    updateButtonStates();
                    new Thread(() -> {
                        boolean success = FileUtils.deleteRecursive(dir);
                        runOnUiThread(() -> {
                            deletingPaths.remove(dirPath);
                            if (success) {
                                Toast.makeText(this, getString(R.string.toast_deleted, dir.getName()), Toast.LENGTH_SHORT).show();
                            } else {
                                Toast.makeText(this, getString(R.string.toast_delete_failed, dir.getName()), Toast.LENGTH_SHORT).show();
                            }
                            refreshModelList();
                        });
                    }).start();
                })
                .setNegativeButton(R.string.dialog_cancel, null)
                .show());
        row2.addView(btnDel);

        card.addView(row2);

        modelListLayout.addView(card);
    }

    // --- Drawable Helpers ---

    private GradientDrawable roundedBorderBg(int radius) {
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(C_CARD);
        bg.setCornerRadius(radius);
        return bg;
    }

    private StateListDrawable outlinedBtnBg() {
        StateListDrawable sd = new StateListDrawable();
        GradientDrawable pressed = new GradientDrawable();
        pressed.setColor(0x0D4A4A6A);
        pressed.setStroke(dp(1), C_PRIMARY);
        pressed.setCornerRadius(dp(8));
        sd.addState(new int[]{android.R.attr.state_pressed}, pressed);
        GradientDrawable normal = new GradientDrawable();
        normal.setColor(Color.TRANSPARENT);
        normal.setStroke(dp(1), 0x404A4A6A);
        normal.setCornerRadius(dp(8));
        sd.addState(new int[]{}, normal);
        return sd;
    }

    private StateListDrawable textBtnBg() {
        StateListDrawable sd = new StateListDrawable();
        GradientDrawable pressed = new GradientDrawable();
        pressed.setColor(0x0A000000);
        pressed.setCornerRadius(dp(8));
        sd.addState(new int[]{android.R.attr.state_pressed}, pressed);
        GradientDrawable normal = new GradientDrawable();
        normal.setColor(Color.TRANSPARENT);
        normal.setCornerRadius(dp(8));
        sd.addState(new int[]{}, normal);
        return sd;
    }

    private int dp(int value) {
        return FileUtils.dp(this, value);
    }

    // --- Intent Handling ---

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        handleIncomingIntent(intent);
    }

    private void handleIncomingIntent(Intent intent) {
        if (intent == null) return;
        String action = intent.getAction();
        Uri uri = intent.getData();
        if (uri == null) return;

        String uriStr = uri.toString().toLowerCase(Locale.ROOT);
        if (Intent.ACTION_VIEW.equals(action) && (uriStr.endsWith(".lpk") || uriStr.endsWith(".wpk"))) {
            String defaultName = guessNameFromUri(uri);
            showNameInputDialog(
                    getString(R.string.dialog_import_lpk),
                    getString(R.string.dialog_name_hint),
                    defaultName,
                    name -> doImportLpk(uri, name));
        }
    }

    private void startPet() {
        if (!Settings.canDrawOverlays(this)) {
            Intent intent = new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                    Uri.parse("package:" + getPackageName()));
            overlayPermissionLauncher.launch(intent);
            return;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU
                && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            notificationPermLauncher.launch(Manifest.permission.POST_NOTIFICATIONS);
            return;
        }

        SharedPreferences prefs = getSharedPreferences(FloatingWindowService.PREF_NAME, MODE_PRIVATE);
        String lastModelPath = prefs.getString("last_model_path", null);
        if (lastModelPath != null && new File(lastModelPath).exists()) {
            loadingPaths.add(lastModelPath);
            updateButtonStates();
            refreshModelList();
            LpkUnpacker.UnpackResult persisted = LpkUnpacker.loadPersistedConfigs(lastModelPath);
            FloatingWindowService.start(this, lastModelPath,
                    persisted.hitAreaConfig, persisted.lookAtConfig, persisted.modelConfig);
            Toast.makeText(this, R.string.toast_pet_started_loading, Toast.LENGTH_SHORT).show();
        } else {
            FloatingWindowService.start(this, null);
            Toast.makeText(this, R.string.toast_pet_started, Toast.LENGTH_SHORT).show();
            btnStartPet.setText(getString(R.string.btn_pet_running));
            btnStartPet.setTextColor(C_TEXT_SECONDARY);
            btnStartPet.setEnabled(false);
        }
    }

    private void importModel() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        pickModelLauncher.launch(intent);
    }

    private void importLpk() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        pickLpkLauncher.launch(intent);
    }

    /**
     * Import a model from a SAF directory URI with error dialog and retry.
     */
    private void doImportModel(Uri uri, String customName) {
        isImporting = true;
        updateButtonStates();
        importProgressBar.setVisibility(View.VISIBLE);
        Toast.makeText(this, R.string.toast_importing, Toast.LENGTH_SHORT).show();

        new Thread(() -> {
            String modelPath = null;
            try {
                ModelImporter importer = new ModelImporter(this);
                modelPath = importer.importModel(uri, customName);
            } catch (Exception e) {
                Log.e("MainActivity", "Import failed", e);
            }
            final String mp = modelPath;
            runOnUiThread(() -> {
                isImporting = false;
                importProgressBar.setVisibility(View.GONE);
                if (mp != null) {
                    Toast.makeText(this, R.string.toast_import_success, Toast.LENGTH_SHORT).show();
                    refreshModelList();
                    if (Settings.canDrawOverlays(this)) {
                        loadingPaths.add(mp);
                        updateButtonStates();
                        FloatingWindowService.start(this, mp);
                    } else {
                        startPet();
                    }
                } else {
                    showErrorRetryDialog(getString(R.string.toast_import_failed),
                            () -> doImportModel(uri, customName));
                    updateButtonStates();
                }
            });
        }).start();
    }

    /**
     * Import a model from an LPK/WPK file with error dialog and retry.
     */
    private void doImportLpk(Uri uri, String customName) {
        isImporting = true;
        updateButtonStates();
        importProgressBar.setVisibility(View.VISIBLE);
        Toast.makeText(this, R.string.toast_unpacking, Toast.LENGTH_SHORT).show();

        new Thread(() -> {
            LpkUnpacker.UnpackResult result = null;
            try {
                LpkUnpacker unpacker = new LpkUnpacker(this);
                result = unpacker.unpackWithConfig(uri, customName);
            } catch (Exception e) {
                Log.e("MainActivity", "LPK unpack failed", e);
            }
            final LpkUnpacker.UnpackResult r = result;
            runOnUiThread(() -> {
                isImporting = false;
                importProgressBar.setVisibility(View.GONE);
                if (r != null && r.modelPath != null) {
                    Toast.makeText(this, R.string.toast_lpk_success, Toast.LENGTH_SHORT).show();
                    refreshModelList();
                    if (Settings.canDrawOverlays(this)) {
                        loadingPaths.add(r.modelPath);
                        updateButtonStates();
                        FloatingWindowService.start(this, r.modelPath,
                                r.hitAreaConfig, r.lookAtConfig, r.modelConfig);
                    } else {
                        startPet();
                    }
                } else {
                    showErrorRetryDialog(getString(R.string.toast_lpk_failed),
                            () -> doImportLpk(uri, customName));
                    updateButtonStates();
                }
            });
        }).start();
    }

    /**
     * Export model directory to user-chosen SAF tree.
     */
    private void doExportModel(Uri treeUri) {
        isExporting = true;
        updateButtonStates();
        importProgressBar.setVisibility(View.VISIBLE);
        Toast.makeText(this, R.string.toast_exporting, Toast.LENGTH_SHORT).show();

        new Thread(() -> {
            boolean success = false;
            try {
                DocumentFile destRoot = DocumentFile.fromTreeUri(this, treeUri);
                if (destRoot != null) {
                    DocumentFile destDir = destRoot.createDirectory(exportSourceName);
                    if (destDir != null) {
                        success = copyDirectoryToSaf(exportSourceDir, destDir);
                    }
                }
            } catch (Exception e) {
                Log.e("MainActivity", "Export failed", e);
            }
            final boolean ok = success;
            runOnUiThread(() -> {
                isExporting = false;
                importProgressBar.setVisibility(View.GONE);
                if (ok) {
                    Toast.makeText(this, R.string.toast_export_success, Toast.LENGTH_SHORT).show();
                } else {
                    Toast.makeText(this, R.string.toast_export_failed, Toast.LENGTH_SHORT).show();
                }
            });
        }).start();
    }

    private boolean copyDirectoryToSaf(File srcDir, DocumentFile destDir) {
        File[] files = srcDir.listFiles();
        if (files == null) return true;
        for (File srcFile : files) {
            if (srcFile.isDirectory()) {
                DocumentFile subDir = destDir.createDirectory(srcFile.getName());
                if (subDir == null || !copyDirectoryToSaf(srcFile, subDir)) return false;
            } else {
                String mime = FileUtils.guessMimeType(srcFile.getName());
                DocumentFile destFile = destDir.createFile(mime, srcFile.getName());
                if (destFile == null) return false;
                try (InputStream is = new java.io.FileInputStream(srcFile);
                     OutputStream os = getContentResolver().openOutputStream(destFile.getUri())) {
                    if (os == null) return false;
                    byte[] buf = new byte[8192];
                    int len;
                    while ((len = is.read(buf)) > 0) {
                        os.write(buf, 0, len);
                    }
                } catch (Exception e) {
                    Log.e("MainActivity", "Copy failed: " + srcFile.getName(), e);
                    return false;
                }
            }
        }
        return true;
    }

    /**
     * Show an error dialog with retry and close options.
     */
    private void showErrorRetryDialog(String message, Runnable onRetry) {
        new AlertDialog.Builder(this)
                .setTitle(R.string.dialog_error_title)
                .setMessage(message)
                .setPositiveButton(R.string.dialog_retry, (dialog, which) -> onRetry.run())
                .setNegativeButton(R.string.dialog_close, null)
                .show();
    }

    private interface NameCallback {
        void onName(String name);
    }

    private void showNameInputDialog(String title, String hint, String defaultName,
                                      NameCallback onConfirmed) {
        EditText input = new EditText(this);
        input.setHint(hint);
        input.setSingleLine(true);
        if (defaultName != null && !defaultName.isEmpty()) {
            input.setText(defaultName);
            input.selectAll();
        }
        input.setPadding(dp(16), dp(12), dp(16), dp(4));

        new AlertDialog.Builder(this)
                .setTitle(title)
                .setView(input)
                .setPositiveButton(R.string.dialog_confirm, (dialog, which) -> {
                    String name = input.getText().toString().trim();
                    onConfirmed.onName(name.isEmpty() ? null : name);
                })
                .setNegativeButton(R.string.dialog_auto_name, (dialog, which) -> onConfirmed.onName(null))
                .setCancelable(false)
                .show();
    }

    private String guessNameFromUri(Uri uri) {
        String lastSeg = uri.getLastPathSegment();
        if (lastSeg == null) return null;
        int slash = lastSeg.lastIndexOf('/');
        if (slash >= 0) lastSeg = lastSeg.substring(slash + 1);
        String lower = lastSeg.toLowerCase(Locale.ROOT);
        if (lower.endsWith(".wpk")) lastSeg = lastSeg.substring(0, lastSeg.length() - 4);
        else if (lower.endsWith(".lpk")) lastSeg = lastSeg.substring(0, lastSeg.length() - 4);
        return lastSeg.isEmpty() ? null : lastSeg;
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
    }

    private String parseModelDisplayName(File jsonFile) {
        try {
            String json = FileUtils.readFileAsString(jsonFile);
            if (json == null) return null;

            String fileName = jsonFile.getName();
            String fallbackName;
            if (fileName.endsWith(".model3.json")) {
                fallbackName = fileName.replace(".model3.json", "");
            } else {
                fallbackName = fileName.replace(".model.json", "");
            }

            JSONObject obj = new JSONObject(json);
            if (obj.has("Name")) {
                String name = obj.optString("Name", "");
                if (!name.isEmpty()) return name;
            }
            if (obj.has("name")) {
                String name = obj.optString("name", "");
                if (!name.isEmpty()) return name;
            }

            return fallbackName;
        } catch (Exception e) {
            return null;
        }
    }

    private static int calculateInSampleSize(File file, int reqWidth, int reqHeight) {
        BitmapFactory.Options opts = new BitmapFactory.Options();
        opts.inJustDecodeBounds = true;
        BitmapFactory.decodeFile(file.getAbsolutePath(), opts);
        int width = opts.outWidth;
        int height = opts.outHeight;
        int inSampleSize = 1;
        if (width > reqWidth || height > reqHeight) {
            int halfW = width / 2;
            int halfH = height / 2;
            while ((halfW / inSampleSize) >= reqWidth && (halfH / inSampleSize) >= reqHeight) {
                inSampleSize *= 2;
            }
        }
        return inSampleSize;
    }
}
