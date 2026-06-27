#include "LAppLive2DManager.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <set>
#include "JniBridgeC.hpp"
#include <GLES2/gl2.h>
#include <Rendering/CubismRenderer.hpp>
#include <Rendering/OpenGL/CubismOffscreenManager_OpenGLES2.hpp>
#include "LAppPal.hpp"
#include "LAppDefine.hpp"
#include "LAppDelegate.hpp"
#include "LAppModel.hpp"
#include "LAppModelCubism2.hpp"
#include "MotionSequencer.hpp"
#include "TriangleHitTest.hpp"
#include "LAppView.hpp"
#include "third_party/nlohmann/json.hpp"

using namespace Csm;
using namespace LAppDefine;

namespace {
    LAppLive2DManager *s_instance = nullptr;

    bool NormalizeTouchForController(csmFloat32 x, csmFloat32 y, csmFloat32& outX, csmFloat32& outY) {
        LAppDelegate* app = LAppDelegate::GetInstance();
        if (app == nullptr) {
            outX = x;
            outY = y;
            return false;
        }

        const int width = app->GetWindowWidth();
        const int height = app->GetWindowHeight();
        if (width <= 0 || height <= 0) {
            outX = x;
            outY = y;
            return false;
        }

        outX = x * 2.0F / static_cast<csmFloat32>(width) - 1.0F;
        outY = 1.0F - y * 2.0F / static_cast<csmFloat32>(height);
        return true;
    }

    LAppModel* GetPrimaryCubism3Model(LAppLive2DManager* mgr) {
        if (mgr == nullptr) {
            return nullptr;
        }

        const int selectedSlot = mgr->GetSelectedModelSlot();
        if (selectedSlot >= 0) {
            LAppModel* selected = mgr->GetModel(static_cast<csmUint32>(selectedSlot));
            if (selected != nullptr && selected->GetModel() != nullptr) {
                return selected;
            }
        }

        const csmUint32 modelCount = mgr->GetModelNum();
        for (csmUint32 i = 0; i < modelCount; i++) {
            LAppModel* model = mgr->GetModel(i);
            if (model != nullptr && model->GetModel() != nullptr) {
                return model;
            }
        }
        return nullptr;
    }

    int ResolveMotionPriority(LAppLive2DManager* mgr, const std::string& group,
                              int selectedIndex, int fallbackPriority) {
        if (mgr == nullptr) {
            return fallbackPriority;
        }

        if (selectedIndex >= 0) {
            const MotionMeta* meta = mgr->FindMotionMeta(group, selectedIndex);
            if (meta != nullptr && meta->priority > 0) {
                return meta->priority;
            }
        }

        const auto& motionMetas = mgr->GetMotionMetas();
        auto it = motionMetas.find(group);
        if (it == motionMetas.end() || it->second.empty()) {
            return fallbackPriority;
        }

        int bestPriority = fallbackPriority;
        for (const auto& meta : it->second) {
            if (meta.priority > bestPriority) {
                bestPriority = meta.priority;
            }
        }
        return bestPriority;
    }

    std::string DeriveMotionGroupFromHitAreaName(const std::string& hitAreaName) {
        size_t firstDash = hitAreaName.find('-');
        if (firstDash == std::string::npos || firstDash + 1 >= hitAreaName.size()) {
            return "";
        }
        size_t dotPos = hitAreaName.find('.', firstDash + 1);
        if (dotPos == std::string::npos || dotPos + 1 >= hitAreaName.size()) {
            return "";
        }
        return hitAreaName.substr(0, firstDash) + "-" + hitAreaName.substr(dotPos + 1);
    }

    void BeganMotion(ACubismMotion *self) {
    }

    // Forward declaration — defined below, used by FinishedMotion.
    bool SyncGroupParamOverrides(const std::string& param, float value);

    void FinishedMotion(ACubismMotion *self) {
        // NOTE: _motionControlledParts is already cleared by OnMotionFinishedCubism3
        // before this callback runs. Do NOT clear again here.
        // SyncVarFloatPartOverrides acts as a safety net for toggle motions that
        // have NO VarFloat definitions (EvaluateVarFloats returns early, skipping
        // SyncAllVarFloatLinks). For motions WITH VarFloats, this is a no-op because
        // SyncAllVarFloatLinks already updated the group indices and started PartFade.
        MotionSequencer::SyncVarFloatPartOverrides();
    }

    /// Start motion with VarFloat-aware selection: evaluates conditions first,
    /// falls back to random if no conditions match.
    /// VarFloat Type 2 assignments are deferred until motion finishes, so the
    /// motion's PartOpacity curves have full visual control during playback.
    /// Also notifies Java side about motion sound for explicitly selected motions.
    void StartMotionWithVarFloat(LAppModel* model, const char* group, int priority,
                                  ACubismMotion::FinishedMotionCallback onFinished,
                                  ACubismMotion::BeganMotionCallback onBegan) {
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
        LAppPal::PrintLogLn("[Sound] StartMotionWithVarFloat: group=[%s]", group);

        // Handle "group:file.motion3.json" format — play the specific motion
        std::string groupStr(group);
        size_t colonPos = groupStr.find(':');
        if (colonPos != std::string::npos) {
            std::string groupName = groupStr.substr(0, colonPos);
            std::string fileName = groupStr.substr(colonPos + 1);
            auto it = mgr->GetModelConfig().motions.find(groupName);
            if (it != mgr->GetModelConfig().motions.end()) {
                for (int i = 0; i < (int)it->second.size(); i++) {
                    if (it->second[i].file == fileName) {
                        int resolvedPriority = ResolveMotionPriority(mgr, groupName, i, priority);
                        model->StartMotion(groupName.c_str(), i, resolvedPriority, onFinished, onBegan);
                        const MotionMeta* meta = mgr->FindMotionMeta(groupName, i);
                        JniBridgeC::NotifyMotionSoundForMeta(meta);
                        return;
                    }
                }
            }
            // File not found — fall through to play random from group
            groupStr = groupName;
            group = groupStr.c_str();
        }

        int selected = MotionSequencer::SelectMotionByVarFloats(group);
        int resolvedPriority = ResolveMotionPriority(mgr, group, selected, priority);
        if (selected >= 0) {
            const MotionMeta* meta = mgr->FindMotionMeta(group, selected);
            model->StartMotion(group, selected, resolvedPriority, onFinished, onBegan);
            JniBridgeC::NotifyMotionSoundForMeta(meta);
        } else {
            // Random fallback: pick a random index, start motion, and trigger sound.
            auto it = mgr->GetModelConfig().motions.find(group);
            int motionCount = (it != mgr->GetModelConfig().motions.end())
                              ? static_cast<int>(it->second.size()) : 0;
            if (motionCount > 0) {
                int randomIdx = rand() % motionCount;
                const MotionMeta* meta = mgr->FindMotionMeta(group, randomIdx);
                model->StartMotion(group, randomIdx, resolvedPriority, onFinished, onBegan);
                JniBridgeC::NotifyMotionSoundForMeta(meta);
            }
        }
    }

    void StartMotionWithVarFloat(LAppModelCubism2* model, const char* group, int priority) {
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();

        // Handle "group:file.motion3.json" format — play the specific motion
        std::string groupStr(group);
        size_t colonPos = groupStr.find(':');
        if (colonPos != std::string::npos) {
            std::string groupName = groupStr.substr(0, colonPos);
            std::string fileName = groupStr.substr(colonPos + 1);
            auto it = mgr->GetModelConfig().motions.find(groupName);
            if (it != mgr->GetModelConfig().motions.end()) {
                for (int i = 0; i < (int)it->second.size(); i++) {
                    if (it->second[i].file == fileName) {
                        int resolvedPriority = ResolveMotionPriority(mgr, groupName, i, priority);
                        // Cubism2: no FinishedMotion callback, keep VarFloat update immediate
                        MotionSequencer::EvaluateVarFloats(groupName, i);
                        model->StartMotion(groupName.c_str(), i, resolvedPriority);
                        MotionSequencer::SyncVarFloatPartOverrides();
                        const MotionMeta* meta = mgr->FindMotionMeta(groupName, i);
                        JniBridgeC::NotifyMotionSoundForMeta(meta);
                        return;
                    }
                }
            }
            // File not found — fall through to play random from group
            groupStr = groupName;
            group = groupStr.c_str();
        }

        int selected = MotionSequencer::SelectMotionByVarFloats(group);
        int resolvedPriority = ResolveMotionPriority(mgr, group, selected, priority);
        if (selected >= 0) {
            // Cubism2: no FinishedMotion callback, keep VarFloat update immediate
            MotionSequencer::EvaluateVarFloats(group, selected);
            model->StartMotion(group, selected, resolvedPriority);
            MotionSequencer::SyncVarFloatPartOverrides();
            const MotionMeta* meta = mgr->FindMotionMeta(group, selected);
            JniBridgeC::NotifyMotionSoundForMeta(meta);
        } else {
            auto it = mgr->GetModelConfig().motions.find(group);
            int motionCount = (it != mgr->GetModelConfig().motions.end())
                              ? static_cast<int>(it->second.size()) : 0;
            if (motionCount > 0) {
                int randomIdx = rand() % motionCount;
                MotionSequencer::EvaluateVarFloats(group, randomIdx);
                model->StartMotion(group, randomIdx, resolvedPriority);
                MotionSequencer::SyncVarFloatPartOverrides();
                const MotionMeta* meta = mgr->FindMotionMeta(group, randomIdx);
                JniBridgeC::NotifyMotionSoundForMeta(meta);
            }
        }
    }

    /// Start a random motion from a group on the selected model slot (or all models).
    void StartMotionOnSelectedModels(const char* group, int priority = PriorityNormal) {
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
        int sel = mgr->GetSelectedModelSlot();
        for (csmUint32 i = 0; i < mgr->GetModelCount(); i++) {
            if (sel >= 0 && static_cast<int>(i) != sel) continue;
            LAppModel* m = mgr->GetModel(i);
            if (m) m->StartRandomMotion(group, priority);
        }
        for (csmUint32 i = 0; i < mgr->GetCubism2ModelNum(); i++) {
            if (sel >= 0 && static_cast<int>(i) != sel) continue;
            LAppModelCubism2* m = mgr->GetCubism2Model(i);
            if (m) m->StartRandomMotion(group, priority);
        }
    }

    /// Test hit area with triangle-level precision and ignore_visibility support.
    /// Priority: triangle hit test > AABB (fallback when no triangle indices).
    /// If ignore_visibility is set, skips visibility/opacity checks.
    bool IsHitAreaHit(LAppModel* model, const HitAreaConfig& config,
                      const CubismIdHandle drawID, float hitX, float hitY) {
        // Use full MVP inverse for all paths — correctly handles mirror, rotation, and aspect ratio.
        const CubismMatrix44& mvp = model->GetLastFrameMVP();

        // Toggle hit areas (param + values) must remain tappable even when their
        // controlling Part is hidden by VarFloat. Without this, hiding a Part makes
        // its toggle untappable — a visibility deadlock.
        bool isToggle = !config.param.empty() && !config.values.empty();
        if (config.ignore_visibility || isToggle) {
            // Live AABB from current drawable vertices (follows motion/physics).
            // Falls back to static config AABB if model or drawable not available.
            CubismModel* cubismModel = model->GetModel();
            if (cubismModel) {
                const csmInt32 idx = cubismModel->GetDrawableIndex(drawID);
                if (idx >= 0) {
                    const csmInt32 vertCount = cubismModel->GetDrawableVertexCount(idx);
                    const csmFloat32* verts = cubismModel->GetDrawableVertices(idx);
                    if (vertCount > 0 && verts) {
                        float minX = verts[0], maxX = verts[0];
                        float minY = verts[1], maxY = verts[1];
                        for (csmInt32 v = 1; v < vertCount; v++) {
                            float vx = verts[v * 2];
                            float vy = verts[v * 2 + 1];
                            if (vx < minX) minX = vx;
                            if (vx > maxX) maxX = vx;
                            if (vy < minY) minY = vy;
                            if (vy > maxY) maxY = vy;
                        }
                        CubismMatrix44 mvpCopy = mvp;
                        float localX = mvpCopy.InvertTransformX(hitX);
                        float localY = mvpCopy.InvertTransformY(hitY);
                        float cx = (minX + maxX) * 0.5f;
                        float cy = (minY + maxY) * 0.5f;
                        float w = maxX - minX;
                        float h = maxY - minY;
                        return std::abs(localX - cx) <= w * 0.5f && std::abs(localY - cy) <= h * 0.5f;
                    }
                }
            }
            // Fallback: static AABB from config (rest-pose)
            if (config.width <= 0 || config.height <= 0) return false;
            CubismMatrix44 mvpCopy = mvp;
            float localX = mvpCopy.InvertTransformX(hitX);
            float localY = mvpCopy.InvertTransformY(hitY);
            float dx = localX - config.centerX;
            float dy = localY - config.centerY;
            return std::abs(dx) <= config.width * 0.5f && std::abs(dy) <= config.height * 0.5f;
        }

        // Triangle-level hit test using cached MVP from last render frame
        CubismModel* cubismModel = model->GetModel();
        if (cubismModel) {
            const csmInt32 idx = cubismModel->GetDrawableIndex(drawID);
            if (idx >= 0) {
                if (!cubismModel->GetDrawableDynamicFlagIsVisible(idx)) return false;
                if (cubismModel->GetDrawableOpacity(idx) <= 0.0F) return false;
                // Also check parent Part opacity — drawables under hidden Parts
                // (e.g. 跟宠 companions) should not intercept taps.
                const csmInt32 parentPart = cubismModel->GetDrawableParentPartIndex(idx);
                if (parentPart >= 0 && cubismModel->GetPartOpacity(parentPart) <= 0.0F) return false;
                CubismMatrix44 mvpCopy = mvp;
                return TriangleHitTest::HitTestDrawable(
                    cubismModel, idx, hitX, hitY, &mvpCopy);
            }
        }

        return false;
    }

    /// Case-insensitive exact match for Head/Face hit area names.
    /// Matches "Head", "Face", "head", "face", "HEAD", "FACE", etc.
    /// Does NOT match "Headphone", "Facebook", etc.
    bool IsHeadFaceArea(const std::string& name) {
        if (name.size() != 4 && name.size() != 5) return false;
        std::string lower = name;
        for (auto& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        return lower == "head" || lower == "face";
    }

    /// Sync GroupConfig part overrides when a param value changes.
    /// Uses SetGroupIndex for VarFloat cascade. Returns true if matched.
    bool SyncGroupParamOverrides(const std::string& param, float value) {
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
        for (auto& g : mgr->GetModelConfig().groups) {
            if ((g.target == param || g.var_float == param) && !g.values.empty()) {
                for (size_t vi = 0; vi < g.values.size(); vi++) {
                    if (g.values[vi] == value) {
                        mgr->SetGroupIndex(g, static_cast<int>(vi));
                        return true;
                    }
                }
            }
        }
        return false;
    }

    /// Shared hit area action dispatch logic.
    /// startMotionFn(group) handles model-specific motion starting.
    template<typename StartMotionFn, typename SetExprFn, typename GetTapFn>
    void DispatchHitAreaActionImpl(HitAreaConfig& config, bool skipMotion,
                                    StartMotionFn startMotionFn,
                                    SetExprFn setExprFn, GetTapFn getTapFn) {
        LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
        std::string effectiveMotion = config.motion;
        if (!config.param.empty() && !config.values.empty()) {
            // Sync config.currentIndex from VarFloat state (source of truth).
            // VarFloat may have changed from motion callbacks, menu toggles, etc.
            // Without this sync, config.currentIndex drifts from actual state.
            for (auto& g : mgr->GetModelConfig().groups) {
                if ((g.target == config.param || g.var_float == config.param) && !g.var_float.empty()) {
                    float vfVal = mgr->GetVarFloat(g.var_float);
                    int matchedIdx = -1;
                    for (size_t vi = 0; vi < g.values.size(); vi++) {
                        if (std::abs(g.values[vi] - vfVal) < 0.001F) {
                            matchedIdx = static_cast<int>(vi);
                            break;
                        }
                    }
                    if (matchedIdx >= 0 && matchedIdx != config.currentIndex) {
                        config.currentIndex = matchedIdx;
                    }
                    break;
                }
            }
            int prevIdx = config.currentIndex;
            int nextIdx = (prevIdx + 1) % static_cast<int>(config.values.size());
            config.currentIndex = nextIdx;
            // NOTE: Do NOT set _paramOverrides[config.param] here.
            // config.param is a GroupConfig target (e.g., "ParamAuto_kuzi"), not a toggle
            // parameter (e.g., "asdq_9"). Setting it in _paramOverrides would cause Phase 8
            // to force it every frame, hiding parts before the toggle animation finishes.
            // Toggle parameter values are stored in _paramOverrides by OnFinishedInternal
            // after the motion animation completes.
            if (!skipMotion && !effectiveMotion.empty()) {
                // Motion self-manages VarFloat state transitions via its own VarFloat definitions:
                //   Type=1 condition selects the correct entry based on CURRENT state
                //   Type=2 assign sets the NEW state after motion finishes
                // SelectMotionByVarFloats reads the CURRENT VarFloat to pick the right entry.
                // Do NOT pre-set VarFloat here — it would cause wrong motion selection on 2nd click.
                // Do NOT use deferred sync — EvaluateVarFloats handles the transition correctly.
                startMotionFn(effectiveMotion);
            } else {
                // No motion — cascade immediately via SetGroupIndex (includes PartFade)
                SyncGroupParamOverrides(config.param, config.values[nextIdx]);
            }
        } else if (!config.param.empty()) {
            mgr->ToggleParam(config.param.c_str());
            if (!skipMotion && !effectiveMotion.empty()) {
                startMotionFn(effectiveMotion);
            }
        } else if (!skipMotion && !effectiveMotion.empty()) {
            startMotionFn(effectiveMotion);
        } else if (!skipMotion && IsHeadFaceArea(config.name)) {
            setExprFn();
        } else if (!skipMotion) {
            startMotionFn(getTapFn());
        }
    }

    void DispatchHitAreaAction(HitAreaConfig& config, LAppModel* model, bool skipMotion = false) {
        DispatchHitAreaActionImpl(config, skipMotion,
            [&](const std::string& group) {
                StartMotionWithVarFloat(model, group.c_str(), PriorityNormal, FinishedMotion, BeganMotion);
            },
            [&]() { model->SetRandomExpression(); },
            [&]() -> std::string { return model->GetTapMotionGroup().GetRawString(); }
        );
    }

    void DispatchHitAreaAction(HitAreaConfig& config, LAppModelCubism2* model, bool skipMotion = false) {
        DispatchHitAreaActionImpl(config, skipMotion,
            [&](const std::string& group) {
                StartMotionWithVarFloat(model, group.c_str(), PriorityNormal);
            },
            [&]() { model->SetRandomExpression(); },
            [&]() -> std::string { return model->GetTapMotionGroup().GetRawString(); }
        );
    }
}

LAppLive2DManager *LAppLive2DManager::GetInstance() {
    if (s_instance == nullptr) {
        s_instance = new LAppLive2DManager();
    }
    return s_instance;
}

void LAppLive2DManager::ReleaseInstance() {


    delete s_instance;

    s_instance = nullptr;
}

LAppLive2DManager::LAppLive2DManager()
        : _viewMatrix(nullptr), _isCubism2(false) {
    _viewMatrix = new CubismMatrix44();
}

LAppLive2DManager::~LAppLive2DManager() {
    ReleaseAllModel();
    delete _viewMatrix;
    Csm::Rendering::CubismOffscreenManager_OpenGLES2::ReleaseInstance();
}

void LAppLive2DManager::ReleaseAllModel() {
    _controllerEngine.OnTouchCancelled();
    for (csmUint32 i = 0; i < _models.GetSize(); i++) {
        delete _models[i];
    }
    _models.Clear();

    for (csmUint32 i = 0; i < _modelsCubism2.GetSize(); i++) {
        delete _modelsCubism2[i];
    }
    _modelsCubism2.Clear();

    _isCubism2 = false;
    _hitAreaConfigs.clear();

    // Clear stale model config to prevent old motions/groups from persisting
    // across model loads. LoadModelFromPath will rebuild from the new model's data.
    _modelConfig.motions.clear();
    _modelConfig.groups.clear();
    _modelConfig.var_floats.clear();
    _modelConfig.var_float_part_overrides.clear();
    _varFloatGroupMap.clear();
    _paramOverrides.clear();
    _partOverrides.clear();
    _partOverridesDirty = true;
    _motionControlledParts.clear();
    _varFloatsDirty = true;  // force re-evaluation on next frame after new model loads

    // Reset motion chain state to prevent stale chain depth from previous model
    MotionSequencer::ResetChainState();
}

void LAppLive2DManager::ReloadCubism2Renderers() {
    for (csmUint32 i = 0; i < _modelsCubism2.GetSize(); i++) {
        if (_modelsCubism2[i] != nullptr) {
            _modelsCubism2[i]->ReloadRenderer();
        }
    }
}

LAppModel *LAppLive2DManager::GetModel(csmUint32 no) const {
    if (no < _models.GetSize()) {
        return _models[no];
    }
    return nullptr;
}

LAppModelCubism2 *LAppLive2DManager::GetCubism2Model(csmUint32 no) const {
    if (no < _modelsCubism2.GetSize()) {
        return _modelsCubism2[no];
    }
    return nullptr;
}

void LAppLive2DManager::SetRenderTargetSize(csmUint32 width, csmUint32 height) {
    _renderWidth = width;
    _renderHeight = height;
    for (csmUint32 i = 0; i < _models.GetSize(); i++) {
        LAppModel *model = GetModel(i);
        if (model) model->SetRenderTargetSize(width, height);
    }
    // Cubism 2 models handle viewport internally
}

void LAppLive2DManager::OnDrag(csmFloat32 x, csmFloat32 y) const {
    for (csmUint32 i = 0; i < _models.GetSize(); i++) {
        if (_selectedModelSlot >= 0 && static_cast<int>(i) != _selectedModelSlot) continue;
        LAppModel *model = GetModel(i);
        if (model) model->SetDragging(x, y);
    }
    for (csmUint32 i = 0; i < _modelsCubism2.GetSize(); i++) {
        if (_selectedModelSlot >= 0 && static_cast<int>(i) != _selectedModelSlot) continue;
        if (_modelsCubism2[i]) _modelsCubism2[i]->SetDragging(x, y);
    }
}

void LAppLive2DManager::OnHitAreaBegan(csmFloat32 x, csmFloat32 y) {
    _pressedHitAreaIndex = -1;
    if (_hitAreaConfigs.empty()) return;

    float hitX, hitY;
    TransformTouchForModelTransform(x, y, hitX, hitY);

    // Check Cubism 3/4/5 models (reverse order for Z-priority)
    csmInt32 hitStart = static_cast<csmInt32>(_models.GetSize()) - 1;
    csmInt32 hitEnd = 0;
    if (_selectedModelSlot >= 0 && _selectedModelSlot < static_cast<int>(_models.GetSize())) {
        hitStart = _selectedModelSlot;
        hitEnd = _selectedModelSlot;
    }
    for (csmInt32 i = hitStart; i >= hitEnd; i--) {
        LAppModel *model = GetModel(i);
        if (model == nullptr || model->GetModel() == nullptr) continue;
        ICubismModelSetting *setting = model->GetModelSetting();
        if (setting == nullptr) continue;
        for (csmInt32 h = 0; h < setting->GetHitAreasCount(); h++) {
            const csmChar *areaName = setting->GetHitAreaName(h);
            const CubismIdHandle drawID = setting->GetHitAreaId(h);
            for (size_t c = 0; c < _hitAreaConfigs.size(); c++) {
                if (!_hitAreaConfigs[c].enabled) continue;
                if (_hitAreaConfigs[c].id == areaName || _hitAreaConfigs[c].name == areaName) {
                    if (IsHitAreaHit(model, _hitAreaConfigs[c], drawID, hitX, hitY)) {
                        _pressedHitAreaIndex = static_cast<int>(c);
                        std::string downMotion = _hitAreaConfigs[c].downMtn;
                        LAppPal::PrintLogLn("[Touch] HitAreaBegan: pos=(%.1f,%.1f) area=[%s] downGroup=[%s]",
                            hitX, hitY, _hitAreaConfigs[c].name.c_str(), downMotion.c_str());
                        if (!downMotion.empty()) {
                            StartMotionOnSelectedModels(downMotion.c_str(), PriorityIdle);
                        }
                        return;
                    }
                }
            }
        }
    }

    // Check Cubism 2 models
    csmInt32 c2Start = static_cast<csmInt32>(_modelsCubism2.GetSize()) - 1;
    csmInt32 c2End = 0;
    if (_selectedModelSlot >= 0 && _selectedModelSlot < static_cast<int>(_modelsCubism2.GetSize())) {
        c2Start = _selectedModelSlot;
        c2End = _selectedModelSlot;
    }
    for (csmInt32 i = c2Start; i >= c2End; i--) {
        LAppModelCubism2 *model = _modelsCubism2[i];
        if (model == nullptr || !model->IsInitialized()) continue;
        Cubism2ModelSetting *setting = model->GetModelSetting();
        if (setting == nullptr) continue;
        for (csmInt32 h = 0; h < setting->GetHitAreasCount(); h++) {
            const csmChar *areaName = setting->GetHitAreaName(h);
            for (size_t c = 0; c < _hitAreaConfigs.size(); c++) {
                if (!_hitAreaConfigs[c].enabled) continue;
                if (_hitAreaConfigs[c].id == areaName || _hitAreaConfigs[c].name == areaName) {
                    if (model->HitTest(areaName, hitX, hitY)) {
                        _pressedHitAreaIndex = static_cast<int>(c);
                        std::string downMotion = _hitAreaConfigs[c].downMtn;
                        LAppPal::PrintLogLn("[Touch] HitAreaBegan(C2): pos=(%.1f,%.1f) area=[%s] downGroup=[%s]",
                            hitX, hitY, _hitAreaConfigs[c].name.c_str(), downMotion.c_str());
                        if (!downMotion.empty()) {
                            StartMotionOnSelectedModels(downMotion.c_str(), PriorityIdle);
                        }
                        return;
                    }
                }
            }
        }
    }
}

void LAppLive2DManager::OnHitAreaEnded(csmFloat32 x, csmFloat32 y) {
    _upMtnFired = false;
    _lastPressedHitAreaIndex = _pressedHitAreaIndex;
    if (_pressedHitAreaIndex >= 0 && _pressedHitAreaIndex < static_cast<int>(_hitAreaConfigs.size())) {
        const auto& cfg = _hitAreaConfigs[_pressedHitAreaIndex];
        std::string upMotion = cfg.upMtn;
        LAppPal::PrintLogLn("[Touch] HitAreaEnded: pos=(%.1f,%.1f) area=[%s] upGroup=[%s]",
            x, y, cfg.name.c_str(), upMotion.c_str());
        if (!upMotion.empty()) {
            StartMotionOnSelectedModels(upMotion.c_str(), PriorityIdle);
            _upMtnFired = true;
        }
    }
    // Reset _pressedHitAreaIndex so next touch starts fresh.
    // _lastPressedHitAreaIndex preserves the value for OnTap param dispatch.
    _pressedHitAreaIndex = -1;
}

void LAppLive2DManager::OnDragWithHitArea(csmFloat32 x, csmFloat32 y) {
    // Transform touch coords for mirror/rotation so hit areas match visual position
    float hitX, hitY;
    TransformTouchForModelTransform(x, y, hitX, hitY);

    // Check hit area enter/exit transitions
    if (_hitAreaConfigs.empty()) {
        OnDrag(x, y);
        _currentHitAreaIndex = -1;
        return;
    }

    int newHitIndex = -1;

    // Check hit areas using SDK IsHit (reverse order for Z-priority)
    // Iterate model's built-in hit areas, match configs by Id
    csmInt32 hitStart = static_cast<csmInt32>(_models.GetSize()) - 1;
    csmInt32 hitEnd = 0;
    if (_selectedModelSlot >= 0 && _selectedModelSlot < static_cast<int>(_models.GetSize())) {
        hitStart = _selectedModelSlot;
        hitEnd = _selectedModelSlot;
    }
    for (csmInt32 i = hitStart; i >= hitEnd; i--) {
        LAppModel *model = GetModel(i);
        if (model == nullptr || model->GetModel() == nullptr) {
            continue;
        }
        ICubismModelSetting *setting = model->GetModelSetting();
        if (setting == nullptr) {
            continue;
        }
        for (csmInt32 h = 0; h < setting->GetHitAreasCount(); h++) {
            const csmChar *areaName = setting->GetHitAreaName(h);
            const CubismIdHandle drawID = setting->GetHitAreaId(h);
            // Find matching config to check ignore_visibility
            for (size_t c = 0; c < _hitAreaConfigs.size(); c++) {
                if (!_hitAreaConfigs[c].enabled) continue;
                if (_hitAreaConfigs[c].id == areaName || _hitAreaConfigs[c].name == areaName) {
                    if (IsHitAreaHit(model, _hitAreaConfigs[c], drawID, hitX, hitY)) {
                        newHitIndex = static_cast<int>(c);
                        goto done_hit_check;
                    }
                    break;
                }
            }
        }
    }
    {
        csmInt32 c2HitStart = static_cast<csmInt32>(_modelsCubism2.GetSize()) - 1;
        csmInt32 c2HitEnd = 0;
        if (_selectedModelSlot >= 0 && _selectedModelSlot < static_cast<int>(_modelsCubism2.GetSize())) {
            c2HitStart = _selectedModelSlot;
            c2HitEnd = _selectedModelSlot;
        }
        for (csmInt32 i = c2HitStart; i >= c2HitEnd; i--) {
            LAppModelCubism2 *model = _modelsCubism2[i];
            if (model == nullptr || !model->IsInitialized()) {
                continue;
            }
            Cubism2ModelSetting *setting = model->GetModelSetting();
            if (setting == nullptr) {
                continue;
            }
            for (csmInt32 h = 0; h < setting->GetHitAreasCount(); h++) {
                const csmChar *areaName = setting->GetHitAreaName(h);
                for (size_t c = 0; c < _hitAreaConfigs.size(); c++) {
                    if (!_hitAreaConfigs[c].enabled) continue;
                    if (_hitAreaConfigs[c].id == areaName || _hitAreaConfigs[c].name == areaName) {
                        if (model->HitTest(areaName, hitX, hitY)) {
                            newHitIndex = static_cast<int>(c);
                            goto done_hit_check;
                        }
                        break;
                    }
                }
            }
        }
    }
    done_hit_check:

    // Update ControllerEngine hit area context
    _controllerEngine.SetCurrentHitArea(
        (newHitIndex >= 0 && newHitIndex < static_cast<int>(_hitAreaConfigs.size()))
            ? _hitAreaConfigs[newHitIndex].name : "");

    if (newHitIndex >= 0) {
        // Inside a hit area
        if (newHitIndex != _currentHitAreaIndex) {
            if (_currentHitAreaIndex >= 0) {
                // Transition BETWEEN hit areas: dispatch params, skip motion
                LAppPal::PrintLogLn("[Touch] DragTransition: [%s] -> [%s] pos=(%.1f,%.1f)",
                    _hitAreaConfigs[_currentHitAreaIndex].name.c_str(),
                    _hitAreaConfigs[newHitIndex].name.c_str(), hitX, hitY);
                {
                    auto& cfg = _hitAreaConfigs[newHitIndex];
                    LAppModel* model = GetModel(_selectedModelSlot >= 0 ? _selectedModelSlot : 0);
                    if (model && model->GetModel()) {
                        DispatchHitAreaAction(cfg, model, /*skipMotion=*/true);
                    } else {
                        // Cubism2 fallback
                        int c2Slot = _selectedModelSlot >= 0 ? _selectedModelSlot : 0;
                        if (c2Slot < static_cast<int>(_modelsCubism2.GetSize()) && _modelsCubism2[c2Slot] && _modelsCubism2[c2Slot]->IsInitialized()) {
                            DispatchHitAreaAction(cfg, _modelsCubism2[c2Slot], /*skipMotion=*/true);
                        }
                    }
                    // Sync GroupConfig/VarFloat after param cycling so part overrides update immediately
                    if (!cfg.param.empty() && !cfg.values.empty()) {
                        int syncIdx = cfg.currentIndex >= 0 ? cfg.currentIndex : 0;
                        LAppPal::PrintLogLn("[Switch] DragSync: param=[%s] cfgIdx=%d syncIdx=%d value=%.1f",
                            cfg.param.c_str(), cfg.currentIndex, syncIdx, cfg.values[syncIdx]);
                        SyncGroupParamOverrides(cfg.param, cfg.values[syncIdx]);
                    }
                }
                _dragTriggeredHitArea = true;

                // Play exit_mtn from old area, then enter_mtn for new area (both, not exclusive)
                const std::string &exitMtn = _hitAreaConfigs[_currentHitAreaIndex].exitMtn;
                const std::string &enterMtn = _hitAreaConfigs[newHitIndex].enterMtn;
                if (!exitMtn.empty()) {
                    StartMotionOnSelectedModels(exitMtn.c_str());
                }
                if (!enterMtn.empty()) {
                    StartMotionOnSelectedModels(enterMtn.c_str());
                }
            } else {
                // Entering from outside (-1): dispatch params + play enter_mtn
                {
                    auto& cfg = _hitAreaConfigs[newHitIndex];
                    LAppModel* model = GetModel(_selectedModelSlot >= 0 ? _selectedModelSlot : 0);
                    if (model && model->GetModel()) {
                        DispatchHitAreaAction(cfg, model, /*skipMotion=*/true);
                    } else {
                        int c2Slot = _selectedModelSlot >= 0 ? _selectedModelSlot : 0;
                        if (c2Slot < static_cast<int>(_modelsCubism2.GetSize()) && _modelsCubism2[c2Slot] && _modelsCubism2[c2Slot]->IsInitialized()) {
                            DispatchHitAreaAction(cfg, _modelsCubism2[c2Slot], /*skipMotion=*/true);
                        }
                    }
                    // Sync GroupConfig/VarFloat after param cycling so part overrides update immediately
                    if (!cfg.param.empty() && !cfg.values.empty()) {
                        int syncIdx = cfg.currentIndex >= 0 ? cfg.currentIndex : 0;
                        LAppPal::PrintLogLn("[Switch] EnterSync: param=[%s] cfgIdx=%d syncIdx=%d value=%.1f",
                            cfg.param.c_str(), cfg.currentIndex, syncIdx, cfg.values[syncIdx]);
                        SyncGroupParamOverrides(cfg.param, cfg.values[syncIdx]);
                    }
                }
                _dragTriggeredHitArea = true;

                const std::string &enterMtn = _hitAreaConfigs[newHitIndex].enterMtn;
                if (!enterMtn.empty()) {
                    StartMotionOnSelectedModels(enterMtn.c_str());
                }
            }
        }
        _currentHitAreaIndex = newHitIndex;
    } else {
        // Outside all hit areas
        if (_currentHitAreaIndex >= 0 &&
            _currentHitAreaIndex < static_cast<int>(_hitAreaConfigs.size())) {
            // Exiting a hit area: play exitMtn
            const std::string &exitMtn = _hitAreaConfigs[_currentHitAreaIndex].exitMtn;
            if (!exitMtn.empty()) {
                StartMotionOnSelectedModels(exitMtn.c_str());
            }
        }
        OnDrag(x, y);
        _currentHitAreaIndex = newHitIndex;
    }
}

void LAppLive2DManager::SetHitAreaConfig(const char *json) {
    ParseHitAreaJson(json);
}

void LAppLive2DManager::ParseHitAreaJson(const char *jsonStr) {
    _hitAreaConfigs.clear();
    if(jsonStr == nullptr || strlen(jsonStr) == 0) {
        return;
    }

    try {
        nlohmann::json arr = nlohmann::json::parse(jsonStr);
        if(!arr.is_array()) {
            return;
        }

        for (const auto &item: arr) {
            if(!item.is_object()) {
                continue;
            }

            HitAreaConfig config;
            config.name = item.value("name", "");
            config.id = item.value("id", "");
            config.motion = item.value("motion", "");
            config.enterMtn = item.value("enter_mtn", "");
            config.exitMtn = item.value("exit_mtn", "");
            config.downMtn = item.value("down_mtn", "");
            config.upMtn = item.value("up_mtn", "");
            config.ignore_visibility = item.value("ignore_visibility", false);
            config.param = item.value("param", "");
            config.enabled = item.value("enabled", true);
            config.width = item.value("width", 0.0F);
            config.height = item.value("height", 0.0F);
            config.centerX = item.value("center_x", 0.0F);
            config.centerY = item.value("center_y", 0.0F);
            config.order = item.value("order", 0);

            // Parse values array
            if (item.contains("values") && item["values"].is_array()) {
                for (const auto &v: item["values"]) {
                    if (v.is_number()) {
                        config.values.push_back(v.get<float>());
                    } else if (v.is_string()) {
                        config.values.push_back(strtof(v.get<std::string>().c_str(), nullptr));
                    }
                }
            }

            if (!config.name.empty() && config.enabled) {
                _hitAreaConfigs.push_back(config);
                if (!config.param.empty()) {
                    std::string valStr;
                    for (size_t vi = 0; vi < config.values.size(); vi++) {
                        if(vi > 0) {
                            valStr += ",";
                        }
                        valStr += std::to_string(config.values[vi]);
                    }
                }
            }
        }
    } catch (const std::exception &e) {
        LAppPal::PrintLogLn("[APP]Failed to parse hit area JSON: %s", e.what());
    }
}

void LAppLive2DManager::AutoGenerateHitAreas() {
    // Try to read HitAreas from model3.json and merge them into the current
    // hit-area table. This is important because imported configs may define
    // only part of the hit areas, or may omit the Motion field entirely.
    if (!_lastModelPath.empty()) {
        AutoGenerateHitAreasFromModelJson(_lastModelPath.c_str());
    }

    // If we already have usable hit areas after merging model3.json, stop here.
    if(!_hitAreaConfigs.empty()) {
        return;
    }

    LAppModel *model = GetPrimaryCubism3Model(this);
    if(model == nullptr) {
        return;
    }

    CubismModel *cubismModel = model->GetModel();
    if(cubismModel == nullptr) {
        return;
    }

    csmInt32 count = cubismModel->GetDrawableCount();
    int generated = 0;

    for (csmInt32 i = 0; i < count; i++) {
        CubismIdHandle id = cubismModel->GetDrawableId(i);
        const csmChar *idStr = id->GetString().GetRawString();

        std::string idLower = idStr;
        for(auto &c: idLower) c = tolower(c);

        bool isHitArea = (idLower.find("hitarea") != std::string::npos ||
                          idLower.find("hit_area") != std::string::npos ||
                          idLower.find("d_ref.") != std::string::npos);
        if(!isHitArea) {
            continue;
        }

        csmInt32 vertCount = cubismModel->GetDrawableVertexCount(i);
        if(vertCount <= 0) {
            continue;
        }

        const csmFloat32 *verts = cubismModel->GetDrawableVertices(i);
        if(verts == nullptr) {
            continue;
        }

        float minX = verts[0], maxX = verts[0];
        float minY = verts[1], maxY = verts[1];
        for (csmInt32 v = 1; v < vertCount; v++) {
            if(verts[v * 2] < minX) {
                minX = verts[v * 2];
            }
            if(verts[v * 2] > maxX) {
                maxX = verts[v * 2];
            }
            if(verts[v * 2 + 1] < minY) {
                minY = verts[v * 2 + 1];
            }
            if(verts[v * 2 + 1] > maxY) {
                maxY = verts[v * 2 + 1];
            }
        }

        float w = maxX - minX;
        float h = maxY - minY;
        if(w < 0.05F || h < 0.05F) {
            continue;
        }

        HitAreaConfig config;
        config.name = idStr;
        config.id = idStr;
        config.enabled = true;
        config.centerX = (minX + maxX) * 0.5F;
        config.centerY = (minY + maxY) * 0.5F;
        config.width = w;
        config.height = h;
        config.order = generated;
        _hitAreaConfigs.push_back(config);
        generated++;
    }
}

void LAppLive2DManager::AutoGenerateHitAreasFromModelJson(const char *modelJsonPath) {
    // Read model3.json file via JNI LoadFile
    csmSizeInt fileSize = 0;
    csmByte *buf = LAppPal::LoadFileAsBytes(modelJsonPath, &fileSize);
    if (buf == nullptr || fileSize == 0) {
        LAppPal::PrintLogLn("[APP]AutoHitAreas: failed to load model json: %s", modelJsonPath);
        return;
    }

    std::string jsonStr(reinterpret_cast<char *>(buf), fileSize);
    LAppPal::ReleaseBytes(buf);

    try {
        nlohmann::json json = nlohmann::json::parse(jsonStr);

        // Find HitAreas array - try top-level and under FileReferences
        nlohmann::json *hitAreas = nullptr;
        if (json.contains("HitAreas") && json["HitAreas"].is_array()) {
            hitAreas = &json["HitAreas"];
        } else if (json.contains("FileReferences") && json["FileReferences"].contains("HitAreas")) {
            hitAreas = &json["FileReferences"]["HitAreas"];
        }

        if (hitAreas == nullptr || hitAreas->empty()) {
            LAppPal::PrintLogLn("[APP]AutoHitAreas: no HitAreas in model json");
            return;
        }

        LAppModel *model = GetPrimaryCubism3Model(this);
        if(model == nullptr) {
            return;
        }

        CubismModel *cubismModel = model->GetModel();
        if(cubismModel == nullptr) {
            return;
        }

        csmInt32 drawableCount = cubismModel->GetDrawableCount();
        int generated = 0;

        for (const auto &area: *hitAreas) {
            std::string name = area.value("Name", area.value("name", ""));
            std::string id = area.value("Id", area.value("id", ""));
            std::string motionRaw = area.value("Motion", area.value("motion", ""));

            if(id.empty()) {
                continue;
            }

            // Store full Motion field (e.g. "桌面:motion/数位板.motion3.json")
            // StartMotionWithVarFloat handles both "group" and "group:file" formats.
            std::string motion = motionRaw;

            // Find drawable by Id
            for (csmInt32 i = 0; i < drawableCount; i++) {
                CubismIdHandle drawableId = cubismModel->GetDrawableId(i);
                const csmChar *drawableIdStr = drawableId->GetString().GetRawString();

                if(id != drawableIdStr) {
                    continue;
                }

                csmInt32 vertCount = cubismModel->GetDrawableVertexCount(i);
                if(vertCount <= 0) {
                    break;
                }

                const csmFloat32 *verts = cubismModel->GetDrawableVertices(i);
                if(verts == nullptr) {
                    break;
                }

                float minX = verts[0], maxX = verts[0];
                float minY = verts[1], maxY = verts[1];
                for (csmInt32 v = 1; v < vertCount; v++) {
                    if(verts[v * 2] < minX) {
                        minX = verts[v * 2];
                    }
                    if(verts[v * 2] > maxX) {
                        maxX = verts[v * 2];
                    }
                    if(verts[v * 2 + 1] < minY) {
                        minY = verts[v * 2 + 1];
                    }
                    if(verts[v * 2 + 1] > maxY) {
                        maxY = verts[v * 2 + 1];
                    }
                }

                HitAreaConfig config;
                config.name = name;
                config.id = id;
                config.motion = motion;
                config.enabled = true;
                config.centerX = (minX + maxX) * 0.5F;
                config.centerY = (minY + maxY) * 0.5F;
                config.width = maxX - minX;
                config.height = maxY - minY;
                config.order = generated;

                // Extract Param and Values for component toggle support
                std::string param = area.value("Param", area.value("param", ""));
                config.param = param;
                if (area.contains("Values") && area["Values"].is_array()) {
                    for (const auto &v : area["Values"]) {
                        if (v.is_number()) config.values.push_back(v.get<float>());
                    }
                } else if (area.contains("values") && area["values"].is_array()) {
                    for (const auto &v : area["values"]) {
                        if (v.is_number()) config.values.push_back(v.get<float>());
                    }
                }

                // Auto-detect VarFloat toggle motion groups.
                // model3.json hit areas reference motion groups that toggle VarFloats.
                // The toggle pattern has 2 entries, each with 2 VarFloats:
                //   Entry 0 "On":  [Type=1, equal V1] [Type=2, assign V0]
                //   Entry 1 "Off": [Type=1, equal V0] [Type=2, assign V1]
                // These hit areas need param/values/ignore_visibility for proper toggle dispatch.
                if (config.param.empty() && !motion.empty()) {
                    const auto& metas = GetMotionMetas();
                    auto it = metas.find(motion);
                    if (it != metas.end() && it->second.size() == 2) {
                        const auto& e0 = it->second[0];
                        const auto& e1 = it->second[1];
                        if (e0.var_floats.size() == 2 && e1.var_floats.size() == 2) {
                            const auto& vf0cond = e0.var_floats[0]; // Type=1 condition
                            const auto& vf0assign = e0.var_floats[1]; // Type=2 assign
                            const auto& vf1cond = e1.var_floats[0];
                            const auto& vf1assign = e1.var_floats[1];
                            if (vf0cond.type == 1 && vf0cond.has_equal &&
                                vf0assign.type == 2 && vf0assign.has_assign &&
                                vf1cond.type == 1 && vf1cond.has_equal &&
                                vf1assign.type == 2 && vf1assign.has_assign &&
                                vf0cond.name == vf1cond.name &&
                                std::abs(vf0cond.equal - vf1assign.assign) < 0.001F &&
                                std::abs(vf0assign.assign - vf1cond.equal) < 0.001F) {
                                config.param = vf0cond.name;
                                config.values.clear();
                                config.values.push_back(vf0cond.equal);
                                config.values.push_back(vf1cond.equal);
                                config.ignore_visibility = true;
                                LAppPal::PrintLogLn("[APP] AutoToggle: area=[%s] param=[%s] values=[%.1f,%.1f]",
                                    name.c_str(), config.param.c_str(),
                                    config.values[0], config.values[1]);
                            }
                        }
                    }
                }

                HitAreaConfig *existing = nullptr;
                for (auto &cfg : _hitAreaConfigs) {
                    if (cfg.id == config.id || cfg.name == config.name) {
                        existing = &cfg;
                        break;
                    }
                }

                if (existing != nullptr) {
                    if (existing->motion.empty() && !config.motion.empty()) {
                        existing->motion = config.motion;
                    }
                    if (existing->param.empty() && !config.param.empty()) {
                        existing->param = config.param;
                        existing->values = config.values;
                    }
                    if (config.ignore_visibility) {
                        existing->ignore_visibility = true;
                    }
                    if (existing->width <= 0.0F && config.width > 0.0F) {
                        existing->width = config.width;
                    }
                    if (existing->height <= 0.0F && config.height > 0.0F) {
                        existing->height = config.height;
                    }
                    if (existing->centerX == 0.0F && existing->centerY == 0.0F) {
                        existing->centerX = config.centerX;
                        existing->centerY = config.centerY;
                    }
                    if (existing->order == 0 && config.order != 0) {
                        existing->order = config.order;
                    }
                } else {
                    _hitAreaConfigs.push_back(config);
                }
                generated++;
                break;
            }
        }

        LAppPal::PrintLogLn("[APP]AutoHitAreas: %d hit areas from model3.json", generated);
    } catch (const std::exception &e) {
        LAppPal::PrintLogLn("[APP]AutoHitAreas: parse error: %s", e.what());
    }
}

void LAppLive2DManager::OnTap(csmFloat32 x, csmFloat32 y, bool wasDragging) {
    // If the Java-side menu is open, don't process model hits.
    // The menu dismisses itself on outside touch; model taps would be unintended.
    if (_menuVisible) {
        return;
    }

    // Drag end: don't dispatch hit area action (param changes already handled during drag).
    if (wasDragging) {
        return;
    }

    // up_mtn already fired in OnHitAreaEnded — skip param/switch dispatch
    // but still allow motion playback (tap motion is separate from up_mtn).
    bool skipParamDispatch = _upMtnFired;

    // If drag already triggered the hit area action (param toggle during drag),
    // don't return early — still allow motion playback for switch areas.
    // The param was already toggled by drag, so skip param dispatch but play motion.
    bool dragAlreadyHandled = false;
    if (_dragTriggeredHitArea) {
        _dragTriggeredHitArea = false;
        dragAlreadyHandled = true;
    }

    // Transform touch coords for mirror/rotation so hit areas match visual position
    float hitX, hitY;
    TransformTouchForModelTransform(x, y, hitX, hitY);

    // x, y are in screen space (from _deviceToScreen transform in LAppView::OnTouchesEnded).
    // SDK's IsHit expects screen-space coords — it applies _modelMatrix->InvertTransform internally.
    // Config-based hit areas use model-space coords — we convert via _modelMatrix->InvertTransform.
    //
    // Iteration order: reverse (highest index = frontmost, drawn last = visually on top).
    // Only the first hit model responds; all others are skipped.

    LAppPal::PrintLogLn("[Touch] OnTap: pos=(%.1f,%.1f)", hitX, hitY);

    // Process Cubism 3/4/5 models in reverse order (frontmost first)
    bool hit = false;
    csmInt32 tapStart = static_cast<csmInt32>(_models.GetSize()) - 1;
    csmInt32 tapEnd = 0;
    if (_selectedModelSlot >= 0 && _selectedModelSlot < static_cast<int>(_models.GetSize())) {
        tapStart = _selectedModelSlot;
        tapEnd = _selectedModelSlot;
    }
    for (csmInt32 i = tapStart; i >= tapEnd; i--) {
        LAppModel *model = GetModel(i);
        if(model == nullptr || model->GetModel() == nullptr) {
            continue;
        }

        bool modelHit = false;
        ICubismModelSetting *setting = model->GetModelSetting();
        if (setting != nullptr && !_hitAreaConfigs.empty()) {
            for (csmInt32 h = 0; h < setting->GetHitAreasCount(); h++) {
                const csmChar *areaName = setting->GetHitAreaName(h);
                const CubismIdHandle drawID = setting->GetHitAreaId(h);

                HitAreaConfig *matched = nullptr;
                int matchedIdx = -1;
                for (size_t ci = 0; ci < _hitAreaConfigs.size(); ci++) {
                    if (!_hitAreaConfigs[ci].enabled) continue;
                    if (_hitAreaConfigs[ci].id == areaName || _hitAreaConfigs[ci].name == areaName) {
                        matched = &_hitAreaConfigs[ci];
                        matchedIdx = static_cast<int>(ci);
                        break;
                    }
                }

                // Triangle-level hit test with AABB fallback
                bool isHit = false;
                if (matched != nullptr) {
                    isHit = IsHitAreaHit(model, *matched, drawID, hitX, hitY);
                } else if (model->GetModel()) {
                    const CubismMatrix44& mvp = model->GetLastFrameMVP();
                    CubismMatrix44 mvpCopy = mvp;
                    const csmInt32 idx = model->GetModel()->GetDrawableIndex(drawID);
                    const csmInt32 parentPart = idx >= 0 ? model->GetModel()->GetDrawableParentPartIndex(idx) : -1;
                    if (idx >= 0 && model->GetModel()->GetDrawableDynamicFlagIsVisible(idx)
                        && model->GetModel()->GetDrawableOpacity(idx) > 0.0F
                        && (parentPart < 0 || model->GetModel()->GetPartOpacity(parentPart) > 0.0F)) {
                        isHit = TriangleHitTest::HitTestDrawable(
                            model->GetModel(), idx, hitX, hitY, &mvpCopy);
                    }
                }
                if (!isHit) continue;

                if (matched != nullptr) {
                    // Dispatch param toggle only if: touch ended on the same area that was pressed,
                    // and drag didn't already handle it
                    bool sameAreaAsPressed = (matchedIdx == _lastPressedHitAreaIndex);
                    bool didParamDispatch = false;
                    if (!matched->param.empty() && !dragAlreadyHandled && sameAreaAsPressed) {
                        // DispatchHitAreaAction cycles the param, plays motion, and updates
                        // VarFloat via SyncGroupParamOverrides after motion selection.
                        DispatchHitAreaAction(*matched, model, /*skipMotion=*/false);
                        didParamDispatch = true;
                    }

                    // Play motion for non-switch hit areas (no param toggle dispatched).
                    if (!didParamDispatch) {
                        std::string effectiveMotion = matched->motion;
                        if (!effectiveMotion.empty()) {
                            LAppPal::PrintLogLn("[Touch] OnTap: area=[%s] -> configMotion=[%s]", areaName, effectiveMotion.c_str());
                            StartMotionWithVarFloat(model, effectiveMotion.c_str(), PriorityNormal, FinishedMotion, BeganMotion);
                        } else {
                            csmInt32 motionCount = setting->GetMotionCount(areaName);
                            if (motionCount > 0) {
                                LAppPal::PrintLogLn("[Touch] OnTap: area=[%s] -> motionGroup=[%s] (name-matched)", areaName, areaName);
                                StartMotionWithVarFloat(model, areaName, PriorityNormal, FinishedMotion, BeganMotion);
                            } else if (IsHeadFaceArea(areaName)) {
                                LAppPal::PrintLogLn("[Touch] OnTap: area=[%s] -> SetRandomExpression", areaName);
                                model->SetRandomExpression();
                            } else {
                                csmString tapGroup = model->GetTapMotionGroup();
                                LAppPal::PrintLogLn("[Touch] OnTap: area=[%s] -> tapGroup=[%s] (fallback)", areaName, tapGroup.GetRawString());
                                StartMotionWithVarFloat(model, tapGroup.GetRawString(), PriorityNormal, FinishedMotion, BeganMotion);
                            }
                        }
                    }
                } else {
                    csmInt32 motionCount = setting->GetMotionCount(areaName);
                    if (motionCount > 0) {
                        LAppPal::PrintLogLn("[Touch] OnTap: area=[%s] -> motionGroup=[%s]", areaName, areaName);
                        StartMotionWithVarFloat(model, areaName, PriorityNormal, FinishedMotion,
                                                 BeganMotion);
                    } else if (IsHeadFaceArea(areaName)) {
                        LAppPal::PrintLogLn("[Touch] OnTap: area=[%s] -> SetRandomExpression", areaName);
                        model->SetRandomExpression();
                    } else {
                        csmString tapGroup = model->GetTapMotionGroup();
                        LAppPal::PrintLogLn("[Touch] OnTap: area=[%s] -> tapGroup=[%s]", areaName, tapGroup.GetRawString());
                        StartMotionWithVarFloat(model, tapGroup.GetRawString(), PriorityNormal,
                                                 FinishedMotion, BeganMotion);
                    }
                }
                modelHit = true;
                break;
            }
        }

        if (!modelHit) {
            csmString tapGroup = model->GetTapMotionGroup();
            LAppPal::PrintLogLn("[Touch] OnTap: no area hit -> tapGroup=[%s]", tapGroup.GetRawString());
            StartMotionWithVarFloat(model, tapGroup.GetRawString(), PriorityNormal, FinishedMotion,
                                     BeganMotion);
        }
        hit = true;
        break;  // Only the frontmost model responds
    }

    // Handle Cubism 2 models (only when no Cubism 3/4/5 models exist)
    if (!hit) {
        csmInt32 c2TapStart = static_cast<csmInt32>(_modelsCubism2.GetSize()) - 1;
        csmInt32 c2TapEnd = 0;
        if (_selectedModelSlot >= 0 && _selectedModelSlot < static_cast<int>(_modelsCubism2.GetSize())) {
            c2TapStart = _selectedModelSlot;
            c2TapEnd = _selectedModelSlot;
        }
        for (csmInt32 i = c2TapStart; i >= c2TapEnd; i--) {
            LAppModelCubism2 *model = _modelsCubism2[i];
            if (model == nullptr || !model->IsInitialized()) {
                continue;
            }

            bool modelHit = false;
            Cubism2ModelSetting *setting = model->GetModelSetting();
            if (setting != nullptr && !_hitAreaConfigs.empty()) {
                for (csmInt32 h = 0; h < setting->GetHitAreasCount(); h++) {
                    const csmChar *areaName = setting->GetHitAreaName(h);

                    HitAreaConfig *matched = nullptr;
                    int matchedIdx = -1;
                    for (size_t ci = 0; ci < _hitAreaConfigs.size(); ci++) {
                        if (!_hitAreaConfigs[ci].enabled) continue;
                        if (_hitAreaConfigs[ci].id == areaName || _hitAreaConfigs[ci].name == areaName) {
                            matched = &_hitAreaConfigs[ci];
                            matchedIdx = static_cast<int>(ci);
                            break;
                        }
                    }

                    if (!model->HitTest(areaName, hitX, hitY)) continue;

                    if (matched != nullptr) {
                        // Dispatch param toggle only if: touch ended on the same area that was pressed,
                        // and drag didn't already handle it
                        bool sameAreaAsPressed = (matchedIdx == _lastPressedHitAreaIndex);
                        bool didParamDispatch = false;
                        if (!matched->param.empty() && !dragAlreadyHandled && sameAreaAsPressed) {
                            DispatchHitAreaAction(*matched, model, /*skipMotion=*/false);
                            didParamDispatch = true;
                        }

                        // Play motion for non-switch hit areas (no param toggle dispatched).
                        if (!didParamDispatch) {
                            std::string effectiveMotion = matched->motion;
                            if (!effectiveMotion.empty()) {
                                LAppPal::PrintLogLn("[Touch] OnTap(C2): area=[%s] -> configMotion=[%s]", areaName, effectiveMotion.c_str());
                                StartMotionWithVarFloat(model, effectiveMotion.c_str(), PriorityNormal);
                            } else {
                                csmInt32 motionCount = setting->GetMotionCount(areaName);
                                if (motionCount > 0) {
                                    LAppPal::PrintLogLn("[Touch] OnTap(C2): area=[%s] -> motionGroup=[%s] (name-matched)", areaName, areaName);
                                    StartMotionWithVarFloat(model, areaName, PriorityNormal);
                                } else if (IsHeadFaceArea(areaName)) {
                                    LAppPal::PrintLogLn("[Touch] OnTap(C2): area=[%s] -> SetRandomExpression", areaName);
                                    model->SetRandomExpression();
                                } else {
                                    csmString tapGroup = model->GetTapMotionGroup();
                                    LAppPal::PrintLogLn("[Touch] OnTap(C2): area=[%s] -> tapGroup=[%s] (fallback)", areaName, tapGroup.GetRawString());
                                    StartMotionWithVarFloat(model, tapGroup.GetRawString(), PriorityNormal);
                                }
                            }
                        }
                    } else {
                        csmInt32 motionCount = setting->GetMotionCount(areaName);
                        if (motionCount > 0) {
                            LAppPal::PrintLogLn("[Touch] OnTap(C2): area=[%s] -> motionGroup=[%s]", areaName, areaName);
                            StartMotionWithVarFloat(model, areaName, PriorityNormal);
                        } else if (IsHeadFaceArea(areaName)) {
                            LAppPal::PrintLogLn("[Touch] OnTap(C2): area=[%s] -> SetRandomExpression", areaName);
                            model->SetRandomExpression();
                        } else {
                            csmString tapGroup = model->GetTapMotionGroup();
                            LAppPal::PrintLogLn("[Touch] OnTap(C2): area=[%s] -> tapGroup=[%s]", areaName, tapGroup.GetRawString());
                            StartMotionWithVarFloat(model, tapGroup.GetRawString(), PriorityNormal);
                        }
                    }
                    modelHit = true;
                    break;
                }
            }
            if (!modelHit) {
                csmString tapGroup = model->GetTapMotionGroup();
                StartMotionWithVarFloat(model, tapGroup.GetRawString(), PriorityNormal);
            }
            hit = true;
            break;  // Only the frontmost model responds
        }
    }
}

void LAppLive2DManager::ToggleParam(const char *paramName) {
    if(paramName == nullptr || strlen(paramName) == 0) {
        return;
    }

    LAppPal::PrintLogLn("[Toggle] ToggleParam: [%s] hitAreaConfigs=%d groups=%d",
        paramName, (int)_hitAreaConfigs.size(), (int)_modelConfig.groups.size());

    // Check HitAreaConfig first (param + values cycle)
    for (auto &config: _hitAreaConfigs) {
        if (config.param == paramName && !config.values.empty()) {
            int nextIdx = (config.currentIndex + 1) % static_cast<int>(config.values.size());
            config.currentIndex = nextIdx;
            // Find matching GroupConfig and use SetGroupIndex for cascade
            for (auto &g : _modelConfig.groups) {
                if (g.target == config.param || g.var_float == config.param) {
                    // Find matching value index in group
                    for (size_t vi = 0; vi < g.values.size(); vi++) {
                        if (g.values[vi] == config.values[nextIdx]) {
                            SetGroupIndex(g, static_cast<int>(vi));
                            return;
                        }
                    }
                    break;
                }
            }
            // No matching group — just set param override
            _paramOverrides[config.param] = config.values[nextIdx];
            return;
        }
    }

    // Check GroupConfig (Live2DViewerEX component/clothing toggle via Groups)
    for (auto &group : _modelConfig.groups) {
        if (group.target == paramName || group.var_float == paramName) {
            LAppPal::PrintLogLn("[Toggle] ToggleParam: [%s] matched GroupConfig target=[%s] ids=%d values=%d idx=%d",
                paramName, group.target.c_str(), (int)group.ids.size(), (int)group.values.size(), group.currentIndex);
            if (!group.values.empty()) {
                int nextIdx = (group.currentIndex + 1) % static_cast<int>(group.values.size());
                SetGroupIndex(group, nextIdx);
            } else if (!group.ids.empty()) {
                int nextIdx = (group.currentIndex + 1) % static_cast<int>(group.ids.size());
                SetGroupIndex(group, nextIdx);
            } else {
                // No values, no ids — simple 0/1 toggle
                float current = 0.0F;
                auto it = _paramOverrides.find(paramName);
                if (it != _paramOverrides.end()) current = it->second;
                _paramOverrides[paramName] = (current > 0.5F) ? 0.0F : 1.0F;
            }
            return;
        }
    }

    // If no config found, try to toggle with 0/1 default
    LAppPal::PrintLogLn("[Toggle] ToggleParam: [%s] NO config found — fallback 0/1 toggle", paramName);
    auto it = _paramOverrides.find(paramName);
    if (it != _paramOverrides.end()) {
        it->second = (it->second > 0.5F) ? 0.0F : 1.0F;
    } else {
        _paramOverrides[paramName] = 1.0F;
    }
}

void LAppLive2DManager::TransformTouchForModelTransform(float touchX, float touchY,
                                                         float& outX, float& outY) const {
    // Fast path: no transform needed
    if (!_mirror && _rotation == 0.0f && _touchScale == 1.0F
        && _touchOffsetX == 0.0F && _touchOffsetY == 0.0F) {
        outX = touchX;
        outY = touchY;
        return;
    }

    // Step 1: Apply floating window offset (pixel → model-local)
    float x = touchX - _touchOffsetX;
    float y = touchY - _touchOffsetY;

    // Step 2: Apply scale (inverse of model scale from pinch zoom)
    if (_touchScale > 0.0F && _touchScale != 1.0F) {
        x /= _touchScale;
        y /= _touchScale;
    }

    // If only offset/scale changed (no mirror/rotation), we're done
    if (!_mirror && _rotation == 0.0f) {
        outX = x;
        outY = y;
        return;
    }

    // Step 3: Convert to intermediate space (inverse of base projection)
    int width = LAppDelegate::GetInstance()->GetWindowWidth();
    int height = LAppDelegate::GetInstance()->GetWindowHeight();
    if (width <= 0 || height <= 0) {
        outX = x;
        outY = y;
        return;
    }

    float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    float displayRatio = static_cast<float>(height) / static_cast<float>(width);

    float canvasRatio = 1.0f;
    if (_models.GetSize() > 0 && _models[0] && _models[0]->GetModel()) {
        canvasRatio = _models[0]->GetModel()->GetCanvasHeight()
                    / _models[0]->GetModel()->GetCanvasWidth();
    } else if (_modelsCubism2.GetSize() > 0 && _modelsCubism2[0]
               && _modelsCubism2[0]->IsInitialized()) {
        float cw = _modelsCubism2[0]->GetCanvasWidth();
        canvasRatio = (cw > 0) ? _modelsCubism2[0]->GetCanvasHeight() / cw : 1.0f;
    }

    float invProjX, invProjY;
    if (canvasRatio < displayRatio) {
        invProjX = 1.0f;
        invProjY = 1.0f / aspectRatio;
    } else {
        invProjX = aspectRatio;
        invProjY = 1.0f;
    }

    float ix = x * invProjX;
    float iy = y * invProjY;

    // Step 4: Apply inverse mirror (flip X around model center)
    if (_mirror) {
        ix = -ix;
    }

    // Step 5: Apply inverse rotation around model center (0,0)
    if (_rotation != 0.0f) {
        float rad = -_rotation * 3.14159265f / 180.0f;
        float c = cosf(rad), s = sinf(rad);
        float rx = ix * c - iy * s;
        float ry = ix * s + iy * c;
        ix = rx;
        iy = ry;
    }

    // Step 6: Re-apply base projection
    outX = ix / invProjX;
    outY = iy / invProjY;
}

void LAppLive2DManager::OnUpdate() {
    int width = LAppDelegate::GetInstance()->GetWindowWidth();
    int height = LAppDelegate::GetInstance()->GetWindowHeight();

    if (width == 0 || height == 0) {
        return;
    }

    float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    float displayRatio = static_cast<float>(height) / static_cast<float>(width);


    // Per-frame continuous sync: re-evaluate all VarFloat→GroupConfig mappings.
    // Ensures any indirect VarFloat mutations (animation curves, physics, external API)
    // are reflected in GroupConfig state.
    SyncAllVarFloats();

    // Compute VarFloat-driven part overrides once per frame.
    // The motion files do NOT contain PartOpacity curves — visibility is controlled
    // entirely by the VarFloat → GroupConfig → PartOpacity system.
    ApplyVarFloatPartOverrides();

    // Update Cubism 3/4/5 models
    // ALL models must Update() every frame (motions, physics, eye blink keep running).
    // Only Draw() is gated by _selectedModelSlot.
    csmUint32 modelCount = _models.GetSize();

    // Phase 1: Update all models (keeps animations alive for non-selected slots)
    // Motion finish callbacks fire inside model->Update() and may start PartFade.
    // After Update, apply PartFade values directly to the model to override
    // Phase 9's poseOpacity*varFloatVis multiplication (which zeros out the
    // fade when the Pose system or motion sets PartOpacity to 0).
    for (csmUint32 i = 0; i < modelCount; ++i) {
        LAppModel *model = GetModel(i);
        if (model == nullptr || model->GetModel() == nullptr) continue;
        model->Update();

        // Apply PartFade interpolated values directly to the Cubism model.
        // This overrides Phase 9's poseOpacity*varFloatVis which can zero out
        // the fade value when the Pose system or motion's PartOpacity curves
        // set PartOpacity to 0. By setting PartOpacity directly, the fade
        // controls visibility independently of the Pose system.
        if (!_partFades.empty()) {
            CubismModel* cm = model->GetModel();
            if (cm) {
                for (const auto& kv : _partFades) {
                    CubismIdHandle id = CubismFramework::GetIdManager()->GetId(kv.first.c_str());
                    csmInt32 partIndex = cm->GetPartIndex(id);
                    if (partIndex >= 0) {
                        float t = (kv.second.duration > 0.0F)
                            ? (kv.second.elapsed / kv.second.duration) : 1.0F;
                        if (t > 1.0F) t = 1.0F;
                        t = t * t * (3.0F - 2.0F * t);  // smoothstep
                        float fadeOpacity = kv.second.from + (kv.second.to - kv.second.from) * t;
                        cm->SetPartOpacity(partIndex, fadeOpacity);
                    }
                }
            }
        }
    }

    // Phase 2: Draw only the selected model (or all if none selected)
    csmUint32 drawStart = 0;
    csmUint32 drawEnd = modelCount;
    if (_selectedModelSlot >= 0 && static_cast<csmUint32>(_selectedModelSlot) < modelCount) {
        drawStart = static_cast<csmUint32>(_selectedModelSlot);
        drawEnd = drawStart + 1;
    }
    for (csmUint32 i = drawStart; i < drawEnd; ++i) {
        CubismMatrix44 projection;
        LAppModel *model = GetModel(i);

        if (model == nullptr) {
            LAppPal::PrintLogLn("[APP]OnUpdate: model[%d] is nullptr", i);
            continue;
        }

        if (model->GetModel() == nullptr) {
            LAppPal::PrintLogLn("[APP]OnUpdate: model[%d] GetModel() is nullptr", i);
            continue;
        }

        float canvasRatio =
                model->GetModel()->GetCanvasHeight() / model->GetModel()->GetCanvasWidth();

        if (canvasRatio < displayRatio) {
            model->GetModelMatrix()->SetWidth(2.0F);
            projection.Scale(1.0F, aspectRatio);
        } else {
            model->GetModelMatrix()->SetHeight(2.0F);
            projection.Scale(1.0F / aspectRatio, 1.0F);
        }

        // Re-apply layout centering after SetWidth/SetHeight recalculates scale
        if (model->GetModelSetting()) {
            csmMap<csmString, csmFloat32> layout;
            model->GetModelSetting()->GetLayoutMap(layout);
            csmFloat32* m = model->GetModelMatrix()->GetArray();
            for (auto ite = layout.Begin(); ite != layout.End(); ++ite) {
                if (ite->First == "center_x") {
                    m[12] = ite->Second;
                } else if (ite->First == "center_y") {
                    m[13] = ite->Second;
                }
            }
        }

        // Apply mirror (flip X) and rotation (Z-axis) in intermediate space,
        // BEFORE view matrix. Model center is (0,0) here, so transforms
        // are naturally centered on the model.
        if (_mirror) {
            projection.Scale(-1.0f, 1.0f);
        }
        if (_rotation != 0.0f) {
            float rad = _rotation * 3.14159265f / 180.0f;
            float c = cosf(rad), s = sinf(rad);
            CubismMatrix44 rot;
            float* r = rot.GetArray();
            r[0] = c;   r[1] = s;
            r[4] = -s;  r[5] = c;
            projection.MultiplyByMatrix(&rot);
        }

        if (_viewMatrix != nullptr) {
            projection.MultiplyByMatrix(_viewMatrix);
        }

        // Z-depth offset: later models (higher index) are drawn on top (closer to camera)
        if (modelCount > 1) {
            projection.GetArray()[14] = -static_cast<float>(i) * 0.001F;
        }

        model->Draw(projection);

        // Hit area debug overlay
        if (_debugHitAreaVisible) {
            LAppView *view = LAppDelegate::GetInstance()->GetView();
            if(view) view->PostModelDraw(*model);
        }
    }

    // Update Cubism 2 models
    // Same pattern: Update all, Draw only selected.
    csmFloat32 deltaTime = LAppPal::GetDeltaTime();
    csmUint32 c2Count = _modelsCubism2.GetSize();

    // Phase 1: Update all Cubism 2 models
    for (csmUint32 i = 0; i < c2Count; i++) {
        LAppModelCubism2 *model = _modelsCubism2[i];
        if (model == nullptr || !model->IsInitialized()) continue;
        model->Update(deltaTime);
    }

    // Phase 2: Draw only selected
    csmUint32 c2Start = 0;
    csmUint32 c2End = c2Count;
    if (_selectedModelSlot >= 0 && static_cast<csmUint32>(_selectedModelSlot) < c2Count) {
        c2Start = static_cast<csmUint32>(_selectedModelSlot);
        c2End = c2Start + 1;
    }
    for (csmUint32 i = c2Start; i < c2End; i++) {
        LAppModelCubism2 *model = _modelsCubism2[i];
        if (model == nullptr) {
            LAppPal::PrintLogLn("[APP]OnUpdate: cubism2 model[%d] is nullptr", i);
            continue;
        }
        if(!model->IsInitialized()) {
            continue;
        }

        CubismMatrix44 projection;
        float cw = model->GetCanvasWidth();
        float ch = model->GetCanvasHeight();
        float canvasRatio = (cw > 0) ? ch / cw : 1.0F;

        if (canvasRatio < displayRatio) {
            model->GetModelMatrix()->SetWidth(2.0F);
            projection.Scale(1.0F, aspectRatio);
        } else {
            model->GetModelMatrix()->SetHeight(2.0F);
            projection.Scale(1.0F / aspectRatio, 1.0F);
        }

        // Center Cubism 2 model: vertex coords may not be centered at origin
        {
            float cx = (model->GetCanvasMinX() + model->GetCanvasMaxX()) / 2.0F;
            float cy = (model->GetCanvasMinY() + model->GetCanvasMaxY()) / 2.0F;
            csmFloat32* m = model->GetModelMatrix()->GetArray();
            m[12] = -cx * m[0];  // translate X so canvas center maps to viewport center
            m[13] = -cy * m[5];  // translate Y so canvas center maps to viewport center
        }

        // Apply mirror (flip X) and rotation (Z-axis) in intermediate space,
        // BEFORE view matrix. Model center is (0,0) here, so transforms
        // are naturally centered on the model.
        if (_mirror) {
            projection.Scale(-1.0f, 1.0f);
        }
        if (_rotation != 0.0f) {
            float rad = _rotation * 3.14159265f / 180.0f;
            float c = cosf(rad), s = sinf(rad);
            CubismMatrix44 rot;
            float* r = rot.GetArray();
            r[0] = c;   r[1] = s;
            r[4] = -s;  r[5] = c;
            projection.MultiplyByMatrix(&rot);
        }

        if (_viewMatrix != nullptr) {
            projection.MultiplyByMatrix(_viewMatrix);
        }

        // Z-depth offset: later models (higher index) are drawn on top (closer to camera)
        if (_modelsCubism2.GetSize() > 1) {
            projection.GetArray()[14] = -static_cast<float>(i) * 0.001F;
        }

        model->Draw(projection);
    }
}

void LAppLive2DManager::LoadModelFromPath(const char *modelPath) {
    ReleaseAllModel();
    _selectedModelSlot = -1;  // reset selection when replacing all models
    _lastModelPath = modelPath;

    // modelPath is like "/data/.../models/haru/haru.model3.json"
    // Extract directory and filename
    csmString path(modelPath);

    // Find last '/' to split dir and filename
    int lastSlash = -1;
    for (int i = static_cast<int>(path.GetLength()) - 1; i >= 0; i--) {
        if (path.GetRawString()[i] == '/') {
            lastSlash = i;
            break;
        }
    }

    csmString dir = "";
    csmString fileName = path;

    if (lastSlash >= 0) {
        dir = csmString(path.GetRawString(), lastSlash + 1);
        fileName = csmString(path.GetRawString() + lastSlash + 1);
    }

    // Detect model format by file extension
    const char *fn = fileName.GetRawString();
    size_t fnLen = strlen(fn);

    if (fnLen > 12 && strcmp(fn + fnLen - 12, ".model3.json") == 0) {
        // Cubism 3/4/5 model
        _isCubism2 = false;
        LAppModel *model = new LAppModel();
        model->LoadAssets(dir.GetRawString(), fileName.GetRawString());
        if (model->GetModel() != nullptr) {
            _models.PushBack(model);
            // Ensure the new model gets the current render target size
            if (_renderWidth > 0 && _renderHeight > 0) {
                model->SetRenderTargetSize(_renderWidth, _renderHeight);
            }
            LAppPal::PrintLogLn("[APP]LoadModel OK (Cubism3+), models=%d render=%dx%d",
                                (int) _models.GetSize(), (int) _renderWidth, (int) _renderHeight);
            AutoGenerateHitAreas();
        } else {
            LAppPal::PrintLogLn("[APP]LoadModel failed, model is null: %s", modelPath);
            delete model;
        }
    } else if (fnLen > 10 && strcmp(fn + fnLen - 10, ".model.json") == 0) {
        // Cubism 2 model
        _isCubism2 = true;
        LAppModelCubism2 *model = new LAppModelCubism2();
        model->LoadAssets(dir.GetRawString(), fileName.GetRawString());
        if (model->IsInitialized()) {
            // Register motion sequencer callbacks for chaining and VarFloat
            model->AddMotionFinishedListener(MotionSequencer::OnMotionFinishedCubism2);
            model->AddMotionBeganListener(MotionSequencer::OnMotionBeganCubism2);
            _modelsCubism2.PushBack(model);
            LAppPal::PrintLogLn("[APP]LoadModel OK (Cubism2), models=%d render=%dx%d",
                                (int) _modelsCubism2.GetSize(), (int) _renderWidth,
                                (int) _renderHeight);
            AutoGenerateHitAreas();
            AutoPopulateHitAreaParams();
        } else {
            LAppPal::PrintLogLn("[APP]LoadModel failed (cubism2), model not initialized: %s",
                                modelPath);
            delete model;
        }
    } else {
        LAppPal::PrintLogLn("[APP]Unknown model format: %s", fn);
    }

    // Always parse model3.json for VarFloat definitions and merge into _modelConfig.
    // VarFloats are defined in model3.json motion groups, not in config.mlve.
    if (!_isCubism2) {
        csmSizeInt fileSize = 0;
        csmByte *buf = LAppPal::LoadFileAsBytes(modelPath, &fileSize);
        if (buf != nullptr && fileSize > 0) {
            std::string jsonStr(reinterpret_cast<char *>(buf), fileSize);
            LAppPal::ReleaseBytes(buf);
            ModelConfig model3Config = ParseModelConfig(jsonStr);
            if (!model3Config.motions.empty()) {
                int newGroups = 0, vfUpdated = 0;
                for (auto& pair : model3Config.motions) {
                    auto it = _modelConfig.motions.find(pair.first);
                    if (it == _modelConfig.motions.end()) {
                        // New group from model3.json — add it entirely
                        _modelConfig.motions[pair.first] = std::move(pair.second);
                        newGroups++;
                    } else if (pair.second.size() == it->second.size()) {
                        // Same group, same entry count — merge VarFloats by index
                        for (size_t i = 0; i < pair.second.size(); i++) {
                            if (!pair.second[i].var_floats.empty() && it->second[i].var_floats.empty()) {
                                it->second[i].var_floats = std::move(pair.second[i].var_floats);
                                vfUpdated++;
                            }
                        }
                    } else {
                        // Same group name but different entry count — use model3.json entries
                        // (model3.json is authoritative for VarFloats and motion file references)
                        LAppPal::PrintLogLn("[APP]LoadModel: group [%s] entry mismatch config=%d model3=%d, using model3",
                                            pair.first.c_str(), (int)it->second.size(), (int)pair.second.size());
                        _modelConfig.motions[pair.first] = std::move(pair.second);
                        newGroups++;
                    }
                }
                LAppPal::PrintLogLn("[APP]LoadModel: model3.json %d groups added/replaced, %d entries VarFloat-updated",
                                    newGroups, vfUpdated);
            }
        }
    }

    // Clear stale groups and param assignments from the first AutoPopulateHitAreaParams call
    // (which ran before model3.json was parsed with empty motions). Hit areas and groups
    // need to be re-created from the complete model3.json data including VarFloat definitions.
    _modelConfig.groups.clear();
    _varFloatGroupMap.clear();
    for (auto& cfg : _hitAreaConfigs) {
        cfg.param.clear();
        cfg.values.clear();
    }

    AutoPopulateHitAreaParams();
}

void LAppLive2DManager::LoadModelAt(const char *modelPath, int slot) {
    _controllerEngine.OnTouchCancelled();

    // Extract directory and filename
    csmString path(modelPath);
    int lastSlash = -1;
    for (int i = static_cast<int>(path.GetLength()) - 1; i >= 0; i--) {
        if (path.GetRawString()[i] == '/') {
            lastSlash = i;
            break;
        }
    }

    csmString dir = "";
    csmString fileName = path;
    if (lastSlash >= 0) {
        dir = csmString(path.GetRawString(), lastSlash + 1);
        fileName = csmString(path.GetRawString() + lastSlash + 1);
    }

    const char *fn = fileName.GetRawString();
    size_t fnLen = strlen(fn);

    if (fnLen > 12 && strcmp(fn + fnLen - 12, ".model3.json") == 0) {
        _isCubism2 = false;
        LAppModel *model = new LAppModel();
        model->LoadAssets(dir.GetRawString(), fileName.GetRawString());
        if (model->GetModel() != nullptr) {
            // Ensure slot exists
            while (static_cast<int>(_models.GetSize()) <= slot) {
                _models.PushBack(nullptr);
            }
            // Remove existing model at slot
            if (_models[slot] != nullptr) {
                delete _models[slot];
            }
            _models[slot] = model;
            if (_renderWidth > 0 && _renderHeight > 0) {
                model->SetRenderTargetSize(_renderWidth, _renderHeight);
            }
            AutoGenerateHitAreas();
            AutoPopulateHitAreaParams();

            // Parse model3.json for VarFloat definitions (same as LoadModel)
            {
                csmSizeInt fileSize = 0;
                std::string model3Path = dir.GetRawString() + std::string(fileName.GetRawString());
                csmByte *buf = LAppPal::LoadFileAsBytes(model3Path.c_str(), &fileSize);
                if (buf != nullptr && fileSize > 0) {
                    std::string jsonStr(reinterpret_cast<char *>(buf), fileSize);
                    LAppPal::ReleaseBytes(buf);
                    ModelConfig model3Config = ParseModelConfig(jsonStr);
                    if (!model3Config.motions.empty()) {
                        for (auto& pair : model3Config.motions) {
                            auto it = _modelConfig.motions.find(pair.first);
                            if (it == _modelConfig.motions.end()) {
                                _modelConfig.motions[pair.first] = std::move(pair.second);
                            } else if (pair.second.size() == it->second.size()) {
                                for (size_t i = 0; i < pair.second.size(); i++) {
                                    if (!pair.second[i].var_floats.empty() && it->second[i].var_floats.empty()) {
                                        it->second[i].var_floats = std::move(pair.second[i].var_floats);
                                    }
                                }
                            } else {
                                _modelConfig.motions[pair.first] = std::move(pair.second);
                            }
                        }
                    }
                }
            }

            // Re-create groups and re-match hit areas with VarFloat-linked groups
            _modelConfig.groups.clear();
            _varFloatGroupMap.clear();
            for (auto& cfg : _hitAreaConfigs) {
                cfg.param.clear();
                cfg.values.clear();
            }
            AutoPopulateHitAreaParams();

            LAppPal::PrintLogLn("[APP]LoadModelAt OK slot=%d total=%d", slot,
                                (int) _models.GetSize());
        } else {
            delete model;
        }
    } else if (fnLen > 10 && strcmp(fn + fnLen - 10, ".model.json") == 0) {
        _isCubism2 = true;
        LAppModelCubism2 *model = new LAppModelCubism2();
        model->LoadAssets(dir.GetRawString(), fileName.GetRawString());
        if (model->IsInitialized()) {
            model->AddMotionFinishedListener(MotionSequencer::OnMotionFinishedCubism2);
            model->AddMotionBeganListener(MotionSequencer::OnMotionBeganCubism2);
            while (static_cast<int>(_modelsCubism2.GetSize()) <= slot) {
                _modelsCubism2.PushBack(nullptr);
            }
            if (_modelsCubism2[slot] != nullptr) {
                delete _modelsCubism2[slot];
            }
            _modelsCubism2[slot] = model;
            AutoPopulateHitAreaParams();
            LAppPal::PrintLogLn("[APP]LoadModelAt OK (Cubism2) slot=%d", slot);
        } else {
            delete model;
        }
    }
}

void LAppLive2DManager::RemoveModel(int slot) {
    // Check both vectors — _isCubism2 is a global flag that may not match
    // the model type in a specific slot if mixed types were loaded.
    if (slot >= 0 && slot < static_cast<int>(_models.GetSize())) {
        if (_models[slot] != nullptr) {
            delete _models[slot];
            _models[slot] = nullptr;
        }
    }
    if (slot >= 0 && slot < static_cast<int>(_modelsCubism2.GetSize())) {
        if (_modelsCubism2[slot] != nullptr) {
            delete _modelsCubism2[slot];
            _modelsCubism2[slot] = nullptr;
        }
    }
    // If the removed slot was selected, fall back to showing all
    if (_selectedModelSlot == slot) {
        _selectedModelSlot = -1;
    }
}

void LAppLive2DManager::SelectModel(int slot) {
    _selectedModelSlot = slot;
    LAppPal::PrintLogLn("[APP]SelectModel: slot=%d", slot);
}

csmUint32 LAppLive2DManager::GetModelNum() const {
    return _models.GetSize();
}

csmUint32 LAppLive2DManager::GetCubism2ModelNum() const {
    return _modelsCubism2.GetSize();
}

// --- Full Model Config ---

static bool ContainsIdCaseInsensitive(const std::vector<std::string>& ids, const std::string& target) {
    for (const auto& id : ids) {
        if (id.size() != target.size()) continue;
        bool match = true;
        for (size_t i = 0; i < id.size(); i++) {
            if (tolower(static_cast<unsigned char>(id[i])) != tolower(static_cast<unsigned char>(target[i]))) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

static bool IsClothingPart(const std::string& name) {
    // Skip generic Part names like "Part", "Part2", "BG"
    if (name == "BG" || name == "Part" || name == "Head" || name == "Body") return false;
    if (name.size() >= 4 && name.substr(0, 4) == "Part") {
        // Check if it's just "PartN" (generic)
        bool allDigits = true;
        for (size_t i = 4; i < name.size(); i++) {
            if (!isdigit(static_cast<unsigned char>(name[i]))) { allDigits = false; break; }
        }
        if (allDigits) return false;
    }

    // Recognized clothing/accessory keywords (case-insensitive, English only — no language-specific mappings)
    static const char* keywords[] = {
        "cloth", "dress", "skirt", "shirt", "pants", "trouser",
        "hat", "cap", "bag", "shoe", "boot", "sock", "stocking",
        "ring", "necklace", "bracelet", "glove", "scarf", "belt",
        "accessory", "wear", "outfit", "armor", "cape", "wing",
        "hair", "tail", "ear", "horn", "ribbon", "bow"
    };
    std::string lower = name;
    for (auto& c : lower) c = tolower(static_cast<unsigned char>(c));
    for (const char* kw : keywords) {
        std::string kwLower = kw;
        for (auto& c : kwLower) c = tolower(static_cast<unsigned char>(c));
        if (lower.find(kwLower) != std::string::npos) return true;
    }
    return false;
}

/// Check if a Part ID is a generic auto-generated name (e.g., "Part6", "Part0").
static bool IsGenericPartId(const std::string& id) {
    if (id.size() < 5 || id.substr(0, 4) != "Part") return false;
    for (size_t i = 4; i < id.size(); i++) {
        if (!isdigit(static_cast<unsigned char>(id[i]))) return false;
    }
    return true;
}

/// Try to match a VarFloat suffix to Part IDs using generic name similarity.
/// Returns ALL matching Part IDs (not just the first), so a VarFloat like "pidai"
/// can control multiple related Parts (PD, DW_PD_JB, DW_PD_KU, DW_PD_NEIKU).
///
/// Strategies (in priority order — higher strategies take precedence):
///   1. Exact case-insensitive match (suffix == PartID) → single result
///   2. Prefix: suffix is a prefix of PartID → ALL prefixed Parts
///      e.g., "PD" matches "PD", "PD_JB", "PD_KU", "DW_PD_JB", etc.
///   3. Substring: suffix contained in PartID → ALL containing Parts
///   4. Pinyin abbreviation: suffix initials match a Part ID → single + prefixed
///   5. Ordered character matching → single result
///   6. Levenshtein distance → single best result
/// Returns empty vector if no match found.
static std::vector<std::string> MatchVarFloatSuffixToPartIds(
    const std::string& suffix, const std::vector<std::string>& allPartIds)
{
    auto toLower = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        return s;
    };
    std::string suffixLower = toLower(suffix);

    auto isVowel = [](char c) {
        return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'v';
    };

    // Strategy 1: Exact case-insensitive match
    for (const auto& pid : allPartIds) {
        if (toLower(pid) == suffixLower) return {pid};
    }

    // Strategy 2: Prefix match — suffix is prefix of PartID, collect ALL matches.
    // e.g., "PD" matches "PD", "PD_JB", "DW_PD_JB", "DW_PD_KU", etc.
    // Also handles DW_ prefix: "neiku" → check if "neiku" is prefix of any Part,
    // or if any Part starts with "DW_" + suffix.
    if (suffixLower.size() >= 2) {
        std::vector<std::string> matches;
        for (const auto& pid : allPartIds) {
            if (IsGenericPartId(pid)) continue;
            std::string pidLower = toLower(pid);
            if (pidLower.find(suffixLower) == 0) {
                matches.push_back(pid);
            }
        }
        // Also match DW_ prefixed Parts: "neiku" → "DW_NEIKU"
        if (matches.empty()) {
            for (const auto& pid : allPartIds) {
                if (IsGenericPartId(pid)) continue;
                std::string pidLower = toLower(pid);
                if (pidLower.size() > 3 && pidLower.substr(0, 3) == "dw_") {
                    std::string dwSuffix = pidLower.substr(3);
                    if (dwSuffix.find(suffixLower) == 0 || suffixLower.find(dwSuffix) == 0) {
                        matches.push_back(pid);
                    }
                }
            }
        }
        if (!matches.empty()) return matches;
    }

    // Strategy 3: Substring — suffix contained in PartID (min 3 chars)
    if (suffixLower.size() >= 3) {
        std::vector<std::string> matches;
        for (const auto& pid : allPartIds) {
            if (IsGenericPartId(pid)) continue;
            std::string pidLower = toLower(pid);
            if (pidLower.size() >= 3 && pidLower.find(suffixLower) != std::string::npos) {
                matches.push_back(pid);
            }
        }
        if (!matches.empty()) return matches;
    }

    // Strategy 4: Pinyin abbreviation — Part ID is initials of VarFloat suffix syllables
    // e.g., "pidai" → syllables "pi","dai" → initials "PD" matches Part "PD"
    // Then also collect all Parts prefixed with that abbreviation.
    if (suffixLower.size() >= 3 && !allPartIds.empty()) {
        std::vector<char> initials;
        for (size_t i = 0; i < suffixLower.size(); i++) {
            if (i == 0) {
                initials.push_back(suffixLower[i]);
            } else if (!isVowel(suffixLower[i]) && i + 1 < suffixLower.size() && isVowel(suffixLower[i + 1])) {
                initials.push_back(suffixLower[i]);
            }
        }

        if (initials.size() >= 2) {
            std::string abbr(initials.begin(), initials.end());
            std::vector<std::string> matches;
            // First pass: exact match on abbreviation
            for (const auto& pid : allPartIds) {
                if (toLower(pid) == abbr) matches.push_back(pid);
            }
            // Second pass: prefix match on abbreviation
            for (const auto& pid : allPartIds) {
                if (IsGenericPartId(pid)) continue;
                std::string pidLower = toLower(pid);
                if (pidLower != abbr && pidLower.find(abbr) == 0) {
                    matches.push_back(pid);
                }
            }
            if (!matches.empty()) return matches;
        }
    }

    // Strategy 5: Ordered character matching — Part ID chars appear in suffix in order
    if (suffixLower.size() >= 4) {
        for (const auto& pid : allPartIds) {
            if (IsGenericPartId(pid)) continue;
            std::string pidLower = toLower(pid);
            if (pidLower.size() < 2 || pidLower.size() > suffixLower.size()) continue;
            size_t si = 0;
            bool allFound = true;
            for (char c : pidLower) {
                bool found = false;
                for (size_t j = si; j < suffixLower.size(); j++) {
                    if (suffixLower[j] == c) { si = j + 1; found = true; break; }
                }
                if (!found) { allFound = false; break; }
            }
            if (allFound) return {pid};
        }
    }

    // Strategy 6: Levenshtein distance — fuzzy match for short strings
    auto levenshtein = [](const std::string& a, const std::string& b) -> int {
        size_t na = a.size(), nb = b.size();
        if (na == 0) return static_cast<int>(nb);
        if (nb == 0) return static_cast<int>(na);
        std::vector<int> prev(nb + 1), curr(nb + 1);
        for (size_t j = 0; j <= nb; j++) prev[j] = static_cast<int>(j);
        for (size_t i = 1; i <= na; i++) {
            curr[0] = static_cast<int>(i);
            for (size_t j = 1; j <= nb; j++) {
                int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
                curr[j] = std::min({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost});
            }
            std::swap(prev, curr);
        }
        return prev[nb];
    };

    if (suffixLower.size() >= 3) {
        int bestDist = 999;
        std::string bestPid;
        for (const auto& pid : allPartIds) {
            if (IsGenericPartId(pid)) continue;
            std::string pidLower = toLower(pid);
            if (pidLower.size() < suffixLower.size() / 2 || pidLower.size() > suffixLower.size() * 2) continue;
            int dist = levenshtein(suffixLower, pidLower);
            int threshold = static_cast<int>(std::max(suffixLower.size(), pidLower.size())) / 3;
            if (threshold < 2) threshold = 2;
            if (dist < bestDist && dist <= threshold) {
                bestDist = dist;
                bestPid = pid;
            }
        }
        if (!bestPid.empty()) return {bestPid};
    }

    return {};
}

/// Infer VarFloat→Part mappings from HitAreas→Motion Group→VarFloat chain.
/// This is the most precise inference method: model3.json HitAreas explicitly link
/// ArtMesh IDs to motion groups, and motion groups contain VarFloat definitions.
/// By tracing this chain, we can determine which Part each VarFloat controls.
///
/// Example chain:
///   HitArea {Id:"HitArea_PIDAI", motion:"开关-皮带"}
///   Motion group "开关-皮带" → motion entry → VarFloat {name:"1.pidai", assign:1}
///   ArtMesh "HitArea_PIDAI" → Drawable → parent Part "PD"
///   Result: VarFloat "1.pidai" controls Part "PD"
void LAppLive2DManager::InferGroupsFromHitAreas() {
    LAppModel* model = GetPrimaryCubism3Model(this);
    CubismModel* cubismModel = model ? model->GetModel() : nullptr;
    if (!cubismModel || _hitAreaConfigs.empty()) return;

    // Build reverse index: VarFloat name → set of motion group names that reference it
    std::map<std::string, std::set<std::string>> vfToGroups;
    for (const auto& kv : _modelConfig.motions) {
        for (const auto& meta : kv.second) {
            for (const auto& vf : meta.var_floats) {
                if (!vf.name.empty()) {
                    vfToGroups[vf.name].insert(kv.first);
                }
            }
        }
    }
    if (vfToGroups.empty()) return;

    // Build reverse index: motion group name → HitAreaConfigs that target it
    std::map<std::string, std::vector<size_t>> groupToHitAreas;
    for (size_t i = 0; i < _hitAreaConfigs.size(); i++) {
        if (!_hitAreaConfigs[i].motion.empty()) {
            groupToHitAreas[_hitAreaConfigs[i].motion].push_back(i);
        }
    }

    csmInt32 drawableCount = cubismModel->GetDrawableCount();
    csmInt32 partCount = cubismModel->GetPartCount();

    // Helper: find parent Part ID for a drawable name (ArtMesh or HitArea)
    auto findParentPartId = [&](const std::string& drawName) -> std::string {
        // Try direct drawable lookup by name
        for (csmInt32 d = 0; d < drawableCount; d++) {
            CubismIdHandle drawId = cubismModel->GetDrawableId(d);
            if (drawName == drawId->GetString().GetRawString()) {
                csmInt32 parentPart = cubismModel->GetDrawableParentPartIndex(d);
                if (parentPart >= 0 && parentPart < partCount) {
                    return cubismModel->GetPartId(parentPart)->GetString().GetRawString();
                }
                break;
            }
        }
        // Try stripping "HitArea_" prefix (model3.json HitArea Ids often use this pattern)
        if (drawName.size() > 9 && drawName.substr(0, 9) == "HitArea_") {
            std::string stripped = drawName.substr(9);
            for (csmInt32 d = 0; d < drawableCount; d++) {
                CubismIdHandle drawId = cubismModel->GetDrawableId(d);
                std::string dn = drawId->GetString().GetRawString();
                // Match by suffix (case-insensitive)
                auto toLower = [](std::string s) {
                    for (auto& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
                    return s;
                };
                if (toLower(dn).find(toLower(stripped)) != std::string::npos) {
                    csmInt32 parentPart = cubismModel->GetDrawableParentPartIndex(d);
                    if (parentPart >= 0 && parentPart < partCount) {
                        return cubismModel->GetPartId(parentPart)->GetString().GetRawString();
                    }
                }
            }
        }
        return "";
    };

    int inferred = 0;
    for (const auto& vfPair : vfToGroups) {
        const std::string& vfName = vfPair.first;

        // Skip if a group with this var_float already exists
        bool exists = false;
        for (const auto& g : _modelConfig.groups) {
            if (g.var_float == vfName) { exists = true; break; }
        }
        if (exists) continue;

        // Collect all Part IDs from HitAreas that reference this VarFloat
        std::set<std::string> matchedPartIds;
        for (const auto& groupName : vfPair.second) {
            auto hitIt = groupToHitAreas.find(groupName);
            if (hitIt == groupToHitAreas.end()) continue;

            for (size_t hi : hitIt->second) {
                const auto& ha = _hitAreaConfigs[hi];
                // Try the ArtMesh/HitArea ID
                if (!ha.id.empty()) {
                    std::string partId = findParentPartId(ha.id);
                    if (!partId.empty() && !IsGenericPartId(partId)) matchedPartIds.insert(partId);
                }
                // Also try the hit area name (may be different from id)
                if (!ha.name.empty() && ha.name != ha.id) {
                    std::string partId = findParentPartId(ha.name);
                    if (!partId.empty() && !IsGenericPartId(partId)) matchedPartIds.insert(partId);
                }
            }
        }

        if (matchedPartIds.empty()) continue;

        // Determine values array from VarFloat assignments in motion entries
        std::set<float> assignValues;
        for (const auto& kv : _modelConfig.motions) {
            for (const auto& meta : kv.second) {
                for (const auto& vf : meta.var_floats) {
                    if (vf.name == vfName && vf.type == 2 && vf.has_assign) {
                        assignValues.insert(vf.assign);
                    }
                }
            }
        }

        // Extract suffix for display name
        std::string suffix = vfName;
        size_t dot = vfName.rfind('.');
        if (dot != std::string::npos) suffix = vfName.substr(dot + 1);

        GroupConfig g;
        g.name = suffix;
        g.text = suffix;
        g.target = "ParamAuto_" + suffix;
        g.var_float = vfName;
        for (const auto& pid : matchedPartIds) {
            g.ids.push_back(pid);
        }

        if (assignValues.size() > 2) {
            for (float v : assignValues) g.values.push_back(v);
        } else {
            g.values = {0.0F, 1.0F};
        }
        g.currentIndex = 0;

        _modelConfig.groups.push_back(g);
        inferred++;
        LAppPal::PrintLogLn("[APP]InferHitAreas: VarFloat [%s] → Part(s) [%s]",
            vfName.c_str(), [&]() {
                std::string s;
                for (const auto& pid : matchedPartIds) {
                    if (!s.empty()) s += ", ";
                    s += pid;
                }
                return s;
            }().c_str());
    }

    if (inferred > 0) {
        LAppPal::PrintLogLn("[APP]InferHitAreas: inferred %d groups from HitArea chain", inferred);
    }
}

/// Infer VarFloat→Part mappings from motion3.json Curves.
/// For VarFloats not covered by HitArea chain, check if the motion files' Curves
/// contain Target:"Part" references. If all motions in a group operate on the same
/// Part, that Part is likely the VarFloat's control target.
void LAppLive2DManager::InferGroupsFromMotionCurves() {
    if (_lastModelPath.empty()) return;

    // Extract model directory from _lastModelPath
    std::string modelDir;
    size_t lastSlash = _lastModelPath.rfind('/');
    if (lastSlash != std::string::npos) {
        modelDir = _lastModelPath.substr(0, lastSlash + 1);
    }

    // Collect VarFloat names that already have groups
    std::set<std::string> mappedVfNames;
    for (const auto& g : _modelConfig.groups) {
        if (!g.var_float.empty()) mappedVfNames.insert(g.var_float);
    }

    // Collect all VarFloat names from motion metadata
    std::map<std::string, std::set<std::string>> vfToGroups;
    for (const auto& kv : _modelConfig.motions) {
        for (const auto& meta : kv.second) {
            for (const auto& vf : meta.var_floats) {
                if (!vf.name.empty() && mappedVfNames.find(vf.name) == mappedVfNames.end()) {
                    vfToGroups[vf.name].insert(kv.first);
                }
            }
        }
    }
    if (vfToGroups.empty()) return;

    int inferred = 0;
    for (const auto& vfPair : vfToGroups) {
        const std::string& vfName = vfPair.first;

        // Check override map first
        auto overrideIt = _modelConfig.var_float_part_overrides.find(vfName);
        if (overrideIt != _modelConfig.var_float_part_overrides.end() && !overrideIt->second.empty()) {
            std::string suffix = vfName;
            size_t dot = vfName.rfind('.');
            if (dot != std::string::npos) suffix = vfName.substr(dot + 1);

            GroupConfig g;
            g.name = suffix;
            g.text = suffix;
            g.target = "ParamAuto_" + suffix;
            g.var_float = vfName;
            g.ids = overrideIt->second;
            g.values = {0.0F, 1.0F};
            g.currentIndex = 0;
            _modelConfig.groups.push_back(g);
            mappedVfNames.insert(vfName);
            inferred++;
            continue;
        }

        // Collect Part IDs from motion Curves across all groups referencing this VarFloat
        std::map<std::string, int> partIdCounts;  // Part ID → occurrence count
        int totalMotions = 0;

        for (const auto& groupName : vfPair.second) {
            auto motionIt = _modelConfig.motions.find(groupName);
            if (motionIt == _modelConfig.motions.end()) continue;

            for (const auto& meta : motionIt->second) {
                // Check if this motion actually references the VarFloat
                bool refsVf = false;
                for (const auto& vf : meta.var_floats) {
                    if (vf.name == vfName) { refsVf = true; break; }
                }
                if (!refsVf) continue;

                // Load motion file to check Curves
                if (meta.file.empty()) continue;
                std::string motionPath = modelDir + meta.file;

                csmSizeInt fileSize = 0;
                csmByte* buf = LAppPal::LoadFileAsBytes(motionPath.c_str(), &fileSize);
                if (buf == nullptr || fileSize == 0) continue;

                std::string jsonStr(reinterpret_cast<char*>(buf), fileSize);
                LAppPal::ReleaseBytes(buf);

                try {
                    nlohmann::json mj = nlohmann::json::parse(jsonStr);
                    if (mj.contains("Curves") && mj["Curves"].is_array()) {
                        for (const auto& curve : mj["Curves"]) {
                            if (!curve.contains("Target") || !curve.contains("Id")) continue;
                            std::string target = curve["Target"].get<std::string>();
                            if (target == "Part" || target == "PartOpacity") {
                                std::string partId = curve["Id"].get<std::string>();
                                partIdCounts[partId]++;
                            }
                        }
                    }
                } catch (...) {
                    // Skip malformed motion files
                }
                totalMotions++;
            }
        }

        if (partIdCounts.empty() || totalMotions == 0) continue;

        // Find the most common Part ID(s) — must appear in majority of motions
        int threshold = (totalMotions + 1) / 2;
        std::set<std::string> matchedParts;
        for (const auto& pc : partIdCounts) {
            if (pc.second >= threshold) {
                matchedParts.insert(pc.first);
            }
        }

        // Filter out generic Part IDs — they appear in too many motions and cause
        // false matches where ALL hit areas link to the same group.
        std::set<std::string> realParts;
        for (const auto& pid : matchedParts) {
            if (!IsGenericPartId(pid)) realParts.insert(pid);
        }

        // If only generic Parts remain, skip creating a group from motion curves.
        // The suffix matching layer (Layer 1) or fallback layer will handle it.
        if (realParts.empty()) continue;

        std::string suffix = vfName;
        size_t dot = vfName.rfind('.');
        if (dot != std::string::npos) suffix = vfName.substr(dot + 1);

        GroupConfig g;
        g.name = suffix;
        g.text = suffix;
        g.target = "ParamAuto_" + suffix;
        g.var_float = vfName;
        for (const auto& pid : realParts) {
            g.ids.push_back(pid);
        }

        // Collect assign values
        std::set<float> assignValues;
        for (const auto& kv : _modelConfig.motions) {
            for (const auto& meta : kv.second) {
                for (const auto& vf : meta.var_floats) {
                    if (vf.name == vfName && vf.type == 2 && vf.has_assign) {
                        assignValues.insert(vf.assign);
                    }
                }
            }
        }
        if (assignValues.size() > 2) {
            for (float v : assignValues) g.values.push_back(v);
        } else {
            g.values = {0.0F, 1.0F};
        }
        g.currentIndex = 0;

        _modelConfig.groups.push_back(g);
        mappedVfNames.insert(vfName);
        inferred++;
        LAppPal::PrintLogLn("[APP]InferMotionCurves: VarFloat [%s] → Part(s) [%s] (from %d motions)",
            vfName.c_str(), [&]() {
                std::string s;
                for (const auto& pid : matchedParts) {
                    if (!s.empty()) s += ", ";
                    s += pid;
                }
                return s;
            }().c_str(), totalMotions);
    }

    if (inferred > 0) {
        LAppPal::PrintLogLn("[APP]InferMotionCurves: inferred %d groups from motion Curves", inferred);
    }
}

/// Auto-generate GroupConfig entries from VarFloat definitions in motion metadata.
/// Uses a 3-layer inference strategy (aligned with ViewerEX data-driven approach):
///   Layer 1: HitAreas → Motion Group → VarFloat chain (most precise)
///   Layer 2: Motion Curves → Part target references
///   Layer 3: Name similarity matching + user override map (fallback)
///
/// Unlike the old implementation, this does NOT skip all generation when some groups
/// already have var_float links. Instead, it only skips VarFloats that already have
/// a corresponding GroupConfig.
void LAppLive2DManager::AutoGenerateVarFloatGroups() {
    // Collect all unique VarFloat names from motion metadata
    std::set<std::string> vfNames;
    for (const auto& kv : _modelConfig.motions) {
        for (const auto& meta : kv.second) {
            for (const auto& vf : meta.var_floats) {
                if (!vf.name.empty()) vfNames.insert(vf.name);
            }
        }
    }
    if (vfNames.empty()) return;

    // Layer 1 (HIGHEST PRIORITY): Name similarity + override map.
    // Run FIRST because it uses direct suffix→Part matching which is most reliable.
    // Motion curve inference can create false positives (e.g., menu motions containing
    // clothing PartOpacity curves), so suffix matching must take precedence.
    LAppModel* model = GetPrimaryCubism3Model(this);
    CubismModel* cubismModel = model ? model->GetModel() : nullptr;
    csmInt32 partCount = cubismModel ? cubismModel->GetPartCount() : 0;

    std::vector<std::string> allPartIds;
    if (partCount > 0) {
        for (csmInt32 p = 0; p < partCount; p++) {
            CubismIdHandle partId = cubismModel->GetPartId(p);
            allPartIds.push_back(partId->GetString().GetRawString());
        }
    }

    // Diagnostic: log all available Parts for debugging VarFloat→Part mapping
    LAppPal::PrintLogLn("[APP]AutoVarFloatGroups: model=%p cubismModel=%p partCount=%d allPartIds=%d",
        model, cubismModel, (int)partCount, (int)allPartIds.size());
    if (!allPartIds.empty()) {
        std::string partList;
        for (const auto& pid : allPartIds) {
            if (!partList.empty()) partList += ", ";
            partList += pid;
        }
        LAppPal::PrintLogLn("[APP]AutoVarFloatGroups: available Parts (%d): [%s]",
                            (int)allPartIds.size(), partList.c_str());
    }

    int generated = 0;
    for (const auto& vfName : vfNames) {
        // Skip if a group with this var_float already exists
        bool exists = false;
        for (const auto& g : _modelConfig.groups) {
            if (g.var_float == vfName) { exists = true; break; }
        }
        if (exists) continue;

        // Check user override map first
        auto overrideIt = _modelConfig.var_float_part_overrides.find(vfName);
        if (overrideIt != _modelConfig.var_float_part_overrides.end() && !overrideIt->second.empty()) {
            std::string suffix = vfName;
            size_t dot = vfName.rfind('.');
            if (dot != std::string::npos) suffix = vfName.substr(dot + 1);

            GroupConfig g;
            g.name = suffix;
            g.text = suffix;
            g.target = "ParamAuto_" + suffix;
            g.var_float = vfName;
            g.ids = overrideIt->second;
            g.values = {0.0F, 1.0F};

            // Initialize VarFloat=1.0 to match the part's default visible state.
            // Parts are visible by default; first click should toggle to hidden.
            _modelConfig.var_floats[vfName] = 1.0F;
            g.currentIndex = 1;

            _modelConfig.groups.push_back(g);
            generated++;
            LAppPal::PrintLogLn("[APP]AutoVarFloatGroups: override [%s] → [%s]",
                vfName.c_str(), [&]() {
                    std::string s;
                    for (const auto& pid : overrideIt->second) {
                        if (!s.empty()) s += ", ";
                        s += pid;
                    }
                    return s;
                }().c_str());
            continue;
        }

        // Extract suffix: "1.pidai" → "pidai"
        std::string suffix = vfName;
        size_t dot = vfName.rfind('.');
        if (dot != std::string::npos) suffix = vfName.substr(dot + 1);

        // Try to match suffix to Part ID(s) using generic name similarity.
        // Returns ALL matching Parts (e.g., "pidai" → PD, DW_PD_JB, DW_PD_KU, DW_PD_NEIKU).
        std::vector<std::string> matchedPartIds = MatchVarFloatSuffixToPartIds(suffix, allPartIds);
        if (matchedPartIds.empty()) continue;

        // Determine values array from VarFloat assignments in motion entries
        std::set<float> assignValues;
        for (const auto& kv : _modelConfig.motions) {
            for (const auto& meta : kv.second) {
                for (const auto& vf : meta.var_floats) {
                    if (vf.name == vfName && vf.type == 2 && vf.has_assign) {
                        assignValues.insert(vf.assign);
                    }
                }
            }
        }

        GroupConfig g;
        g.name = suffix;
        g.text = suffix;
        g.target = "ParamAuto_" + suffix;
        g.ids = matchedPartIds;
        g.var_float = vfName;

        if (assignValues.size() > 2) {
            for (float v : assignValues) g.values.push_back(v);
        } else {
            g.values = {0.0F, 1.0F};
        }

        // Initialize VarFloat=1.0 for binary toggles to match the part's default
        // visible state. Parts are visible by default; first click toggles to hidden.
        if (g.values.size() == 2 && g.values[0] == 0.0F && g.values[1] == 1.0F) {
            _modelConfig.var_floats[vfName] = 1.0F;
            g.currentIndex = 1;
        } else {
            g.currentIndex = 0;
        }

        _modelConfig.groups.push_back(g);
        generated++;
        LAppPal::PrintLogLn("[APP]AutoVarFloatGroups: [%s] → %d Part(s): [%s]",
            vfName.c_str(), (int)matchedPartIds.size(), [&]() {
                std::string s;
                for (const auto& pid : matchedPartIds) {
                    if (!s.empty()) s += ", ";
                    s += pid;
                }
                return s;
            }().c_str());
    }

    LAppPal::PrintLogLn("[APP]AutoVarFloatGroups: suffix matching: %d VarFloats, %d Parts available, %d groups generated",
                        (int)vfNames.size(), (int)allPartIds.size(), generated);
    if (!allPartIds.empty()) {
        for (const auto& vfName : vfNames) {
            bool exists = false;
            for (const auto& g : _modelConfig.groups) {
                if (g.var_float == vfName) { exists = true; break; }
            }
            if (exists) continue;
            std::string suffix = vfName;
            size_t dot = vfName.rfind('.');
            if (dot != std::string::npos) suffix = vfName.substr(dot + 1);
            auto matched = MatchVarFloatSuffixToPartIds(suffix, allPartIds);
            LAppPal::PrintLogLn("[APP]AutoVarFloatGroups: [%s] suffix=[%s] matched=%d",
                vfName.c_str(), suffix.c_str(), (int)matched.size());
        }
    }

    // Layer 2 (FALLBACK): Infer from motion Curves → Part references.
    // Only for VarFloats not matched by suffix. May create false positives
    // (e.g., menu motions containing clothing PartOpacity curves), so suffix
    // matching (Layer 1) takes precedence.
    InferGroupsFromMotionCurves();

    // Layer 3 (FALLBACK): Infer from HitAreas → Motion Group → VarFloat chain.
    // Only fills gaps not covered by Layers 1 and 2.
    InferGroupsFromHitAreas();

    // Layer 4 (LAST RESORT): Create minimal groups for unmatched VarFloats.
    // These groups have no Part IDs (ids empty), but allow Strategy 4 in
    // AutoPopulateHitAreaParams to link hit areas to the correct VarFloat via
    // the motion group → VarFloat → GroupConfig chain. Without this, hit areas
    // for VarFloats like "3.kuzi" would incorrectly match generic groups.
    {
        int fallback = 0;
        for (const auto& vfName : vfNames) {
            bool found = false;
            for (const auto& g : _modelConfig.groups) {
                if (g.var_float == vfName) { found = true; break; }
            }
            if (found) continue;

            std::string suffix = vfName;
            size_t dot = vfName.rfind('.');
            if (dot != std::string::npos) suffix = vfName.substr(dot + 1);

            // Collect assign values for cycling
            std::set<float> assignValues;
            for (const auto& kv : _modelConfig.motions) {
                for (const auto& meta : kv.second) {
                    for (const auto& vf : meta.var_floats) {
                        if (vf.name == vfName && vf.type == 2 && vf.has_assign) {
                            assignValues.insert(vf.assign);
                        }
                    }
                }
            }

            GroupConfig g;
            g.name = suffix;
            g.text = suffix;
            g.target = "ParamAuto_" + suffix;
            g.var_float = vfName;
            // ids intentionally empty — no Part mapping available
            if (assignValues.size() > 2) {
                for (float v : assignValues) g.values.push_back(v);
            } else {
                g.values = {0.0F, 1.0F};
            }
            g.currentIndex = 0;

            _modelConfig.groups.push_back(g);
            fallback++;
            LAppPal::PrintLogLn("[APP]AutoVarFloatGroups: fallback [%s] → ParamAuto_%s (no Part mapping)",
                vfName.c_str(), suffix.c_str());
        }
        if (fallback > 0) {
            LAppPal::PrintLogLn("[APP]AutoVarFloatGroups: created %d fallback groups (no Part IDs)", fallback);
        }
    }

    BuildVarFloatGroupMap();
}

void LAppLive2DManager::AutoPopulateHitAreaParams() {
    LAppPal::PrintLogLn("[APP]AutoParam: groups=%d hitAreas=%d",
                        (int)_modelConfig.groups.size(), (int)_hitAreaConfigs.size());

    LAppModel *model = GetPrimaryCubism3Model(this);
    CubismModel *cubismModel = model ? model->GetModel() : nullptr;
    csmInt32 partCount = cubismModel ? cubismModel->GetPartCount() : 0;
    csmInt32 drawableCount = cubismModel ? cubismModel->GetDrawableCount() : 0;

    // Auto-generate VarFloat-linked groups BEFORE clothing groups.
    // This ensures VarFloat→Group→Part cascade works for all models, not just .mlve ones.
    AutoGenerateVarFloatGroups();

    // If no groups in config, auto-generate from clothing Parts.
    // Uses generic name matching (no language-specific mappings) aligned with ViewerEX
    // data-driven approach. Models with explicit Groups in model3.json skip this path.
    if (_modelConfig.groups.empty() && partCount > 0) {
        int autoGroupIdx = 0;
        for (csmInt32 p = 0; p < partCount; p++) {
            CubismIdHandle partId = cubismModel->GetPartId(p);
            std::string partName = partId->GetString().GetRawString();
            if (!IsClothingPart(partName)) continue;

            GroupConfig g;
            g.name = partName;
            g.text = partName;
            g.target = "ParamAuto_" + partName;
            g.ids.push_back(partName);
            g.values = {0.0F, 1.0F};
            g.currentIndex = 0;
            _modelConfig.groups.push_back(g);
            autoGroupIdx++;
        }
        // Link VarFloat names to groups by matching suffix to Part ID.
        // Uses generic matching (direct + abbreviation + substring).
        if (autoGroupIdx > 0) {
            auto toLower = [](std::string s) {
                for (auto& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
                return s;
            };
            std::set<std::string> vfNames;
            for (const auto& kv : _modelConfig.motions) {
                for (const auto& meta : kv.second) {
                    for (const auto& vf : meta.var_floats) {
                        if (!vf.name.empty()) vfNames.insert(vf.name);
                    }
                }
            }
            for (auto& g : _modelConfig.groups) {
                if (g.target.substr(0, 10) != "ParamAuto_") continue;
                std::string partLower = toLower(g.ids[0]);
                for (const auto& vfName : vfNames) {
                    std::string suffix = vfName;
                    size_t d = vfName.rfind('.');
                    if (d != std::string::npos) suffix = vfName.substr(d + 1);
                    std::string suffixLower = toLower(suffix);
                    // Direct match: "kuzi" ↔ "KuZi"
                    if (suffixLower == partLower) {
                        g.var_float = vfName;
                        break;
                    }
                    // Generic substring/abbreviation match
                    auto matched = MatchVarFloatSuffixToPartIds(suffix, {g.ids[0]});
                    if (!matched.empty() && matched[0] == g.ids[0]) {
                        g.var_float = vfName;
                        break;
                    }
                }
            }
            LAppPal::PrintLogLn("[APP]AutoParam: auto-generated %d clothing groups", autoGroupIdx);
        }
    }

    if (_modelConfig.groups.empty()) return;

    int linked = 0;
    for (auto &cfg : _hitAreaConfigs) {
        if (!cfg.param.empty()) continue;  // already has param

        const GroupConfig *bestGroup = nullptr;

        // Strategy 1: match drawable's parent Part against group ids
        if (cubismModel && drawableCount > 0) {
            for (csmInt32 d = 0; d < drawableCount; d++) {
                CubismIdHandle drawId = cubismModel->GetDrawableId(d);
                if (cfg.id != drawId->GetString().GetRawString()) continue;

                csmInt32 parentPart = cubismModel->GetDrawableParentPartIndex(d);
                if (parentPart >= 0 && parentPart < partCount) {
                    CubismIdHandle partId = cubismModel->GetPartId(parentPart);
                    std::string partName = partId->GetString().GetRawString();
                    for (const auto &g : _modelConfig.groups) {
                        if (!g.ids.empty() && ContainsIdCaseInsensitive(g.ids, partName)) {
                            bestGroup = &g;
                            break;
                        }
                    }
                }
                break;
            }
        }

        // Strategy 2: match hit area name against group ids
        if (!bestGroup) {
            for (const auto &g : _modelConfig.groups) {
                if (g.ids.empty() || g.target.empty()) continue;
                if (ContainsIdCaseInsensitive(g.ids, cfg.name)) {
                    bestGroup = &g;
                    break;
                }
            }
        }

        // Strategy 3: strip "HitArea_" prefix from drawable ID and match against group ids
        if (!bestGroup && cfg.id.size() > 9 && cfg.id.substr(0, 9) == "HitArea_") {
            std::string stripped = cfg.id.substr(9);
            for (const auto &g : _modelConfig.groups) {
                if (g.ids.empty() || g.target.empty()) continue;
                if (ContainsIdCaseInsensitive(g.ids, stripped)) {
                    bestGroup = &g;
                    break;
                }
            }
        }

        // Strategy 4: link via motion group → VarFloat → GroupConfig.
        // HitArea has a motion group (e.g., "开关-皮带"). That group's motion entries
        // contain VarFloat definitions (e.g., "1.pidai"). Auto-generated GroupConfigs
        // have var_float set to that name. Match via _varFloatGroupMap.
        if (!bestGroup && !cfg.motion.empty()) {
            auto it = _modelConfig.motions.find(cfg.motion);
            if (it != _modelConfig.motions.end()) {
                for (const auto& meta : it->second) {
                    for (const auto& vf : meta.var_floats) {
                        if (vf.name.empty()) continue;
                        auto mapIt = _varFloatGroupMap.find(vf.name);
                        if (mapIt != _varFloatGroupMap.end() && !mapIt->second.empty()) {
                            size_t gIdx = mapIt->second[0];
                            if (gIdx < _modelConfig.groups.size()) {
                                bestGroup = &_modelConfig.groups[gIdx];
                            }
                            break;
                        }
                    }
                    if (bestGroup) break;
                }
            }
        }

        if (bestGroup && !bestGroup->target.empty()) {
            cfg.param = bestGroup->target;
            if (!bestGroup->values.empty()) {
                cfg.values = bestGroup->values;
            }
            cfg.currentIndex = bestGroup->currentIndex;
            linked++;
            LAppPal::PrintLogLn("[APP]AutoParam: hitArea=[%s] drawID=[%s] → group target=[%s] varFloat=[%s] ids=%d",
                cfg.name.c_str(), cfg.id.c_str(), bestGroup->target.c_str(),
                bestGroup->var_float.c_str(), (int)bestGroup->ids.size());
        }
    }

    if (linked > 0) {
        LAppPal::PrintLogLn("[APP]AutoParam: linked %d hit areas to groups", linked);
    }

    // Diagnostic: count hit areas with/without motion fields
    int withMotion = 0, withParam = 0;
    for (const auto& cfg : _hitAreaConfigs) {
        if (!cfg.motion.empty()) withMotion++;
        if (!cfg.param.empty()) withParam++;
    }
    LAppPal::PrintLogLn("[APP]AutoParam: total=%d withMotion=%d withParam=%d groups=%d",
                        (int)_hitAreaConfigs.size(), withMotion, withParam, (int)_modelConfig.groups.size());
}

void LAppLive2DManager::SetPartOverride(const std::string& partId, float value, bool useFade) {
    if (useFade) {
        StartPartFade(partId, value);
    }
    _partOverrides[partId] = value;
}

void LAppLive2DManager::OnMotionStartedWithPartOpacity(const std::vector<std::string>& partIds) {
    _motionControlledParts.clear();
    for (const auto& id : partIds) {
        _motionControlledParts.insert(id);
    }
    _partOverridesDirty = true;
}

void LAppLive2DManager::ClearMotionControlledParts() {
    // Start fade for parts NOT managed by VarFloat groups.
    // VarFloat-managed parts get their PartFade from SyncAllVarFloatLinks
    // (called via EvaluateVarFloats when the motion finishes).
    // Without this, non-VarFloat parts would snap instantly to the new state.
    for (const auto& partId : _motionControlledParts) {
        bool isVarFloatManaged = false;
        float targetOpacity = -1.0F;
        for (const auto& g : _modelConfig.groups) {
            bool found = false;
            for (const auto& id : g.ids) {
                if (id == partId) { found = true; break; }
            }
            if (!found) continue;

            if (!g.var_float.empty()) {
                isVarFloatManaged = true;
            } else if (g.currentIndex >= 0) {
                if (!g.values.empty() && g.currentIndex < static_cast<int>(g.values.size())) {
                    targetOpacity = (g.values[g.currentIndex] >= 0.0F) ? 1.0F : 0.0F;
                } else {
                    targetOpacity = (g.currentIndex == 0) ? 1.0F : 0.0F;
                }
            }
            break;
        }
        if (!isVarFloatManaged && targetOpacity >= 0.0F) {
            StartPartFade(partId, targetOpacity, 0.3F);
        }
    }
    _motionControlledParts.clear();
    _partOverridesDirty = true;
}

void LAppLive2DManager::StartPartFade(const std::string& partId, float toValue, float duration) {
    // Read the ACTUAL current PartOpacity from the Cubism model as the fade start.
    // This is critical when a motion with PartOpacity curves just finished —
    // the motion animated the Part to its end value, and we must fade from THERE,
    // not from the stale _partVisualOpacity (which holds the pre-motion VarFloat value).
    float currentVisual = 1.0F;
    LAppModel* model = GetModel(0);
    if (model && model->GetModel()) {
        CubismIdHandle id = CubismFramework::GetIdManager()->GetId(partId.c_str());
        currentVisual = model->GetModel()->GetPartOpacity(id);
    } else {
        // Model not available — fall back to cached value
        auto vit = _partVisualOpacity.find(partId);
        if (vit != _partVisualOpacity.end()) {
            currentVisual = vit->second;
        } else {
            auto oit = _partOverrides.find(partId);
            if (oit != _partOverrides.end()) currentVisual = oit->second;
        }
    }
    PartFadeState fade;
    fade.from = currentVisual;
    fade.to = toValue;
    fade.elapsed = 0.0F;
    fade.duration = duration;
    _partFades[partId] = fade;
    _partOverridesDirty = true;
}

void LAppLive2DManager::SetModelConfig(const char *json) {
    if(json == nullptr || strlen(json) == 0) {
        LAppPal::PrintLogLn("[APP]SetModelConfig: null or empty json");
        return;
    }
    _modelConfig = ParseModelConfig(std::string(json));
    LAppPal::PrintLogLn("[APP]SetModelConfig: parsed groups=%d motions=%d costume_sets=%d",
                        (int)_modelConfig.groups.size(), (int)_modelConfig.motions.size(),
                        (int)_modelConfig.costume_sets.size());
    BuildVarFloatGroupMap();

    // Set up VarFloat change callback: controller value changes → VarFloat cascade
    _controllerEngine.SetVarFloatChangeCallback(
        [this](const std::string& name, float value) {
            SyncAllVarFloatLinks(name);
        });

    _controllerEngine.Initialize(_modelConfig);
    LAppDelegate::GetInstance()->GetRandomSpeaker().Initialize(_modelConfig);
    LAppDelegate::GetInstance()->GetRandomSpeaker().SetEnabled(
            _modelConfig.controllers.random_speak_enabled);
    LAppDelegate::GetInstance()->GetRandomSpeaker().SetInterval(
            _modelConfig.controllers.random_speak_interval);
    AutoGenerateHitAreas();
    AutoPopulateHitAreaParams();
}

void LAppLive2DManager::BuildVarFloatGroupMap() {
    _varFloatGroupMap.clear();
    for (size_t i = 0; i < _modelConfig.groups.size(); i++) {
        if (!_modelConfig.groups[i].var_float.empty()) {
            _varFloatGroupMap[_modelConfig.groups[i].var_float].push_back(i);
        }
    }
}

bool LAppLive2DManager::IsVarFloatManagedByGroup(const std::string& varFloatName) const {
    auto git = _varFloatGroupMap.find(varFloatName);
    if (git == _varFloatGroupMap.end()) return false;
    for (size_t idx : git->second) {
        if (idx < _modelConfig.groups.size() && !_modelConfig.groups[idx].ids.empty()) {
            return true;
        }
    }
    return false;
}

void LAppLive2DManager::SyncAllVarFloatLinks(const std::string& varFloatName) {
    auto it = _varFloatGroupMap.find(varFloatName);
    if (it == _varFloatGroupMap.end()) {
        return;
    }

    float vfValue = GetVarFloat(varFloatName);
    for (size_t idx : it->second) {
        if (idx >= _modelConfig.groups.size()) continue;
        auto& g = _modelConfig.groups[idx];

        int oldIndex = g.currentIndex;
        int newIndex = oldIndex;

        if (g.values.empty()) {
            newIndex = (vfValue > 0.5F) ? 1 : 0;
            _paramOverrides[g.target] = vfValue;
        } else {
            bool found = false;
            for (size_t vi = 0; vi < g.values.size(); vi++) {
                if (std::abs(g.values[vi] - vfValue) < 0.001F) {
                    newIndex = static_cast<int>(vi);
                    _paramOverrides[g.target] = vfValue;
                    found = true;
                    break;
                }
            }
            if (!found) {
                _paramOverrides[g.target] = vfValue;
            }
        }

        // Start smooth fade transition when group state changes
        if (newIndex != oldIndex) {
            g.currentIndex = newIndex;
            _partOverridesDirty = true;
            // Derive opacity from VarFloat value, not index position.
            // With values: matched value determines visibility (e.g., [-1,0]: -1→hidden, 0→visible).
            // Without values: binary toggle, must match ApplyVarFloatPartOverrides threshold (>0.5F).
            float newOpacity;
            if (!g.values.empty()) {
                newOpacity = (vfValue >= 0.0F) ? 1.0F : 0.0F;
            } else {
                newOpacity = (vfValue > 0.5F) ? 1.0F : 0.0F;
            }
            for (const auto& partId : g.ids) {
                StartPartFade(partId, newOpacity, 0.3F);
            }
        }
    }
}


void LAppLive2DManager::SyncAllVarFloats() {
    if (!_varFloatsDirty) return;
    _varFloatsDirty = false;
    for (const auto& kv : _modelConfig.var_floats) {
        // Skip VarFloats managed by groups with Part IDs (toggle state).
        // These are set by EvaluateVarFloats/SetGroupIndex, not by bulk sync.
        if (IsVarFloatManagedByGroup(kv.first)) continue;
        SyncAllVarFloatLinks(kv.first);
    }
}

void LAppLive2DManager::SetGroupIndex(GroupConfig& group, int index) {
    if (group.ids.empty()) {
        LAppPal::PrintLogLn("[Switch] SetGroupIndex: group=[%s] ids EMPTY, skipping", group.name.c_str());
        return;
    }
    // Bounds check: use values.size if available (multi-value toggle), else ids.size
    int maxIndex = !group.values.empty() ? static_cast<int>(group.values.size()) : static_cast<int>(group.ids.size());
    if (index < 0 || index >= maxIndex) {
        LAppPal::PrintLogLn("[Switch] SetGroupIndex: group=[%s] index=%d OUT OF RANGE (max=%d values=%d ids=%d)",
            group.name.c_str(), index, maxIndex, (int)group.values.size(), (int)group.ids.size());
        return;
    }

    int oldIndex = group.currentIndex;
    group.currentIndex = index;
    _partOverridesDirty = true;

    // Update param override
    if (!group.values.empty() && index < static_cast<int>(group.values.size())) {
        _paramOverrides[group.target] = group.values[index];
    } else {
        _paramOverrides[group.target] = (index > 0) ? 1.0F : 0.0F;
    }

    // Start smooth fade when group state changes.
    // Must happen HERE (not in SyncAllVarFloatLinks) because SetGroupIndex updates
    // g.currentIndex before calling SyncAllVarFloatLinks, making the index appear
    // "unchanged" to SyncAllVarFloatLinks's old-vs-new comparison.
    if (oldIndex != index) {
        // Derive opacity from value semantics, not index position.
        // With values: use matched value (e.g., [-1,0]: -1→hidden, 0→visible).
        // Without values: auto-generated groups, currentIndex=0 is initial/visible.
        float newOpacity;
        if (!group.values.empty() && index < static_cast<int>(group.values.size())) {
            newOpacity = (group.values[index] >= 0.0F) ? 1.0F : 0.0F;
        } else {
            newOpacity = (index == 0) ? 1.0F : 0.0F;
        }
        LAppPal::PrintLogLn("[Switch] SetGroupIndex: group=[%s] idx %d->%d opacity=%.1f parts=%d — starting fade",
            group.name.c_str(), oldIndex, index, newOpacity, (int)group.ids.size());
        for (const auto& partId : group.ids) {
            StartPartFade(partId, newOpacity, 0.3F);
        }
    }

    // Update VarFloat and cascade to all linked groups
    if (!group.var_float.empty()) {
        float vfVal = 0.0F;
        if (!group.values.empty() && index < static_cast<int>(group.values.size())) {
            vfVal = group.values[index];
        } else {
            vfVal = (index > 0) ? 1.0F : 0.0F;
        }
        float oldVf = GetVarFloat(group.var_float);
        SetVarFloat(group.var_float, vfVal);
        LAppPal::PrintLogLn("[Switch] SetGroupIndex: group=[%s] oldIdx=%d newIdx=%d var_float=[%s] oldVf=%.1f newVf=%.1f",
            group.name.c_str(), oldIndex, index, group.var_float.c_str(), oldVf, vfVal);
        SyncAllVarFloatLinks(group.var_float);
    } else {
        LAppPal::PrintLogLn("[Switch] SetGroupIndex: group=[%s] oldIdx=%d newIdx=%d NO var_float",
            group.name.c_str(), oldIndex, index);
    }
}

void LAppLive2DManager::ApplyVarFloatPartOverrides() {
    // Skip recomputation when nothing changed (no VarFloat mutation, no fade active).
    if (!_partOverridesDirty && _partFades.empty()) {
        return;
    }
    _partOverridesDirty = false;

    // Compute part overrides from VarFloat state for ALL groups.
    // This is the single source of truth for part opacity, replacing the old
    // PartFade + PartOverrides per-frame blocks.
    //
    // Execution order per frame:
    //   Pose (SDK) → ParamOverrides → this → model->Update() (render)
    //
    // Parts NOT in any group retain Pose's output (head/body mutual exclusion).
    // Parts IN a group are fully controlled by VarFloat/GroupConfig state.
    //
    // Smooth transitions: PartFade state machine provides smoothstep interpolation
    // for rapid toggles. The fade runs until completion, then the final value holds.
    _partOverrides.clear();

    // Skip VarFloat-driven overrides for Parts currently animated by a motion's
    // PartOpacity curves. Without this, the VarFloat (old state) would override
    // the motion's fade animation every frame, making the transition invisible.
    auto skipMotionPart = [&](const std::string& id) -> bool {
        return _motionControlledParts.count(id) > 0;
    };

    for (auto& g : _modelConfig.groups) {
        if (g.ids.empty()) continue;

        float targetOpacity = -1.0F;  // -1 = not computed yet

        if (!g.var_float.empty()) {
            // VarFloat-linked group: derive opacity from VarFloat value
            auto it = _modelConfig.var_floats.find(g.var_float);
            if (it == _modelConfig.var_floats.end()) {
                continue;
            }
            float vfValue = it->second;
            if (!g.values.empty()) {
                // Multi-value group: each id maps to a value by index.
                // When values.size() > ids.size(), it's a toggle —
                // opacity derived from matched value (>=0 visible, <0 hidden).
                bool anyMatch = false;
                int matchIdx = -1;
                for (size_t vi = 0; vi < g.values.size(); vi++) {
                    if (std::abs(g.values[vi] - vfValue) < 0.001F) {
                        matchIdx = static_cast<int>(vi);
                        anyMatch = true;
                        break;
                    }
                }
                if (g.values.size() <= g.ids.size()) {
                    // Strict 1:1 mapping: only the matching id is opaque
                    for (size_t pi = 0; pi < g.ids.size(); pi++) {
                        if (skipMotionPart(g.ids[pi])) continue;
                        float opacity = (static_cast<int>(pi) == matchIdx) ? 1.0F : 0.0F;
                        _partOverrides[g.ids[pi]] = opacity;
                        _partVisualOpacity[g.ids[pi]] = opacity;
                    }
                } else {
                    // More values than ids: toggle — derive opacity from matched value.
                    // For values=[-1,0]: -1→hidden, 0→visible. For [0,1]: 0→hidden, 1→visible.
                    float opacity = (anyMatch && g.values[matchIdx] >= 0.0F) ? 1.0F : 0.0F;
                    for (const auto& id : g.ids) {
                        if (skipMotionPart(id)) continue;
                        _partOverrides[id] = opacity;
                        _partVisualOpacity[id] = opacity;
                    }
                }
            } else {
                // Toggle group: all ids share the same visibility
                float opacity = (vfValue > 0.5F) ? 1.0F : 0.0F;
                for (const auto& id : g.ids) {
                    if (skipMotionPart(id)) continue;
                    _partOverrides[id] = opacity;
                    _partVisualOpacity[id] = opacity;
                }
            }
            // Diagnostic: log group summary (debug only — fires every frame)
            if (LAppDefine::DebugLogEnable) {
                LAppPal::PrintLogLn("[VarFloat] ApplyOverrides: group=[%s] vf=[%s]=%.2f",
                    g.name.c_str(), g.var_float.c_str(), vfValue);
            }
        } else if (g.currentIndex >= 0) {
            // Non-VarFloat group with tracked index
            if (!g.values.empty()) {
                // Multi-value group: currentIndex selects which id is visible
                for (size_t pi = 0; pi < g.ids.size(); pi++) {
                    if (skipMotionPart(g.ids[pi])) continue;
                    float opacity = (static_cast<int>(pi) == g.currentIndex) ? 1.0F : 0.0F;
                    _partOverrides[g.ids[pi]] = opacity;
                    _partVisualOpacity[g.ids[pi]] = opacity;
                }
            } else {
                // Toggle group without values array.
                // Auto-generated groups: currentIndex starts at 0, meaning "initial/visible".
                // All ids share the same visibility state.
                float opacity = (g.currentIndex == 0) ? 1.0F : 0.0F;
                for (const auto& id : g.ids) {
                    if (skipMotionPart(id)) continue;
                    auto oit = _partOverrides.find(id);
                    _partOverrides[id] = opacity;
                    _partVisualOpacity[id] = opacity;
                }
            }
        }
    }

    // Apply active PartFade transitions (smoothstep interpolation).
    // Fades override the target opacity with a smooth transition.
    if (!_partFades.empty()) {
        float dt = LAppPal::GetDeltaTime();
        std::vector<std::string> completed;
        for (auto& kv : _partFades) {
            auto& fade = kv.second;
            fade.elapsed += dt;
            float t = (fade.duration > 0.0F) ? (fade.elapsed / fade.duration) : 1.0F;
            if (t >= 1.0F) t = 1.0F;
            // Smoothstep easing (aligned with ViewerEX CubismFadeController::Evaluate)
            t = t * t * (3.0F - 2.0F * t);
            float opacity = fade.from + (fade.to - fade.from) * t;
            auto oit = _partOverrides.find(kv.first);
            _partOverrides[kv.first] = opacity;
            _partVisualOpacity[kv.first] = opacity;
            if (fade.elapsed >= fade.duration) {
                completed.push_back(kv.first);
            }
        }
        for (const auto& key : completed) {
            _partFades.erase(key);
        }
    }
}

void LAppLive2DManager::ChangeCostume(const std::string& name) {
    if (name.empty()) return;

    // Strategy 1: match costume_set name
    for (auto& cs : _modelConfig.costume_sets) {
        if (cs.name == name) {
            LAppPal::PrintLogLn("[Costume] Activate set: [%s] groups=%d mutual_exclude=%d",
                cs.name.c_str(), (int)cs.groups.size(), (int)cs.mutual_exclude.size());

            // Deactivate mutual_exclude sets: close ALL groups in each excluded set.
            for (const auto& exName : cs.mutual_exclude) {
                for (auto& ex : _modelConfig.costume_sets) {
                    if (ex.name != exName) continue;

                    LAppPal::PrintLogLn("[Costume]   mutual_exclude: [%s] vf=%s groups=%d",
                        exName.c_str(), ex.var_float.c_str(), (int)ex.groups.size());

                    // Set excluded set's VarFloat to 0 and cascade
                    if (!ex.var_float.empty()) {
                        SetVarFloat(ex.var_float, 0.0F);
                        SyncAllVarFloatLinks(ex.var_float);
                    }

                    // Close ALL groups in the excluded set (hide all parts)
                    for (const auto& exGroupTarget : ex.groups) {
                        for (auto& g : _modelConfig.groups) {
                            if (g.target == exGroupTarget || g.name == exGroupTarget) {
                                SetGroupIndex(g, 0);  // 0 = hidden
                                // Start fade transition for each part in this group
                                for (const auto& partId : g.ids) {
                                    StartPartFade(partId, 0.0F, 0.3F);
                                }
                                break;
                            }
                        }
                    }
                }
            }

            // Activate this set's VarFloat (set to 1) and cascade
            if (!cs.var_float.empty()) {
                SetVarFloat(cs.var_float, 1.0F);
                SyncAllVarFloatLinks(cs.var_float);
            }

            // Activate ALL groups in this set with fade-in
            for (const auto& groupTarget : cs.groups) {
                for (auto& g : _modelConfig.groups) {
                    if (g.target == groupTarget || g.name == groupTarget) {
                        SetGroupIndex(g, 1);  // 1 = visible
                        // Start fade-in transition for each part in this group
                        for (const auto& partId : g.ids) {
                            StartPartFade(partId, 1.0F, 0.3F);
                        }
                        break;
                    }
                }
            }

            // Track current costume and dispatch costume_changed event
            _currentCostumeName = cs.name;
            JniBridgeC::NotifyCostumeChanged(cs.name.c_str(), nullptr);
            return;
        }
    }

    // Strategy 2: match group target or name directly (single group toggle)
    for (auto& g : _modelConfig.groups) {
        if (g.target == name || g.name == name) {
            LAppPal::PrintLogLn("[Costume] Toggle group: [%s] target=[%s] idx=%d",
                name.c_str(), g.target.c_str(), g.currentIndex);
            int nextIdx = 0;
            if (!g.values.empty()) {
                nextIdx = (g.currentIndex + 1) % static_cast<int>(g.values.size());
            } else if (!g.ids.empty()) {
                nextIdx = (g.currentIndex + 1) % static_cast<int>(g.ids.size());
            }
            SetGroupIndex(g, nextIdx);
            // PartFade is already started inside SetGroupIndex — no duplicate needed.
            // Dispatch costume_changed event for Java side
            JniBridgeC::NotifyCostumeChanged(nullptr, g.target.c_str());
            return;
        }
    }

    LAppPal::PrintLogLn("[Costume] No match for [%s]", name.c_str());
}

const MotionMeta *LAppLive2DManager::FindMotionMeta(const std::string &group, int index) const {
    auto it = _modelConfig.motions.find(group);
    if (it != _modelConfig.motions.end()) {
        const auto &vec = it->second;
        if (index >= 0 && index < (int) vec.size()) {
            return &vec[index];
        }
    }
    return nullptr;
}

float LAppLive2DManager::GetVarFloat(const std::string &name) const {
    auto it = _modelConfig.var_floats.find(name);
    if(it != _modelConfig.var_floats.end()) {
        return it->second;
    }
    return 0.0F;
}

void LAppLive2DManager::SetVarFloat(const std::string &name, float value) {
    if (_modelConfig.var_floats[name] != value) {
        _modelConfig.var_floats[name] = value;
        _varFloatsDirty = true;
        _partOverridesDirty = true;
        // Notify Java side about VarFloat change (for menu sync)
        JniBridgeC::NotifyVarFloatChanged(name.c_str(), value);
    }
}

std::string LAppLive2DManager::GetMotionMetaJson() const {
    // Serialize motion metadata as JSON for Java side
    // Format: {"groupName": [{"name":"...", "index":0, "priority":2, "enabled":true, ...}, ...], ...}
    std::string result = "{";
    bool firstGroup = true;
    for (auto &entry: _modelConfig.motions) {
        const std::string &groupName = entry.first;
        const std::vector<MotionMeta> &motions = entry.second;
        if(!firstGroup) {
            result += ",";
        }
        firstGroup = false;
        result += "\"" + groupName + "\":[";
        for (int i = 0; i < (int) motions.size(); i++) {
            const MotionMeta &m = motions[i];
            if(i > 0) {
                result += ",";
            }
            result += "{";
            result += "\"name\":\"" + m.name + "\"";
            result += ",\"index\":" + std::to_string(m.motionIndex);
            result += ",\"group\":\"" + m.group + "\"";
            result += ",\"priority\":" + std::to_string(m.priority);
            result += ",\"fade_in\":" + std::to_string(m.fade_in);
            result += ",\"fade_out\":" + std::to_string(m.fade_out);
            result += ",\"enabled\":" + std::string(m.enabled ? "true" : "false");
            result += ",\"weight\":" + std::to_string(m.weight);
            result += ",\"expression\":\"" + m.expression + "\"";
            result += ",\"next_mtn\":\"" + m.next_mtn + "\"";
            result += ",\"text\":\"" + m.text + "\"";
            result += ",\"sound\":\"" + m.sound + "\"";
            // Choices
            if (!m.choices.empty()) {
                result += ",\"choices\":[";
                for (int c = 0; c < (int) m.choices.size(); c++) {
                    if(c > 0) {
                        result += ",";
                    }
                    const auto &ch = m.choices[c];
                    result += "{\"text\":\"" + ch.text + "\"";
                    result += ",\"group\":\"" + ch.group + "\"";
                    result += ",\"motion\":\"" + ch.motion + "\"";
                    result += ",\"next_mtn\":\"" + ch.next_mtn + "\"}";
                }
                result += "]";
            }
            // TimeLimit
            if (m.time_limit.hour >= 0 || m.time_limit.month >= 0 || m.time_limit.begin >= 0 ||
                m.time_limit.birthday) {
                result += ",\"time_limit\":{";
                bool first = true;
                if (m.time_limit.hour >= 0) {
                    result += "\"hour\":" + std::to_string(m.time_limit.hour);
                    first = false;
                }
                if (m.time_limit.minute >= 0) {
                    if(!first) {
                        result += ",";
                    }
                    result += "\"minute\":" + std::to_string(m.time_limit.minute);
                    first = false;
                }
                if (m.time_limit.month >= 0) {
                    if(!first) {
                        result += ",";
                    }
                    result += "\"month\":" + std::to_string(m.time_limit.month);
                    first = false;
                }
                if (m.time_limit.day >= 0) {
                    if(!first) {
                        result += ",";
                    }
                    result += "\"day\":" + std::to_string(m.time_limit.day);
                    first = false;
                }
                if (m.time_limit.begin >= 0) {
                    if(!first) {
                        result += ",";
                    }
                    result += "\"begin\":" + std::to_string(m.time_limit.begin);
                    first = false;
                }
                if (m.time_limit.end >= 0) {
                    if(!first) {
                        result += ",";
                    }
                    result += "\"end\":" + std::to_string(m.time_limit.end);
                    first = false;
                }
                if (m.time_limit.birthday) {
                    if(!first) {
                        result += ",";
                    }
                    result += "\"birthday\":true";
                }
                result += "}";
            }
            result += "}";
        }
        result += "]";
    }
    result += "}";
    return result;
}

std::string LAppLive2DManager::GetGroupConfigJson() const {
    std::string result = "[";
    for (int i = 0; i < (int) _modelConfig.groups.size(); i++) {
        const GroupConfig &g = _modelConfig.groups[i];
        if(i > 0) {
            result += ",";
        }
        result += "{";
        result += "\"target\":\"" + g.target + "\"";
        result += ",\"name\":\"" + g.name + "\"";
        result += ",\"text\":\"" + g.text + "\"";
        result += ",\"hidden\":" + std::string(g.hidden ? "true" : "false");
        result += ",\"value\":" + std::to_string(g.value);
        result += ",\"currentIndex\":" + std::to_string(g.currentIndex);
        result += ",\"values\":[";
        for (int j = 0; j < (int) g.values.size(); j++) {
            if(j > 0) result += ",";
            result += std::to_string(g.values[j]);
        }
        result += "]";
        result += ",\"ids\":[";
        for (int j = 0; j < (int) g.ids.size(); j++) {
            if(j > 0) {
                result += ",";
            }
            result += "\"" + g.ids[j] + "\"";
        }
        result += "]";
        result += "}";
    }
    result += "]";
    return result;
}

void LAppLive2DManager::OnTouchBeganForController(Csm::csmFloat32 x, Csm::csmFloat32 y) {
    csmFloat32 viewX = x;
    csmFloat32 viewY = y;
    NormalizeTouchForController(x, y, viewX, viewY);
    _controllerEngine.OnTouchBegan(viewX, viewY);
}

void LAppLive2DManager::OnTouchMovedForController(Csm::csmFloat32 x, Csm::csmFloat32 y) {
    csmFloat32 viewX = x;
    csmFloat32 viewY = y;
    NormalizeTouchForController(x, y, viewX, viewY);
    _controllerEngine.OnTouchMoved(viewX, viewY);
}

void LAppLive2DManager::OnTouchEndedForController() {
    _controllerEngine.OnTouchEnded();
}

void LAppLive2DManager::OnTouchCancelledForController() {
    _controllerEngine.OnTouchCancelled();
}

void LAppLive2DManager::OnGestureEvent(int gestureType, bool isDown) {
    _controllerEngine.OnGestureEvent(gestureType, isDown);
}
