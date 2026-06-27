#include "MotionSequencer.hpp"
#include "LAppLive2DManager.hpp"
#include "LAppModel.hpp"
#include "LAppModelCubism2.hpp"
#include "LAppPal.hpp"
#include "LAppDefine.hpp"
#include "JniBridgeC.hpp"
#include "ColorUtils.hpp"
#include "DrawableUtils.hpp"
#include <Motion/ACubismMotion.hpp>
#include <Motion/CubismMotion.hpp>
#include <Model/CubismModelMultiplyAndScreenColor.hpp>
#include <Id/CubismIdManager.hpp>
#include <Utils/CubismString.hpp>
#include <CubismFramework.hpp>
#include <Live2DCubismCore.hpp>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cmath>

using namespace Csm;

/// Data stored as motion custom data for callback context.
struct MotionCallbackData {
    std::string group;
    int index;
    ACubismMotion::FinishedMotionCallback userFinished = nullptr;
    ACubismMotion::BeganMotionCallback userBegan = nullptr;
    bool isIdle = false;  // true for idle/random motions (no user callbacks)
};

/// Pending motion to play after pre_mtn finishes.
/// (Moved to per-model state in LAppModel to prevent global state corruption)

/// Get the PendingMotion for the currently selected model slot.
/// Returns nullptr if no model at slot or model is null.
static PendingMotion* GetPendingMotionCubism3(LAppLive2DManager* mgr) {
    int slot = mgr->GetSelectedModelSlot();
    if (slot < 0) slot = 0;
    LAppModel* m = mgr->GetModel(static_cast<Csm::csmUint32>(slot));
    return m ? &m->GetPendingMotion() : nullptr;
}

static PendingMotion* GetPendingMotionCubism2(LAppLive2DManager* mgr) {
    int slot = mgr->GetSelectedModelSlot();
    if (slot < 0) slot = 0;
    LAppModelCubism2* m = mgr->GetCubism2Model(static_cast<Csm::csmUint32>(slot));
    return m ? &m->GetPendingMotion() : nullptr;
}

/// Parse a motion string "Group:Index" or just "Group".
static bool ParseMotionString(const std::string& motionStr, std::string& outGroup, int& outIndex)
{
    if(motionStr.empty()) {
        return false;
    }

    size_t colonPos = motionStr.find(':');
    if (colonPos != std::string::npos) {
        outGroup = motionStr.substr(0, colonPos);
        std::string indexStr = motionStr.substr(colonPos + 1);
        try {
            outIndex = std::stoi(indexStr);
        } catch (...) {
            outIndex = 0;
        }
    } else {
        outGroup = motionStr;
        outIndex = -1;  // Random from group
    }
    return !outGroup.empty();
}

namespace MotionSequencer {

/// Try to find a group whose id matches a VarFloat name.
///
/// Aligned with Live2DViewerEX's suffix matching mechanism:
/// VarFloat name suffix (after last '.') is matched against Group.target suffix.
/// Example: "1.kuzi" suffix "kuzi" matches Group target "kuzi" or "ParamAuto_kuzi".
///
/// Matching strategies (in priority order):
///   1. Explicit var_float field on GroupConfig (fast path, set by config.mlve or auto-gen)
///   2. Target suffix match — extract suffix from Group.target, compare to VarFloat suffix
///      (handles "ParamAuto_PD" → "PD" matching "1.pd")
///   3. Exact match on target or id
///   4. Case-insensitive suffix↔id match (suffix vs first Group id)
///   5. Substring match — suffix contained in id or vice versa
GroupConfig* FindGroupForVarFloat(LAppLive2DManager* mgr, const std::string& vfName) {
    auto& groups = mgr->GetModelConfig().groups;
    if (groups.empty()) return nullptr;

    auto toLower = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        return s;
    };

    // Extract VarFloat suffix after last '.' — "1.pidai" → "pidai"
    std::string suffix = vfName;
    size_t dot = vfName.rfind('.');
    if (dot != std::string::npos) suffix = vfName.substr(dot + 1);
    std::string suffixLower = toLower(suffix);

    // --- Strategy 1: explicit var_float field on GroupConfig ---
    // This is the canonical link (set by config.mlve or auto-populated by AutoPopulateHitAreaParams).
    for (auto& g : groups) {
        if (g.var_float.empty()) continue;
        if (g.var_float == vfName) return &g;
        // Also match by suffix: "kuzi" matches var_float "1.kuzi"
        std::string vfSuffix = g.var_float;
        size_t d = vfSuffix.rfind('.');
        if (d != std::string::npos) vfSuffix = vfSuffix.substr(d + 1);
        if (suffixLower == toLower(vfSuffix)) return &g;
    }

    // --- Strategy 2: target suffix match (ViewerEX mechanism) ---
    // Extract suffix from Group.target: "ParamAuto_PD" → "PD", "kuzi" → "kuzi"
    // Compare case-insensitively to VarFloat suffix.
    for (auto& g : groups) {
        if (g.target.empty()) continue;
        std::string targetSuffix = g.target;
        // Strip "ParamAuto_" prefix (our auto-gen convention)
        const std::string prefix = "ParamAuto_";
        if (targetSuffix.size() > prefix.size() &&
            targetSuffix.substr(0, prefix.size()) == prefix) {
            targetSuffix = targetSuffix.substr(prefix.size());
        }
        if (!targetSuffix.empty() && toLower(targetSuffix) == suffixLower) return &g;
    }

    // --- Strategy 3: exact match on target or id ---
    for (auto& g : groups) {
        if (g.ids.empty()) continue;
        if (g.target == vfName) return &g;
        for (const auto& id : g.ids) {
            if (id == vfName) return &g;
        }
    }

    // --- Strategy 4: case-insensitive suffix↔id match ---
    if (suffixLower.size() >= 3) {
        for (auto& g : groups) {
            if (g.ids.empty()) continue;
            const std::string& id = g.ids[0];
            if (id.size() >= 3 && toLower(id) == suffixLower) return &g;
        }
    }

    // --- Strategy 5: substring match ---
    if (suffixLower.size() >= 3) {
        for (auto& g : groups) {
            if (g.ids.empty()) continue;
            const std::string& id = g.ids[0];
            std::string idLower = toLower(id);
            if (idLower.size() >= 3 &&
                (idLower.find(suffixLower) != std::string::npos ||
                 suffixLower.find(idLower) != std::string::npos)) return &g;
        }
    }

    return nullptr;
}

/// Check if a VarFloat condition is met against the current value.
/// Multiple condition fields (equal, not_equal, greater, lower) are OR'd:
/// returns true if ANY specified condition is satisfied.
static bool CheckVarFloatCondition(const VarFloatConfig& vf, float currentValue) {
    if (vf.has_equal && std::abs(currentValue - vf.equal) < 0.001F) return true;
    if (vf.has_not_equal && std::abs(currentValue - vf.not_equal) >= 0.001F) return true;
    if (vf.has_greater && currentValue > vf.greater) return true;
    if (vf.has_lower && currentValue < vf.lower) return true;
    return false;
}

void EvaluateVarFloats(const std::string& group, int index)
{
    auto* mgr = LAppLive2DManager::GetInstance();
    const MotionMeta* meta = mgr->FindMotionMeta(group, index);
    if(!meta || meta->var_floats.empty()) {
        return;
    }

    // Only evaluate the selected entry's VarFloats.
    // Each entry has its own condition+assignment pair. Evaluating all entries
    // would cause opposing assignments to cancel out.
    for (const auto& vf : meta->var_floats) {
        if(vf.name.empty()) {
            continue;
        }

        float currentValue = mgr->GetVarFloat(vf.name);
        float result = currentValue;
        bool shouldSet = false;
        if (LAppDefine::DebugLogEnable) {
            LAppPal::PrintLogLn("[VarFloat] Eval: [%s] group=[%s] idx=%d type=%d current=%.2f",
                vf.name.c_str(), group.c_str(), index, vf.type, currentValue);
        }

        // Type mapping (aligned with Live2DViewerEX):
        //   Type=0: no VarFloat (skip)
        //   Type=1: condition — check condition, assign if true
        //   Type=2: unconditional — always assign
        switch (vf.type) {
            case 1: { // Conditional: check condition, then assign
                bool condMet = CheckVarFloatCondition(vf, currentValue);
                if(condMet && vf.has_assign) {
                    result = vf.assign;
                    shouldSet = true;
                }
                break;
            }

            case 2: // Unconditional assign
                result = vf.assign;
                shouldSet = true;
                break;

            default: // Type=0 or unknown — skip
                break;
        }

        if (shouldSet) {
            float oldVal = mgr->GetVarFloat(vf.name);
            mgr->SetVarFloat(vf.name, result);
            if (LAppDefine::DebugLogEnable) {
                LAppPal::PrintLogLn("[VarFloat] SET: [%s] %.2f -> %.2f (group=[%s] idx=%d)",
                    vf.name.c_str(), oldVal, result, group.c_str(), index);
            }
        }

        // Arithmetic operations (applied after assign, reading latest value).
        // A single VarFloat entry can combine assign + arithmetic (e.g., assign 0 then add 1).
        bool hasArith = false;
        if (vf.has_add) {
            float v = mgr->GetVarFloat(vf.name) + vf.add;
            mgr->SetVarFloat(vf.name, v);
            hasArith = true;
        }
        if (vf.has_subtract) {
            float v = mgr->GetVarFloat(vf.name) - vf.subtract;
            mgr->SetVarFloat(vf.name, v);
            hasArith = true;
        }
        if (vf.has_multiply) {
            float v = mgr->GetVarFloat(vf.name) * vf.multiply;
            mgr->SetVarFloat(vf.name, v);
            hasArith = true;
        }
        if (vf.has_divide && vf.divide != 0.0F) {
            float v = mgr->GetVarFloat(vf.name) / vf.divide;
            mgr->SetVarFloat(vf.name, v);
            hasArith = true;
        }

        // Cascade VarFloat change to all linked groups.
        // This updates GroupConfig.currentIndex and _paramOverrides for every group
        // bound to this VarFloat (by var_float field or suffix matching).
        // Part opacity is then computed per-frame by ApplyVarFloatPartOverrides.
        if (shouldSet || hasArith) {
            mgr->SyncAllVarFloatLinks(vf.name);
        }
    }
}

void SyncVarFloatPartOverrides()
{
    auto* mgr = LAppLive2DManager::GetInstance();
    const auto& varFloats = mgr->GetModelConfig().var_floats;
    auto& overrides = mgr->GetParamOverrides();

    if (LAppDefine::DebugLogEnable) {
        LAppPal::PrintLogLn("[VarFloat] SyncVarFloatPartOverrides: groups=%d varFloats=%d",
            (int)mgr->GetModelConfig().groups.size(), (int)varFloats.size());
    }

    // Update GroupConfig.currentIndex AND _paramOverrides from VarFloat state.
    // Part opacity is computed per-frame by ApplyVarFloatPartOverrides.
    for (auto& g : mgr->GetModelConfig().groups) {
        if (g.var_float.empty() || g.ids.empty()) continue;
        auto it = varFloats.find(g.var_float);
        if (it == varFloats.end()) continue;
        float vfValue = it->second;
        int oldIdx = g.currentIndex;
        int newIdx = oldIdx;

        if (!g.values.empty()) {
            for (size_t vi = 0; vi < g.values.size(); vi++) {
                if (std::abs(g.values[vi] - vfValue) < 0.001F) {
                    newIdx = static_cast<int>(vi);
                    overrides[g.target] = vfValue;
                    break;
                }
            }
        } else {
            newIdx = (vfValue > 0.5F) ? 1 : 0;
            overrides[g.target] = vfValue;
        }

        if (newIdx != oldIdx) {
            g.currentIndex = newIdx;
            // Start smooth fade transition when group state changes after motion finishes.
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
                mgr->StartPartFade(partId, newOpacity, 0.3F);
            }
            if (LAppDefine::DebugLogEnable) {
                LAppPal::PrintLogLn("[VarFloat] SyncVarFloatPartOverrides: group=[%s] idx %d->%d vf=%.2f target=[%s]",
                    g.name.c_str(), oldIdx, newIdx, vfValue, g.target.c_str());
            }
        }
    }
}

/// FindDrawableIndex is now in DrawableUtils.hpp (shared utility).

/// Split a command argument string at the first space into (first, rest).
/// Returns (arg, "") if no space is found.
static std::pair<std::string, std::string> SplitArg(const std::string& arg) {
    size_t sp = arg.find(' ');
    if (sp == std::string::npos) return {arg, ""};
    return {arg.substr(0, sp), arg.substr(sp + 1)};
}

/// Get the CubismModel at slot 0 (primary model). Returns nullptr if unavailable.
static Csm::CubismModel* GetPrimaryModel() {
    auto* mgr = LAppLive2DManager::GetInstance();
    LAppModel* model = mgr->GetModel(0);
    return model ? model->GetModel() : nullptr;
}

using CommandHandler = std::function<void(const std::string&)>;

static const std::unordered_map<std::string, CommandHandler>& GetCommandHandlers() {
    static const std::unordered_map<std::string, CommandHandler> handlers = {
        {"set_exp", [](const std::string& arg) {
            auto* mgr = LAppLive2DManager::GetInstance();
            LAppModel* model = mgr->GetModel(0);
            if (model) model->SetExpression(arg.c_str());
        }},
        {"play_sound", [](const std::string& arg) {
            JniBridgeC::NotifyMotionSound(arg.c_str(), 0);
        }},
        {"change_costume", [](const std::string& arg) {
            auto* mgr = LAppLive2DManager::GetInstance();
            mgr->ChangeCostume(arg);
            LAppPal::PrintLogLn("[Command] change_costume: %s", arg.c_str());
        }},
        {"trigger_event", [](const std::string& arg) {
            LAppPal::PrintLogLn("[Command] trigger_event: %s", arg.c_str());
        }},
        {"set_artmesh_opacity", [](const std::string& arg) {
            auto [id, valStr] = SplitArg(arg);
            if (valStr.empty()) return;
            float value = std::strtof(valStr.c_str(), nullptr);
            Csm::CubismModel* cm = GetPrimaryModel();
            if (!cm) return;
            Csm::csmInt32 d = FindDrawableIndex(cm, id);
            if (d >= 0) {
                Csm::csmInt32 parentPart = cm->GetDrawableParentPartIndex(d);
                if (parentPart >= 0) cm->SetPartOpacity(cm->GetPartId(parentPart), value);
            }
        }},
        {"set_artmesh_color", [](const std::string& arg) {
            auto [id, hex] = SplitArg(arg);
            float r, g, b, a;
            if (!ColorUtils::ParseHexColor(hex, r, g, b, a)) return;
            Csm::CubismModel* cm = GetPrimaryModel();
            if (!cm) return;
            Csm::csmInt32 d = FindDrawableIndex(cm, id);
            if (d >= 0) {
                auto& co = cm->GetOverrideMultiplyAndScreenColor();
                co.SetMultiplyColorEnabled(true);
                co.SetDrawableMultiplyColorEnabled(d, true);
                co.SetDrawableMultiplyColor(d, r, g, b, a);
            }
        }},
        {"set_slot_opacity", [](const std::string& arg) {
            auto [id, valStr] = SplitArg(arg);
            if (valStr.empty()) return;
            float value = std::strtof(valStr.c_str(), nullptr);
            Csm::CubismModel* cm = GetPrimaryModel();
            if (!cm) return;
            const Csm::CubismId* partId = Csm::CubismFramework::GetIdManager()->GetId(id.c_str());
            if (cm->GetPartIndex(partId) >= 0) cm->SetPartOpacity(partId, value);
        }},
        {"set_slot_color", [](const std::string& arg) {
            auto [id, hex] = SplitArg(arg);
            float r, g, b, a;
            if (!ColorUtils::ParseHexColor(hex, r, g, b, a)) return;
            Csm::CubismModel* cm = GetPrimaryModel();
            if (!cm) return;
            const Csm::CubismId* partId = Csm::CubismFramework::GetIdManager()->GetId(id.c_str());
            Csm::csmInt32 partIdx = cm->GetPartIndex(partId);
            if (partIdx >= 0) {
                auto& co = cm->GetOverrideMultiplyAndScreenColor();
                co.SetMultiplyColorEnabled(true);
                co.SetPartMultiplyColorEnabled(partIdx, true);
                co.SetPartMultiplyColor(partIdx, r, g, b, a);
            }
        }},
        {"set_hue_offset", [](const std::string& arg) {
            auto [id, degStr] = SplitArg(arg);
            if (degStr.empty()) return;
            float degrees = std::strtof(degStr.c_str(), nullptr);
            Csm::CubismModel* cm = GetPrimaryModel();
            if (!cm) return;
            Csm::csmInt32 d = FindDrawableIndex(cm, id);
            if (d < 0) return;
            auto mc = cm->GetDrawableMultiplyColor(d);
            float r = mc.X, g = mc.Y, b = mc.Z, a = mc.W;
            float h, s, l;
            ColorUtils::RgbToHsl(r, g, b, h, s, l);
            h = std::fmod(h + degrees / 360.0f + 1.0f, 1.0f);
            ColorUtils::HslToRgb(h, s, l, r, g, b);
            auto& co = cm->GetOverrideMultiplyAndScreenColor();
            co.SetMultiplyColorEnabled(true);
            co.SetDrawableMultiplyColorEnabled(d, true);
            co.SetDrawableMultiplyColor(d, r, g, b, a);
        }},
        {"set_param", [](const std::string& arg) {
            auto [paramId, valStr] = SplitArg(arg);
            if (valStr.empty()) return;
            float value = std::strtof(valStr.c_str(), nullptr);
            Csm::CubismModel* cm = GetPrimaryModel();
            if (!cm) return;
            cm->SetParameterValue(Csm::CubismFramework::GetIdManager()->GetId(paramId.c_str()), value);
        }},
        {"toggle_param", [](const std::string& arg) {
            Csm::CubismModel* cm = GetPrimaryModel();
            if (!cm) return;
            const auto* id = Csm::CubismFramework::GetIdManager()->GetId(arg.c_str());
            float current = cm->GetParameterValue(id);
            cm->SetParameterValue(id, current > 0.5F ? 0.0F : 1.0F);
        }},
        {"fade_param", [](const std::string& arg) {
            auto [paramId, valStr] = SplitArg(arg);
            if (valStr.empty()) return;
            float targetValue = std::strtof(valStr.c_str(), nullptr);
            Csm::CubismModel* cm = GetPrimaryModel();
            if (!cm) return;
            cm->SetParameterValue(Csm::CubismFramework::GetIdManager()->GetId(paramId.c_str()), targetValue);
        }},
        {"set_varfloat", [](const std::string& arg) {
            // Format: "varname=value" e.g., "1.pidai=1"
            auto eq = arg.find('=');
            if (eq == std::string::npos) return;
            std::string name = arg.substr(0, eq);
            float val = std::strtof(arg.substr(eq + 1).c_str(), nullptr);
            auto* mgr = LAppLive2DManager::GetInstance();
            if (mgr->IsVarFloatManagedByGroup(name)) {
                LAppPal::PrintLogLn("[Command] set_varfloat: SKIP [%s] (managed by group)", name.c_str());
            } else {
                mgr->SetVarFloat(name, val);
                mgr->SyncAllVarFloatLinks(name);
                LAppPal::PrintLogLn("[Command] set_varfloat: %s=%.2f", name.c_str(), val);
            }
        }},
        {"add_varfloat", [](const std::string& arg) {
            // Format: "varname=value" e.g., "6.JJMOD=1"
            auto eq = arg.find('=');
            if (eq == std::string::npos) return;
            std::string name = arg.substr(0, eq);
            float delta = std::strtof(arg.substr(eq + 1).c_str(), nullptr);
            auto* mgr = LAppLive2DManager::GetInstance();
            if (mgr->IsVarFloatManagedByGroup(name)) {
                LAppPal::PrintLogLn("[Command] add_varfloat: SKIP [%s] (managed by group)", name.c_str());
            } else {
                float current = mgr->GetVarFloat(name);
                mgr->SetVarFloat(name, current + delta);
                mgr->SyncAllVarFloatLinks(name);
                LAppPal::PrintLogLn("[Command] add_varfloat: %s += %.2f (now %.2f)", name.c_str(), delta, current + delta);
            }
        }},
        {"toggle_varfloat", [](const std::string& arg) {
            // Format: "varname" — toggles between 0 and 1
            auto* mgr = LAppLive2DManager::GetInstance();
            if (mgr->IsVarFloatManagedByGroup(arg)) {
                LAppPal::PrintLogLn("[Command] toggle_varfloat: SKIP [%s] (managed by group)", arg.c_str());
            } else {
                float current = mgr->GetVarFloat(arg);
                float newVal = (current > 0.5F) ? 0.0F : 1.0F;
                mgr->SetVarFloat(arg, newVal);
                mgr->SyncAllVarFloatLinks(arg);
                LAppPal::PrintLogLn("[Command] toggle_varfloat: %s %.2f -> %.2f", arg.c_str(), current, newVal);
            }
        }},
    };
    return handlers;
}

static void ExecuteCommand(const std::string& command) {
    if (command.empty()) return;

    size_t spacePos = command.find(' ');
    std::string cmd = (spacePos != std::string::npos) ? command.substr(0, spacePos) : command;
    std::string arg = (spacePos != std::string::npos) ? command.substr(spacePos + 1) : "";

    const auto& handlers = GetCommandHandlers();
    auto it = handlers.find(cmd);
    if (it != handlers.end()) {
        if (!arg.empty()) {
            it->second(arg);
        }
    } else {
        LAppPal::PrintLogLn("[Command] Unknown command: %s", command.c_str());
    }
}

bool IsToggleGroup(const std::string& group) {
    auto* mgr = LAppLive2DManager::GetInstance();
    const auto& metas = mgr->GetMotionMetas();
    auto it = metas.find(group);
    if (it == metas.end()) return false;
    for (const auto& meta : it->second) {
        bool hasCondition = false;
        bool hasAssign = false;
        for (const auto& vf : meta.var_floats) {
            if (vf.type == 1) hasCondition = true;
            if (vf.type == 2) hasAssign = true;
        }
        if (hasCondition && hasAssign) return true;
    }
    return false;
}

// Chain loop protection state
static int s_chainDepth = 0;
static std::string s_chainRoot;

static void OnFinishedInternal(const std::string& group, int index)
{
    // 1. VarFloat evaluation (aligned with ViewerEX MotionEnd → EvaluateVarFloats)
    EvaluateVarFloats(group, index);

    auto* mgr = LAppLive2DManager::GetInstance();
    const MotionMeta* meta = mgr->FindMotionMeta(group, index);

    // 1b. Toggle motion: store parameter values in _paramOverrides for persistence.
    // This replaces the fragile SaveParameters/LoadParameters mechanism.
    // Toggle motions are one-shot (Loop=false, set in StartMotion), so when they
    // finish, the model parameters hold the final curve values. Storing them in
    // _paramOverrides (Phase 8 in Update) ensures they survive any subsequent motion.
    bool isToggleMotion = IsToggleGroup(group);
    if (isToggleMotion && !mgr->IsCubism2Model()) {
        int slot = mgr->GetSelectedModelSlot();
        if (slot < 0) slot = 0;
        LAppModel* model = mgr->GetModel(static_cast<Csm::csmUint32>(slot));
        if (model && model->GetModel()) {
            // Find the cached motion to get its parameter curve targets
            auto* modelSetting = model->GetModelSetting();
            if (modelSetting) {
                const Csm::csmInt32 motionCount = modelSetting->GetMotionCount(group.c_str());
                if (index >= 0 && index < motionCount) {
                    const Csm::csmString name = Csm::Utils::CubismString::GetFormatedString("%s_%d", group.c_str(), index);
                    auto* motion = static_cast<Csm::CubismMotion*>(model->GetMotion(name));
                    if (motion) {
                        // 1. Store changed values (start != end) for this group's own parameters.
                        auto changedValues = motion->GetParameterChangedValues();
                        auto allTargets = motion->GetParameterTargets();
                        if (!changedValues.empty()) {
                            auto& overrides = mgr->GetParamOverrides();
                            for (const auto& [paramId, curveValue] : changedValues) {
                                std::string paramName = paramId->GetString().GetRawString();
                                overrides[paramName] = curveValue;
                            }
                        }

                        // 2. Clear stale overrides for ALL toggle parameters (asdq_*) in this motion.
                        //    When a toggle motion ends with a parameter at 0 (default visible), any
                        //    non-zero override from another group's "Off" motion must be cleared.
                        //    Also sync the owning group's VarFloat to 1 (visible) so the Part shows.
                        //    Example: pants "On" finishes → asdq_7 is 0 → clear -1 override from
                        //    underwear "Off" → set VarFloat 2.neiku=1 → underwear Part becomes visible.
                        {
                            auto& overrides = mgr->GetParamOverrides();

                            // Build param→VarFloat map from ALL toggle groups' "Off" motions.
                            // Used to distinguish "current group's params" from "other groups' params".
                            std::map<std::string, std::string> paramToVarFloat;
                            for (const auto& [mGroup, mEntries] : mgr->GetModelConfig().motions) {
                                if (!IsToggleGroup(mGroup)) continue;
                                for (int ei = 0; ei < (int)mEntries.size(); ei++) {
                                    const auto& mEntry = mEntries[ei];
                                    bool isOffEntry = false;
                                    for (const auto& vf : mEntry.var_floats) {
                                        if (vf.type == 1 && vf.has_equal && vf.equal == 0.0F) {
                                            isOffEntry = true; break;
                                        }
                                    }
                                    if (!isOffEntry) continue;
                                    std::string vfName;
                                    for (const auto& vf : mEntry.var_floats) {
                                        if (vf.type == 2) { vfName = vf.name; break; }
                                    }
                                    if (vfName.empty()) continue;
                                    Csm::csmString mKey = Csm::Utils::CubismString::GetFormatedString("%s_%d", mGroup.c_str(), ei);
                                    auto* cachedMotion = static_cast<Csm::CubismMotion*>(model->GetMotion(mKey));
                                    if (cachedMotion) {
                                        for (auto* pid : cachedMotion->GetParameterTargets()) {
                                            std::string pName = pid->GetString().GetRawString();
                                            if (!paramToVarFloat.count(pName)) {
                                                paramToVarFloat[pName] = vfName;
                                            }
                                        }
                                    }
                                }
                            }

                            // Get current group's VarFloat name to distinguish own params from others'.
                            std::string currentGroupVf;
                            for (const auto& vf : meta->var_floats) {
                                if (vf.type == 2) { currentGroupVf = vf.name; break; }
                            }

                            for (auto* paramId : allTargets) {
                                std::string paramName = paramId->GetString().GetRawString();
                                if (!paramToVarFloat.count(paramName)) continue;
                                auto vfIt = paramToVarFloat.find(paramName);
                                bool isCurrentGroupParam = (!currentGroupVf.empty() && vfIt->second == currentGroupVf);

                                if (isCurrentGroupParam) {
                                    // Current group's param: clear stale override when at default
                                    auto it = overrides.find(paramName);
                                    if (it != overrides.end() && it->second == 0.0F) {
                                        overrides.erase(it);
                                    }
                                } else {
                                    // Other group's param: don't clear its override, but sync
                                    // VarFloat to match actual override state. If no override exists
                                    // (param at default), restore owning group to visible.
                                    auto it = overrides.find(paramName);
                                    if (it == overrides.end()) {
                                        float curVf = mgr->GetVarFloat(vfIt->second);
                                        if (curVf < 0.5F) {
                                            mgr->SetVarFloat(vfIt->second, 1.0F);
                                            mgr->SyncAllVarFloatLinks(vfIt->second);
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        LAppPal::PrintLogLn("[Toggle] WARNING: motion [%s] NOT FOUND in cache", name.GetRawString());
                    }
                } else {
                    LAppPal::PrintLogLn("[Toggle] WARNING: index=%d out of range (motionCount=%d) for group=[%s]",
                        index, motionCount, group.c_str());
                }
            }
        }
    }

    // 2. Post-command execution (aligned with ViewerEX MotionEnd → ExecuteCommand)
    if (meta && !meta->post_command.empty()) {
        ExecuteCommand(meta->post_command);
    }

    // 3. next_mtn chain with deadlock protection
    if (meta && !meta->next_mtn.empty()) {
        s_chainDepth++;
        if (s_chainDepth > MotionSequencer::MAX_CHAIN_DEPTH) {
            LAppPal::PrintLogLn("[Motion] CHAIN DEADLOCK: next_mtn depth exceeded %d, stopping. Root=[%s_%d]",
                MotionSequencer::MAX_CHAIN_DEPTH, group.c_str(), index);
            s_chainDepth = 0;
            s_chainRoot.clear();
            // Fall through to pre_mtn check
        } else {
            // Cycle detection: if next_mtn points back to the chain root, stop.
            // Only check after depth > 1 because depth == 1 means we just set the root.
            if (s_chainRoot.empty()) {
                s_chainRoot = group + ":" + std::to_string(index);
            }
            std::string nextTarget = meta->next_mtn;
            if (s_chainDepth > 1 && nextTarget.find(s_chainRoot) != std::string::npos) {
                LAppPal::PrintLogLn("[Motion] CHAIN CYCLE: next_mtn [%s] loops back to root [%s], stopping",
                    nextTarget.c_str(), s_chainRoot.c_str());
                s_chainDepth = 0;
                s_chainRoot.clear();
                // Fall through to pre_mtn check
            } else {
                StartMotionFromString(meta->next_mtn);
                return;
            }
        }
    } else {
        // No next_mtn — chain ended naturally
        s_chainDepth = 0;
        s_chainRoot.clear();
    }

    // 4. pre_mtn pending motion
    int slot = mgr->GetSelectedModelSlot();
    if (slot < 0) slot = 0;
    if (!mgr->IsCubism2Model()) {
        auto* pending = GetPendingMotionCubism3(mgr);
        if (pending && pending->active) {
            PendingMotion target = *pending;
            pending->active = false;
            LAppModel* m = mgr->GetModel(static_cast<csmUint32>(slot));
            if (m) m->StartMotion(target.group.c_str(), target.index, target.priority, nullptr, nullptr, target.fadeIn, target.fadeOut);
        }
    } else {
        auto* pending = GetPendingMotionCubism2(mgr);
        if (pending && pending->active) {
            PendingMotion target = *pending;
            pending->active = false;
            LAppModelCubism2* m = mgr->GetCubism2Model(static_cast<csmUint32>(slot));
            if (m) m->StartMotion(target.group.c_str(), target.index, target.priority, target.fadeIn, target.fadeOut);
        }
    }
}

static void OnBeganInternal(const std::string& group, int index)
{
    // Execute command on motion begin (aligned with Live2DViewerEX)
    auto* mgr = LAppLive2DManager::GetInstance();
    const MotionMeta* meta = mgr->FindMotionMeta(group, index);
    if (meta && !meta->command.empty()) {
        ExecuteCommand(meta->command);
    }

    // NOTE: VarFloat evaluation is deferred to OnFinishedInternal.
    // Evaluating on begin would update VarFloat before the motion plays,
    // causing ApplyVarFloatPartOverrides to set part opacity to the new state
    // and fight the motion's PartOpacity curves during playback.
}

// Forward declarations for callback functions (defined below)
static void OnMotionFinishedCubism3(ACubismMotion* self);
static void OnMotionBeganCubism3(ACubismMotion* self);

void SetupMotionCallbacks(void* motionPtr, const std::string& group, int index,
                          ACubismMotion::FinishedMotionCallback userFinished,
                          ACubismMotion::BeganMotionCallback userBegan)
{
    ACubismMotion* motion = static_cast<ACubismMotion*>(motionPtr);
    if(!motion) {
        return;
    }

    // Reuse existing callback data if present (motion is cached), otherwise allocate.
    // Always overwrite group/index/user callbacks for the new play.
    bool isIdle = (userFinished == nullptr && userBegan == nullptr);
    auto* existing = static_cast<MotionCallbackData*>(motion->GetFinishedMotionCustomData());
    MotionCallbackData* data;
    if (existing) {
        existing->group = group;
        existing->index = index;
        existing->userFinished = userFinished;
        existing->userBegan = userBegan;
        existing->isIdle = isIdle;
        data = existing;
    } else {
        data = new MotionCallbackData{group, index, userFinished, userBegan, isIdle};
    }

    motion->SetFinishedMotionHandler(OnMotionFinishedCubism3);
    motion->SetFinishedMotionCustomData(data);
    motion->SetBeganMotionHandler(OnMotionBeganCubism3);
    motion->SetBeganMotionCustomData(data);
}

void OnMotionFinishedCubism3(ACubismMotion* self)
{
    // Clear motion-controlled parts BEFORE processing finish.
    // This ensures:
    // - For next_mtn chains: old targets cleared before new motion starts
    // - For normal finish: old targets cleared before FinishedMotion callback
    // - During crossfade in StartMotion: targets are preserved (not cleared prematurely)
    LAppLive2DManager::GetInstance()->ClearMotionControlledParts();

    void* customData = self->GetFinishedMotionCustomData();
    if (customData) {
        auto* data = static_cast<MotionCallbackData*>(customData);
        OnFinishedInternal(data->group, data->index);
        auto userCb = data->userFinished;
        // Clear both pointers BEFORE deleting to prevent began callback from accessing freed memory
        self->SetFinishedMotionCustomData(nullptr);
        self->SetBeganMotionCustomData(nullptr);
        delete data;
        if (userCb) userCb(self);
    }
}

void OnMotionBeganCubism3(ACubismMotion* self)
{
    void* customData = self->GetBeganMotionCustomData();
    if (customData) {
        auto* data = static_cast<MotionCallbackData*>(customData);
        OnBeganInternal(data->group, data->index);
        if (data->userBegan) data->userBegan(self);
    }
}

bool HandlePreMotion(const std::string& group, int index, int priority, float fadeIn, float fadeOut)
{
    auto* mgr = LAppLive2DManager::GetInstance();
    const MotionMeta* meta = mgr->FindMotionMeta(group, index);
    if(!meta || meta->pre_mtn.empty()) {
        return false;
    }

    std::string preGroup;
    int preIndex;
    if (ParseMotionString(meta->pre_mtn, preGroup, preIndex)) {
        // Store the target motion to play after pre_mtn finishes (per-model)
        if (!mgr->IsCubism2Model()) {
            auto* pending = GetPendingMotionCubism3(mgr);
            if (pending) {
                pending->group = group;
                pending->index = index;
                pending->priority = priority;
                pending->fadeIn = fadeIn;
                pending->fadeOut = fadeOut;
                pending->active = true;
            }
        } else {
            auto* pending = GetPendingMotionCubism2(mgr);
            if (pending) {
                pending->group = group;
                pending->index = index;
                pending->priority = priority;
                pending->fadeIn = fadeIn;
                pending->fadeOut = fadeOut;
                pending->active = true;
            }
        }
        StartMotionFromString(meta->pre_mtn);
        return true;
    }
    return false;
}

void OnMotionFinishedCubism2(const char* group, int index)
{
    OnFinishedInternal(std::string(group), index);
}

void OnMotionBeganCubism2(const char* group, int index)
{
    OnBeganInternal(std::string(group), index);
}

/// Select a motion index from a group by evaluating VarFloat conditions.
/// Returns the index of the first entry whose Type 1 (Conditional) VarFloats are all satisfied,
/// or -1 if no conditions match (caller should fall back to random).
int SelectMotionByVarFloats(const std::string& group)
{
    auto* mgr = LAppLive2DManager::GetInstance();
    const auto& metas = mgr->GetMotionMetas();
    auto it = metas.find(group);
    if (it == metas.end()) {
        return -1;
    }
    const auto& entries = it->second;

    // Track first entry with only Type=2 (unconditional assign) VarFloats as fallback.
    // These entries have no conditions — they should be selected when no Type=1 entry matches.
    // This aligns with Live2DViewerEX: unconditional VarFloats always execute.
    int defaultEntry = -1;

    for (int i = 0; i < (int)entries.size(); i++) {
        const auto& meta = entries[i];

        // Costume filter: skip motions that require a different costume set
        if (!meta.require_costume.empty()) {
            const std::string& currentCos = mgr->GetCurrentCostumeName();
            if (currentCos != meta.require_costume) {
                continue;
            }
        }

        if (meta.var_floats.empty()) {
            continue;
        }

        // Check all Type 1 (Conditional) VarFloats — ALL must be satisfied
        bool allMatch = true;
        bool hasCondition = false;
        bool hasType2 = false;
        for (const auto& vf : meta.var_floats) {
            if (vf.type == 2) hasType2 = true;
            if (vf.type != 1) continue;
            hasCondition = true;

            float currentValue = mgr->GetVarFloat(vf.name);
            if (!CheckVarFloatCondition(vf, currentValue)) {
                allMatch = false;
                break;
            }
        }

        if (hasCondition && allMatch) {
            return i;
        }

        // Entry with only Type=2 (no Type=1 conditions) — track as fallback
        if (!hasCondition && hasType2 && defaultEntry < 0) {
            defaultEntry = i;
        }
    }

    // No Type=1 condition matched — use Type=2 default if available
    if (defaultEntry >= 0) {
        return defaultEntry;
    }

    return -1;
}

void StartMotionFromString(const std::string& motionStr)
{
    std::string group;
    int index;
    if(!ParseMotionString(motionStr, group, index)) {
        return;
    }

    auto* mgr = LAppLive2DManager::GetInstance();

    // When index is -1 (random), try VarFloat-aware selection first
    if (index < 0) {
        int selected = SelectMotionByVarFloats(group);
        if (selected >= 0) {
            index = selected;
            // VarFloat evaluation is DEFERRED to OnFinishedInternal callback
            // (set up by SetupMotionCallbacks inside StartMotion).
            // Evaluating immediately would cause ApplyVarFloatPartOverrides to
            // fight the motion's PartOpacity curves during playback.
        }
    }

    // Resolve priority from target motion's metadata, fall back to PriorityForce
    int priority = 3;  // PriorityForce default
    if (index >= 0) {
        const MotionMeta* targetMeta = mgr->FindMotionMeta(group, index);
        if (targetMeta) {
            priority = targetMeta->priority;
        }
    }

    int slot = mgr->GetSelectedModelSlot();
    if (slot < 0) slot = 0;

    if (mgr->IsCubism2Model()) {
        LAppModelCubism2* m = mgr->GetCubism2Model(static_cast<csmUint32>(slot));
        if (m) {
            if (index >= 0) {
                m->StartMotion(group.c_str(), index, priority);
            } else {
                m->StartRandomMotion(group.c_str(), priority);
            }
        }
    } else {
        LAppModel* m = mgr->GetModel(static_cast<csmUint32>(slot));
        if (m) {
            if (index >= 0) {
                m->StartMotion(group.c_str(), index, priority);
            } else {
                m->StartRandomMotion(group.c_str(), priority);
            }
        }
    }
}

void ResetChainState() {
    s_chainDepth = 0;
    s_chainRoot.clear();
}

} // namespace MotionSequencer
