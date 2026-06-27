#pragma once

#include <map>
#include <string>
#include <cmath>
#include "MotionLayer.hpp"

/// Unified fade controller aligned with Live2DViewerEX CubismFadeController.
/// Manages fade-in/fade-out curve calculation for all motion layers.
/// Supports per-parameter fade curves from .motion3.json (aligned with ViewerEX
/// CubismFadeMotionData.ParameterFadeInTimes/ParameterFadeOutTimes).
class CubismFadeController {
public:
    CubismFadeController() = default;

    /// Initialize layer states. Call once after model load.
    /// \param mainMotionManager The SDK's CubismMotionManager for MainMotion layer (not owned).
    /// BaseIdle layer's manager is set externally via GetLayer(BaseIdle).manager.
    void Initialize(Csm::CubismMotionManager* mainMotionManager = nullptr) {
        for (int i = 0; i < static_cast<int>(MotionLayer::Count); i++) {
            _layers[i] = MotionLayerState{};
            _layers[i].fadeWeight = (i == static_cast<int>(MotionLayer::BaseIdle)) ? 1.0F : 0.0F;
        }
        // MainMotion layer uses the SDK's motion manager for actual playback
        if (mainMotionManager) {
            _layers[static_cast<int>(MotionLayer::MainMotion)].manager = mainMotionManager;
        }
        // Allocate managers for non-BaseIdle, non-MainMotion layers.
        // BaseIdle manager is provided externally (dedicated idle manager in LAppModel).
        for (int i = 0; i < static_cast<int>(MotionLayer::Count); i++) {
            if (i == static_cast<int>(MotionLayer::BaseIdle)) continue;
            if (_layers[i].manager == nullptr) {
                _layers[i].manager = new Csm::CubismMotionManager();
            }
        }
    }

    /// Release all layer managers. Call on model destruction.
    /// \param mainMotionManager The SDK's manager (not owned, skip deletion).
    void Release(Csm::CubismMotionManager* mainMotionManager = nullptr) {
        for (int i = 0; i < static_cast<int>(MotionLayer::Count); i++) {
            // Don't delete the SDK's manager (MainMotion layer shares it)
            if (_layers[i].manager != mainMotionManager) {
                delete _layers[i].manager;
            }
            _layers[i].manager = nullptr;
        }
    }

    /// Get layer state (read-write).
    MotionLayerState& GetLayer(MotionLayer layer) {
        return _layers[static_cast<int>(layer)];
    }

    /// Get layer state (read-only).
    const MotionLayerState& GetLayer(MotionLayer layer) const {
        return _layers[static_cast<int>(layer)];
    }

    /// Get the motion manager for a specific layer.
    Csm::CubismMotionManager* GetManager(MotionLayer layer) {
        return _layers[static_cast<int>(layer)].manager;
    }

    /// Start a fade transition on a layer.
    void StartFade(MotionLayer layer, float fromWeight, float toWeight, float duration) {
        auto& state = _layers[static_cast<int>(layer)];
        state.fadeFrom = fromWeight;
        state.fadeTo = toWeight;
        state.fadeElapsed = 0.0F;
        state.fadeDuration = duration;
        state.isFading = true;
    }

    /// Compute smoothstep weight for a given elapsed time and duration.
    static float ComputeSmoothstepWeight(float elapsed, float duration) {
        float t = (duration > 0.0F) ? (elapsed / duration) : 1.0F;
        if (t >= 1.0F) t = 1.0F;
        return t * t * (3.0F - 2.0F * t);
    }

    /// Update all active fade transitions. Call once per frame.
    /// Must be called AFTER motion update and SDK updaters (aligned with ViewerEX
    /// CubismFadeController.OnLateUpdate which runs after motion + SDK pipeline).
    void UpdateFades(float deltaTimeSeconds) {
        for (int i = 0; i < static_cast<int>(MotionLayer::Count); i++) {
            auto& state = _layers[i];

            // Update fade transitions
            if (state.isFading) {
                state.fadeElapsed += deltaTimeSeconds;
                float t = (state.fadeDuration > 0.0F) ? (state.fadeElapsed / state.fadeDuration) : 1.0F;
                if (t >= 1.0F) t = 1.0F;

                // Smoothstep easing (aligned with Live2DViewerEX CubismFadeController::Evaluate)
                t = t * t * (3.0F - 2.0F * t);

                state.fadeWeight = state.fadeFrom + (state.fadeTo - state.fadeFrom) * t;

                if (state.fadeElapsed >= state.fadeDuration) {
                    state.isFading = false;
                    state.fadeWeight = state.fadeTo;
                }
            }

            // Per-parameter fade curves (aligned with ViewerEX CubismFadeMotionData)
            // For parameters with custom FadeInTime/FadeOutTime, compute independent weights.
            if (!state.paramFadeOverrides.empty()) {
                for (const auto& ov : state.paramFadeOverrides) {
                    float paramDuration = state.fadeDuration;
                    // Use per-curve override if set
                    if (state.fadeTo > state.fadeFrom && ov.fadeInTime >= 0) {
                        paramDuration = ov.fadeInTime;
                    } else if (state.fadeTo < state.fadeFrom && ov.fadeOutTime >= 0) {
                        paramDuration = ov.fadeOutTime;
                    }
                    float rawT = ComputeSmoothstepWeight(state.fadeElapsed, paramDuration);
                    state.paramWeights[ov.paramId] = state.fadeFrom + (state.fadeTo - state.fadeFrom) * rawT;
                }
            }

            // Handle fade-out waiting completion
            if (state.isFadeOutWaiting) {
                state.fadeOutWaitElapsed += deltaTimeSeconds;
                if (!state.isFading && state.fadeWeight <= 0.0F) {
                    // Fade-out complete, signal that pending motion can start
                    state.isFadeOutWaiting = false;
                    state.isFinished = true;
                }
            }
        }
    }

    /// Check if a layer's motion has fully faded out (weight == 0 and not fading).
    bool IsLayerFadedOut(MotionLayer layer) const {
        const auto& state = _layers[static_cast<int>(layer)];
        return !state.isFading && state.fadeWeight <= 0.0F && state.isFinished;
    }

    /// Check if a layer is actively playing (has motion or is fading in).
    bool IsLayerActive(MotionLayer layer) const {
        const auto& state = _layers[static_cast<int>(layer)];
        return state.fadeWeight > 0.0F || state.isFading;
    }

    /// Get the current fade weight for a layer (0.0 = fully faded out, 1.0 = fully visible).
    float GetWeight(MotionLayer layer) const {
        return _layers[static_cast<int>(layer)].fadeWeight;
    }

    /// Get per-parameter fade weight. Returns global weight if no per-curve override exists.
    float GetParamWeight(MotionLayer layer, const std::string& paramId) const {
        const auto& state = _layers[static_cast<int>(layer)];
        auto it = state.paramWeights.find(paramId);
        if (it != state.paramWeights.end()) {
            return it->second;
        }
        return state.fadeWeight;
    }

    /// Start a motion on a specific layer with priority check and fade-in.
    bool StartLayerMotion(MotionLayer layer, Csm::ACubismMotion* motion,
                          float fadeInTime, float /*fadeOutTime*/, int priority) {
        auto& state = _layers[static_cast<int>(layer)];
        if (!state.manager) return false;

        // Priority check: reject if priority <= current (unless Force)
        if (priority <= state.priority && priority != 3) return false;

        state.manager->StopAllMotions();
        state.manager->StartMotion(motion, false);
        state.priority = priority;
        state.isFinished = false;
        state.isFadeOutWaiting = false;
        state.isAnimationEndEventInvoked = false;

        // Start fade-in
        StartFade(layer, 0.0F, 1.0F, fadeInTime);
        return true;
    }

    /// Request a motion that waits for the current motion to fade out first.
    void RequestMotionWithFadeOut(MotionLayer layer, const std::string& group,
                                   int index, int priority, float fadeOutTime) {
        auto& state = _layers[static_cast<int>(layer)];
        if (state.fadeWeight > 0.0F && !state.isFinished) {
            StartFade(layer, state.fadeWeight, 0.0F, fadeOutTime);
            state.isFadeOutWaiting = true;
            state.pendingGroup = group;
            state.pendingIndex = index;
            state.pendingPriority = priority;
            state.fadeOutWaitDuration = fadeOutTime;
            state.fadeOutWaitElapsed = 0.0F;
        }
    }

    bool HasPendingMotion(MotionLayer layer) const {
        return _layers[static_cast<int>(layer)].isFadeOutWaiting;
    }

    void GetPendingMotion(MotionLayer layer, std::string& outGroup, int& outIndex, int& outPriority) const {
        const auto& state = _layers[static_cast<int>(layer)];
        outGroup = state.pendingGroup;
        outIndex = state.pendingIndex;
        outPriority = state.pendingPriority;
    }

    void ClearPendingMotion(MotionLayer layer) {
        auto& state = _layers[static_cast<int>(layer)];
        state.isFadeOutWaiting = false;
        state.pendingGroup.clear();
        state.pendingIndex = -1;
        state.pendingPriority = 0;
    }

private:
    MotionLayerState _layers[static_cast<int>(MotionLayer::Count)];
};
