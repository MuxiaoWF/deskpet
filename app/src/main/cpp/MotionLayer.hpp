#pragma once

#include <string>
#include <map>
#include <vector>
#include <Motion/ACubismMotion.hpp>
#include <Motion/CubismMotionManager.hpp>

/// Layer hierarchy aligned with Live2DViewerEX Cubism Playable system.
/// Rendering order: BaseIdle (bottom) -> MainMotion -> EyeBlink -> LipSync -> Effect -> UIOverlay (top).
enum class MotionLayer : int {
    BaseIdle = 0,   // Default idle motion, always running at bottom
    MainMotion,     // User-triggered motions (tap, shake, flick, etc.)
    EyeBlink,       // Eye blink overlay
    LipSync,        // Lip sync overlay
    Effect,         // Particle/visual effects
    UIOverlay,      // UI elements on top
    Count           // Sentinel for array sizing
};

/// Per-curve fade override (from .motion3.json per-curve FadeInTime/FadeOutTime).
/// Aligns with ViewerEX CubismFadeMotionData.ParameterFadeInTimes/ParameterFadeOutTimes.
struct ParamFadeOverride {
    std::string paramId;
    float fadeInTime = -1.0F;   // -1 = use global
    float fadeOutTime = -1.0F;
};

/// Per-layer state data. Each layer independently maintains its own motion state.
struct MotionLayerState {
    Csm::CubismMotionManager* manager = nullptr;  // Independent motion manager per layer
    float fadeWeight = 1.0F;                       // Current fade weight (0.0-1.0), driven by CubismFadeController
    float fadeElapsed = 0.0F;                      // Seconds since fade started
    float fadeDuration = 0.0F;                     // Total fade duration
    float fadeFrom = 0.0F;                         // Weight at fade start
    float fadeTo = 1.0F;                           // Target weight
    bool isFading = false;                         // True when a fade transition is active
    bool isFinished = true;                        // True when layer has no active motion
    bool isAnimationEndEventInvoked = false;       // Prevents duplicate end callbacks (aligned with ViewerEX)
    int priority = 0;                              // Current motion priority on this layer
    std::string activeGroup;                       // Group of current motion
    int activeIndex = -1;                          // Index of current motion

    // Blend mode (aligned with ViewerEX blend_mode/blend_weight separation)
    int blendMode = 0;                             // 0=normal (overwrite), 1=additive
    float blendWeight = 1.0F;                      // weight for additive blending

    // Per-parameter fade curves (from .motion3.json)
    std::vector<ParamFadeOverride> paramFadeOverrides;
    float modelFadeInTime = -1.0F;                 // global fade-in from .motion3.json meta
    float modelFadeOutTime = -1.0F;                // global fade-out from .motion3.json meta
    std::map<std::string, float> paramWeights;     // computed per-param fade weights

    // Fade-out waiting: wait for current motion to fade out before starting the next one
    // Aligned with Unity Animator state machine transition behavior
    bool isFadeOutWaiting = false;                 // True when waiting for fade-out to complete
    float fadeOutWaitElapsed = 0.0F;               // Time spent waiting
    float fadeOutWaitDuration = 0.0F;              // Total fade-out duration to wait
    std::string pendingGroup;                      // Motion to start after fade-out completes
    int pendingIndex = -1;                         // Index of pending motion
    int pendingPriority = 0;                       // Priority of pending motion
};

/// Convert MotionLayer enum to debug string.
inline const char* MotionLayerToString(MotionLayer layer) {
    switch (layer) {
        case MotionLayer::BaseIdle:   return "BaseIdle";
        case MotionLayer::MainMotion: return "MainMotion";
        case MotionLayer::EyeBlink:   return "EyeBlink";
        case MotionLayer::LipSync:    return "LipSync";
        case MotionLayer::Effect:     return "Effect";
        case MotionLayer::UIOverlay:  return "UIOverlay";
        default:                      return "Unknown";
    }
}
