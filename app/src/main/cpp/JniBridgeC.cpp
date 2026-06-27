#include "JniBridgeC.hpp"
#include <algorithm>
#include <jni.h>
#include <malloc.h>
#include "LAppDelegate.hpp"
#include "LAppLive2DManager.hpp"
#include "LAppModel.hpp"
#include "LAppModelCubism2.hpp"
#include "LAppPal.hpp"
#include "MotionSequencer.hpp"
#include "TriangleHitTest.hpp"

using namespace Csm;

static JavaVM* g_JVM;
static jclass  g_Live2DNativeBridgeClass;
static jmethodID g_LoadFileMethodId;

static JNIEnv* GetEnv()
{
    JNIEnv* env = nullptr;
    g_JVM->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
    return env;
}

jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
{
    (void)reserved;
    g_JVM = vm;

    JNIEnv *env;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK)
    {
        return JNI_ERR;
    }

    jclass clazz = env->FindClass("com/muxiao/deskpet/live2d/Live2DNativeBridge");
    g_Live2DNativeBridgeClass = reinterpret_cast<jclass>(env->NewGlobalRef(clazz));
    g_LoadFileMethodId = env->GetStaticMethodID(g_Live2DNativeBridgeClass, "LoadFile", "(Ljava/lang/String;)[B");

    return JNI_VERSION_1_6;
}

void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved)
{
    (void)vm;
    (void)reserved;
    JNIEnv *env = GetEnv();
    if (env) {
        env->DeleteGlobalRef(g_Live2DNativeBridgeClass);
    }
}

char* JniBridgeC::LoadFileAsBytesFromJava(const char* filePath, unsigned int* outSize)
{
    JNIEnv *env = GetEnv();
    if (!env) return nullptr;

    jstring jFilePath = env->NewStringUTF(filePath);
    auto obj = (jbyteArray)env->CallStaticObjectMethod(
        g_Live2DNativeBridgeClass, g_LoadFileMethodId, jFilePath);
    env->DeleteLocalRef(jFilePath);

    if (!obj)
    {
        return nullptr;
    }

    *outSize = static_cast<unsigned int>(env->GetArrayLength(obj));

    // Use 64-byte aligned allocation for Cubism Core compatibility (csmAlignofMoc)
    char* buffer = static_cast<char*>(memalign(64, *outSize));
    if (buffer == nullptr) {
        env->DeleteLocalRef(obj);
        return nullptr;
    }
    env->GetByteArrayRegion(obj, 0, *outSize, reinterpret_cast<jbyte *>(buffer));

    return buffer;
}

JavaVM* JniBridgeC::GetJVM()
{
    return g_JVM;
}

static void NotifyMotionText(const char* text, int durationMs, int delayMs)
{
    JNIEnv* env = GetEnv();
    if(env == nullptr) {
        return;
    }
    jmethodID method = env->GetStaticMethodID(g_Live2DNativeBridgeClass,
        "onMotionText", "(Ljava/lang/String;II)V");
    if(method == nullptr) {
        return;
    }
    jstring jText = env->NewStringUTF(text);
    env->CallStaticVoidMethod(g_Live2DNativeBridgeClass, method, jText, durationMs, delayMs);
    env->DeleteLocalRef(jText);
}

void JniBridgeC::NotifyMotionSound(const char* soundPath, int delayMs)
{
    JNIEnv* env = GetEnv();
    if(env == nullptr) {
        return;
    }
    jmethodID method = env->GetStaticMethodID(g_Live2DNativeBridgeClass,
        "onMotionSound", "(Ljava/lang/String;I)V");
    if(method == nullptr) {
        return;
    }
    jstring jPath = env->NewStringUTF(soundPath);
    env->CallStaticVoidMethod(g_Live2DNativeBridgeClass, method, jPath, delayMs);
    env->DeleteLocalRef(jPath);
}

void JniBridgeC::NotifyMotionSoundForMeta(const MotionMeta* meta)
{
    if (meta != nullptr && !meta->sound.empty()) {
        LAppPal::PrintLogLn("[Sound] NotifyMotionSoundForMeta: sound=[%s] delay=%d",
            meta->sound.c_str(), meta->sound_delay);
        NotifyMotionSound(meta->sound.c_str(), meta->sound_delay);
    } else {
        LAppPal::PrintLogLn("[Sound] NotifyMotionSoundForMeta: meta=%s sound=[%s]",
            meta ? "valid" : "null", meta ? meta->sound.c_str() : "");
    }
}

void JniBridgeC::NotifyCostumeChanged(const char* costumeName, const char* groupName)
{
    JNIEnv* env = GetEnv();
    if(env == nullptr) {
        return;
    }
    jmethodID method = env->GetStaticMethodID(g_Live2DNativeBridgeClass,
        "onCostumeChanged", "(Ljava/lang/String;Ljava/lang/String;)V");
    if(method == nullptr) {
        return;
    }
    jstring jCostume = env->NewStringUTF(costumeName ? costumeName : "");
    jstring jGroup = env->NewStringUTF(groupName ? groupName : "");
    env->CallStaticVoidMethod(g_Live2DNativeBridgeClass, method, jCostume, jGroup);
    env->DeleteLocalRef(jCostume);
    env->DeleteLocalRef(jGroup);
}

void JniBridgeC::NotifyVarFloatChanged(const char* name, float value)
{
    JNIEnv* env = GetEnv();
    if(env == nullptr) {
        return;
    }
    jmethodID method = env->GetStaticMethodID(g_Live2DNativeBridgeClass,
        "onVarFloatChanged", "(Ljava/lang/String;F)V");
    if(method == nullptr) {
        return;
    }
    jstring jName = env->NewStringUTF(name ? name : "");
    env->CallStaticVoidMethod(g_Live2DNativeBridgeClass, method, jName, value);
    env->DeleteLocalRef(jName);
}

extern "C"
{
    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeInit(JNIEnv *env, jclass type)
    {
    (void)env;
    (void)type;
        LAppDelegate::ResetReleased();
        LAppDelegate* app = LAppDelegate::GetInstance();
        if(app) app->OnStart();
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeOnSurfaceCreated(JNIEnv *env, jclass type)
    {
    (void)env;
    (void)type;
        LAppDelegate* app = LAppDelegate::GetInstance();
        if(app) app->OnSurfaceCreate();
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeOnSurfaceChanged(JNIEnv *env, jclass type, jint width, jint height)
    {
    (void)env;
    (void)type;
        LAppDelegate* app = LAppDelegate::GetInstance();
        if(app) app->OnSurfaceChanged(width, height);
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeOnDrawFrame(JNIEnv *env, jclass type)
    {
    (void)env;
    (void)type;
        LAppDelegate* app = LAppDelegate::GetInstance();
        if(app) app->Run();
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeOnTouchesBegan(JNIEnv *env, jclass type, jfloat pointX, jfloat pointY)
    {
    (void)env;
    (void)type;
        LAppDelegate* app = LAppDelegate::GetInstance();
        if(app) app->OnTouchBegan(pointX, pointY);
        LAppLive2DManager::GetInstance()->OnTouchBeganForController(pointX, pointY);
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeOnTouchesEnded(JNIEnv *env, jclass type, jfloat pointX, jfloat pointY, jboolean wasDragging)
    {
    (void)env;
    (void)type;
        LAppDelegate* app = LAppDelegate::GetInstance();
        if(app) app->OnTouchEnded(pointX, pointY, wasDragging != JNI_FALSE);
        LAppLive2DManager::GetInstance()->OnTouchEndedForController();
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeOnTouchesCancelled(JNIEnv *env, jclass type)
    {
    (void)env;
    (void)type;
        LAppDelegate* app = LAppDelegate::GetInstance();
        if(app) app->OnTouchCancelled();
        LAppLive2DManager::GetInstance()->OnTouchCancelledForController();
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeOnTouchesMoved(JNIEnv *env, jclass type, jfloat pointX, jfloat pointY)
    {
    (void)env;
    (void)type;
        LAppDelegate* app = LAppDelegate::GetInstance();
        if(app) app->OnTouchMoved(pointX, pointY);
        LAppLive2DManager::GetInstance()->OnTouchMovedForController(pointX, pointY);
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeLoadModel(JNIEnv *env, jclass type, jstring modelPath)
    {
    (void)type;
        const char* path = env->GetStringUTFChars(modelPath, nullptr);
        LAppPal::PrintLogLn("[APP]nativeLoadModel: %s", path);
        LAppDelegate* app = LAppDelegate::GetInstance();
        if(app) app->LoadModel(path);
        env->ReleaseStringUTFChars(modelPath, path);
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeRelease(JNIEnv *env, jclass type)
    {
    (void)env;
    (void)type;
        LAppDelegate* app = LAppDelegate::GetInstance();
        if (app)
        {
            app->OnStop();
            app->OnDestroy();
        }
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeLoadMotion(JNIEnv *env, jclass type, jstring motionGroup, jint index, jint priority, jfloat fadeInTime, jfloat fadeOutTime)
    {
    (void)type;
        const char* group = env->GetStringUTFChars(motionGroup, nullptr);
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();

        // Handle pre_mtn chaining (stores target motion to play after pre_mtn)
        if (MotionSequencer::HandlePreMotion(std::string(group), index, priority, fadeInTime, fadeOutTime)) {
            env->ReleaseStringUTFChars(motionGroup, group);
            return;
        }

        if (mgr->IsCubism2Model())
        {
            LAppModelCubism2* m = mgr->GetCubism2Model(0);
            if(m) m->StartMotion(group, index, priority, fadeInTime, fadeOutTime);
        }
        else
        {
            LAppModel* m = mgr->GetModel(0);
            if(m) m->StartMotion(group, index, priority, nullptr, nullptr, fadeInTime, fadeOutTime);
        }

        // Notify about motion text and sound if available
        const MotionMeta* meta = mgr->FindMotionMeta(std::string(group), index);
        if (meta != nullptr) {
            if (!meta->text.empty()) {
                NotifyMotionText(meta->text.c_str(), meta->text_duration > 0 ? meta->text_duration : 3000, meta->text_delay);
            }
            JniBridgeC::NotifyMotionSoundForMeta(meta);
        }

        // VarFloat evaluation is DEFERRED to the MotionSequencer's OnFinishedInternal
        // callback (set up by SetupMotionCallbacks inside StartMotion).
        // Evaluating immediately would update VarFloat before the motion plays,
        // causing ApplyVarFloatPartOverrides to fight the motion's PartOpacity curves.

        env->ReleaseStringUTFChars(motionGroup, group);
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeLoadMotionByGroup(JNIEnv *env, jclass type, jstring motionGroup)
    {
    (void)type;
        const char* group = env->GetStringUTFChars(motionGroup, nullptr);
        std::string groupStr(group);
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();

        // Use VarFloat-aware selection to pick the right entry
        int selected = MotionSequencer::SelectMotionByVarFloats(groupStr);
        LAppPal::PrintLogLn("[Toggle] group=[%s] → entry=%d", group, selected);
        if (selected >= 0) {
            // Read actual priority from metadata (toggle groups may use high priority like 8)
            const MotionMeta* meta = mgr->FindMotionMeta(groupStr, selected);
            int priority = (meta != nullptr && meta->priority > 0) ? meta->priority : 2 /*PriorityNormal*/;
            LAppPal::PrintLogLn("[Toggle] nativeLoadMotionByGroup: group=[%s] selected=%d priority=%d meta=%s",
                group, selected, priority, meta ? "found" : "null");

            // Handle pre_mtn chaining
            if (MotionSequencer::HandlePreMotion(groupStr, selected, priority)) {
                LAppPal::PrintLogLn("[Toggle] nativeLoadMotionByGroup: pre_mtn chained, returning early");
                env->ReleaseStringUTFChars(motionGroup, group);
                return;
            }

            LAppPal::PrintLogLn("[Toggle] nativeLoadMotionByGroup: calling StartMotion group=[%s] index=%d", group, selected);
            if (mgr->IsCubism2Model()) {
                // Cubism2: no FinishedMotion callback, keep VarFloat update immediate
                MotionSequencer::EvaluateVarFloats(groupStr, selected);
                LAppModelCubism2* m = mgr->GetCubism2Model(0);
                if (m) m->StartMotion(group, selected, priority);
                MotionSequencer::SyncVarFloatPartOverrides();
            } else {
                // Cubism3: VarFloat evaluation is deferred to MotionSequencer's
                // OnFinishedInternal callback (set up by SetupMotionCallbacks inside
                // StartMotion). No user callbacks needed — sequencer handles VarFloat
                // sync on finish, avoiding conflict with motion's PartOpacity curves.
                LAppModel* m = mgr->GetModel(0);
                if (m) m->StartMotion(group, selected, priority);
            }

            // Notify about motion text and sound
            if (meta != nullptr) {
                if (!meta->text.empty()) {
                    NotifyMotionText(meta->text.c_str(), meta->text_duration > 0 ? meta->text_duration : 3000, meta->text_delay);
                }
                JniBridgeC::NotifyMotionSoundForMeta(meta);
            }
        } else {
            // No VarFloat match — fall back to random
            if (mgr->IsCubism2Model()) {
                LAppModelCubism2* m = mgr->GetCubism2Model(0);
                if (m) m->StartRandomMotion(group, 2 /*PriorityNormal*/);
            } else {
                LAppModel* m = mgr->GetModel(0);
                if (m) m->StartRandomMotion(group, 2 /*PriorityNormal*/);
            }
        }

        env->ReleaseStringUTFChars(motionGroup, group);
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeToggleParam(JNIEnv *env, jclass type, jstring paramName)
    {
    (void)type;
        const char* name = env->GetStringUTFChars(paramName, nullptr);
        LAppPal::PrintLogLn("[Toggle] nativeToggleParam: param=[%s]", name);
        LAppLive2DManager::GetInstance()->ToggleParam(name);
        env->ReleaseStringUTFChars(paramName, name);
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeSetExpression(JNIEnv *env, jclass type, jstring expressionName)
    {
    (void)type;
        const char* name = env->GetStringUTFChars(expressionName, nullptr);
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
        if (mgr->IsCubism2Model())
        {
            LAppModelCubism2* m = mgr->GetCubism2Model(0);
            if(m) m->SetExpression(name);
        }
        else
        {
            LAppModel* m = mgr->GetModel(0);
            if(m) m->SetExpression(name);
        }
        env->ReleaseStringUTFChars(expressionName, name);
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeSetHitAreaConfig(JNIEnv *env, jclass type, jstring hitAreaJson)
    {
    (void)type;
        const char* json = env->GetStringUTFChars(hitAreaJson, nullptr);
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
        mgr->SetHitAreaConfig(json);
        env->ReleaseStringUTFChars(hitAreaJson, json);
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeSetLookAtConfig(JNIEnv *env, jclass type, jstring lookAtJson)
    {
    (void)type;
        const char* json = env->GetStringUTFChars(lookAtJson, nullptr);
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
        // Pass to the active model
        if (mgr->IsCubism2Model())
        {
            LAppModelCubism2* m = mgr->GetCubism2Model(0);
            if(m) m->SetLookAtConfig(json);
        }
        else
        {
            LAppModel* m = mgr->GetModel(0);
            if(m) m->SetLookAtConfig(json);
        }
        env->ReleaseStringUTFChars(lookAtJson, json);
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeSetExpressionBlend(JNIEnv *env, jclass type, jstring expressionsJson)
    {
    (void)type;
        const char* json = env->GetStringUTFChars(expressionsJson, nullptr);
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
        if (mgr->IsCubism2Model())
        {
            LAppModelCubism2* m = mgr->GetCubism2Model(0);
            if(m) m->SetExpressionBlendConfig(json);
        }
        else
        {
            LAppModel* m = mgr->GetModel(0);
            if(m) m->SetExpressionBlendConfig(json);
        }
        env->ReleaseStringUTFChars(expressionsJson, json);
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeLastExpression(JNIEnv *env, jclass type)
    {
    (void)env;
    (void)type;
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
        if (mgr->IsCubism2Model())
        {
            LAppModelCubism2* m = mgr->GetCubism2Model(0);
            if(m) m->LastExpression();
        }
        else
        {
            LAppModel* m = mgr->GetModel(0);
            if(m) m->LastExpression();
        }
    }

    JNIEXPORT jstring JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeGetCurrentExpression(JNIEnv *env, jclass type)
    {
    (void)type;
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
        const char* name = "";
        if (mgr->IsCubism2Model())
        {
            LAppModelCubism2* m = mgr->GetCubism2Model(0);
            if(m) name = m->GetCurrentExpressionName();
        }
        else
        {
            LAppModel* m = mgr->GetModel(0);
            if(m) name = m->GetCurrentExpressionId();
        }
        return env->NewStringUTF(name);
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeSetExpressionBlendMode(JNIEnv *env, jclass type, jint mode)
    {
    (void)env;
    (void)type;
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
        if (mgr->IsCubism2Model())
        {
            LAppModelCubism2* m = mgr->GetCubism2Model(0);
            if(m) m->SetExpressionBlendMode(static_cast<ExpressionBlendMode>(mode));
        }
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeSetPhysicsWeight(JNIEnv *env, jclass type, jfloat weight)
    {
    (void)env;
    (void)type;
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
        if (mgr->IsCubism2Model())
        {
            LAppModelCubism2* m = mgr->GetCubism2Model(0);
            if(m) m->SetPhysicsWeight(weight);
        }
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeEnablePhysicsWithFade(JNIEnv *env, jclass type, jfloat targetWeight, jfloat fadeTime)
    {
    (void)env;
    (void)type;
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
        if (mgr->IsCubism2Model())
        {
            LAppModelCubism2* m = mgr->GetCubism2Model(0);
            if(m) m->EnablePhysicsWithFade(targetWeight, fadeTime);
        }
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeDisablePhysicsWithFade(JNIEnv *env, jclass type, jfloat fadeTime)
    {
    (void)env;
    (void)type;
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
        if (mgr->IsCubism2Model())
        {
            LAppModelCubism2* m = mgr->GetCubism2Model(0);
            if(m) m->DisablePhysicsWithFade(fadeTime);
        }
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeSetModelConfig(JNIEnv *env, jclass type, jstring configJson)
    {
    (void)type;
        const char* json = env->GetStringUTFChars(configJson, nullptr);
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
        mgr->SetModelConfig(json);
        env->ReleaseStringUTFChars(configJson, json);
    }

    JNIEXPORT jstring JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeGetMotionMetaList(JNIEnv *env, jclass type)
    {
    (void)type;
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
        const std::string json = mgr->GetMotionMetaJson();
        return env->NewStringUTF(json.c_str());
    }

    JNIEXPORT jstring JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeGetGroupList(JNIEnv *env, jclass type)
    {
    (void)type;
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
        const std::string json = mgr->GetGroupConfigJson();
        return env->NewStringUTF(json.c_str());
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeOnGestureEvent(JNIEnv *env, jclass type, jint gestureType, jboolean isDown)
    {
    (void)env;
    (void)type;
        LAppLive2DManager::GetInstance()->OnGestureEvent(gestureType, isDown);
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeSetFollowMode(JNIEnv *env, jclass type, jint mode)
    {
    (void)env;
    (void)type;
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
        if (mgr->IsCubism2Model())
        {
            // Cubism2 models handle look-at differently, skip for now
        }
        else
        {
            LAppModel* m = mgr->GetModel(0);
            if(m) m->SetFollowMode(mode);
        }
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeShowHitAreas(JNIEnv *env, jclass type, jboolean show)
    {
    (void)env;
    (void)type;
        LAppLive2DManager::GetInstance()->SetDebugHitAreaVisible(show);
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeSetMenuVisible(JNIEnv *env, jclass type, jboolean visible)
    {
    (void)env;
    (void)type;
        LAppLive2DManager::GetInstance()->SetMenuVisible(visible);
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeSetMirror(JNIEnv *env, jclass type, jboolean mirror)
    {
    (void)env;
    (void)type;
        LAppLive2DManager::GetInstance()->SetMirror(mirror);
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeSetRotation(JNIEnv *env, jclass type, jfloat degrees)
    {
    (void)env;
    (void)type;
        LAppLive2DManager::GetInstance()->SetRotation(degrees);
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeSetTouchOffset(JNIEnv *env, jclass type, jfloat dx, jfloat dy)
    {
    (void)env;
    (void)type;
        LAppLive2DManager::GetInstance()->SetTouchOffset(dx, dy);
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeSetTouchScale(JNIEnv *env, jclass type, jfloat scale)
    {
    (void)env;
    (void)type;
        LAppLive2DManager::GetInstance()->SetTouchScale(scale);
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeLoadModelAt(JNIEnv *env, jclass type, jstring modelPath, jint slot)
    {
    (void)type;
        const char* path = env->GetStringUTFChars(modelPath, nullptr);
        LAppPal::PrintLogLn("[APP]nativeLoadModelAt: %s slot=%d", path, slot);
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
        mgr->LoadModelAt(path, static_cast<int>(slot));
        env->ReleaseStringUTFChars(modelPath, path);
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeRemoveModel(JNIEnv *env, jclass type, jint slot)
    {
    (void)env;
    (void)type;
        LAppLive2DManager::GetInstance()->RemoveModel(static_cast<int>(slot));
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeSelectModel(JNIEnv *env, jclass type, jint slot)
    {
    (void)env;
    (void)type;
        LAppLive2DManager::GetInstance()->SelectModel(static_cast<int>(slot));
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeSetRandomSpeakEnabled(JNIEnv *env, jclass type, jboolean enabled)
    {
    (void)env;
    (void)type;
        LAppDelegate::GetInstance()->GetRandomSpeaker().SetEnabled(enabled);
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeSetRandomSpeakInterval(JNIEnv *env, jclass type, jint seconds)
    {
    (void)env;
    (void)type;
        LAppDelegate::GetInstance()->GetRandomSpeaker().SetInterval(seconds);
    }

    JNIEXPORT jstring JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeGetSlotState(JNIEnv *env, jclass type)
    {
    (void)type;
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
        const auto& groups = mgr->GetModelConfig().groups;

        // Build JSON: [{"name":"...", "target":"...", "active":true/false, "ids":[...]}]
        std::string result = "[";
        bool first = true;
        for (const auto& g : groups) {
            if (g.hidden || g.ids.empty()) continue;
            if (!first) result += ",";
            first = false;
            result += "{\"name\":\"" + g.name + "\",\"target\":\"" + g.target + "\"";
            result += ",\"currentIndex\":" + std::to_string(g.currentIndex);
            result += ",\"values\":[";
            for (size_t i = 0; i < g.values.size(); i++) {
                if (i > 0) result += ",";
                result += std::to_string(g.values[i]);
            }
            result += "],\"ids\":[";
            for (size_t i = 0; i < g.ids.size(); i++) {
                if (i > 0) result += ",";
                result += "\"" + g.ids[i] + "\"";
            }
            result += "]}";
        }
        result += "]";
        return env->NewStringUTF(result.c_str());
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeSetSlotState(JNIEnv *env, jclass type, jstring groupName, jint slotIndex)
    {
    (void)type;
        const char* name = env->GetStringUTFChars(groupName, nullptr);
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
        auto& groups = mgr->GetModelConfig().groups;

        for (auto& g : groups) {
            if (g.name == name && !g.ids.empty() && !g.values.empty()) {
                int idx = static_cast<int>(slotIndex);
                if (idx >= 0 && idx < static_cast<int>(g.ids.size())) {
                    mgr->SetGroupIndex(g, idx);
                }
                break;
            }
        }

        env->ReleaseStringUTFChars(groupName, name);
    }

    JNIEXPORT void JNICALL
    Java_com_muxiao_deskpet_live2d_Live2DNativeBridge_nativeSetAabbHitTestMode(JNIEnv *env, jclass type, jboolean enabled)
    {
    (void)env;
    (void)type;
        TriangleHitTest::AabbOnlyMode() = enabled;
        LAppPal::PrintLogLn("[APP]AABB-only hit test mode: %s", enabled ? "ON" : "OFF");
    }
}
