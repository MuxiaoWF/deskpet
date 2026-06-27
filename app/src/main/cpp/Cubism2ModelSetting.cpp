#include "Cubism2ModelSetting.hpp"
#include "Cubism2ParamBridge.hpp"
#include <Utils/CubismJson.hpp>

using namespace Csm;
using namespace Utils;

Cubism2ModelSetting::Cubism2ModelSetting(const csmByte* buffer, csmSizeInt size)
    : _json(nullptr)
{
    _json = CubismJson::Create(buffer, size);
}

Cubism2ModelSetting::~Cubism2ModelSetting()
{
    CubismJson::Delete(_json);
}

const csmChar* Cubism2ModelSetting::GetModelFileName()
{
    if (_json == nullptr) { return "";
}
    Value& v = _json->GetRoot()["model"];
    return !v.IsNull() ? v.GetRawString() : "";
}

csmInt32 Cubism2ModelSetting::GetTextureCount()
{
    if (_json == nullptr) { return 0;
}
    Value& v = _json->GetRoot()["textures"];
    if (v.IsNull()) { return 0;
}
    csmVector<Value*>* vec = v.GetVector();
    return vec ? static_cast<csmInt32>(vec->GetSize()) : 0;
}

const csmChar* Cubism2ModelSetting::GetTextureDirectory()
{
    return "";
}

const csmChar* Cubism2ModelSetting::GetTextureFileName(csmInt32 index)
{
    if (_json == nullptr) { return "";
}
    Value& v = _json->GetRoot()["textures"];
    if (v.IsNull()) { return "";
}
    Value& item = v[index];
    return !item.IsNull() ? item.GetRawString() : "";
}

csmInt32 Cubism2ModelSetting::GetHitAreasCount()
{
    if (_json == nullptr) { return 0;
}
    Value& v = _json->GetRoot()["hit_areas"];
    if (v.IsNull()) { return 0;
}
    csmVector<Value*>* vec = v.GetVector();
    return vec ? static_cast<csmInt32>(vec->GetSize()) : 0;
}

CubismIdHandle Cubism2ModelSetting::GetHitAreaId(csmInt32 index)
{
    if (_json == nullptr) { return nullptr;
}
    Value& arr = _json->GetRoot()["hit_areas"];
    if (arr.IsNull()) { return nullptr;
}
    Value& item = arr[index];
    if (item.IsNull()) { return nullptr;
}
    Value& idVal = item["id"];
    if (idVal.IsNull()) { return nullptr;
}
    return Cubism2ParamBridge::GetMappedId(idVal.GetRawString());
}

const csmChar* Cubism2ModelSetting::GetHitAreaName(csmInt32 index)
{
    if (_json == nullptr) { return "";
}
    Value& arr = _json->GetRoot()["hit_areas"];
    if (arr.IsNull()) { return "";
}
    Value& item = arr[index];
    if (item.IsNull()) { return "";
}
    Value& nameVal = item["name"];
    return !nameVal.IsNull() ? nameVal.GetRawString() : "";
}

const csmChar* Cubism2ModelSetting::GetPhysicsFileName()
{
    if (_json == nullptr) { return "";
}
    Value& v = _json->GetRoot()["physics"];
    return !v.IsNull() ? v.GetRawString() : "";
}

const csmChar* Cubism2ModelSetting::GetPoseFileName()
{
    if (_json == nullptr) { return "";
}
    Value& v = _json->GetRoot()["pose"];
    return !v.IsNull() ? v.GetRawString() : "";
}

const csmChar* Cubism2ModelSetting::GetDisplayInfoFileName()
{
    return "";
}

csmInt32 Cubism2ModelSetting::GetExpressionCount()
{
    if (_json == nullptr) { return 0;
}
    Value& v = _json->GetRoot()["expressions"];
    if (v.IsNull()) { return 0;
}
    csmVector<Value*>* vec = v.GetVector();
    return vec ? static_cast<csmInt32>(vec->GetSize()) : 0;
}

const csmChar* Cubism2ModelSetting::GetExpressionName(csmInt32 index)
{
    if (_json == nullptr) { return "";
}
    Value& arr = _json->GetRoot()["expressions"];
    if (arr.IsNull()) { return "";
}
    Value& item = arr[index];
    if (item.IsNull()) { return "";
}
    // Cubism 2 uses "name", Cubism 3+ uses "Name"
    Value& nameVal = item["name"];
    if (!nameVal.IsNull()) { return nameVal.GetRawString();
}
    Value& nameVal2 = item["Name"];
    return !nameVal2.IsNull() ? nameVal2.GetRawString() : "";
}

const csmChar* Cubism2ModelSetting::GetExpressionFileName(csmInt32 index)
{
    if (_json == nullptr) { return "";
}
    Value& arr = _json->GetRoot()["expressions"];
    if (arr.IsNull()) { return "";
}
    Value& item = arr[index];
    if (item.IsNull()) { return "";
}
    Value& fileVal = item["file"];
    if (!fileVal.IsNull()) { return fileVal.GetRawString();
}
    Value& fileVal2 = item["File"];
    return !fileVal2.IsNull() ? fileVal2.GetRawString() : "";
}

csmInt32 Cubism2ModelSetting::GetMotionGroupCount()
{
    if (_json == nullptr) { return 0;
}
    Value& v = _json->GetRoot()["motions"];
    if (v.IsNull()) { return 0;
}
    csmVector<csmString> const& keys = v.GetKeys();
    return static_cast<csmInt32>(keys.GetSize());
}

const csmChar* Cubism2ModelSetting::GetMotionGroupName(csmInt32 index)
{
    if (_json == nullptr) { return "";
}
    Value& v = _json->GetRoot()["motions"];
    if (v.IsNull()) { return "";
}
    csmVector<csmString>& keys = v.GetKeys();
    if (index >= static_cast<csmInt32>(keys.GetSize())) { return "";
}
    return keys[index].GetRawString();
}

csmInt32 Cubism2ModelSetting::GetMotionCount(const csmChar* groupName)
{
    if (_json == nullptr) { return 0;
}
    Value& motions = _json->GetRoot()["motions"];
    if (motions.IsNull()) { return 0;
}
    Value& group = motions[groupName];
    if (group.IsNull()) { return 0;
}
    csmVector<Value*>* vec = group.GetVector();
    return vec ? static_cast<csmInt32>(vec->GetSize()) : 0;
}

const csmChar* Cubism2ModelSetting::GetMotionFileName(const csmChar* groupName, csmInt32 index)
{
    if (_json == nullptr) { return "";
}
    Value& motions = _json->GetRoot()["motions"];
    if (motions.IsNull()) { return "";
}
    Value& group = motions[groupName];
    if (group.IsNull()) { return "";
}
    Value& item = group[index];
    if (item.IsNull()) { return "";
}
    Value& fileVal = item["file"];
    if (!fileVal.IsNull()) { return fileVal.GetRawString();
}
    Value& fileVal2 = item["File"];
    return !fileVal2.IsNull() ? fileVal2.GetRawString() : "";
}

const csmChar* Cubism2ModelSetting::GetMotionSoundFileName(const csmChar* groupName, csmInt32 index)
{
    if (_json == nullptr) { return "";
}
    Value& motions = _json->GetRoot()["motions"];
    if (motions.IsNull()) { return "";
}
    Value& group = motions[groupName];
    if (group.IsNull()) { return "";
}
    Value& item = group[index];
    if (item.IsNull()) { return "";
}
    Value& soundVal = item["sound"];
    if (!soundVal.IsNull()) { return soundVal.GetRawString();
}
    Value& soundVal2 = item["Sound"];
    return !soundVal2.IsNull() ? soundVal2.GetRawString() : "";
}

csmFloat32 Cubism2ModelSetting::GetMotionFadeInTimeValue(const csmChar* groupName, csmInt32 index)
{
    if (_json == nullptr) { return 0.5F;
}
    Value& motions = _json->GetRoot()["motions"];
    if (motions.IsNull()) { return 0.5F;
}
    Value& group = motions[groupName];
    if (group.IsNull()) { return 0.5F;
}
    Value& item = group[index];
    if (item.IsNull()) { return 0.5F;
}
    Value& fadeVal = item["fade_in"];
    if(!fadeVal.IsNull()) { return fadeVal.ToFloat() / 1000.0F;
}
    Value& fadeVal2 = item["FadeInTime"];
    if(!fadeVal2.IsNull()) { return fadeVal2.ToFloat() / 1000.0F;
}
    return 0.5F;
}

csmFloat32 Cubism2ModelSetting::GetMotionFadeOutTimeValue(const csmChar* groupName, csmInt32 index)
{
    if (_json == nullptr) { return 0.5F;
}
    Value& motions = _json->GetRoot()["motions"];
    if (motions.IsNull()) { return 0.5F;
}
    Value& group = motions[groupName];
    if (group.IsNull()) { return 0.5F;
}
    Value& item = group[index];
    if (item.IsNull()) { return 0.5F;
}
    Value& fadeVal = item["fade_out"];
    if(!fadeVal.IsNull()) { return fadeVal.ToFloat() / 1000.0F;
}
    Value& fadeVal2 = item["FadeOutTime"];
    if(!fadeVal2.IsNull()) { return fadeVal2.ToFloat() / 1000.0F;
}
    return 0.5F;
}

const csmChar* Cubism2ModelSetting::GetUserDataFile()
{
    if (_json == nullptr) { return "";
}
    Value& v = _json->GetRoot()["userdata"];
    return !v.IsNull() ? v.GetRawString() : "";
}

csmBool Cubism2ModelSetting::GetLayoutMap(csmMap<csmString, csmFloat32>& outLayoutMap)
{
    if (_json == nullptr) { return false;
}
    Value& v = _json->GetRoot()["layout"];
    if (v.IsNull()) { return false;
}
    csmVector<csmString>& keys = v.GetKeys();
    for (csmUint32 i = 0; i < keys.GetSize(); i++)
    {
        const csmChar* key = keys[i].GetRawString();
        Value& val = v[key];
        if (!val.IsNull())
        {
            outLayoutMap[key] = val.ToFloat();
        }
    }
    return outLayoutMap.GetSize() > 0;
}

csmInt32 Cubism2ModelSetting::GetEyeBlinkParameterCount()
{
    // Cubism 2 models don't declare eye blink parameters in settings
    return 0;
}

CubismIdHandle Cubism2ModelSetting::GetEyeBlinkParameterId(csmInt32  /*index*/)
{
    return nullptr;
}

csmInt32 Cubism2ModelSetting::GetLipSyncParameterCount()
{
    return 0;
}

CubismIdHandle Cubism2ModelSetting::GetLipSyncParameterId(csmInt32  /*index*/)
{
    return nullptr;
}
