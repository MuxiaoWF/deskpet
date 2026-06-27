#include "LAppModelCubism2.hpp"
#include "Cubism2ParamBridge.hpp"
#include "LAppPal.hpp"
#include "LAppTextureManager.hpp"
#include "LAppDelegate.hpp"
#include "LAppLive2DManager.hpp"
#include "LAppDefine.hpp"
#include "MotionSequencer.hpp"
#include "MotionGroupUtils.hpp"
#include "ControllerEngine.hpp"
#include "third_party/nlohmann/json.hpp"

#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cmath>

using namespace Csm;
using namespace Utils;

LAppModelCubism2::LAppModelCubism2()
    : _modelSetting(nullptr)
    , _mocLoader(nullptr)
    , _currentExpressionIndex(-1)
    , _previousExpressionIndex(-1)
    , _exprBlendMode(ExprBlend_Overwrite)
    , _physicsWeight(1.0F)
    , _physicsTargetWeight(1.0F)
    , _physicsFadeSpeed(0)
    , _physicsWeightDirty(false)
    , _dragX(0), _dragY(0)
    , _initialized(false)
{
    _currentMotion.playing = false;
    _currentMotion.currentTime = 0;
    _currentMotion.priority = 0;
    _currentMotion.index = -1;
    _previousMotion.playing = false;
    _previousMotion.index = -1;
    _motionFadeTimer = 0;
}

LAppModelCubism2::~LAppModelCubism2()
{
    delete _modelSetting;
    delete _mocLoader;
}

void LAppModelCubism2::LoadAssets(const csmChar* dir, const csmChar* fileName)
{
    _modelHomeDir = dir;
    LAppPal::PrintLogLn("[Cubism2] LoadAssets dir=%s file=%s", dir, fileName);

    csmSizeInt size;
    const csmString path = csmString(dir) + fileName;
    csmByte* buffer = LAppPal::LoadFileAsBytes(path.GetRawString(), &size);
    if (buffer == nullptr)
    {
        LAppPal::PrintLogLn("[Cubism2] Failed to load model settings");
        return;
    }

    _modelSetting = new Cubism2ModelSetting(buffer, size);
    LAppPal::ReleaseBytes(buffer);

    const csmChar* mocFile = _modelSetting->GetModelFileName();
    if (strcmp(mocFile, "") != 0)
    {
        LoadMoc(mocFile);
    }

    LoadTextures();

    // Load expressions
    for (csmInt32 i = 0; i < _modelSetting->GetExpressionCount(); i++)
    {
        const csmChar* exprName = _modelSetting->GetExpressionName(i);
        const csmChar* exprFile = _modelSetting->GetExpressionFileName(i);
        if(strcmp(exprFile, "") == 0) continue;

        const csmString exprPath = _modelHomeDir + csmString(exprFile);
        csmSizeInt exprSize;
        csmByte* exprBuf = LAppPal::LoadFileAsBytes(exprPath.GetRawString(), &exprSize);
        if(exprBuf == nullptr) continue;

        CubismJson* json = CubismJson::Create(exprBuf, exprSize);
        LAppPal::ReleaseBytes(exprBuf);
        if(json == nullptr) continue;

        ExpressionEntry entry;
        entry.name = exprName;
        entry.blendMode = ExprBlend_Overwrite;

        Value& root = json->GetRoot();
        Value& params = root["params"];
        if (!params.IsNull())
        {
            csmVector<Value*>* vec = params.GetVector();
            if (vec)
            {
                for (csmUint32 p = 0; p < vec->GetSize(); p++)
                {
                    Value& item = params[static_cast<csmInt32>(p)];
                    if(item.IsNull()) continue;
                    Value& idVal = item["id"];
                    Value& valVal = item["value"];
                    Value& blendVal = item["blend"];
                    if (!idVal.IsNull() && !valVal.IsNull())
                    {
                        entry.paramValues[idVal.GetRawString()] = valVal.ToFloat();
                        if (!blendVal.IsNull())
                        {
                            const char* blend = blendVal.GetRawString();
                            if (strcmp(blend, "add") == 0) entry.blendMode = ExprBlend_Add;
                            else if (strcmp(blend, "multiply") == 0) entry.blendMode = ExprBlend_Multiply;
                            else entry.blendMode = ExprBlend_Overwrite;
                        }
                    }
                }
            }
        }
        _expressions.push_back(entry);
        CubismJson::Delete(json);
    }

    _initialized = (_mocLoader != nullptr && _mocLoader->GetMocData().drawableCount > 0);

    if (_initialized)
    {
        float cw = _mocLoader->GetCanvasWidth();
        float ch = _mocLoader->GetCanvasHeight();
        _modelMatrix = CubismModelMatrix(cw, ch);
    }

    LAppPal::PrintLogLn("[Cubism2] Model loaded, initialized=%d", _initialized ? 1 : 0);
}

void LAppModelCubism2::LoadMoc(const csmChar* mocFile)
{
    const csmString mocPath = _modelHomeDir + csmString(mocFile);
    csmSizeInt size;
    csmByte* buffer = LAppPal::LoadFileAsBytes(mocPath.GetRawString(), &size);
    if (buffer == nullptr)
    {
        LAppPal::PrintLogLn("[Cubism2] Failed to load moc: %s", mocPath.GetRawString());
        return;
    }

    _mocLoader = new Cubism2MocLoader();
    if (!_mocLoader->LoadFromBuffer(buffer, size))
    {
        LAppPal::PrintLogLn("[Cubism2] Failed to parse moc");
        delete _mocLoader;
        _mocLoader = nullptr;
    }
    LAppPal::ReleaseBytes(buffer);
}

void LAppModelCubism2::LoadTextures()
{
    if (!_mocLoader) return;

    for (csmInt32 i = 0; i < _modelSetting->GetTextureCount(); i++)
    {
        const csmChar* texFile = _modelSetting->GetTextureFileName(i);
        if(strcmp(texFile, "") == 0) continue;

        std::string texPath = std::string(_modelHomeDir.GetRawString()) + texFile;
        LAppTextureManager::TextureInfo* tex =
            LAppDelegate::GetInstance()->GetTextureManager()->CreateTextureFromPngFile(texPath);
        if (tex)
        {
            _mocLoader->SetTexture(i, tex->id);
        }
        else
        {
            LAppPal::PrintLogLn("[Cubism2] ERROR: Failed to load texture %d: %s", i, texPath.c_str());
        }
    }
}

Cubism2MotionData LAppModelCubism2::LoadMotionFile(const csmChar* path)
{
    Cubism2MotionData motion;
    motion.duration = 0;
    motion.fadeInTime = 0.5F;
    motion.fadeOutTime = 0.5F;

    csmSizeInt size;
    csmByte* buffer = LAppPal::LoadFileAsBytes(path, &size);
    if(buffer == nullptr) return motion;

    CubismJson* json = CubismJson::Create(buffer, size);
    LAppPal::ReleaseBytes(buffer);
    if(json == nullptr) return motion;

    Value& root = json->GetRoot();

    Value& durVal = root["duration"];
    if(!durVal.IsNull()) motion.duration = durVal.ToFloat();

    Value& fadeIn = root["fade_in"];
    if(!fadeIn.IsNull()) motion.fadeInTime = fadeIn.ToFloat() / 1000.0F;

    Value& fadeOut = root["fade_out"];
    if(!fadeOut.IsNull()) motion.fadeOutTime = fadeOut.ToFloat() / 1000.0F;

    Value& curves = root["curves"];
    if (!curves.IsNull())
    {
        csmVector<Value*>* curveVec = curves.GetVector();
        if (curveVec)
        {
            for (csmUint32 c = 0; c < curveVec->GetSize(); c++)
            {
                Value& curve = curves[static_cast<csmInt32>(c)];
                if(curve.IsNull()) continue;

                Cubism2MotionData::Curve cv;

                Value& targetVal = curve["target"];
                Value& idVal = curve["id"];
                Value& segments = curve["segments"];

                Value& curveFadeIn = curve["fade_in"];
                Value& curveFadeOut = curve["fade_out"];
                if(!curveFadeIn.IsNull()) cv.fadeInTime = curveFadeIn.ToFloat() / 1000.0F;
                if(!curveFadeOut.IsNull()) cv.fadeOutTime = curveFadeOut.ToFloat() / 1000.0F;

                if (!targetVal.IsNull())
                {
                    const char* target = targetVal.GetRawString();
                    if(strcmp(target, "model") == 0) cv.target = Cubism2Target_Model;
                    else if (strcmp(target, "parameter") == 0) cv.target = Cubism2Target_Parameter;
                    else if (strcmp(target, "part") == 0) cv.target = Cubism2Target_Part;
                    else if (strcmp(target, "opacity") == 0) cv.target = Cubism2Target_Opacity;
                    else cv.target = Cubism2Target_Parameter;
                }

                if(!idVal.IsNull()) cv.id = idVal.GetRawString();

                // Parse segments: [time, value, segType, ...]
                if (!segments.IsNull())
                {
                    csmVector<Value*>* segVec = segments.GetVector();
                    if (segVec)
                    {
                        auto segCount = static_cast<Csm::csmInt32>(segVec->GetSize());
                        if (segCount >= 2)
                        {
                            cv.initTime = segments[0].ToFloat();
                            cv.initValue = segments[1].ToFloat();

                            csmInt32 idx = 2;
                            while (idx < segCount)
                            {
                                const csmInt32 segType = segments[idx].ToInt();

                                if (segType == Cubism2MotionData::SEG_LINEAR && idx + 2 < segCount)
                                {
                                    Cubism2MotionData::Segment seg;
                                    seg.type = Cubism2MotionData::SEG_LINEAR;
                                    seg.t0 = cv.segments.empty() ? cv.initTime : cv.segments.back().t1;
                                    seg.v0 = cv.segments.empty() ? cv.initValue : cv.segments.back().v1;
                                    seg.t1 = segments[idx + 1].ToFloat();
                                    seg.v1 = segments[idx + 2].ToFloat();
                                    cv.segments.push_back(seg);
                                    idx += 3;
                                }
                                else if (segType == Cubism2MotionData::SEG_BEZIER && idx + 6 < segCount)
                                {
                                    Cubism2MotionData::Segment seg;
                                    seg.type = Cubism2MotionData::SEG_BEZIER;
                                    seg.t0 = cv.segments.empty() ? cv.initTime : cv.segments.back().t1;
                                    seg.v0 = cv.segments.empty() ? cv.initValue : cv.segments.back().v1;
                                    seg.t1 = segments[idx + 1].ToFloat();
                                    seg.v1 = segments[idx + 2].ToFloat();
                                    seg.t2 = segments[idx + 3].ToFloat();
                                    seg.v2 = segments[idx + 4].ToFloat();
                                    seg.t3 = segments[idx + 5].ToFloat();
                                    seg.v3 = segments[idx + 6].ToFloat();
                                    cv.segments.push_back(seg);
                                    idx += 7;
                                }
                                else if (segType == Cubism2MotionData::SEG_STEPPED && idx + 2 < segCount)
                                {
                                    Cubism2MotionData::Segment seg;
                                    seg.type = Cubism2MotionData::SEG_STEPPED;
                                    seg.t0 = cv.segments.empty() ? cv.initTime : cv.segments.back().t1;
                                    seg.v0 = cv.segments.empty() ? cv.initValue : cv.segments.back().v1;
                                    seg.t1 = segments[idx + 1].ToFloat();
                                    seg.v1 = segments[idx + 2].ToFloat();
                                    cv.segments.push_back(seg);
                                    idx += 3;
                                }
                                else
                                {
                                    break;
                                }
                            }
                        }

                        if (!cv.segments.empty())
                        {
                            motion.duration = std::max(motion.duration, cv.segments.back().t1);
                        }
                    }
                }

                motion.curves.push_back(cv);
            }
        }
    }

    CubismJson::Delete(json);
    return motion;
}

// Evaluate a Cubism 2 motion curve at time t with proper bezier/stepped interpolation
static csmFloat32 EvaluateSegmentCurve(const Cubism2MotionData::Curve& curve, csmFloat32 t)
{
    if (curve.segments.empty()) return curve.initValue;

    // Past all segments: hold last value
    if (t >= curve.segments.back().t1) {
        return curve.segments.back().v1;
    }
    // Before all segments: hold initial value
    if (t <= curve.initTime) {
        return curve.initValue;
    }

    for (const auto& seg : curve.segments)
    {
        if (t < seg.t0) continue;
        if (t > seg.t1) continue;

        if (seg.type == Cubism2MotionData::SEG_STEPPED)
        {
            return seg.v0;
        }
        else if (seg.type == Cubism2MotionData::SEG_BEZIER)
        {
            // De Casteljau's algorithm for cubic bezier
            csmFloat32 duration = seg.t3 - seg.t0;
            if (duration <= 0) return seg.v3;
            csmFloat32 u = (t - seg.t0) / duration;
            csmFloat32 oneMinusU = 1.0F - u;

            // Time bezier to find u for the given t
            // P(t) = (1-u)^3*t0 + 3*(1-u)^2*u*t1 + 3*(1-u)*u^2*t2 + u^3*t3
            // Solve for u numerically (Newton's method, 4 iterations)
            for (int iter = 0; iter < 4; iter++) {
                csmFloat32 omu = 1.0F - u;
                csmFloat32 bezT = omu*omu*omu*seg.t0 + 3*omu*omu*u*seg.t1 + 3*omu*u*u*seg.t2 + u*u*u*seg.t3;
                csmFloat32 deriv = 3*omu*omu*(seg.t1 - seg.t0) + 6*omu*u*(seg.t2 - seg.t1) + 3*u*u*(seg.t3 - seg.t2);
                if (std::abs(deriv) > 1e-6F) {
                    u += (t - bezT) / deriv;
                    u = std::max(0.0F, std::min(1.0F, u));
                }
            }

            oneMinusU = 1.0F - u;
            return oneMinusU*oneMinusU*oneMinusU*seg.v0 + 3*oneMinusU*oneMinusU*u*seg.v1
                 + 3*oneMinusU*u*u*seg.v2 + u*u*u*seg.v3;
        }
        else // SEG_LINEAR
        {
            csmFloat32 duration = seg.t1 - seg.t0;
            if (duration <= 0) return seg.v1;
            csmFloat32 ratio = (t - seg.t0) / duration;
            return seg.v0 + (seg.v1 - seg.v0) * ratio;
        }
    }

    return curve.segments.back().v1;
}

void LAppModelCubism2::ApplyMotion(csmFloat32 time)
{
    if(!_mocLoader || !_currentMotion.playing) return;

    const auto& motion = _currentMotion.data;
    csmFloat32 t = fmod(time, motion.duration > 0 ? motion.duration : 1.0F);

    for (const auto& curve : motion.curves)
    {
        if(curve.segments.empty()) continue;

        csmFloat32 value = EvaluateSegmentCurve(curve, t);

        switch (curve.target)
        {
            case Cubism2Target_Parameter:
            {
                const char* mappedId = Cubism2ParamBridge::MapParamName(curve.id.c_str());
                _mocLoader->SetParameterValue(mappedId, value);
                break;
            }
            case Cubism2Target_Opacity:
            {
                // Opacity target: try to match drawable by index
                char* endPtr = nullptr;
                long idx = strtol(curve.id.c_str(), &endPtr, 10);
                if (endPtr != curve.id.c_str()) {
                    _mocLoader->SetDrawableOpacity(static_cast<Csm::csmInt32>(idx), value);
                }
                break;
            }
            case Cubism2Target_Part:
            {
                // Part visibility: 0 = hidden, non-zero = visible
                char* endPtr = nullptr;
                long idx = strtol(curve.id.c_str(), &endPtr, 10);
                if (endPtr != curve.id.c_str()) {
                    _mocLoader->SetDrawableOpacity(static_cast<Csm::csmInt32>(idx), value > 0 ? 1.0F : 0.0F);
                }
                break;
            }
            case Cubism2Target_Model:
                // Model-level targets (positionX/Y, scaleX/Y, angle) - not supported yet
                break;
        }
    }
}

void LAppModelCubism2::StartMotion(const csmChar* group, csmInt32 no, csmInt32 priority, csmFloat32 fadeInTime, csmFloat32 fadeOutTime)
{
    if(!_modelSetting) return;

    if (_currentMotion.playing && priority < LAppDefine::PriorityForce && _currentMotion.priority >= priority)
    {
        return;
    }

    const csmChar* motionFile = _modelSetting->GetMotionFileName(group, no);
    if(strcmp(motionFile, "") == 0) return;

    std::string cacheKey = std::string(group) + "_" + std::to_string(no);
    auto cacheIt = _motionCache.find(cacheKey);
    Cubism2MotionData motion;
    if (cacheIt != _motionCache.end())
    {
        motion = cacheIt->second;
    }
    else
    {
        const csmString motionPath = _modelHomeDir + csmString(motionFile);
        motion = LoadMotionFile(motionPath.GetRawString());
        if(motion.curves.empty()) return;
        _motionCache[cacheKey] = motion;
    }

    // Apply motion metadata from config.mlve
    const auto& motionMetas = LAppLive2DManager::GetInstance()->GetMotionMetas();
    auto groupIt = motionMetas.find(std::string(group));
    if (groupIt != motionMetas.end()) {
        for (const auto& meta : groupIt->second) {
            if (meta.motionIndex == no) {
                if (fadeInTime < 0.0F && meta.fade_in > 0) motion.fadeInTime = meta.fade_in / 1000.0F;
                if (fadeOutTime < 0.0F && meta.fade_out > 0) motion.fadeOutTime = meta.fade_out / 1000.0F;
                break;
            }
        }
    }

    if(fadeInTime >= 0.0F) motion.fadeInTime = fadeInTime;
    if(fadeOutTime >= 0.0F) motion.fadeOutTime = fadeOutTime;

    if (_currentMotion.playing) NotifyMotionFinished(_currentMotion.group.c_str(), _currentMotion.index);

    if (_currentMotion.playing)
    {
        _previousMotion = _currentMotion;
        _motionFadeTimer = 0;
    }

    _currentMotion.data = motion;
    _currentMotion.currentTime = 0;
    _currentMotion.priority = priority;
    _currentMotion.playing = true;
    _currentMotion.fadeInWeight = 0;
    _currentMotion.group = group;
    _currentMotion.index = no;

    NotifyMotionBegan(group, no);

    // Auto-trigger associated expression
    {
        const auto& metas = LAppLive2DManager::GetInstance()->GetMotionMetas();
        auto gi = metas.find(std::string(group));
        if (gi != metas.end()) {
            for (const auto& meta : gi->second) {
                if (meta.motionIndex == no && !meta.expression.empty()) {
                    SetExpression(meta.expression.c_str());
                    break;
                }
            }
        }
    }

}

void LAppModelCubism2::StartRandomMotion(const csmChar* group, csmInt32 priority, csmFloat32 fadeInTime, csmFloat32 fadeOutTime)
{
    if(!_modelSetting) return;
    const csmInt32 count = _modelSetting->GetMotionCount(group);
    if(count == 0) return;
    StartMotion(group, rand() % count, priority, fadeInTime, fadeOutTime);
}

void LAppModelCubism2::SetExpression(const csmChar* expressionName)
{
    for (size_t i = 0; i < _expressions.size(); i++)
    {
        if (_expressions[i].name == expressionName)
        {
            if (_currentExpressionIndex >= 0 && _currentExpressionIndex != static_cast<csmInt32>(i))
                _previousExpressionIndex = _currentExpressionIndex;
            _currentExpressionIndex = static_cast<csmInt32>(i);
            return;
        }
    }
}

void LAppModelCubism2::LastExpression()
{
    if (_previousExpressionIndex >= 0 && _previousExpressionIndex < static_cast<csmInt32>(_expressions.size()))
    {
        const csmInt32 temp = _currentExpressionIndex;
        _currentExpressionIndex = _previousExpressionIndex;
        _previousExpressionIndex = temp;
    }
}

const char* LAppModelCubism2::GetCurrentExpressionName() const
{
    if (_currentExpressionIndex >= 0 && _currentExpressionIndex < static_cast<csmInt32>(_expressions.size()))
        return _expressions[_currentExpressionIndex].name.c_str();
    return "";
}

void LAppModelCubism2::SetRandomExpression()
{
    if(_expressions.empty()) return;
    _currentExpressionIndex = static_cast<csmInt32>(rand() % _expressions.size());
}

void LAppModelCubism2::SetDragging(csmFloat32 x, csmFloat32 y)
{
    _dragX = x;
    _dragY = y;
}

csmBool LAppModelCubism2::HitTest(const csmChar* hitAreaName, csmFloat32 x, csmFloat32 y)
{
    if(!_modelSetting || !_mocLoader) return false;

    // x, y are NDC [-1,1]. AABB regions are in canvas space.
    // Convert NDC → canvas via model matrix inverse.
    // Note: this is approximate — the projection's aspect ratio scale is not inverted.
    // For portrait models on portrait screens the error is small.
    csmFloat32 canvasX = _modelMatrix.InvertTransformX(x);
    csmFloat32 canvasY = _modelMatrix.InvertTransformY(y);

    for (csmInt32 i = 0; i < _modelSetting->GetHitAreasCount(); i++)
    {
        if (strcmp(_modelSetting->GetHitAreaName(i), hitAreaName) == 0)
        {
            csmFloat32 cw = _mocLoader->GetCanvasWidth();
            csmFloat32 ch = _mocLoader->GetCanvasHeight();
            const csmFloat32 cx = cw / 2.0F;
            csmFloat32 cy = ch / 2.0F;
            csmFloat32 hw = cw * 0.25F;
            csmFloat32 hh = ch * 0.25F;

            if (strstr(hitAreaName, "Head") || strstr(hitAreaName, "head"))
            { cy = ch * 0.75F; hw = cw * 0.2F; hh = ch * 0.15F; }
            else if (strstr(hitAreaName, "Face") || strstr(hitAreaName, "face"))
            { cy = ch * 0.7F; hw = cw * 0.18F; hh = ch * 0.12F; }
            else if (strstr(hitAreaName, "Body") || strstr(hitAreaName, "body"))
            { cy = ch * 0.4F; hw = cw * 0.25F; hh = ch * 0.25F; }
            else if (strstr(hitAreaName, "Hair") || strstr(hitAreaName, "hair"))
            { cy = ch * 0.8F; hw = cw * 0.22F; hh = ch * 0.12F; }

            return (canvasX >= cx - hw && canvasX <= cx + hw && canvasY >= cy - hh && canvasY <= cy + hh);
        }
    }
    return false;
}

csmString LAppModelCubism2::GetIdleMotionGroup() const
{
    return MotionGroupUtils::FindIdleGroup(_modelSetting, true);
}

csmString LAppModelCubism2::GetTapMotionGroup() const
{
    return MotionGroupUtils::FindTapGroup(_modelSetting, true);
}

void LAppModelCubism2::Update(csmFloat32 deltaTimeSeconds)
{
    if(!_initialized || !_mocLoader) return;

    UpdatePhysicsFade(deltaTimeSeconds);

    // Advance motion time
    if (_currentMotion.playing)
    {
        _currentMotion.currentTime += deltaTimeSeconds;
        _currentMotion.fadeInWeight = std::min(1.0F,
            _currentMotion.fadeInWeight + deltaTimeSeconds / std::max(0.01F, _currentMotion.data.fadeInTime));

        if (_currentMotion.currentTime >= _currentMotion.data.duration)
        {
            _currentMotion.playing = false;
            NotifyMotionFinished(_currentMotion.group.c_str(), _currentMotion.index);
            // Re-sync VarFloat-driven part overrides after motion finishes.
            // During motion playback, VarFloat parts were skipped so the motion's
            // parameter animation could drive the visual transition. Now that the
            // motion is done, re-apply VarFloat state to partOverrides.
            MotionSequencer::SyncVarFloatPartOverrides();
        }

        ApplyMotion(_currentMotion.currentTime);
    }

    // Crossfade from previous motion with parameter blending
    if (_previousMotion.playing)
    {
        _motionFadeTimer += deltaTimeSeconds;
        float fadeOutWeight = 1.0F - std::min(1.0F, _motionFadeTimer / std::max(0.01F, _previousMotion.data.fadeOutTime));

        if (_motionFadeTimer >= _previousMotion.data.fadeOutTime) {
            _previousMotion.playing = false;
        } else if (fadeOutWeight > 0.0F && _currentMotion.playing) {
            // Blend: apply previous motion parameters with fadeOutWeight
            // This creates a smooth crossfade between old and new motion
            const auto& prevMotion = _previousMotion.data;
            csmFloat32 prevTime = fmod(_previousMotion.currentTime, prevMotion.duration > 0 ? prevMotion.duration : 1.0F);
            for (const auto& curve : prevMotion.curves) {
                if (curve.segments.empty() || curve.target != Cubism2Target_Parameter) continue;
                csmFloat32 prevValue = EvaluateSegmentCurve(curve, prevTime);
                const char* mappedId = Cubism2ParamBridge::MapParamName(curve.id.c_str());
                csmFloat32 currentValue = _mocLoader->GetParameterValue(mappedId);
                // Blend: current already applied by ApplyMotion, mix with previous
                csmFloat32 blended = currentValue * _currentMotion.fadeInWeight + prevValue * fadeOutWeight * (1.0F - _currentMotion.fadeInWeight);
                _mocLoader->SetParameterValue(mappedId, blended);
            }
        }
    }

    ApplyExpression();

    // Apply dragging as angle parameters
    _mocLoader->SetParameterValue("ParamAngleX", _dragX * _lookAtParams.angleXFactor);
    _mocLoader->SetParameterValue("ParamAngleY", _dragY * _lookAtParams.angleYFactor);
    _mocLoader->SetParameterValue("ParamBodyAngleX", _dragX * _lookAtParams.bodyAngleXFactor);
    _mocLoader->SetParameterValue("ParamEyeBallX", _dragX * _lookAtParams.eyeBallXFactor);
    _mocLoader->SetParameterValue("ParamEyeBallY", _dragY * _lookAtParams.eyeBallYFactor);

    // Update per-parameter fade states
    for (auto& kv : _paramFadeStates)
    {
        auto& state = kv.second;
        if (state.fadeSpeed > 0 && state.currentValue != state.targetValue)
        {
            csmFloat32 diff = state.targetValue - state.currentValue;
            csmFloat32 step = state.fadeSpeed * deltaTimeSeconds;
            if (std::abs(diff) <= step) state.currentValue = state.targetValue;
            else state.currentValue += (diff > 0 ? step : -step);
            const char* mappedId = Cubism2ParamBridge::MapParamName(kv.first.c_str());
            _mocLoader->SetParameterValue(mappedId, state.currentValue);
        }
    }

    // Apply parameter toggle overrides
    {
        const auto& overrides = LAppLive2DManager::GetInstance()->GetParamOverrides();
        for (const auto& kv : overrides)
        {
            const char* mappedId = Cubism2ParamBridge::MapParamName(kv.first.c_str());
            _mocLoader->SetParameterValue(mappedId, kv.second);
        }
    }

    // Apply part opacity overrides (component/clothing visibility toggle via Group ids).
    // Aligned with Live2DViewerEX: part opacity controlled by motion curves and fade system.
    {
        const auto& partOverrides = LAppLive2DManager::GetInstance()->GetPartOverrides();
        for (const auto& kv : partOverrides)
        {
            _mocLoader->SetDrawableOpacityById(kv.first.c_str(), kv.second);
        }
    }

    // Update controller engine
    {
        auto& engine = LAppLive2DManager::GetInstance()->GetControllerEngine();
        engine.UpdateCubism2(deltaTimeSeconds);
        for (const auto& kv : engine.GetControllerOverrides())
            _controllerParams[kv.first] = kv.second;
    }

    for (const auto& kv : _controllerParams)
        _mocLoader->SetParameterValue(kv.first.c_str(), kv.second);
    _controllerParams.clear();

    _mocLoader->Update();
}

void LAppModelCubism2::ApplyExpression()
{
    if (_currentExpressionIndex < 0 || _currentExpressionIndex >= static_cast<csmInt32>(_expressions.size()))
        return;

    const auto& expr = _expressions[_currentExpressionIndex];

    for (const auto& kv : expr.paramValues)
    {
        const char* mappedId = Cubism2ParamBridge::MapParamName(kv.first.c_str());
        csmFloat32 exprValue = kv.second;

        switch (expr.blendMode)
        {
            case ExprBlend_Add:
            {
                csmFloat32 baseValue = _mocLoader->GetParameterValue(mappedId);
                _mocLoader->SetParameterValue(mappedId, baseValue + exprValue);
                break;
            }
            case ExprBlend_Multiply:
            {
                csmFloat32 baseValue = _mocLoader->GetParameterValue(mappedId);
                _mocLoader->SetParameterValue(mappedId, baseValue * exprValue);
                break;
            }
            case ExprBlend_Overwrite:
            default:
                _mocLoader->SetParameterValue(mappedId, exprValue);
                break;
        }
    }
}

void LAppModelCubism2::NotifyMotionBegan(const char* group, csmInt32 no)
{
    for (const auto& cb : _motionBeganListeners)
        if(cb) cb(group, no);
}

void LAppModelCubism2::NotifyMotionFinished(const char* group, csmInt32 no)
{
    for (const auto& cb : _motionFinishedListeners)
        if(cb) cb(group, no);
}

void LAppModelCubism2::UpdatePhysicsFade(csmFloat32 dt)
{
    if(_physicsWeight == _physicsTargetWeight) return;

    const csmFloat32 diff = _physicsTargetWeight - _physicsWeight;
    if (_physicsFadeSpeed <= 0)
    {
        _physicsWeight = _physicsTargetWeight;
    }
    else
    {
        const csmFloat32 step = _physicsFadeSpeed * dt;
        if (std::abs(diff) <= step) _physicsWeight = _physicsTargetWeight;
        else _physicsWeight += (diff > 0 ? step : -step);
    }
    _physicsWeightDirty = true;
}

void LAppModelCubism2::EnablePhysicsWithFade(csmFloat32 targetWeight, csmFloat32 fadeTime)
{
    _physicsTargetWeight = std::max(0.0F, std::min(1.0F, targetWeight));
    _physicsFadeSpeed = (fadeTime > 0) ? std::abs(_physicsTargetWeight - _physicsWeight) / fadeTime : 999.0F;
}

void LAppModelCubism2::DisablePhysicsWithFade(csmFloat32 fadeTime)
{
    EnablePhysicsWithFade(0.0F, fadeTime);
}

void LAppModelCubism2::Draw(CubismMatrix44& matrix)
{
    if(!_initialized || !_mocLoader) return;

    matrix.MultiplyByMatrix(&_modelMatrix);
    _mocLoader->Draw(matrix.GetArray());
}

void LAppModelCubism2::ReloadRenderer()
{
    if (_mocLoader) _mocLoader->ReloadRenderer();
    LoadTextures();
}

void LAppModelCubism2::ParseLookAtJson(const char* jsonStr)
{
    if(jsonStr == nullptr || strlen(jsonStr) == 0) return;

    try {
        nlohmann::json j = nlohmann::json::parse(jsonStr);
        if(j.contains("angleXFactor")) _lookAtParams.angleXFactor = j["angleXFactor"].get<float>();
        if(j.contains("angleYFactor")) _lookAtParams.angleYFactor = j["angleYFactor"].get<float>();
        if(j.contains("bodyAngleXFactor")) _lookAtParams.bodyAngleXFactor = j["bodyAngleXFactor"].get<float>();
        if(j.contains("eyeBallXFactor")) _lookAtParams.eyeBallXFactor = j["eyeBallXFactor"].get<float>();
        if(j.contains("eyeBallYFactor")) _lookAtParams.eyeBallYFactor = j["eyeBallYFactor"].get<float>();
    } catch (const std::exception& e) {
        LAppPal::PrintLogLn("[Cubism2] Failed to parse look-at JSON: %s", e.what());
    }
}

void LAppModelCubism2::SetLookAtConfig(const char* json) { ParseLookAtJson(json); }

void LAppModelCubism2::SetExpressionBlendConfig(const char* json)
{
    if(json == nullptr || strlen(json) == 0) return;
    LAppPal::PrintLogLn("[Cubism2] Model config received: %d expressions loaded", (int)_expressions.size());
}
