#pragma once

#include <CubismFramework.hpp>
#include <Math/CubismMatrix44.hpp>
#include <Math/CubismModelMatrix.hpp>
#include <Type/csmVector.hpp>
#include <Type/csmString.hpp>
#include <functional>

#include "Cubism2MocLoader.hpp"
#include "Cubism2ModelSetting.hpp"
#include "PendingMotion.hpp"

/// Cubism 2 motion curve target types
enum Cubism2CurveTarget {
    Cubism2Target_Model,
    Cubism2Target_Parameter,
    Cubism2Target_Part,
    Cubism2Target_Opacity
};

/// Expression blend modes (borrowed from Live2DViewer)
enum ExpressionBlendMode {
    ExprBlend_Overwrite,  // Replace parameter values
    ExprBlend_Add,        // Add to base parameter values
    ExprBlend_Multiply    // Multiply with base parameter values
};

/// Parsed Cubism 2 motion data
struct Cubism2MotionData {
    // Segment types for motion curve interpolation
    static const int SEG_LINEAR = 0;
    static const int SEG_BEZIER = 1;
    static const int SEG_STEPPED = 2;

    /// A single segment in a motion curve (type + control points)
    struct Segment {
        int type;       // SEG_LINEAR, SEG_BEZIER, SEG_STEPPED
        float t0, v0;   // Start point
        float t1, v1;   // End point (or cp1 for bezier)
        float t2, v2;   // cp1 time/value (bezier only, else unused)
        float t3, v3;   // cp2 time/value (bezier only, else unused)
    };

    struct Curve {
        Cubism2CurveTarget target;
        std::string id;
        Csm::csmFloat32 initTime;
        Csm::csmFloat32 initValue;
        std::vector<Segment> segments;
        // Per-curve fade-in/fade-out times (borrowed from Live2DViewer)
        Csm::csmFloat32 fadeInTime;
        Csm::csmFloat32 fadeOutTime;
        Curve() : initTime(0), initValue(0), fadeInTime(-1.0F), fadeOutTime(-1.0F) {}
    };
    Csm::csmFloat32 duration;
    Csm::csmFloat32 fadeInTime;
    Csm::csmFloat32 fadeOutTime;
    std::vector<Curve> curves;
};

/// Motion listener callback types (borrowed from Live2DViewer)
using MotionBeganCallback = std::function<void(const char* group, Csm::csmInt32 no)>;
using MotionFinishedCallback = std::function<void(const char* group, Csm::csmInt32 no)>;

/// Cubism 2 model implementation.
/// Loads .model.json + .moc, handles motions/expressions, renders via OpenGL.
class LAppModelCubism2 {
public:
    LAppModelCubism2();
    ~LAppModelCubism2();

    void LoadAssets(const Csm::csmChar* dir, const Csm::csmChar* fileName);

    void Update(Csm::csmFloat32 deltaTimeSeconds);
    void Draw(Csm::CubismMatrix44& matrix);

    void StartMotion(const Csm::csmChar* group, Csm::csmInt32 no, Csm::csmInt32 priority, Csm::csmFloat32 fadeInTime = -1.0F, Csm::csmFloat32 fadeOutTime = -1.0F);
    void StartRandomMotion(const Csm::csmChar* group, Csm::csmInt32 priority, Csm::csmFloat32 fadeInTime = -1.0F, Csm::csmFloat32 fadeOutTime = -1.0F);
    void SetExpression(const Csm::csmChar* expressionName);
    void SetRandomExpression();
    /// Restore the previous expression (borrowed from Live2DViewer)
    void LastExpression();

    void SetDragging(Csm::csmFloat32 x, Csm::csmFloat32 y);
    Csm::csmBool HitTest(const Csm::csmChar* hitAreaName, Csm::csmFloat32 x, Csm::csmFloat32 y);

    Cubism2ModelSetting* GetModelSetting() const { return _modelSetting; }
    Csm::CubismModelMatrix* GetModelMatrix() { return &_modelMatrix; }
    Csm::csmFloat32 GetCanvasWidth() const { return _mocLoader ? _mocLoader->GetCanvasWidth() : 1.0F; }
    Csm::csmFloat32 GetCanvasHeight() const { return _mocLoader ? _mocLoader->GetCanvasHeight() : 1.0F; }
    Csm::csmFloat32 GetCanvasMinX() const { return _mocLoader ? _mocLoader->GetCanvasMinX() : 0.0F; }
    Csm::csmFloat32 GetCanvasMaxX() const { return _mocLoader ? _mocLoader->GetCanvasMaxX() : 0.0F; }
    Csm::csmFloat32 GetCanvasMinY() const { return _mocLoader ? _mocLoader->GetCanvasMinY() : 0.0F; }
    Csm::csmFloat32 GetCanvasMaxY() const { return _mocLoader ? _mocLoader->GetCanvasMaxY() : 0.0F; }

    Csm::csmString GetIdleMotionGroup() const;
    Csm::csmString GetTapMotionGroup() const;

    bool IsInitialized() const { return _initialized; }

    /// Get the underlying Cubism 2 moc loader (for direct parameter control).
    Cubism2MocLoader* GetMocLoader() const { return _mocLoader; }

    /// Recreate GL resources after context loss.
    void ReloadRenderer();

    // Current expression name for state persistence
    const char* GetCurrentExpressionName() const;

    // Config-driven features
    void SetLookAtConfig(const char* json);
    void SetExpressionBlendConfig(const char* json);

    // Expression blend mode control (borrowed from Live2DViewer)
    void SetExpressionBlendMode(ExpressionBlendMode mode) { _exprBlendMode = mode; }

    // Motion listener registration (borrowed from Live2DViewer)
    void AddMotionBeganListener(MotionBeganCallback cb) { _motionBeganListeners.push_back(std::move(cb)); }
    void AddMotionFinishedListener(MotionFinishedCallback cb) { _motionFinishedListeners.push_back(std::move(cb)); }

    // Physics weight control (borrowed from Live2DViewer)
    void SetPhysicsWeight(Csm::csmFloat32 weight) { _physicsWeight = weight; _physicsWeightDirty = true; }
    void EnablePhysicsWithFade(Csm::csmFloat32 targetWeight, Csm::csmFloat32 fadeTime);
    void DisablePhysicsWithFade(Csm::csmFloat32 fadeTime);

    // Controller parameter overrides (from ControllerEngine)
    void SetControllerParam(const char* paramId, Csm::csmFloat32 value) { _controllerParams[paramId] = value; }

    // Per-model pending motion for pre_mtn chaining
    PendingMotion& GetPendingMotion() { return _pendingMotion; }

private:
    void LoadMoc(const Csm::csmChar* path);
    void LoadTextures();
    static Cubism2MotionData LoadMotionFile(const Csm::csmChar* path);
    void ApplyMotion(Csm::csmFloat32 time);
    void ApplyExpression();
    void ParseLookAtJson(const char* json);
    void NotifyMotionBegan(const char* group, Csm::csmInt32 no);
    void NotifyMotionFinished(const char* group, Csm::csmInt32 no);
    void UpdatePhysicsFade(Csm::csmFloat32 dt);

    Cubism2ModelSetting* _modelSetting;
    Cubism2MocLoader* _mocLoader;
    Csm::csmString _modelHomeDir;

    // Motion state
    struct MotionState {
        Cubism2MotionData data;
        Csm::csmFloat32 currentTime;
        Csm::csmFloat32 fadeInWeight;
        Csm::csmInt32 priority;
        bool playing;
        std::string group;
        Csm::csmInt32 index;
    };
    MotionState _currentMotion;
    MotionState _previousMotion;
    Csm::csmFloat32 _motionFadeTimer;

    // Motion cache (avoids re-loading from disk on each play)
    std::map<std::string, Cubism2MotionData> _motionCache;

    // Per-model pending motion for pre_mtn chaining
    PendingMotion _pendingMotion;

    // Expression state
    struct ExpressionEntry {
        std::string name;
        std::map<std::string, Csm::csmFloat32> paramValues;
        ExpressionBlendMode blendMode;
        ExpressionEntry() : blendMode(ExprBlend_Overwrite) {}
    };
    std::vector<ExpressionEntry> _expressions;
    Csm::csmInt32 _currentExpressionIndex;
    Csm::csmInt32 _previousExpressionIndex;  // For LastExpression()

    // Expression blend mode (borrowed from Live2DViewer)
    ExpressionBlendMode _exprBlendMode;

    // Per-parameter fade state (borrowed from Live2DViewer)
    struct ParamFadeState {
        Csm::csmFloat32 currentValue;
        Csm::csmFloat32 targetValue;
        Csm::csmFloat32 fadeSpeed;  // units per second
        ParamFadeState() : currentValue(0), targetValue(0), fadeSpeed(0) {}
    };
    std::map<std::string, ParamFadeState> _paramFadeStates;

    // Motion listeners (borrowed from Live2DViewer)
    std::vector<MotionBeganCallback> _motionBeganListeners;
    std::vector<MotionFinishedCallback> _motionFinishedListeners;

    // Physics fade control (borrowed from Live2DViewer)
    Csm::csmFloat32 _physicsWeight;
    Csm::csmFloat32 _physicsTargetWeight;
    Csm::csmFloat32 _physicsFadeSpeed;
    bool _physicsWeightDirty;

    // Look-at config
    struct LookAtParams {
        float angleXFactor;
        float angleYFactor;
        float bodyAngleXFactor;
        float eyeBallXFactor;
        float eyeBallYFactor;
        LookAtParams() : angleXFactor(30.0F), angleYFactor(30.0F),
            bodyAngleXFactor(10.0F), eyeBallXFactor(1.0F), eyeBallYFactor(1.0F) {}
    };
    LookAtParams _lookAtParams;

    Csm::CubismModelMatrix _modelMatrix;
    Csm::csmFloat32 _dragX, _dragY;
    bool _initialized;

    // Controller parameter overrides (from ControllerEngine, applied each frame)
    std::map<std::string, Csm::csmFloat32> _controllerParams;
};
