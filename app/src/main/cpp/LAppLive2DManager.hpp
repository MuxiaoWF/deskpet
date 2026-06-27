#pragma once

#include <CubismFramework.hpp>
#include <Math/CubismMatrix44.hpp>
#include <Type/csmVector.hpp>
#include <Type/csmString.hpp>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <cmath>

#include "ModelConfigParser.hpp"
#include "ControllerEngine.hpp"

class LAppModel;
class LAppModelCubism2;

/// Hit area configuration from config.mlve
struct HitAreaConfig {
    std::string name;
    std::string id;
    std::string motion;      // motion group to play on tap
    std::string enterMtn;    // motion on hover enter
    std::string exitMtn;     // motion on hover exit
    std::string downMtn;     // motion on touch began inside area
    std::string upMtn;       // motion on touch ended inside area
    bool ignore_visibility = false;  // AABB hit test ignoring drawable visibility
    bool enabled;

    float width;
    float height;
    float centerX;
    float centerY;
    int order;

    // Parameter toggle support (for model control panels)
    std::string param;              // parameter ID to toggle
    std::vector<float> values;      // cycle values
    int currentIndex = -1;          // current position in values[], -1 = unset
};

/// Smooth opacity transition state for a single part.
struct PartFadeState {
    float from = 1.0F;       // opacity at fade start
    float to = 1.0F;         // target opacity
    float elapsed = 0.0F;    // seconds since fade started
    float duration = 0.3F;   // total fade duration in seconds
};

/// Central model manager: loads/unloads Cubism 3/4/5 and Cubism 2 models,
/// dispatches touch events, manages hit areas, parameter overrides, and the
/// per-frame rendering/update loop.
class LAppLive2DManager
{
public:
    static LAppLive2DManager* GetInstance();
    static void ReleaseInstance();

    LAppModel* GetModel(Csm::csmUint32 no) const;
    LAppModelCubism2* GetCubism2Model(Csm::csmUint32 no) const;
    Csm::csmUint32 GetModelCount() const { return _models.GetSize(); }
    void SetRenderTargetSize(Csm::csmUint32 width, Csm::csmUint32 height);
    void ReleaseAllModel();

    void OnDrag(Csm::csmFloat32 x, Csm::csmFloat32 y) const;
    void OnDragWithHitArea(Csm::csmFloat32 x, Csm::csmFloat32 y);
    void OnTap(Csm::csmFloat32 x, Csm::csmFloat32 y, bool wasDragging = false);
    void OnHitAreaBegan(Csm::csmFloat32 x, Csm::csmFloat32 y);
    void OnHitAreaEnded(Csm::csmFloat32 x, Csm::csmFloat32 y);
    void ResetHitAreaTracking() { _currentHitAreaIndex = -1; _dragTriggeredHitArea = false; _pressedHitAreaIndex = -1; _lastPressedHitAreaIndex = -1; _upMtnFired = false; }

    // Parameter toggle overrides (applied each frame after motion/expression)
    const std::map<std::string, float>& GetParamOverrides() const { return _paramOverrides; }
    std::map<std::string, float>& GetParamOverrides() { return _paramOverrides; }
    void ToggleParam(const char* paramName);

    // Part opacity overrides (for component/clothing visibility toggle via Group ids)
    const std::map<std::string, float>& GetPartOverrides() const { return _partOverrides; }
    std::map<std::string, float>& GetPartOverrides() { return _partOverrides; }
    void SetPartOverride(const std::string& partId, float value, bool useFade = true);

    /// Called when a motion with PartOpacity curves starts playing.
    /// Registers the Part IDs so ApplyVarFloatPartOverrides skips them
    /// during playback, letting the motion's curves drive the transition.
    void OnMotionStartedWithPartOpacity(const std::vector<std::string>& partIds);

    /// Clears motion-controlled parts tracking (called when motion finishes).
    void ClearMotionControlledParts();

    // PartFade smooth opacity transitions
    std::map<std::string, PartFadeState>& GetPartFades() { return _partFades; }
    std::map<std::string, float>& GetPartVisualOpacity() { return _partVisualOpacity; }
    void StartPartFade(const std::string& partId, float toValue, float duration = 0.3F);
    void ClearPartFades() { _partFades.clear(); }

    void OnUpdate();

    void LoadModelFromPath(const char* modelPath);
    void LoadModelAt(const char* modelPath, int slot);
    void RemoveModel(int slot);
    void SelectModel(int slot);  // -1 = show all, >=0 = show only that slot

    Csm::csmUint32 GetModelNum() const;
    Csm::csmUint32 GetCubism2ModelNum() const;
    int GetSelectedModelSlot() const { return _selectedModelSlot; }
    bool IsCubism2Model() const { return _isCubism2; }
    Csm::CubismMatrix44* GetViewMatrix() const { return _viewMatrix; }

    // Config-driven hit area support
    void SetHitAreaConfig(const char* json);
    const std::vector<HitAreaConfig>& GetHitAreaConfigs() const { return _hitAreaConfigs; }

    // Full model config (motion metadata, groups, controllers)
    void SetModelConfig(const char* json);
    const ModelConfig& GetModelConfig() const { return _modelConfig; }
    ModelConfig& GetModelConfig() { return _modelConfig; }
    const std::map<std::string, std::vector<MotionMeta>>& GetMotionMetas() const { return _modelConfig.motions; }

    // VarFloat variable store
    float GetVarFloat(const std::string& name) const;
    void SetVarFloat(const std::string& name, float value);

    // VarFloat cascade: build reverse mapping and sync all linked groups
    void BuildVarFloatGroupMap();
    void SyncAllVarFloatLinks(const std::string& varFloatName);
    /// Per-frame: re-sync dirty VarFloat→GroupConfig mappings.
    void SyncAllVarFloats();
    /// Apply VarFloat-driven part overrides every frame (replaces PartFade + PartOverrides).
    void ApplyVarFloatPartOverrides();
    /// Check if a VarFloat is managed by a group with Part IDs (toggle state).
    /// Managed VarFloats should not be overwritten by commands or bulk sync.
    bool IsVarFloatManagedByGroup(const std::string& varFloatName) const;
    /// High-level: update a group's index and cascade via VarFloat.
    void SetGroupIndex(GroupConfig& group, int index);


    /// Activate a costume set by name (or a single group by target/name).
    /// Handles mutual_exclude deactivation and VarFloat cascade.
    void ChangeCostume(const std::string& name);

    /// Get the currently active costume set name (empty if none).
    const std::string& GetCurrentCostumeName() const { return _currentCostumeName; }

    // Controller engine (ParamHit, ParamLoop, ParamTrigger, AreaTrigger, KeyTrigger)
    ControllerEngine& GetControllerEngine() { return _controllerEngine; }
    void OnTouchBeganForController(Csm::csmFloat32 x, Csm::csmFloat32 y);
    void OnTouchMovedForController(Csm::csmFloat32 x, Csm::csmFloat32 y);
    void OnTouchEndedForController();
    void OnTouchCancelledForController();
    void OnGestureEvent(int gestureType, bool isDown);

    // Model transform: mirror (flip X) and rotation (degrees around Z axis)
    void SetMirror(bool mirror) { _mirror = mirror; }
    bool IsMirrored() const { return _mirror; }
    void SetRotation(float degrees) { _rotation = degrees; }
    float GetRotation() const { return _rotation; }

    // Floating window offset and scale (for coordinate transform)
    void SetTouchOffset(float dx, float dy) { _touchOffsetX = dx; _touchOffsetY = dy; }
    void SetTouchScale(float scale) { _touchScale = scale; }

    /// Transform touch coordinates to account for mirror/rotation, so IsHit
    /// tests against the visually transformed model position.
    void TransformTouchForModelTransform(float touchX, float touchY,
                                          float& outX, float& outY) const;

    // Debug hit area visualization
    void SetDebugHitAreaVisible(bool visible) { _debugHitAreaVisible = visible; }
    bool IsDebugHitAreaVisible() const { return _debugHitAreaVisible; }

    // Menu visibility state (synced from Java ActionMenuManager)
    void SetMenuVisible(bool visible) { _menuVisible = visible; }
    bool IsMenuVisible() const { return _menuVisible; }

    // Reload GL resources for Cubism 2 models after context loss
    void ReloadCubism2Renderers();

    // Motion metadata lookup
    const MotionMeta* FindMotionMeta(const std::string& group, int index) const;
    std::string GetMotionMetaJson() const;
    std::string GetGroupConfigJson() const;

private:
    LAppLive2DManager();
    virtual ~LAppLive2DManager();

    void ParseHitAreaJson(const char* json);
    void AutoGenerateHitAreas();
    void AutoGenerateHitAreasFromModelJson(const char* modelJsonPath);
    void InferGroupsFromHitAreas();
    void InferGroupsFromMotionCurves();
    void AutoGenerateVarFloatGroups();
    void AutoPopulateHitAreaParams();

    Csm::CubismMatrix44*        _viewMatrix;
    Csm::csmVector<LAppModel*>  _models;
    Csm::csmVector<LAppModelCubism2*> _modelsCubism2;
    bool _isCubism2;
    std::vector<HitAreaConfig> _hitAreaConfigs;
    int _currentHitAreaIndex = -1;  // tracks enter/exit state for hit areas
    int _pressedHitAreaIndex = -1;  // hit area index at touch-down (for up_mtn on release); -1 after OnHitAreaEnded
    int _lastPressedHitAreaIndex = -1;  // preserves _pressedHitAreaIndex across OnHitAreaEnded reset for OnTap dispatch
    bool _upMtnFired = false;           // true when OnHitAreaEnded played up_mtn; cleared by OnTap
    bool _dragTriggeredHitArea = false;  // drag already triggered hit area action
    std::map<std::string, float> _paramOverrides;  // parameter toggle values
    std::map<std::string, float> _partOverrides;   // part opacity overrides (0.0 or 1.0)
    std::set<std::string> _motionControlledParts;  // Parts with active PartOpacity motion curves — skip VarFloat override
    std::string _currentCostumeName;               // active costume set name
    std::map<std::string, PartFadeState> _partFades;   // active fade transitions
    std::map<std::string, float> _partVisualOpacity;  // current visual opacity per part (for fade start)
    ModelConfig _modelConfig;  // full model config with motion metadata, groups, controllers
    std::map<std::string, std::vector<size_t>> _varFloatGroupMap;  // reverse: var_float name → group indices
    bool _varFloatsDirty = false;  // set when SetVarFloat changes a value, cleared by SyncAllVarFloats
    bool _partOverridesDirty = true;  // set by SetVarFloat, SetGroupIndex, PartFade changes; cleared by ApplyVarFloatPartOverrides
    ControllerEngine _controllerEngine;  // runtime controller engine
    bool _mirror = false;               // mirror model (flip X axis)
    float _rotation = 0.0f;             // rotation angle in degrees (around Z axis)
    float _touchOffsetX = 0.0F;         // floating window X offset (pixels)
    float _touchOffsetY = 0.0F;         // floating window Y offset (pixels)
    float _touchScale = 1.0F;           // model scale factor (from pinch zoom)
    bool _debugHitAreaVisible = false;  // runtime flag for hit area debug visualization
    bool _menuVisible = false;          // true when ActionMenuManager popup is showing
    Csm::csmUint32 _renderWidth = 0;   // cached for SetRenderTargetSize on new models
    Csm::csmUint32 _renderHeight = 0;
    std::string _lastModelPath;         // stored for AutoGenerateHitAreas
    int _selectedModelSlot = -1;        // -1 = show all, >=0 = only render that slot
};
