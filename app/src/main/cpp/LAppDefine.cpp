#include "LAppDefine.hpp"
#include <CubismFramework.hpp>

namespace LAppDefine {

    using namespace Csm;

    const csmInt32 PriorityNone = 0;
    const csmInt32 PriorityIdle = 1;
    const csmInt32 PriorityNormal = 2;
    const csmInt32 PriorityForce = 3;

    const csmBool DebugLogEnable = false;
    const CubismFramework::Option::LogLevel CubismLoggingLevel = CubismFramework::Option::LogLevel_Verbose;

    // Standard motion group constants aligned with Live2DViewerEX
    const char* const GroupIdle = "idle";
    const char* const GroupTap = "tap";
    const char* const GroupShake = "shake";
    const char* const GroupFlick = "flick";
    const char* const GroupLeave = "leave";
    const char* const GroupShot = "shot";
    const char* const GroupStart = "start";
    const char* const GroupCosChange = "cos_change";
    const char* const GroupTick = "tick";
    const char* const GroupReminder = "reminder";
}
