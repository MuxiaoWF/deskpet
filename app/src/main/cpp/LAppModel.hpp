#pragma once

#include <CubismFramework.hpp>
#include <ICubismModelSetting.hpp>
#include <Type/csmRectF.hpp>
#include <string>

#include "LAppModel_Common.hpp"
#include "CubismFadeController.hpp"
#include "PendingMotion.hpp"

/// Motion asset pool type (aligned with Live2DViewerEX MotionAssetPool).
/// Cubism Native SDK loads .motion3.json into CubismMotion objects (equivalent to
/// Unity AnimationClip). This cache serves as the pre-conversion pipeline:
/// motions are loaded lazily on first use and cached for reuse, equivalent to
/// CubismMotion3Json.ToAnimationClip + MotionAssetPool in the Unity pipeline.
using MotionAssetPool = Csm::csmMap<Csm::csmString, Csm::ACubismMotion*>;

/// Look-at parameter factors from config.mlve
struct LookAtConfig {
    float angleXFactor;
    float angleYFactor;
    float bodyAngleXFactor;
    float eyeBallXFactor;
    float eyeBallYFactor;
    float damping;
    bool autoReset;

    LookAtConfig()
        : angleXFactor(30.0F), angleYFactor(30.0F)
        , bodyAngleXFactor(10.0F), eyeBallXFactor(1.0F)
        , eyeBallYFactor(1.0F), damping(0.5F), autoReset(true) {}
};

/// Expression blend entry for multi-expression support
struct ExpressionBlendEntry {
    Csm::csmString name;
    Csm::ACubismMotion* motion;
    Csm::csmFloat32 weight;

    ExpressionBlendEntry() : motion(nullptr), weight(1.0F) {}
};

enum class FollowMode {
    AUTO_FOLLOW = 0,
    CLICK_FOLLOW = 1,
    DISABLE = 2
};

class LAppModel : public LAppModel_Common
{
public:
    LAppModel();
    virtual ~LAppModel();

    void LoadAssets(const Csm::csmChar* dir, const Csm::csmChar* fileName);
    void ReloadRenderer();
    void Update();
    void Draw(Csm::CubismMatrix44& matrix);

    Csm::CubismMotionQueueEntryHandle StartMotion(const Csm::csmChar* group, Csm::csmInt32 no, Csm::csmInt32 priority, Csm::ACubismMotion::FinishedMotionCallback onFinishedMotionHandler = nullptr, Csm::ACubismMotion::BeganMotionCallback onBeganMotionHandler = nullptr, Csm::csmFloat32 fadeInTime = -1.0F, Csm::csmFloat32 fadeOutTime = -1.0F);
    Csm::CubismMotionQueueEntryHandle StartRandomMotion(const Csm::csmChar* group, Csm::csmInt32 priority, Csm::ACubismMotion::FinishedMotionCallback onFinishedMotionHandler = nullptr, Csm::ACubismMotion::BeganMotionCallback onBeganMotionHandler = nullptr, Csm::csmFloat32 fadeInTime = -1.0F, Csm::csmFloat32 fadeOutTime = -1.0F);

    /// Start an idle motion on the dedicated idle layer manager.
    /// Idle motions run independently of interaction motions and are never interrupted.
    void StartIdleMotion(const Csm::csmChar* group, Csm::csmInt32 no);

    void SetExpression(const Csm::csmChar* expressionID);
    void SetRandomExpression();
    /// Restore the previous expression (borrowed from Live2DViewer)
    void LastExpression();
    Csm::ICubismModelSetting* GetModelSetting() const { return _modelSetting; }

    /// Look up a cached motion by name (e.g., "开关-皮带_0").
    /// Returns nullptr if not cached.
    Csm::ACubismMotion* GetMotion(const Csm::csmString& name) {
        return _motions.IsExist(name) ? _motions[name] : nullptr;
    }

    Csm::csmString GetIdleMotionGroup() const;
    Csm::csmString GetTapMotionGroup() const;

    // Layer-based motion system (aligned with Live2DViewerEX CubismPlayble)
    CubismFadeController& GetFadeController() { return _fadeController; }
    const CubismFadeController& GetFadeController() const { return _fadeController; }

    // Per-model pending motion for pre_mtn chaining
    PendingMotion& GetPendingMotion() { return _pendingMotion; }

    // Dual-mode compatibility: enable legacy frame-lock for CG plot business
    void SetLegacyFrameLockMode(bool enabled) { _legacyFrameLockMode = enabled; }
    bool IsLegacyFrameLockMode() const { return _legacyFrameLockMode; }

    virtual void MotionEventFired(const Live2D::Cubism::Framework::csmString& eventValue);
    virtual Csm::csmBool HitTest(const Csm::csmChar* hitAreaName, Csm::csmFloat32 x, Csm::csmFloat32 y);

    // Config-driven features
    void SetLookAtConfig(const char* json);
    void SetExpressionBlendConfig(const char* json);
    void SetFollowMode(int mode) { _followMode = static_cast<FollowMode>(mode); }

    // Current expression name for state persistence (borrowed from Live2DViewer)
    const char* GetCurrentExpressionId() const { return _currentExpressionId.GetRawString(); }

protected:
    void DoDraw();

private:
    void SetupModel(Csm::ICubismModelSetting* setting);
    void SetupTextures();
    void PreloadMotionGroup(const Csm::csmChar* group);
    void ReleaseMotionGroup(const Csm::csmChar* group);
    void ReleaseMotions();
    /// Evict cached motions that are not active on any layer.
    /// Called after MainMotion layer finishes to reclaim memory.
    void EvictUnusedMotions();
    void ReleaseExpressions();
    void ParseLookAtJson(const char* json);
    Csm::csmInt32 CalculateMaskBufferCount() const;

    Csm::ICubismModelSetting* _modelSetting;
    Csm::csmString _modelHomeDir;
    Csm::csmFloat32 _userTimeSeconds;
    Csm::csmVector<Csm::CubismIdHandle> _eyeBlinkIds;
    Csm::csmVector<Csm::CubismIdHandle> _lipSyncIds;
    MotionAssetPool   _motions;  // Pre-converted motion assets (equivalent to ViewerEX MotionAssetPool)
    Csm::CubismMotionManager* _idleMotionManager = nullptr;  // Dedicated idle layer manager (separate from interaction)
    Csm::csmMap<Csm::csmString, Csm::ACubismMotion*>   _expressions;
    Csm::csmVector<Csm::csmRectF> _hitArea;
    Csm::csmVector<Csm::csmRectF> _userArea;
    const Csm::CubismId* _idParamAngleX;
    const Csm::CubismId* _idParamAngleY;
    const Csm::CubismId* _idParamAngleZ;
    const Csm::CubismId* _idParamBodyAngleX;
    const Csm::CubismId* _idParamEyeBallX;
    const Csm::CubismId* _idParamEyeBallY;
    Csm::csmBool _motionUpdated;
    std::string _activeMotionGroup;   // group of current motion (for interruptable lookup)
    int _activeMotionIndex = -1;      // index of current motion (for interruptable lookup)
    bool _isAnimationEndEventInvoked = false;  // prevents duplicate end callbacks (aligned with ViewerEX)
    float _motionStartRealTime = 0.0F; // wall-clock time when current motion started (for timeout)
    PendingMotion _pendingMotion;       // per-model pending motion for pre_mtn chaining

    // Config-driven features
    LookAtConfig _lookAtConfig;

    // Last expression tracking (borrowed from Live2DViewer)
    Csm::csmString _currentExpressionId;
    Csm::csmString _previousExpressionId;

    FollowMode _followMode = FollowMode::AUTO_FOLLOW;

    // Layer-based motion system
    CubismFadeController _fadeController;

    // Dual-mode compatibility switch (Constraint Note C.1):
    // When true, uses legacy frame-lock hold behavior for CG plot business.
    // When false (default), uses standard ViewerEX-aligned CubismFade system.
    // Legacy frame-lock code is isolated from the standard main process.
    bool _legacyFrameLockMode = false;

    // Last frame's MVP matrix for hit testing (ensures hit test uses same transform as rendering)
    Csm::CubismMatrix44 _lastFrameMVP;

public:
    const Csm::CubismMatrix44& GetLastFrameMVP() const { return _lastFrameMVP; }
};
