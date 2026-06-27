#include "ControllerEngine.hpp"
#include "LAppLive2DManager.hpp"
#include "LAppModel.hpp"
#include "LAppModelCubism2.hpp"
#include "LAppPal.hpp"
#include "MotionSequencer.hpp"

#include <CubismFramework.hpp>
#include <Id/CubismIdManager.hpp>
#include <Model/CubismModelMultiplyAndScreenColor.hpp>
#include "ColorUtils.hpp"
#include "DrawableUtils.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

using namespace Csm;

float ControllerEngine::ApplySpringSmooth(SmoothedParamState& ss, float target, float dt, float smoothTime) {
    ss.targetValue = target;
    float omega = 2.0F / smoothTime;
    float exp_val = std::exp(-omega * dt);
    float change = ss.currentValue - ss.targetValue;
    float temp = (ss.velocity + omega * change) * dt;
    ss.velocity = (ss.velocity - omega * temp) * exp_val;
    ss.currentValue = ss.targetValue + (change + temp) * exp_val;
    return ss.currentValue;
}

// --- ControllerEngine ---

ControllerEngine::ControllerEngine()
    : _initialized(false)
    , _touchActive(false)
    , _touchStartX(0), _touchStartY(0)
{
}

void ControllerEngine::Initialize(const ModelConfig& config) {
    _ctrlCopy = config.controllers;
    _initialized = true;
    ResetRuntimeState();
    _loopStates.clear();
    _prevParamValues.clear();
    _activeHitAreas.clear();
    _prevActiveHitAreas.clear();
    _keyDownMotions.clear();
    _keyUpMotions.clear();
    _controllerOverrides.clear();
    _pendingVarFloatChanges.clear();

    // Pre-size hit param and area trigger vectors
    _activeHitParamIndices.resize(_ctrlCopy.hit_params.size(), false);
    _smoothedStates.resize(_ctrlCopy.hit_params.size());
    std::fill(_smoothedStates.begin(), _smoothedStates.end(), SmoothedParamState{});
    _activeHitAreas.resize(_ctrlCopy.area_triggers.size(), false);
    _prevActiveHitAreas.resize(_ctrlCopy.area_triggers.size(), false);

    // Pre-build key trigger lookup maps
    for (const auto& kt : _ctrlCopy.key_triggers) {
        if (!kt.down_mtn.empty()) _keyDownMotions[kt.input] = kt.down_mtn;
        if (!kt.up_mtn.empty()) _keyUpMotions[kt.input] = kt.up_mtn;
    }

    LAppPal::PrintLogLn("[ControllerEngine] Initialized: %zu hit_params, %zu loop_params, %zu param_triggers, %zu area_triggers, %zu key_triggers",
        _ctrlCopy.hit_params.size(),
        _ctrlCopy.loop_params.size(),
        _ctrlCopy.param_triggers.size(),
        _ctrlCopy.area_triggers.size(),
        _ctrlCopy.key_triggers.size());
}

void ControllerEngine::ResetRuntimeState() {
    _touchActive = false;
    _touchStartX = 0.0F;
    _touchStartY = 0.0F;
    std::fill(_activeHitParamIndices.begin(), _activeHitParamIndices.end(), false);
    _currentHitAreaName.clear();
    _drawableIndexCache.clear();
    _cachedDrawableCount = 0;
}

float ControllerEngine::Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

void ControllerEngine::SetParameterValue(CubismModel* model, const std::string& paramId, csmFloat32 value) {
    const CubismId* id = CubismFramework::GetIdManager()->GetId(paramId.c_str());
    csmInt32 idx = model->GetParameterIndex(id);
    if (idx >= 0) {
        model->SetParameterValue(id, value);
    }
}

csmFloat32 ControllerEngine::GetParameterValue(CubismModel* model, const std::string& paramId) {
    const CubismId* id = CubismFramework::GetIdManager()->GetId(paramId.c_str());
    return model->GetParameterValue(id);
}

void ControllerEngine::TriggerMotion(const std::string& motionStr) {
    if (motionStr.empty()) return;
    // Format: "group:index" or just "group" (index defaults to 0)
    std::string group;
    int index = 0;
    size_t colon = motionStr.find(':');
    if (colon != std::string::npos) {
        group = motionStr.substr(0, colon);
        char* end = nullptr;
        index = static_cast<int>(std::strtol(motionStr.substr(colon + 1).c_str(), &end, 10));
    } else {
        group = motionStr;
    }
    LAppLive2DManager* mgr = LAppLive2DManager::GetInstance();
    if (!mgr) return;
    if (mgr->IsCubism2Model()) {
        auto* m = static_cast<LAppModelCubism2*>(nullptr);
        const int selectedSlot = mgr->GetSelectedModelSlot();
        if (selectedSlot >= 0) {
            m = mgr->GetCubism2Model(static_cast<Csm::csmUint32>(selectedSlot));
            if (m != nullptr && !m->IsInitialized()) {
                m = nullptr;
            }
        }
        if (m == nullptr) {
            const Csm::csmUint32 modelCount = mgr->GetCubism2ModelNum();
            for (Csm::csmUint32 i = 0; i < modelCount; i++) {
                auto* candidate = mgr->GetCubism2Model(i);
                if (candidate != nullptr && candidate->IsInitialized()) {
                    m = candidate;
                    break;
                }
            }
        }
        if (m) m->StartMotion(group.c_str(), index, 3, 0.5f, 0.5f);
    } else {
        auto* m = static_cast<LAppModel*>(nullptr);
        const int selectedSlot = mgr->GetSelectedModelSlot();
        if (selectedSlot >= 0) {
            m = mgr->GetModel(static_cast<Csm::csmUint32>(selectedSlot));
            if (m != nullptr && m->GetModel() == nullptr) {
                m = nullptr;
            }
        }
        if (m == nullptr) {
            const Csm::csmUint32 modelCount = mgr->GetModelNum();
            for (Csm::csmUint32 i = 0; i < modelCount; i++) {
                auto* candidate = mgr->GetModel(i);
                if (candidate != nullptr && candidate->GetModel() != nullptr) {
                    m = candidate;
                    break;
                }
            }
        }
        if (m) m->StartMotion(group.c_str(), index, 3.0f, nullptr, nullptr, 0.5f, 0.5f);
    }
}

void ControllerEngine::ProcessParamHit(csmFloat32 dt, std::vector<float>& outValues) {
    outValues.clear();
    outValues.resize(_ctrlCopy.hit_params.size(), 0.0F);

    for (size_t i = 0; i < _ctrlCopy.hit_params.size(); ++i) {
        const auto& hp = _ctrlCopy.hit_params[i];
        if (!hp.enabled) continue;

        int idx = static_cast<int>(i);
        bool areaMatch = hp.hit_area.empty() || hp.hit_area == _currentHitAreaName;

        float targetValue = 0.0F;
        if (_touchActive && areaMatch) {
            float pos = hp.relative
                ? ((hp.axis == 0) ? _smoothedTouchX - _touchStartX : _smoothedTouchY - _touchStartY)
                : ((hp.axis == 0) ? _smoothedTouchX : _smoothedTouchY);
            float value = pos * hp.weight * hp.factor;
            if (hp.type == 1) value = std::abs(value);
            targetValue = value;
            _activeHitParamIndices[idx] = true;
        } else if (idx < static_cast<int>(_activeHitParamIndices.size()) && _activeHitParamIndices[idx]) {
            if (hp.release == 1 && !hp.end_mtn.empty()) TriggerMotion(hp.end_mtn);
            _activeHitParamIndices[idx] = false;
        }

        if (idx < static_cast<int>(_smoothedStates.size()) && !hp.id.empty()) {
            float smoothTime = _ctrlCopy.smooth_time > 0 ? _ctrlCopy.smooth_time / 1000.0F : 0.1F;
            outValues[idx] = ApplySpringSmooth(_smoothedStates[idx], targetValue, dt, smoothTime);
        }
    }
}

// --- Cubism3/4/5 update path (called via ICubismUpdater::OnLateUpdate) ---

void ControllerEngine::Update(CubismModel* model, csmFloat32 dt) {
    if (!_initialized) return;

    const csmInt32 drawableCount = model->GetDrawableCount();
    // Rebuild drawable index cache if model changed
    if (_cachedDrawableCount != drawableCount) {
        _drawableIndexCache.clear();
        for (csmInt32 d = 0; d < drawableCount; d++) {
            const CubismId* id = model->GetDrawableId(d);
            if (id) _drawableIndexCache[id->GetString().GetRawString()] = d;
        }
        _cachedDrawableCount = drawableCount;
    }
    auto findDrawable = [&](const std::string& id) -> csmInt32 {
        auto it = _drawableIndexCache.find(id);
        return it != _drawableIndexCache.end() ? it->second : -1;
    };

    // --- ParamLoop: oscillating parameter values ---
    for (const auto& lp : _ctrlCopy.loop_params) {
        if (!lp.enabled) continue;

        std::string key = lp.id.empty() ? lp.name : lp.id;
        auto& state = _loopStates[key];
        state.elapsed += dt;

        // Compute oscillation value using sine wave
        float period = (lp.duration > 0) ? lp.duration / 1000.0f : 2.0f;
        float t = std::fmod(state.elapsed, period) / period;
        float sineVal = std::sin(t * 2.0f * 3.14159265f) * 0.5f + 0.5f; // 0..1
        float value = Lerp(lp.min_value, lp.max_value, sineVal) * lp.weight;

        // Set on primary id
        if (!lp.id.empty()) {
            SetParameterValue(model, lp.id, value);
        }
        // Set on additional ids
        for (const auto& id : lp.ids) {
            SetParameterValue(model, id, value);
        }
    }

    // --- ParamTrigger: fire motion when parameter crosses threshold ---
    for (const auto& pt : _ctrlCopy.param_triggers) {
        if (pt.id.empty()) continue;
        float currentVal = GetParameterValue(model, pt.id);
        float prevVal = 0.0f;
        auto it = _prevParamValues.find(pt.id);
        if (it != _prevParamValues.end()) prevVal = it->second;

        for (const auto& item : pt.items) {
            bool triggered = false;
            if (item.direction > 0) {
                // Trigger when crossing upward past value
                triggered = (prevVal < item.value && currentVal >= item.value);
            } else if (item.direction < 0) {
                // Trigger when crossing downward past value
                triggered = (prevVal > item.value && currentVal <= item.value);
            } else {
                // Trigger on any crossing
                triggered = (prevVal < item.value && currentVal >= item.value) ||
                            (prevVal > item.value && currentVal <= item.value);
            }
            if (triggered) {
                TriggerMotion(item.motion);
                // VarFloat evaluation is deferred to OnFinishedInternal callback.
                // Evaluating immediately would cause ApplyVarFloatPartOverrides to
                // fight the motion's PartOpacity curves during playback.
            }
        }
        _prevParamValues[pt.id] = currentVal;
    }

    // --- AreaTrigger: fire enter/exit motions when hit area changes ---
    for (size_t i = 0; i < _ctrlCopy.area_triggers.size(); i++) {
        if (i >= _activeHitAreas.size() || i >= _prevActiveHitAreas.size()) break;
        const auto& at = _ctrlCopy.area_triggers[i];
        bool currentlyActive = false;
        // Check if current hit area matches target_area or any trigger_areas
        if (!at.target_area.empty() && _currentHitAreaName == at.target_area) {
            currentlyActive = true;
        }
        if (!currentlyActive) {
            for (const auto& ta : at.trigger_areas) {
                if (_currentHitAreaName == ta) { currentlyActive = true; break; }
            }
        }
        bool wasActive = _prevActiveHitAreas[i];

        if (currentlyActive && !wasActive) {
            // Enter event
            if (!at.enter_mtn.empty()) {
                TriggerMotion(at.enter_mtn);
            }
        } else if (!currentlyActive && wasActive) {
            // Exit event
            if (!at.exit_mtn.empty()) {
                TriggerMotion(at.exit_mtn);
            }
        }
        _activeHitAreas[i] = currentlyActive;
        _prevActiveHitAreas[i] = currentlyActive;
    }

    // --- ParamHit: map touch position to parameter values with smoothing ---
    std::vector<float> hitValues;
    ProcessParamHit(dt, hitValues);
    for (size_t i = 0; i < _ctrlCopy.hit_params.size(); ++i) {
        const auto& hp = _ctrlCopy.hit_params[i];
        if (!hp.enabled || hp.id.empty()) continue;
        SetParameterValue(model, hp.id, hitValues[i]);
    }

    // --- ParamValue: set parameter values from config ---
    if (_ctrlCopy.param_value.enabled) {
        for (const auto& item : _ctrlCopy.param_value.items) {
            if (item.hidden) continue;
            for (const auto& id : item.ids) {
                SetParameterValue(model, id, item.value);
            }
            // key_values: per-parameter overrides (key=paramId, value=value)
            for (const auto& kv : item.key_values) {
                SetParameterValue(model, kv.key, kv.value);
            }
        }
    }

    // --- PartOpacity: set part opacity from config ---
    if (_ctrlCopy.part_opacity.enabled) {
        for (const auto& item : _ctrlCopy.part_opacity.items) {
            if (item.hidden) continue;
            for (const auto& id : item.ids) {
                const CubismId* partId = CubismFramework::GetIdManager()->GetId(id.c_str());
                csmInt32 idx = model->GetPartIndex(partId);
                if (idx >= 0) {
                    model->SetPartOpacity(partId, item.value);
                }
            }
        }
    }

    // --- ArtmeshOpacity: set artmesh opacity via parent Part ---
    if (_ctrlCopy.artmesh_opacity.enabled) {
        const csmInt32 partCount = model->GetPartCount();
        auto setDrawablePartOpacity = [&](const std::string& id, float value) {
            csmInt32 d = findDrawable(id);
            if (d >= 0) {
                csmInt32 parentPart = model->GetDrawableParentPartIndex(d);
                if (parentPart >= 0 && parentPart < partCount) {
                    model->SetPartOpacity(parentPart, value);
                }
            }
        };
        for (const auto& item : _ctrlCopy.artmesh_opacity.items) {
            if (item.hidden) continue;
            for (const auto& id : item.ids) {
                setDrawablePartOpacity(id, item.value);
            }
            for (const auto& kv : item.key_values) {
                setDrawablePartOpacity(kv.key, kv.value);
            }
        }
    }

    // --- ArtmeshColor: set multiply color per drawable ---
    // value = alpha/brightness (0-1), RGB defaults to white (1,1,1).
    // key_values can encode packed RGBA via float reinterpret (optional).
    if (_ctrlCopy.artmesh_color.enabled) {
        auto& colorOverride = model->GetOverrideMultiplyAndScreenColor();
        colorOverride.SetMultiplyColorEnabled(true);
        for (const auto& item : _ctrlCopy.artmesh_color.items) {
            if (item.hidden) continue;
            for (const auto& id : item.ids) {
                csmInt32 d = findDrawable(id);
                if (d >= 0) {
                    colorOverride.SetDrawableMultiplyColorEnabled(d, true);
                    colorOverride.SetDrawableMultiplyColor(d, 1.0f, 1.0f, 1.0f, item.value);
                }
            }
            for (const auto& kv : item.key_values) {
                csmInt32 d = findDrawable(kv.key);
                if (d >= 0) {
                    // Reinterpret float bit pattern as packed RGBA (config convention).
                    // Safe on all IEEE 754 platforms (ARM, x86) — static_assert guards portability.
                    static_assert(std::numeric_limits<float>::is_iec559, "packed RGBA requires IEEE 754 float");
                    csmUint32 packed;
                    std::memcpy(&packed, &kv.value, sizeof(float));
                    csmFloat32 r = ((packed >> 24) & 0xFF) / 255.0f;
                    csmFloat32 g = ((packed >> 16) & 0xFF) / 255.0f;
                    csmFloat32 b = ((packed >> 8)  & 0xFF) / 255.0f;
                    csmFloat32 a = (packed & 0xFF) / 255.0f;
                    colorOverride.SetDrawableMultiplyColorEnabled(d, true);
                    colorOverride.SetDrawableMultiplyColor(d, r, g, b, a);
                }
            }
        }
    }

    // --- SlotOpacity: set slot opacity (routed through Part opacity) ---
    // Cubism SDK has no native "slot" concept; slot IDs map to Part IDs.
    if (_ctrlCopy.slot_opacity.enabled) {
        for (const auto& item : _ctrlCopy.slot_opacity.items) {
            if (item.hidden) continue;
            for (const auto& id : item.ids) {
                const CubismId* partId = CubismFramework::GetIdManager()->GetId(id.c_str());
                csmInt32 idx = model->GetPartIndex(partId);
                if (idx >= 0) {
                    model->SetPartOpacity(partId, item.value);
                }
            }
        }
    }

    // --- SlotColor: set slot multiply color (routed through Part-level color) ---
    if (_ctrlCopy.slot_color.enabled) {
        auto& colorOverride = model->GetOverrideMultiplyAndScreenColor();
        colorOverride.SetMultiplyColorEnabled(true);
        for (const auto& item : _ctrlCopy.slot_color.items) {
            if (item.hidden) continue;
            for (const auto& id : item.ids) {
                const CubismId* partId = CubismFramework::GetIdManager()->GetId(id.c_str());
                csmInt32 partIdx = model->GetPartIndex(partId);
                if (partIdx >= 0) {
                    colorOverride.SetPartMultiplyColorEnabled(partIdx, true);
                    colorOverride.SetPartMultiplyColor(partIdx, 1.0f, 1.0f, 1.0f, item.value);
                }
            }
        }
    }

    // --- ArtmeshCullController: batch drawable culling control ---
    if (_ctrlCopy.artmesh_culling.enabled) {
        // cull_front: disable culling (render front face only — default)
        for (const auto& id : _ctrlCopy.artmesh_culling.cull_front) {
            csmInt32 d = findDrawable(id);
            if (d >= 0) model->SetDrawableCulling(d, 0);
        }
        // cull_back: enable culling (render back face only)
        for (const auto& id : _ctrlCopy.artmesh_culling.cull_back) {
            csmInt32 d = findDrawable(id);
            if (d >= 0) model->SetDrawableCulling(d, 1);
        }
        // cull_none: disable culling (render both faces)
        for (const auto& id : _ctrlCopy.artmesh_culling.cull_none) {
            csmInt32 d = findDrawable(id);
            if (d >= 0) model->SetDrawableCulling(d, 0);
        }
    }

    // VarFloat sync is handled directly by callers (MotionSequencer::EvaluateVarFloats,
    // JniBridgeC::nativeSetVarFloat) which call LAppLive2DManager::SyncAllVarFloatLinks.
    // No callback needed here — _pendingVarFloatChanges is intentionally unused.
}

// --- Cubism2 update path (called from LAppModelCubism2::Update) ---

void ControllerEngine::UpdateCubism2(csmFloat32 dt) {
    if (!_initialized) return;
    _controllerOverrides.clear();

    // --- ParamLoop ---
    for (const auto& lp : _ctrlCopy.loop_params) {
        if (!lp.enabled) continue;

        std::string key = lp.id.empty() ? lp.name : lp.id;
        auto& state = _loopStates[key];
        state.elapsed += dt;

        float period = (lp.duration > 0) ? lp.duration / 1000.0f : 2.0f;
        float t = std::fmod(state.elapsed, period) / period;
        float sineVal = std::sin(t * 2.0f * 3.14159265f) * 0.5f + 0.5f;
        float value = Lerp(lp.min_value, lp.max_value, sineVal) * lp.weight;

        if (!lp.id.empty()) {
            _controllerOverrides[lp.id] = value;
        }
        for (const auto& id : lp.ids) {
            _controllerOverrides[id] = value;
        }
    }

    // --- ParamHit (Cubism2 path with smoothing) ---
    std::vector<float> hitValues;
    ProcessParamHit(dt, hitValues);
    for (size_t i = 0; i < _ctrlCopy.hit_params.size(); ++i) {
        const auto& hp = _ctrlCopy.hit_params[i];
        if (!hp.enabled || hp.id.empty()) continue;
        _controllerOverrides[hp.id] = hitValues[i];
    }

    // --- ParamTrigger (Cubism2 path) ---
    // For Cubism2 we can't read parameter values directly here,
    // so param_triggers are only supported on the Cubism3/4/5 path.

    // --- ParamValue (Cubism2 path) ---
    if (_ctrlCopy.param_value.enabled) {
        for (const auto& item : _ctrlCopy.param_value.items) {
            if (item.hidden) continue;
            for (const auto& id : item.ids) {
                _controllerOverrides[id] = item.value;
            }
            for (const auto& kv : item.key_values) {
                _controllerOverrides[kv.key] = kv.value;
            }
        }
    }
}

// --- Touch events ---

void ControllerEngine::OnTouchBegan(csmFloat32 x, csmFloat32 y) {
    _touchActive = true;
    _touchStartX = x;
    _touchStartY = y;
    // Snap smoothed coordinates on touch begin (no lag for initial contact)
    _smoothedTouchX = x;
    _smoothedTouchY = y;
}

void ControllerEngine::OnTouchMoved(csmFloat32 x, csmFloat32 y) {
    // Low-pass filter on touch coordinates (exponential moving average)
    const float alpha = 0.7F;  // 0=full smoothing, 1=no smoothing
    _smoothedTouchX += alpha * (x - _smoothedTouchX);
    _smoothedTouchY += alpha * (y - _smoothedTouchY);
}

void ControllerEngine::OnTouchEnded() {
    _touchActive = false;
    _currentHitAreaName.clear();
}

void ControllerEngine::OnTouchCancelled() {
    _touchActive = false;
    std::fill(_activeHitParamIndices.begin(), _activeHitParamIndices.end(), false);
    _currentHitAreaName.clear();
}

// --- Gesture events (KeyTrigger) ---

void ControllerEngine::OnGestureEvent(int gestureType, bool isDown) {
    if (!_initialized) return;

    if (isDown) {
        auto it = _keyDownMotions.find(gestureType);
        if (it != _keyDownMotions.end()) {
            TriggerMotion(it->second);
        }
    } else {
        auto it = _keyUpMotions.find(gestureType);
        if (it != _keyUpMotions.end()) {
            TriggerMotion(it->second);
        }
    }
}

// --- ControllerEngineUpdater (Cubism3/4/5 ICubismUpdater wrapper) ---

ControllerEngineUpdater::ControllerEngineUpdater(ControllerEngine& engine) // NOLINT: class used in commented-out code, kept for future re-enable
    : _engine(engine)
{
}

void ControllerEngineUpdater::OnLateUpdate(CubismModel* model, csmFloat32 deltaTimeSeconds) {
    _engine.Update(model, deltaTimeSeconds);
}
