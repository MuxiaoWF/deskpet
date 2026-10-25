#pragma once
#include <jni.h>
#include <Type/csmVector.hpp>
#include <Type/csmString.hpp>

struct MotionMeta;

/// JNI bridge: native methods called from Java and Java callbacks from native.
class JniBridgeC
{
public:
    static char* LoadFileAsBytesFromJava(const char* filePath, unsigned int* outSize);
    static JavaVM* GetJVM();

    /// Notify Java side to play a motion sound. delayMs = 0 means immediate.
    static void NotifyMotionSound(const char* soundPath, int delayMs);
    static void NotifyMotionSoundForMeta(const MotionMeta* meta);

    /// Notify Java side about BOTH the motion text bubble and the motion sound
    /// described by the meta. Single entry point so every motion start path
    /// (menu, tap, hit-area down/up, gesture, random speak) stays consistent.
    static void NotifyMotionTextAndSound(const MotionMeta* meta);

    /// Notify Java side that a costume change occurred.
    /// \param costumeName The costume set name that was activated
    /// \param groupName The group target that was toggled (empty for set-level changes)
    static void NotifyCostumeChanged(const char* costumeName, const char* groupName);

    /// Notify Java side that a VarFloat value changed (for menu sync).
    static void NotifyVarFloatChanged(const char* name, float value);

    /// Notify Java side whether the last model load succeeded or failed,
    /// so the UI can tell the user instead of showing a blank window.
    static void NotifyModelLoadResult(bool success, const char* modelPath);
};
