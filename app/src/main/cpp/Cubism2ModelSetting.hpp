#pragma once

#include <ICubismModelSetting.hpp>
#include <Utils/CubismJson.hpp>
#include <Type/csmMap.hpp>

/// Implements ICubismModelSetting for Cubism 2 .model.json files.
/// Parses the JSON and exposes textures, motions, expressions, hit areas, etc.
class Cubism2ModelSetting : public Csm::ICubismModelSetting {
public:
    Cubism2ModelSetting(const Csm::csmByte* buffer, Csm::csmSizeInt size);
    ~Cubism2ModelSetting() override;

    // ICubismModelSetting interface
    const Csm::csmChar* GetModelFileName() override;
    Csm::csmInt32 GetTextureCount() override;
    const Csm::csmChar* GetTextureDirectory() override;
    const Csm::csmChar* GetTextureFileName(Csm::csmInt32 index) override;
    Csm::csmInt32 GetHitAreasCount() override;
    Csm::CubismIdHandle GetHitAreaId(Csm::csmInt32 index) override;
    const Csm::csmChar* GetHitAreaName(Csm::csmInt32 index) override;
    const Csm::csmChar* GetPhysicsFileName() override;
    const Csm::csmChar* GetPoseFileName() override;
    const Csm::csmChar* GetDisplayInfoFileName() override;
    Csm::csmInt32 GetExpressionCount() override;
    const Csm::csmChar* GetExpressionName(Csm::csmInt32 index) override;
    const Csm::csmChar* GetExpressionFileName(Csm::csmInt32 index) override;
    Csm::csmInt32 GetMotionGroupCount() override;
    const Csm::csmChar* GetMotionGroupName(Csm::csmInt32 index) override;
    Csm::csmInt32 GetMotionCount(const Csm::csmChar* groupName) override;
    const Csm::csmChar* GetMotionFileName(const Csm::csmChar* groupName, Csm::csmInt32 index) override;
    const Csm::csmChar* GetMotionSoundFileName(const Csm::csmChar* groupName, Csm::csmInt32 index) override;
    Csm::csmFloat32 GetMotionFadeInTimeValue(const Csm::csmChar* groupName, Csm::csmInt32 index) override;
    Csm::csmFloat32 GetMotionFadeOutTimeValue(const Csm::csmChar* groupName, Csm::csmInt32 index) override;
    const Csm::csmChar* GetUserDataFile() override;
    Csm::csmBool GetLayoutMap(Csm::csmMap<Csm::csmString, Csm::csmFloat32>& outLayoutMap) override;
    Csm::csmInt32 GetEyeBlinkParameterCount() override;
    Csm::CubismIdHandle GetEyeBlinkParameterId(Csm::csmInt32 index) override;
    Csm::csmInt32 GetLipSyncParameterCount() override;
    Csm::CubismIdHandle GetLipSyncParameterId(Csm::csmInt32 index) override;

private:
    Csm::Utils::CubismJson* _json;
};
