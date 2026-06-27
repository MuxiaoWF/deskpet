#pragma once

#include <CubismFramework.hpp>

/// Application-wide constants: view bounds, priorities, debug flags.
namespace LAppDefine {

    extern const Csm::csmInt32 PriorityNone;
    extern const Csm::csmInt32 PriorityIdle;
    extern const Csm::csmInt32 PriorityNormal;
    extern const Csm::csmInt32 PriorityForce;

    extern const Csm::csmBool DebugLogEnable;
    extern const Csm::CubismFramework::Option::LogLevel CubismLoggingLevel;

    // Standard motion group constants aligned with Live2DViewerEX
    extern const char* const GroupIdle;
    extern const char* const GroupTap;
    extern const char* const GroupShake;
    extern const char* const GroupFlick;
    extern const char* const GroupLeave;
    extern const char* const GroupShot;
    extern const char* const GroupStart;
    extern const char* const GroupCosChange;
    extern const char* const GroupTick;
    extern const char* const GroupReminder;
}
