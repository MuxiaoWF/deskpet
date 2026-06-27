#pragma once

#include <map>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

#include <CubismFramework.hpp>
#include <Motion/ICubismUpdater.hpp>
#include <Model/CubismModel.hpp>

#include "ModelConfigParser.hpp"

class LAppLive2DManager;

/// Unified controller engine for all parameter-based controllers.
/// Handles: ParamHit, ParamLoop, ParamTrigger, AreaTrigger, KeyTrigger.
/// Works with both Cubism3/4/5 (via ICubismUpdater) and Cubism2 (via direct Update call).
class ControllerEngine {
public:
    ControllerEngine();

    /// Initialize from model config. Call after SetModelConfig.
    void Initialize(const ModelConfig& config);

    /// Clear transient runtime state without changing the current config.
    void ResetRuntimeState();

    /// Per-frame update for Cubism3/4/5 models (called via ICubismUpdater::OnLateUpdate).
    void Update(Csm::CubismModel* model, Csm::csmFloat32 dt);

    /// Per-frame update for Cubism2 models (called directly from LAppModelCubism2::Update).
    void UpdateCubism2(Csm::csmFloat32 dt);

    /// Touch event handlers (called from LAppLive2DManager).
    void OnTouchBegan(Csm::csmFloat32 x, Csm::csmFloat32 y);
    void OnTouchMoved(Csm::csmFloat32 x, Csm::csmFloat32 y);
    void OnTouchEnded();
    void OnTouchCancelled();

    /// Set the name of the hit area the finger is currently in (empty if none).
    void SetCurrentHitArea(const std::string& name) { _currentHitAreaName = name; }

    /// Gesture event handler (for KeyTrigger).
    void OnGestureEvent(int gestureType, bool isDown);

    /// Get parameter overrides computed during UpdateCubism2 (for Cubism2 path).
    const std::map<std::string, Csm::csmFloat32>& GetControllerOverrides() const { return _controllerOverrides; }

    /// Get VarFloat names that changed during the last Update() call.
    const std::vector<std::string>& GetPendingVarFloatChanges() const { return _pendingVarFloatChanges; }
    void ClearPendingVarFloatChanges() { _pendingVarFloatChanges.clear(); }

    /// Set callback for VarFloat value changes (bridges ControllerEngine → LAppLive2DManager).
    using VarFloatChangeCallback = std::function<void(const std::string& name, float value)>;
    void SetVarFloatChangeCallback(VarFloatChangeCallback cb) { _varFloatChangeCallback = std::move(cb); }

private:
    // Per-param critically-damped spring state for smooth parameter output
    struct SmoothedParamState {
        float currentValue = 0.0F;
        float targetValue = 0.0F;
        float velocity = 0.0F;
    };

    // --- Helpers ---
    static void SetParameterValue(Csm::CubismModel* model, const std::string& paramId, Csm::csmFloat32 value);
    static Csm::csmFloat32 GetParameterValue(Csm::CubismModel* model, const std::string& paramId);
    static void TriggerMotion(const std::string& motionStr);
    static float Lerp(float a, float b, float t);
    static float ApplySpringSmooth(SmoothedParamState& ss, float target, float dt, float smoothTime);

    /// Process ParamHit logic. Returns smoothed values per hit param index.
    void ProcessParamHit(Csm::csmFloat32 dt, std::vector<float>& outValues);

    ControllersConfig _ctrlCopy;
    bool _initialized;

    // ParamLoop state
    struct LoopState {
        float elapsed;
        float intervalTimer;
        float currentValue;
        LoopState() : elapsed(0), intervalTimer(0), currentValue(0) {}
    };
    std::map<std::string, LoopState> _loopStates;

    // ParamHit state
    bool _touchActive;
    Csm::csmFloat32 _touchStartX, _touchStartY;
    std::vector<bool> _activeHitParamIndices;  // indices of active hit params (supports multiple)
    std::string _currentHitAreaName;  // hit area the finger is in (from OnDragWithHitArea)

    // Touch coordinate low-pass filter (CubismCustomController-style smoothing)
    Csm::csmFloat32 _smoothedTouchX = 0.0F;
    Csm::csmFloat32 _smoothedTouchY = 0.0F;

    std::vector<SmoothedParamState> _smoothedStates;

    // ParamTrigger state (previous values for threshold crossing detection)
    std::map<std::string, Csm::csmFloat32> _prevParamValues;

    // AreaTrigger state
    std::vector<bool> _activeHitAreas;
    std::vector<bool> _prevActiveHitAreas;

    // KeyTrigger state
    std::map<int, std::string> _keyDownMotions;
    std::map<int, std::string> _keyUpMotions;

    // Cubism2 parameter overrides (read by LAppModelCubism2 after UpdateCubism2)
    std::map<std::string, Csm::csmFloat32> _controllerOverrides;

    // VarFloat change tracking (for sync back to LAppLive2DManager)
    std::vector<std::string> _pendingVarFloatChanges;
    VarFloatChangeCallback _varFloatChangeCallback;

    // Drawable index cache (built on first Update call, rebuilt on model change)
    std::unordered_map<std::string, Csm::csmInt32> _drawableIndexCache;
    Csm::csmInt32 _cachedDrawableCount = 0;
};

/// ICubismUpdater wrapper for ControllerEngine (Cubism3/4/5 path).
/// Executes at order 100 (before EyeBlink=200).
class ControllerEngineUpdater : public Csm::ICubismUpdater {
public:
    ControllerEngineUpdater(ControllerEngine& engine);
    void OnLateUpdate(Csm::CubismModel* model, Csm::csmFloat32 deltaTimeSeconds) override;

private:
    ControllerEngine& _engine;
};
