#pragma once

#include <string>

/// Shared pending motion struct for pre_mtn chaining.
/// Used by both LAppModel and LAppModelCubism2.
struct PendingMotion {
    std::string group;
    int index = -1;
    int priority = 3;
    float fadeIn = -1.0F;
    float fadeOut = -1.0F;
    bool active = false;
};
