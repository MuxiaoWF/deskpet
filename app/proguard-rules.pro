# ==========================================
# DeskPet ProGuard / R8 Rules
# ==========================================

# ---------- General optimizations ----------
-allowaccessmodification
-repackageclasses ''
-optimizations !code/simplification/variable,!code/simplification/arithmetic

# Keep line numbers for crash stack traces
-keepattributes SourceFile,LineNumberTable
-renamesourcefileattribute SourceFile

# ---------- Keep annotations ----------
-keepattributes *Annotation*
-keepattributes Signature
-keepattributes InnerClasses,EnclosingMethod

# ---------- JNI / Native bridge (critical — native code calls these) ----------
-keep class com.muxiao.deskpet.live2d.Live2DNativeBridge { *; }

# ---------- Entry points (Activity / Service in AndroidManifest) ----------
-keep class com.muxiao.deskpet.MainActivity { *; }
-keep class com.muxiao.deskpet.FloatingWindowService { *; }

# ---------- AndroidX / Material ----------
-keep class androidx.core.app.CoreComponentFactory { *; }
-keep class com.google.android.material.** {*;}

# ---------- Remove Log calls in release (smaller APK, no debug strings) ----------
-assumenosideeffects class android.util.Log {
    public static int v(...);
    public static int d(...);
    public static int i(...);
}
