#pragma once

#include <CubismFramework.hpp>
#include <ICubismModelSetting.hpp>
#include <Type/csmString.hpp>

namespace MotionGroupUtils {

/// Find an idle motion group name by searching for common idle names.
/// Returns fallback if no idle group found and fallbackOnEmpty is true.
inline Csm::csmString FindIdleGroup(Csm::ICubismModelSetting* setting, bool fallbackOnEmpty = false) {
    using namespace Csm;
    if (!setting) return csmString("Idle");
    static const char* idleNames[] = {"Idle", "idle", "IDLE", "Wait", "wait", "Stand", "stand"};
    for (int n = 0; n < 7; n++) {
        for (csmInt32 i = 0; i < setting->GetMotionGroupCount(); i++) {
            if (strcmp(setting->GetMotionGroupName(i), idleNames[n]) == 0) {
                return csmString(idleNames[n]);
            }
        }
    }
    if (fallbackOnEmpty && setting->GetMotionGroupCount() > 0) {
        return csmString(setting->GetMotionGroupName(0));
    }
    return csmString("");
}

/// Find a tap motion group name by searching for common tap names.
/// Falls back to first non-marker group if no tap group found.
inline Csm::csmString FindTapGroup(Csm::ICubismModelSetting* setting, bool fallbackToFirst = false) {
    using namespace Csm;
    if (!setting) return csmString("TapBody");
    static const char* tapNames[] = {"TapBody", "Tap", "Touch", "tap", "touch", "TapHead"};
    for (int n = 0; n < 6; n++) {
        for (csmInt32 i = 0; i < setting->GetMotionGroupCount(); i++) {
            if (strcmp(setting->GetMotionGroupName(i), tapNames[n]) == 0) {
                return csmString(tapNames[n]);
            }
        }
    }
    if (fallbackToFirst) {
        for (csmInt32 i = 0; i < setting->GetMotionGroupCount(); i++) {
            const char* name = setting->GetMotionGroupName(i);
            if (setting->GetMotionCount(name) <= 0) continue;
            if (strstr(name, "\xe3\x80\x90") != nullptr || strstr(name, "\xe3\x80\x91") != nullptr) continue;
            return csmString(name);
        }
    }
    return csmString("TapBody");
}

} // namespace MotionGroupUtils
