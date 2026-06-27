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

    /// Notify Java side that a costume change occurred.
    /// \param costumeName The costume set name that was activated
    /// \param groupName The group target that was toggled (empty for set-level changes)
    static void NotifyCostumeChanged(const char* costumeName, const char* groupName);

    /// Notify Java side that a VarFloat value changed (for menu sync).
    static void NotifyVarFloatChanged(const char* name, float value);
};
