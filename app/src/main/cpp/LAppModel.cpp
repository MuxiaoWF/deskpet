#include "LAppModel.hpp"
#include <cmath>
#include <fstream>
#include <set>
#include <vector>
#include <CubismModelSettingJson.hpp>
#include <Motion/CubismMotion.hpp>
#include <Physics/CubismPhysics.hpp>
#include <CubismDefaultParameterId.hpp>
#include <Model/CubismMoc.hpp>
#include <Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp>
#include <Utils/CubismString.hpp>
#include <Id/CubismIdManager.hpp>
#include <Motion/CubismMotionQueueEntry.hpp>
#include "LAppDefine.hpp"
#include "LAppPal.hpp"
#include "LAppTextureManager.hpp"
#include "LAppDelegate.hpp"
#include "LAppLive2DManager.hpp"
#include "ControllerEngine.hpp"
#include "MotionSequencer.hpp"
#include "MotionGroupUtils.hpp"
#include "MotionLayer.hpp"
#include "Motion/CubismBreathUpdater.hpp"
#include "Motion/CubismLookUpdater.hpp"
#include "Motion/CubismExpressionUpdater.hpp"
#include "Motion/CubismEyeBlinkUpdater.hpp"
#include "Motion/CubismPhysicsUpdater.hpp"
#include "Motion/CubismPoseUpdater.hpp"
#include "third_party/nlohmann/json.hpp"

using namespace Live2D::Cubism::Framework;
using namespace Live2D::Cubism::Framework::DefaultParameterId;
using namespace LAppDefine;

LAppModel::LAppModel()
    : LAppModel_Common()
    , _modelSetting(nullptr)
    , _userTimeSeconds(0.0F)
    , _motionUpdated(false)
    , _idParamAngleX(nullptr)
    , _idParamAngleY(nullptr)
    , _idParamAngleZ(nullptr)
    , _idParamBodyAngleX(nullptr)
    , _idParamEyeBallX(nullptr)
    , _idParamEyeBallY(nullptr)
{
    if (DebugLogEnable)
    {
        _debugMode = true;
    }

    if (CubismFramework::IsInitialized())
    {
        _idParamAngleX = CubismFramework::GetIdManager()->GetId(ParamAngleX);
        _idParamAngleY = CubismFramework::GetIdManager()->GetId(ParamAngleY);
        _idParamAngleZ = CubismFramework::GetIdManager()->GetId(ParamAngleZ);
        _idParamBodyAngleX = CubismFramework::GetIdManager()->GetId(ParamBodyAngleX);
        _idParamEyeBallX = CubismFramework::GetIdManager()->GetId(ParamEyeBallX);
        _idParamEyeBallY = CubismFramework::GetIdManager()->GetId(ParamEyeBallY);
    }
}

LAppModel::~LAppModel()
{
    // Null out BaseIdle layer's manager before Release() to prevent double-free.
    // _idleMotionManager is owned by us, but Release() would also try to delete it.
    _fadeController.GetLayer(MotionLayer::BaseIdle).manager = nullptr;
    if (_idleMotionManager) {
        delete _idleMotionManager;
        _idleMotionManager = nullptr;
    }
    _fadeController.Release(_motionManager);
    ReleaseMotions();
    ReleaseExpressions();

    if (_modelSetting != nullptr)
    {
        delete _modelSetting;
    }
}

void LAppModel::LoadAssets(const csmChar* dir, const csmChar* fileName)
{
    _modelHomeDir = dir;

    csmSizeInt size;
    const csmString path = csmString(dir) + fileName;

    csmByte* buffer = LAppPal::CreateBuffer(path.GetRawString(), &size);
    if (buffer == nullptr)
    {
        LAppPal::PrintLogLn("[APP]LoadAssets: failed to read model settings: %s", path.GetRawString());
        return;
    }
    auto* setting = new CubismModelSettingJson(buffer, size);
    LAppPal::DeleteBuffer(buffer, path.GetRawString());

    SetupModel(setting);

    if (_model == nullptr)
    {
        LAppPal::PrintLogLn("[APP]LoadAssets: _model is nullptr after SetupModel");
        return;
    }

    const csmInt32 maskBufferCount = CalculateMaskBufferCount();
    const int w = LAppDelegate::GetInstance()->GetWindowWidth();
    const int h = LAppDelegate::GetInstance()->GetWindowHeight();
    if (w == 0 || h == 0)
    {
        LAppPal::PrintLogLn("[APP]WARNING: CreateRenderer deferred - zero dimensions (w=%d h=%d). Will be created in ReloadRenderer.", w, h);
        return;
    }
    CreateRenderer(w, h, maskBufferCount);
    SetupTextures();
}

void LAppModel::SetupModel(ICubismModelSetting* setting)
{
    _updating = true;
    _initialized = false;

    _modelSetting = setting;

    csmByte* buffer;
    csmSizeInt size;

    if (strcmp(_modelSetting->GetModelFileName(), "") != 0)
    {
        csmString path = _modelSetting->GetModelFileName();
        path = _modelHomeDir + path;

        buffer = LAppPal::CreateBuffer(path.GetRawString(), &size);
        if (buffer == nullptr)
        {
            LAppPal::PrintLogLn("[APP]SetupModel: failed to read moc file");
        }
        else
        {
            // Validate moc3 header before calling Core (csmGetMocVersion/csmReviveMocInPlace
            // crash on incompatible moc3 files). Read header directly to avoid Core crash.
            // moc3 header format: [4 bytes magic "MOC3"] [4 bytes version LE] ...
            bool headerValid = false;
            csmUint32 fileVersion = 0;
            if (size >= 8 && buffer[0]=='M' && buffer[1]=='O' && buffer[2]=='C' && buffer[3]=='3')
            {
                memcpy(&fileVersion, buffer + 4, 4);  // version at offset 4 (little-endian)
                headerValid = true;
            }

            // Map version number to csmMocVersion enum for comparison
            // csmMocVersion_53=6 is the latest in Core 5.3
            static const csmUint32 kMaxFileVersion = 6;  // csmMocVersion_53
            if (headerValid)
            {
                LAppPal::PrintLogLn("[APP]SetupModel: moc3 header version=%d, max supported=%d", fileVersion, kMaxFileVersion);
                if (fileVersion > kMaxFileVersion)
                {
                    LAppPal::PrintLogLn("[APP]ERROR: moc3 version %d exceeds Core 5.3 support (max %d). Model skipped.", fileVersion, kMaxFileVersion);
                    LAppPal::DeleteBuffer(buffer, path.GetRawString());
                    return;
                }
            }
            else
            {
                LAppPal::PrintLogLn("[APP]WARNING: moc3 header not recognized, attempting load anyway");
            }

            // Note: csmHasMocConsistency() is NOT called here — it can corrupt the heap
            // metadata for large files (>1MB), causing subsequent CSM_MALLOC_ALIGNED to
            // crash with SIGSEGV. Header validation (magic + version) is sufficient.

            LoadModel(buffer, size);
            LAppPal::DeleteBuffer(buffer, path.GetRawString());

            if (_model == nullptr)
            {
                LAppPal::PrintLogLn("[APP]ERROR: SetupModel failed - moc3 incompatible or corrupted (headerVer=%d)", fileVersion);
                return;
            }
        }
    }
    else
    {
        LAppPal::PrintLogLn("[APP]SetupModel: no model file specified");
    }

    if (_modelSetting->GetExpressionCount() > 0)
    {
        const csmInt32 count = _modelSetting->GetExpressionCount();
        for (csmInt32 i = 0; i < count; i++)
        {
            const csmString name = _modelSetting->GetExpressionName(i);
            csmString path = _modelSetting->GetExpressionFileName(i);
            path = _modelHomeDir + path;

            buffer = LAppPal::CreateBuffer(path.GetRawString(), &size);
            ACubismMotion* motion = LoadExpression(buffer, size, name.GetRawString());

            if (motion)
            {
                if (_expressions[name] != nullptr)
                {
                    ACubismMotion::Delete(_expressions[name]);
                    _expressions[name] = nullptr;
                }
                _expressions[name] = motion;
            }

            LAppPal::DeleteBuffer(buffer, path.GetRawString());
        }

        CubismExpressionUpdater* expression = CSM_NEW CubismExpressionUpdater(*_expressionManager, 150);
        _updateScheduler.AddUpdatableList(expression);
    }

    if (strcmp(_modelSetting->GetPhysicsFileName(), "") != 0)
    {
        csmString path = _modelSetting->GetPhysicsFileName();
        path = _modelHomeDir + path;

        buffer = LAppPal::CreateBuffer(path.GetRawString(), &size);
        LoadPhysics(buffer, size);
        if (_physics != nullptr)
        {
            CubismPhysicsUpdater* physics = CSM_NEW CubismPhysicsUpdater(*_physics);
            _updateScheduler.AddUpdatableList(physics);
        }
        LAppPal::DeleteBuffer(buffer, path.GetRawString());
    }

    if (strcmp(_modelSetting->GetPoseFileName(), "") != 0)
    {
        csmString path = _modelSetting->GetPoseFileName();
        path = _modelHomeDir + path;

        buffer = LAppPal::CreateBuffer(path.GetRawString(), &size);
        LoadPose(buffer, size);
        if (_pose != nullptr)
        {
            CubismPoseUpdater* pose = CSM_NEW CubismPoseUpdater(*_pose, 100);
            _updateScheduler.AddUpdatableList(pose);
        }
        LAppPal::DeleteBuffer(buffer, path.GetRawString());
    }

    // Check controller enable flags from config.mlve
    const auto& modelConfig = LAppLive2DManager::GetInstance()->GetModelConfig();
    const auto& controllers = modelConfig.controllers;

    {
        if (_modelSetting->GetEyeBlinkParameterCount() > 0 && controllers.eye_blink_enabled)
        {
            _eyeBlink = CubismEyeBlink::Create(_modelSetting);

            CubismEyeBlinkUpdater* eyeBlink = CSM_NEW CubismEyeBlinkUpdater(_motionUpdated, *_eyeBlink);
            _updateScheduler.AddUpdatableList(eyeBlink);
        }
    }

    if (controllers.auto_breath_enabled)
    {
        _breath = CubismBreath::Create();

        csmVector<CubismBreath::BreathParameterData> breathParameters;

        breathParameters.PushBack(CubismBreath::BreathParameterData(_idParamAngleX, 0.0F, 15.0F, 6.5345F, 0.5F));
        breathParameters.PushBack(CubismBreath::BreathParameterData(_idParamAngleY, 0.0F, 8.0F, 3.5345F, 0.5F));
        breathParameters.PushBack(CubismBreath::BreathParameterData(_idParamAngleZ, 0.0F, 10.0F, 5.5345F, 0.5F));
        breathParameters.PushBack(CubismBreath::BreathParameterData(_idParamBodyAngleX, 0.0F, 4.0F, 15.5345F, 0.5F));
        breathParameters.PushBack(CubismBreath::BreathParameterData(CubismFramework::GetIdManager()->GetId(ParamBreath), 0.5F, 0.5F, 3.2345F, 0.5F));

        _breath->SetParameters(breathParameters);

        CubismBreathUpdater* breath = CSM_NEW CubismBreathUpdater(*_breath);
        _updateScheduler.AddUpdatableList(breath);
    }

    if (strcmp(_modelSetting->GetUserDataFile(), "") != 0)
    {
        csmString path = _modelSetting->GetUserDataFile();
        path = _modelHomeDir + path;
        buffer = LAppPal::CreateBuffer(path.GetRawString(), &size);
        LoadUserData(buffer, size);
        LAppPal::DeleteBuffer(buffer, path.GetRawString());
    }

    {
        const csmInt32 eyeBlinkIdCount = _modelSetting->GetEyeBlinkParameterCount();
        for (csmInt32 i = 0; i < eyeBlinkIdCount; ++i)
        {
            _eyeBlinkIds.PushBack(_modelSetting->GetEyeBlinkParameterId(i));
        }
    }

    {
        const csmInt32 lipSyncIdCount = _modelSetting->GetLipSyncParameterCount();
        for (csmInt32 i = 0; i < lipSyncIdCount; ++i)
        {
            _lipSyncIds.PushBack(_modelSetting->GetLipSyncParameterId(i));
        }
    }

    if (controllers.mouse_tracking_enabled)
    {
        _look = CubismLook::Create();

        csmVector<CubismLook::LookParameterData> lookParameters;

        lookParameters.PushBack(CubismLook::LookParameterData(_idParamAngleX, 30.0F));
        lookParameters.PushBack(CubismLook::LookParameterData(_idParamAngleY, 0.0F, 30.0F));
        lookParameters.PushBack(CubismLook::LookParameterData(_idParamAngleZ, 0.0F, 0.0F, -30.0F));
        lookParameters.PushBack(CubismLook::LookParameterData(_idParamBodyAngleX, 10.0F));
        lookParameters.PushBack(CubismLook::LookParameterData(_idParamEyeBallX, 1.0F));
        lookParameters.PushBack(CubismLook::LookParameterData(_idParamEyeBallY, 0.0F, 1.0F));

        _look->SetParameters(lookParameters);

        CubismLookUpdater* look = CSM_NEW CubismLookUpdater(*_look, *_dragManager);
        _updateScheduler.AddUpdatableList(look);
    }

    _updateScheduler.SortUpdatableList();

    if (_modelSetting == nullptr || _modelMatrix == nullptr)
    {
        LAppPal::PrintLogLn("Failed to SetupModel().");
        return;
    }

    csmMap<csmString, csmFloat32> layout;
    _modelSetting->GetLayoutMap(layout);
    _modelMatrix->SetupFromLayout(layout);

    _model->SaveParameters();

    // Motions are loaded lazily on first use in StartMotion() and cached in _motions.
    // This avoids loading all motion files upfront during model setup.

    _motionManager->StopAllMotions();

    // Initialize layer-based fade controller (aligned with Live2DViewerEX)
    // MainMotion layer shares the SDK's _motionManager for actual motion playback
    _fadeController.Initialize(_motionManager);

    // Create dedicated idle motion manager (separate from interaction manager).
    // Idle runs on its own CubismMotionManager so interaction motions never kill it.
    _idleMotionManager = new Csm::CubismMotionManager();
    _fadeController.GetLayer(MotionLayer::BaseIdle).manager = _idleMotionManager;
    _fadeController.GetLayer(MotionLayer::BaseIdle).fadeWeight = 1.0F;

    _updating = false;
    _initialized = true;
}

void LAppModel::PreloadMotionGroup(const csmChar* group)
{
    const csmInt32 count = _modelSetting->GetMotionCount(group);

    for (csmInt32 i = 0; i < count; i++)
    {
        const csmString name = Utils::CubismString::GetFormatedString("%s_%d", group, i);
        csmString path = _modelSetting->GetMotionFileName(group, i);
        path = _modelHomeDir + path;

        csmByte* buffer;
        csmSizeInt size;
        buffer = LAppPal::CreateBuffer(path.GetRawString(), &size);
        auto* tmpMotion = static_cast<CubismMotion*>(LoadMotion(buffer, size, name.GetRawString(), nullptr, nullptr, _modelSetting, group, i));

        if (tmpMotion)
        {
            tmpMotion->SetEffectIds(_eyeBlinkIds, _lipSyncIds);

            if (_motions[name] != nullptr)
            {
                ACubismMotion::Delete(_motions[name]);
            }
            _motions[name] = tmpMotion;
        }

        LAppPal::DeleteBuffer(buffer, path.GetRawString());
    }
}

void LAppModel::ReleaseMotionGroup(const csmChar* group)
{
    const csmInt32 count = _modelSetting->GetMotionCount(group);
    for (csmInt32 i = 0; i < count; i++)
    {
        const csmString name = Utils::CubismString::GetFormatedString("%s_%d", group, i);
        if (_motions.IsExist(name)) {
            ACubismMotion::Delete(_motions[name]);
            _motions.Erase(name);
        }
    }
}

void LAppModel::ReleaseMotions()
{
    for (csmMap<csmString, ACubismMotion*>::const_iterator iter = _motions.Begin(); iter != _motions.End(); ++iter)
    {
        ACubismMotion::Delete(iter->Second);
    }
    _motions.Clear();
}

void LAppModel::ReleaseExpressions()
{
    for (csmMap<csmString, ACubismMotion*>::const_iterator iter = _expressions.Begin(); iter != _expressions.End(); ++iter)
    {
        ACubismMotion::Delete(iter->Second);
    }
    _expressions.Clear();
}

void LAppModel::EvictUnusedMotions()
{
    // Build set of motion keys currently active on ALL layers.
    std::set<std::string> activeKeys;
    for (int layer = 0; layer < static_cast<int>(MotionLayer::Count); layer++) {
        const auto& state = _fadeController.GetLayer(static_cast<MotionLayer>(layer));
        if (!state.activeGroup.empty() && state.activeIndex >= 0) {
            csmString key = Utils::CubismString::GetFormatedString("%s_%d",
                state.activeGroup.c_str(), state.activeIndex);
            activeKeys.insert(key.GetRawString());
        }
    }

    // Collect keys of cached motions that are not active on any layer.
    csmVector<csmString> toDelete;
    for (auto it = _motions.Begin(); it != _motions.End(); ++it) {
        if (activeKeys.find(it->First.GetRawString()) == activeKeys.end()) {
            toDelete.PushBack(it->First);
        }
    }

    // Delete motion objects and remove from cache.
    for (csmUint32 i = 0; i < toDelete.GetSize(); i++) {
        if (_motions.IsExist(toDelete[i])) {
            ACubismMotion::Delete(_motions[toDelete[i]]);
            _motions.Erase(toDelete[i]);
        }
    }

    // Hard cap: evict oldest cached motions if cache exceeds limit
    constexpr int MAX_MOTION_CACHE = 30;
    if (static_cast<int>(_motions.GetSize()) > MAX_MOTION_CACHE) {
        int excess = static_cast<int>(_motions.GetSize()) - MAX_MOTION_CACHE;
        csmVector<csmString> capEvict;
        for (auto it = _motions.Begin(); it != _motions.End() && static_cast<int>(capEvict.GetSize()) < excess; ++it) {
            if (activeKeys.find(it->First.GetRawString()) == activeKeys.end()) {
                capEvict.PushBack(it->First);
            }
        }
        for (csmUint32 i = 0; i < capEvict.GetSize(); i++) {
            if (_motions.IsExist(capEvict[i])) {
                ACubismMotion::Delete(_motions[capEvict[i]]);
                _motions.Erase(capEvict[i]);
            }
        }
        if (capEvict.GetSize() > 0) {
            LAppPal::PrintLogLn("[MotionAssetPool] Hard cap: evicted %d motions, %d cached (limit=%d)",
                (int)capEvict.GetSize(), (int)_motions.GetSize(), MAX_MOTION_CACHE);
        }
    }

    if (toDelete.GetSize() > 0) {
        LAppPal::PrintLogLn("[MotionAssetPool] Evicted %d unused motions, %d cached",
            (int)toDelete.GetSize(), (int)_motions.GetSize());
    }
}

csmString LAppModel::GetIdleMotionGroup() const
{
    return MotionGroupUtils::FindIdleGroup(_modelSetting, false);
}

csmString LAppModel::GetTapMotionGroup() const
{
    return MotionGroupUtils::FindTapGroup(_modelSetting, true);
}

void LAppModel::Update()
{
    if (_model == nullptr)
    {
        return;
    }

    const csmFloat32 deltaTimeSeconds = LAppPal::GetDeltaTime();
    _userTimeSeconds += deltaTimeSeconds;

    _motionUpdated = false;

    _model->LoadParameters();

    // ========== Phase 1: Motion managers (apply motion curves to parameters) ==========
    // Dual-manager: MainMotion (interaction) + BaseIdle (idle) run on separate managers.
    // Interaction layer
    csmBool motionFinished = _motionManager->IsFinished();
    if (motionFinished)
    {
        // Motion end handling aligned with Live2DViewerEX CubismFade:
        // - Priority auto-cleared in end callback
        // - Idle transition handled by BaseIdle layer (always visible)
        _motionManager->SetReservePriority(PriorityNone);

        auto& mainLayer = _fadeController.GetLayer(MotionLayer::MainMotion);
        mainLayer.isFinished = true;
        mainLayer.priority = 0;

        if (!_isAnimationEndEventInvoked) {
            _isAnimationEndEventInvoked = true;
            LAppPal::PrintLogLn("[Toggle] Update: motionFinished group=[%s] idx=%d (evicting unused motions)",
                _activeMotionGroup.c_str(), _activeMotionIndex);
            _activeMotionGroup.clear();
            _activeMotionIndex = -1;
            EvictUnusedMotions();
        }
    }
    else
    {
        _motionUpdated = _motionManager->UpdateMotion(_model, deltaTimeSeconds);
    }

    // Idle layer (independent manager — never interrupted by interaction)
    if (_idleMotionManager) {
        _idleMotionManager->UpdateMotion(_model, deltaTimeSeconds);
    }

    // Priority stuck fallback: if queue is empty but reserve priority is stuck, clear it.
    // Prevents permanent motion rejection after Force motions or timing edge cases.
    {
        auto* entries = _motionManager->GetCubismMotionQueueEntries();
        bool hasActive = false;
        for (csmUint32 i = 0; i < entries->GetSize(); i++) {
            if (entries->At(i) && !entries->At(i)->IsFinished()) {
                hasActive = true;
                break;
            }
        }
        if (!hasActive) {
            _motionManager->SetReservePriority(PriorityNone);
        }
    }

    _model->SaveParameters();
    _opacity = _model->GetModelOpacity();

    // ========== Phase 2: SDK updaters (ViewerEX order) ==========
    // Pose(100) → Expression(150) → EyeBlink(200) → Look(400) → Breath(500) → LipSync(motion) → Physics(600)
    _updateScheduler.OnLateUpdate(_model, deltaTimeSeconds);

    // ========== Phase 3: Fade evaluation (AFTER motion + SDK updaters) ==========
    // Aligned with ViewerEX CubismFadeController.OnLateUpdate which runs after the
    // motion + SDK pipeline. Eliminates 1-frame lag from pre-update evaluation.
    _fadeController.UpdateFades(deltaTimeSeconds);

    // ========== Phase 4: Layer integration (non-MainMotion layers) ==========
    for (int layerIdx = static_cast<int>(MotionLayer::EyeBlink);
         layerIdx <= static_cast<int>(MotionLayer::Effect); layerIdx++) {
        auto& layer = _fadeController.GetLayer(static_cast<MotionLayer>(layerIdx));
        if (layer.manager && layer.fadeWeight > 0.0F && !layer.isFinished) {
            layer.manager->UpdateMotion(_model, deltaTimeSeconds);
        }
    }

    // ========== Phase 5: Handle fade-out waiting ==========
    auto& mainLayer = _fadeController.GetLayer(MotionLayer::MainMotion);
    if (mainLayer.isFadeOutWaiting && !mainLayer.isFading && mainLayer.fadeWeight <= 0.0F) {
        std::string pendingGroup;
        int pendingIndex, pendingPriority;
        _fadeController.GetPendingMotion(MotionLayer::MainMotion, pendingGroup, pendingIndex, pendingPriority);
        _fadeController.ClearPendingMotion(MotionLayer::MainMotion);
        if (!pendingGroup.empty()) {
            StartMotion(pendingGroup.c_str(), pendingIndex, pendingPriority);
        }
    }

    // ========== Phase 6: Controller engine ==========
    {
        auto& engine = LAppLive2DManager::GetInstance()->GetControllerEngine();
        engine.Update(_model, deltaTimeSeconds);
    }

    // ========== Phase 7: Follow mode override ==========
    if (_followMode == FollowMode::DISABLE && _look != nullptr) {
        _model->SetParameterValue(_idParamAngleX, 0.0F);
        _model->SetParameterValue(_idParamAngleY, 0.0F);
        _model->SetParameterValue(_idParamBodyAngleX, 0.0F);
        _model->SetParameterValue(_idParamEyeBallX, 0.0F);
        _model->SetParameterValue(_idParamEyeBallY, 0.0F);
    }

    // ========== Phase 8: Parameter toggle overrides ==========
    // Applied AFTER all updaters so discrete costume/accessory params are not overridden
    // by physics, expression, or motion.
    // IMPORTANT: Skip overrides for parameters currently being animated by an active
    // motion. This allows toggle motion animations to play visually. The override is
    // restored by OnFinishedInternal when the motion completes.
    {
        const auto& overrides = LAppLive2DManager::GetInstance()->GetParamOverrides();

        // Collect parameter IDs from the currently active motion (if any).
        // These params should NOT be overridden — let the motion animate them.
        std::set<std::string> activeMotionParams;
        if (!_motionManager->IsFinished()) {
            auto* entries = _motionManager->GetCubismMotionQueueEntries();
            for (csmUint32 i = 0; i < entries->GetSize(); i++) {
                auto* entry = entries->At(i);
                if (entry && !entry->IsFinished()) {
                    auto* cubismMotion = dynamic_cast<CubismMotion*>(entry->GetCubismMotion());
                    if (cubismMotion) {
                        auto paramTargets = cubismMotion->GetParameterTargets();
                        for (auto* pid : paramTargets) {
                            activeMotionParams.insert(pid->GetString().GetRawString());
                        }
                    }
                }
            }
        }

        for (const auto& kv : overrides)
        {
            // Skip if this parameter is being animated by the active motion
            if (activeMotionParams.count(kv.first)) {
                continue;
            }

            CubismIdHandle paramId = CubismFramework::GetIdManager()->GetId(kv.first.c_str());
            csmInt32 paramIdx = _model->GetParameterIndex(paramId);
            if (paramIdx >= 0) {
                float oldVal = _model->GetParameterValue(paramIdx);
                _model->SetParameterValue(paramIdx, kv.second);
            } else {
                // Log missing params once per model load (cleared by ReleaseAllModel)
                static std::set<std::string> s_loggedMissing;
                if (s_loggedMissing.find(kv.first) == s_loggedMissing.end()) {
                    s_loggedMissing.insert(kv.first);
                    LAppPal::PrintLogLn("[Toggle] WARNING: param [%s] NOT FOUND in model (override=%.3f ignored)", kv.first.c_str(), kv.second);
                }
            }
        }
    }

    // ========== Phase 9: VarFloat-driven part overrides with Pose blending ==========
    {
        auto* mgr = LAppLive2DManager::GetInstance();
        const auto& partOverrides = mgr->GetPartOverrides();
        if (!partOverrides.empty()) {
            const csmInt32 partCount = _model->GetPartCount();
            const csmInt32 drawableCount = _model->GetDrawableCount();

            for (const auto& kv : partOverrides) {
                CubismIdHandle id = CubismFramework::GetIdManager()->GetId(kv.first.c_str());
                const float varFloatVis = kv.second;

                csmInt32 partIndex = _model->GetPartIndex(id);
                if (partIndex >= 0 && partIndex < partCount) {
                    float poseOpacity = _model->GetPartOpacity(partIndex);
                    _model->SetPartOpacity(partIndex, poseOpacity * varFloatVis);
                    LAppPal::PrintLogLn("[VarFloat] Phase9: part=[%s] partIdx=%d poseOp=%.2f varFloat=%.2f -> %.2f",
                        kv.first.c_str(), partIndex, poseOpacity, varFloatVis, poseOpacity * varFloatVis);
                    continue;
                }

                bool found = false;
                for (csmInt32 d = 0; d < drawableCount; d++) {
                    if (_model->GetDrawableId(d) == id) {
                        csmInt32 parentPart = _model->GetDrawableParentPartIndex(d);
                        if (parentPart >= 0 && parentPart < partCount) {
                            float poseOpacity = _model->GetPartOpacity(parentPart);
                            _model->SetPartOpacity(parentPart, poseOpacity * varFloatVis);
                        } else {
                            CubismIdHandle paramId = CubismFramework::GetIdManager()->GetId(kv.first.c_str());
                            _model->SetParameterValue(paramId, varFloatVis);
                        }
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    CubismIdHandle paramId = CubismFramework::GetIdManager()->GetId(kv.first.c_str());
                    csmInt32 paramIdx = _model->GetParameterIndex(paramId);
                    if (paramIdx >= 0) {
                        _model->SetParameterValue(paramIdx, varFloatVis);
                        LAppPal::PrintLogLn("[VarFloat] Phase9: param=[%s] paramIdx=%d -> %.2f (fallback)",
                            kv.first.c_str(), paramIdx, varFloatVis);
                    } else {
                        LAppPal::PrintLogLn("[VarFloat] Phase9: [%s] NOT FOUND as part, drawable, or param! varFloat=%.2f",
                            kv.first.c_str(), varFloatVis);
                    }
                }
            }
        }
    }

    _model->Update();
}

CubismMotionQueueEntryHandle LAppModel::StartMotion(const csmChar* group, csmInt32 no, csmInt32 priority, ACubismMotion::FinishedMotionCallback onFinishedMotionHandler, ACubismMotion::BeganMotionCallback onBeganMotionHandler, csmFloat32 fadeInTime, csmFloat32 fadeOutTime)
{
    // Reset animation end event flag for new motion
    _isAnimationEndEventInvoked = false;

    // Update MainMotion layer state in fade controller
    auto& mainLayer = _fadeController.GetLayer(MotionLayer::MainMotion);
    mainLayer.isFinished = false;
    mainLayer.isAnimationEndEventInvoked = false;
    mainLayer.priority = priority;
    mainLayer.activeGroup = group;
    mainLayer.activeIndex = no;

    if (priority > PriorityIdle) {
        const csmChar* motionFile = (_modelSetting && _modelSetting->GetMotionCount(group) > no)
            ? _modelSetting->GetMotionFileName(group, no) : "null";
        LAppPal::PrintLogLn("[Motion] StartMotion: priority=%d group=[%s] no=%d file=[%s]",
            priority, group, no, motionFile);
    }

    if (priority == PriorityForce)
    {
        _motionManager->SetReservePriority(priority);
    }
    else
    {
        // Set _activeMotionGroup BEFORE ignorable check so the check always
        // operates on correct data (not stale values from a previous motion).
        std::string prevActiveGroup = _activeMotionGroup;
        int prevActiveIndex = _activeMotionIndex;
        _activeMotionGroup = std::string(group);
        _activeMotionIndex = no;

        // Check if current motion is non-ignorable (aligned with ViewerEX semantics).
        // ignorable=true means this motion can be silently skipped if another is requested.
        // ignorable=false means this motion must play to completion (or timeout).
        if (!prevActiveGroup.empty() && prevActiveIndex >= 0) {
            const MotionMeta* currentMeta = LAppLive2DManager::GetInstance()->FindMotionMeta(prevActiveGroup, prevActiveIndex);
            if (currentMeta && !currentMeta->ignorable) {
                const float maxNonInterruptableDuration = 2.0F;
                if (_userTimeSeconds - _motionStartRealTime < maxNonInterruptableDuration) {
                    _activeMotionGroup = prevActiveGroup;
                    _activeMotionIndex = prevActiveIndex;
                    LAppPal::PrintLogLn("[APP]Motion blocked: current is non-ignorable (%.1fs elapsed)",
                        _userTimeSeconds - _motionStartRealTime);
                    return InvalidMotionQueueEntryHandleValue;
                }
                LAppPal::PrintLogLn("[APP]Motion forced-interrupt: non-ignorable exceeded %.1fs limit",
                    maxNonInterruptableDuration);
            }
            _motionManager->SetReservePriority(PriorityNone);
        }
        if (!_motionManager->ReserveMotion(priority))
        {
            _activeMotionGroup = prevActiveGroup;
            _activeMotionIndex = prevActiveIndex;
            LAppPal::PrintLogLn("[APP]Motion rejected: ReserveMotion(%d) failed for [%s_%d] (reservePriority may be stuck)",
                priority, group, no);
            return InvalidMotionQueueEntryHandleValue;
        }
    }

    // Load motion (lazy: cached in _motions after first load)
    const csmString fileName = _modelSetting->GetMotionFileName(group, no);
    const csmString name = Utils::CubismString::GetFormatedString("%s_%d", group, no);
    CubismMotion* motion = static_cast<CubismMotion*>(_motions[name.GetRawString()]);
    csmBool autoDelete = false;

    if (motion == nullptr)
    {
        csmString path = fileName;
        path = _modelHomeDir + path;

        csmByte* buffer;
        csmSizeInt size;
        buffer = LAppPal::CreateBuffer(path.GetRawString(), &size);
        motion = static_cast<CubismMotion*>(LoadMotion(buffer, size, nullptr, onFinishedMotionHandler, onBeganMotionHandler, _modelSetting, group, no));

        if (motion)
        {
            motion->SetEffectIds(_eyeBlinkIds, _lipSyncIds);
            _motions[name.GetRawString()] = motion;
            autoDelete = false;
        }
        else
        {
            LAppPal::PrintLogLn("[APP]LoadMotion: [%s_%d] FAILED (file=%s size=%d)",
                group, no, path.GetRawString(), (int)size);
        }

        LAppPal::DeleteBuffer(buffer, path.GetRawString());
    }
    else
    {
        motion->SetBeganMotionHandler(onBeganMotionHandler);
        motion->SetFinishedMotionHandler(onFinishedMotionHandler);
    }

    if (motion != nullptr)
    {
        MotionSequencer::SetupMotionCallbacks(static_cast<void*>(motion), std::string(group), no,
                                              onFinishedMotionHandler, onBeganMotionHandler);
    }

    // Single metadata lookup: fade, weight, loop mode, expression trigger.
    // _activeMotionGroup is already set above; cache meta for expression check.
    const MotionMeta* startMeta = nullptr;
    bool metaRequestsLoop = false;
    if (motion != nullptr)
    {
        const auto& motionMetas = LAppLive2DManager::GetInstance()->GetMotionMetas();
        auto groupIt = motionMetas.find(std::string(group));
        if (groupIt != motionMetas.end()) {
            for (const auto& m : groupIt->second) {
                if (m.motionIndex == no) {
                    startMeta = &m;

                    // Fade time priority: explicit config > .motion3.json meta > SDK default
                    if (fadeInTime >= 0.0F) {
                        motion->SetFadeInTime(fadeInTime);
                    } else if (m.fade_in > 0) {
                        motion->SetFadeInTime(m.fade_in / 1000.0F);
                    }
                    // else: keep .motion3.json meta FadeInTime (already in motion from LoadMotion)

                    if (fadeOutTime >= 0.0F) {
                        motion->SetFadeOutTime(fadeOutTime);
                    } else if (m.fade_out > 0) {
                        motion->SetFadeOutTime(m.fade_out / 1000.0F);
                    }

                    if (m.blend_weight != 1.0F) {
                        motion->SetWeight(m.blend_weight);
                    }

                    // blend_mode: store in layer state (do NOT overwrite fadeWeight)
                    // Aligned with ViewerEX: blend weight and fade weight are separate concepts.
                    {
                        auto& layer = _fadeController.GetLayer(MotionLayer::MainMotion);
                        layer.blendMode = m.blend_mode;
                        layer.blendWeight = (m.blend_mode == 1) ? m.blend_weight : 1.0F;
                    }

                    if (m.wrap_mode == 1) {
                        motion->SetLoop(true);
                        metaRequestsLoop = true;
                    }
                    break;
                }
            }
        }
        // Explicit fade times override everything
        if (fadeInTime >= 0.0F) motion->SetFadeInTime(fadeInTime);
        if (fadeOutTime >= 0.0F) motion->SetFadeOutTime(fadeOutTime);

        // Store per-curve fade overrides from .motion3.json in layer state
        // (extracted from the motion buffer if available)
        // Note: per-curve data is parsed during LoadMotion; for cached motions,
        // we rely on the SDK's internal fade handling.
    }

    // Non-idle motions default to non-loop, unless wrap_mode=1 explicitly requests looping.
    if (motion != nullptr && priority > PriorityIdle && !metaRequestsLoop)
    {
        motion->SetLoop(false);
    }

    // Toggle motions must never loop — they are one-shot state transitions.
    // Also force fadeIn=0 so the motion curves apply immediately (weight=1.0 from frame 1).
    if (motion != nullptr && MotionSequencer::IsToggleGroup(group))
    {
        bool wasLooping = motion->GetLoop();
        motion->SetLoop(false);
        motion->SetFadeInTime(0.0F);
        motion->SetFadeOutTime(0.0F);
        auto paramTargets = motion->GetParameterTargets();
        LAppPal::PrintLogLn("[Toggle] StartMotion: group=[%s] no=%d wasLooping=%d -> forced non-loop, fadeIn=0, fadeOut=0, paramTargets=%d",
            group, no, wasLooping ? 1 : 0, (int)paramTargets.size());
        for (auto* pid : paramTargets) {
            LAppPal::PrintLogLn("[Toggle] StartMotion:   param=[%s]", pid->GetString().GetRawString());
        }
    }

    // Track Parts controlled by this motion's PartOpacity curves.
    // ApplyVarFloatPartOverrides skips these Parts during playback so the
    // motion's fade animation isn't suppressed by the VarFloat system.
    // NOTE: Do NOT clear _motionControlledParts here — the old motion's
    // PartOpacity curves still apply during the SDK crossfade. Clearing
    // here would let VarFloat override the fade-out. Cleanup happens in
    // OnMotionFinishedCubism3 (before next_mtn chain or user callback).
    {
        std::vector<std::string> partIds;
        auto partTargets = motion->GetPartOpacityTargets();
        if (!partTargets.empty()) {
            partIds.reserve(partTargets.size());
            for (auto* id : partTargets) {
                partIds.push_back(id->GetString().GetRawString());
            }
        }
        // For toggle animations without PartOpacity curves, also protect the
        // Group's Parts. VarFloat is set immediately on click, but the Part
        // should stay at its OLD opacity during the animation. PartFade takes
        // over after the motion finishes for a smooth transition.
        if (partIds.empty() && startMeta != nullptr && MotionSequencer::IsToggleGroup(group)) {
            for (const auto& vf : startMeta->var_floats) {
                if (vf.type != 2 || vf.name.empty()) continue;
                auto* g = MotionSequencer::FindGroupForVarFloat(
                    LAppLive2DManager::GetInstance(), vf.name);
                if (g && !g->ids.empty()) {
                    for (const auto& id : g->ids) {
                        partIds.push_back(id);
                    }
                }
                break;  // Only need the primary VarFloat's group
            }
        }
        if (!partIds.empty()) {
            LAppLive2DManager::GetInstance()->OnMotionStartedWithPartOpacity(partIds);
        }
    }

    CubismMotionQueueEntryHandle handle = _motionManager->StartMotionPriority(motion, autoDelete, priority);
    _motionStartRealTime = _userTimeSeconds;  // Record when this motion started (for non-interruptable timeout)

    // Auto-trigger associated expression (reuses cached startMeta from above)
    if (startMeta != nullptr && !startMeta->expression.empty())
    {
        SetExpression(startMeta->expression.c_str());
    }

    return handle;
}

CubismMotionQueueEntryHandle LAppModel::StartRandomMotion(const csmChar* group, csmInt32 priority, ACubismMotion::FinishedMotionCallback onFinishedMotionHandler, ACubismMotion::BeganMotionCallback onBeganMotionHandler, csmFloat32 fadeInTime, csmFloat32 fadeOutTime)
{
    if (group == nullptr || group[0] == '\0' || _modelSetting->GetMotionCount(group) == 0)
    {
        return InvalidMotionQueueEntryHandleValue;
    }

    const csmInt32 no = rand() % _modelSetting->GetMotionCount(group);

    return StartMotion(group, no, priority, onFinishedMotionHandler, onBeganMotionHandler, fadeInTime, fadeOutTime);
}

void LAppModel::StartIdleMotion(const csmChar* group, csmInt32 no)
{
    if (!_idleMotionManager || !_modelSetting) return;

    const csmInt32 count = _modelSetting->GetMotionCount(group);
    if (count <= 0 || no < 0 || no >= count) return;

    const csmString fileName = _modelSetting->GetMotionFileName(group, no);
    const csmString name = Utils::CubismString::GetFormatedString("%s_%d", group, no);
    CubismMotion* motion = static_cast<CubismMotion*>(_motions[name.GetRawString()]);

    if (motion == nullptr) {
        csmString path = _modelHomeDir + fileName;
        csmByte* buffer;
        csmSizeInt size;
        buffer = LAppPal::CreateBuffer(path.GetRawString(), &size);
        if (buffer) {
            motion = static_cast<CubismMotion*>(LoadMotion(buffer, size, nullptr));
            if (motion) {
                motion->SetEffectIds(_eyeBlinkIds, _lipSyncIds);
                _motions[name.GetRawString()] = motion;
            }
            LAppPal::DeleteBuffer(buffer, path.GetRawString());
        }
    }

    if (motion) {
        motion->SetLoop(true);
        _idleMotionManager->StartMotion(motion, false);
        _fadeController.StartFade(MotionLayer::BaseIdle, 0.0F, 1.0F, 0.5F);
        auto& idleLayer = _fadeController.GetLayer(MotionLayer::BaseIdle);
        idleLayer.isFinished = false;
        idleLayer.activeGroup = group;
        idleLayer.activeIndex = no;
        LAppPal::PrintLogLn("[Motion] StartIdleMotion: group=[%s] no=%d", group, no);
    }
}

void LAppModel::DoDraw()
{
    if (_model == nullptr)
    {
        LAppPal::PrintLogLn("[APP]DoDraw: _model is nullptr");
        return;
    }
    auto* renderer = GetRenderer<Rendering::CubismRenderer_OpenGLES2>();
    if (renderer == nullptr)
    {
        LAppPal::PrintLogLn("[APP]DoDraw: renderer is nullptr");
        return;
    }
    // Validate drawable state before drawing
    const csmInt32 drawableCount = _model->GetDrawableCount();
    if (drawableCount <= 0)
    {
        LAppPal::PrintLogLn("[APP]DoDraw: no drawables, skipping");
        return;
    }

    renderer->DrawModel();
}

void LAppModel::Draw(CubismMatrix44& matrix)
{
    if (_model == nullptr)
    {
        LAppPal::PrintLogLn("[APP]Draw: _model is nullptr");
        return;
    }

    if (_modelMatrix == nullptr)
    {
        LAppPal::PrintLogLn("[APP]Draw: _modelMatrix is nullptr");
        return;
    }

    matrix.MultiplyByMatrix(_modelMatrix);

    // Cache the final MVP for hit testing (must match the matrix used for rendering)
    _lastFrameMVP = matrix;

    auto* renderer = GetRenderer<Rendering::CubismRenderer_OpenGLES2>();
    if (renderer == nullptr)
    {
        LAppPal::PrintLogLn("[APP]Draw: renderer is nullptr");
        return;
    }

    renderer->SetMvpMatrix(&matrix);

    DoDraw();
}

csmBool LAppModel::HitTest(const csmChar* hitAreaName, csmFloat32 x, csmFloat32 y)
{
    if (_opacity < 1)
    {
        return false;
    }
    const csmInt32 count = _modelSetting->GetHitAreasCount();
    for (csmInt32 i = 0; i < count; i++)
    {
        if (strcmp(_modelSetting->GetHitAreaName(i), hitAreaName) == 0)
        {
            const CubismIdHandle drawID = _modelSetting->GetHitAreaId(i);
            return IsHit(drawID, x, y);
        }
    }
    return false;
}

void LAppModel::SetExpression(const csmChar* expressionID)
{
    ACubismMotion* motion = _expressions[expressionID];

    if (motion != nullptr)
    {
        // Track previous expression for LastExpression() (borrowed from Live2DViewer)
        if (_currentExpressionId.GetLength() > 0 && strcmp(_currentExpressionId.GetRawString(), expressionID) != 0)
        {
            _previousExpressionId = _currentExpressionId;
        }
        _currentExpressionId = expressionID;
        _expressionManager->StartMotion(motion, false);
    }
    else
    {
        if(_debugMode) LAppPal::PrintLogLn("[APP]expression[%s] is null ", expressionID);
    }
}

void LAppModel::LastExpression()
{
    if (_previousExpressionId.GetLength() > 0)
    {
        // Swap current and previous without going through SetExpression
        ACubismMotion* motion = _expressions[_previousExpressionId];
        if (motion != nullptr)
        {
            const csmString temp = _currentExpressionId;
            _currentExpressionId = _previousExpressionId;
            _previousExpressionId = temp;
            _expressionManager->StartMotion(motion, false);
        }
    }
}

void LAppModel::SetRandomExpression()
{
    if (_expressions.GetSize() == 0)
    {
        return;
    }

    auto no = static_cast<csmInt32>(rand() % _expressions.GetSize());
    csmMap<csmString, ACubismMotion*>::const_iterator map_ite;
    csmInt32 i = 0;
    for (map_ite = _expressions.Begin(); map_ite != _expressions.End(); map_ite++)
    {
        if (i == no)
        {
            csmString name = (*map_ite).First;
            SetExpression(name.GetRawString());
            return;
        }
        i++;
    }
}

void LAppModel::ReloadRenderer()
{
    DeleteRenderer();
    const csmInt32 maskBufferCount = CalculateMaskBufferCount();
    CreateRenderer(LAppDelegate::GetInstance()->GetWindowWidth(), LAppDelegate::GetInstance()->GetWindowHeight(), maskBufferCount);
    SetupTextures();
}

csmInt32 LAppModel::CalculateMaskBufferCount() const
{
    csmInt32 maskBufferCount = 1;
    if (_model != nullptr && _model->IsUsingMasking())
    {
        const csmInt32* maskCounts = _model->GetDrawableMaskCounts();
        csmInt32 drawableCount = _model->GetDrawableCount();
        csmInt32 clipGroupCount = 0;
        for (csmInt32 i = 0; i < drawableCount; i++)
        {
            if(maskCounts[i] > 0) {
                clipGroupCount++;
            }
        }
        // 1 texture: max 36, N textures: max 32*N
        if (clipGroupCount > 36) {
            maskBufferCount = (clipGroupCount + 31) / 32;
        }
    }
    return maskBufferCount;
}

void LAppModel::SetupTextures()
{
    for (csmInt32 modelTextureNumber = 0; modelTextureNumber < _modelSetting->GetTextureCount(); modelTextureNumber++)
    {
        if (strcmp(_modelSetting->GetTextureFileName(modelTextureNumber), "") == 0)
        {
            continue;
        }

        csmString texturePath = _modelSetting->GetTextureFileName(modelTextureNumber);
        texturePath = _modelHomeDir + texturePath;

        LAppTextureManager::TextureInfo* texture = LAppDelegate::GetInstance()->GetTextureManager()->CreateTextureFromPngFile(texturePath.GetRawString());
        if (texture == nullptr)
        {
            LAppPal::PrintLogLn("[APP]ERROR: Failed to load texture %d: %s", modelTextureNumber, texturePath.GetRawString());
            continue;
        }
        const csmInt32 glTextueNumber = texture->id;
        GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->BindTexture(modelTextureNumber, glTextueNumber);
    }

    GetRenderer<Rendering::CubismRenderer_OpenGLES2>()->IsPremultipliedAlpha(true);
}

void LAppModel::MotionEventFired(const csmString& eventValue)
{
    CubismLogInfo("%s is fired on LAppModel!!", eventValue.GetRawString());
}

void LAppModel::ParseLookAtJson(const char* jsonStr)
{
    if(jsonStr == nullptr || strlen(jsonStr) == 0) {
        return;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(jsonStr);

        if(j.contains("angleXFactor")) _lookAtConfig.angleXFactor = j["angleXFactor"].get<float>();
        if(j.contains("angleYFactor")) _lookAtConfig.angleYFactor = j["angleYFactor"].get<float>();
        if(j.contains("bodyAngleXFactor")) _lookAtConfig.bodyAngleXFactor = j["bodyAngleXFactor"].get<float>();
        if(j.contains("eyeBallXFactor")) _lookAtConfig.eyeBallXFactor = j["eyeBallXFactor"].get<float>();
        if(j.contains("eyeBallYFactor")) _lookAtConfig.eyeBallYFactor = j["eyeBallYFactor"].get<float>();
        if(j.contains("damping")) _lookAtConfig.damping = j["damping"].get<float>();
        if(j.contains("mouseTrackingAutoReset")) _lookAtConfig.autoReset = j["mouseTrackingAutoReset"].get<bool>();
    } catch (const std::exception& e) {
        LAppPal::PrintLogLn("[APP]Failed to parse look-at JSON: %s", e.what());
    }
}

void LAppModel::SetLookAtConfig(const char* json)
{
    ParseLookAtJson(json);

    // Re-create look updater with new parameters
    if (_look != nullptr)
    {
        csmVector<CubismLook::LookParameterData> lookParameters;

        lookParameters.PushBack(CubismLook::LookParameterData(_idParamAngleX, _lookAtConfig.angleXFactor));
        lookParameters.PushBack(CubismLook::LookParameterData(_idParamAngleY, 0.0F, _lookAtConfig.angleYFactor));
        lookParameters.PushBack(CubismLook::LookParameterData(_idParamAngleZ, 0.0F, 0.0F, -30.0F));
        lookParameters.PushBack(CubismLook::LookParameterData(_idParamBodyAngleX, _lookAtConfig.bodyAngleXFactor));
        lookParameters.PushBack(CubismLook::LookParameterData(_idParamEyeBallX, _lookAtConfig.eyeBallXFactor));
        lookParameters.PushBack(CubismLook::LookParameterData(_idParamEyeBallY, 0.0F, _lookAtConfig.eyeBallYFactor));

        _look->SetParameters(lookParameters);
    }
}

void LAppModel::SetExpressionBlendConfig(const char* json)
{
    if(json == nullptr || strlen(json) == 0) return;
    LAppPal::PrintLogLn("[APP] Expression blend config received");
}
