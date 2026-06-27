#include "Cubism2ParamBridge.hpp"
#include <cstring>

namespace {

    struct ParamMapping {
        const char* cubism2;
        const char* cubism3;
    };

    // Complete mapping table: Cubism 2 → Cubism 3/4/5
    const ParamMapping s_mappings[] = {
        // Angle
        {"PARAM_ANGLE_X",       "ParamAngleX"},
        {"PARAM_ANGLE_Y",       "ParamAngleY"},
        {"PARAM_ANGLE_Z",       "ParamAngleZ"},
        // Body
        {"PARAM_BODY_ANGLE_X",  "ParamBodyAngleX"},
        {"PARAM_BODY_ANGLE_Y",  "ParamBodyAngleY"},
        {"PARAM_BODY_ANGLE_Z",  "ParamBodyAngleZ"},
        // Eye
        {"PARAM_EYE_BALL_X",    "ParamEyeBallX"},
        {"PARAM_EYE_BALL_Y",    "ParamEyeBallY"},
        {"PARAM_EYE_L_OPEN",    "ParamEyeLOpen"},
        {"PARAM_EYE_R_OPEN",    "ParamEyeROpen"},
        {"PARAM_EYE_L_SMILE",   "ParamEyeLSmile"},
        {"PARAM_EYE_R_SMILE",   "ParamEyeRSmile"},
        // Mouth
        {"PARAM_MOUTH_FORM",    "ParamMouthForm"},
        {"PARAM_MOUTH_OPEN_Y",  "ParamMouthOpenY"},
        // Brow
        {"PARAM_BROW_L_Y",      "ParamBrowLY"},
        {"PARAM_BROW_R_Y",      "ParamBrowRY"},
        {"PARAM_BROW_L_ANGLE",  "ParamBrowLAngle"},
        {"PARAM_BROW_R_ANGLE",  "ParamBrowRAngle"},
        {"PARAM_BROW_L_FORM",   "ParamBrowLForm"},
        {"PARAM_BROW_R_FORM",   "ParamBrowRForm"},
        // Other
        {"PARAM_CHEEK",         "ParamCheek"},
        {"PARAM_HAIR_FRONT",    "ParamHairFront"},
        {"PARAM_HAIR_BACK",     "ParamHairBack"},
        {"PARAM_SHOULDER_X",    "ParamShoulderX"},
        {"PARAM_HAND_L",        "ParamHandL"},
        {"PARAM_HAND_R",        "ParamHandR"},
        // Parts
        {"PARAM_PARTS_01",      "ParamParts01"},
        {"PARAM_PARTS_02",      "ParamParts02"},
        {"PARAM_PARTS_03",      "ParamParts03"},
        {"PARAM_PARTS_04",      "ParamParts04"},
        {"PARAM_PARTS_05",      "ParamParts05"},
        {"PARAM_PARTS_06",      "ParamParts06"},
        {"PARAM_PARTS_07",      "ParamParts07"},
        {"PARAM_PARTS_08",      "ParamParts08"},
        {"PARAM_PARTS_09",      "ParamParts09"},
        {"PARAM_PARTS_10",      "ParamParts10"},
        {"PARAM_PARTS_11",      "ParamParts11"},
        {"PARAM_PARTS_12",      "ParamParts12"},
        {"PARAM_PARTS_13",      "ParamParts13"},
        {"PARAM_PARTS_14",      "ParamParts14"},
        {"PARAM_PARTS_15",      "ParamParts15"},
        {"PARAM_PARTS_16",      "ParamParts16"},
        // Breath
        {"PARAM_BREATH",        "ParamBreath"},
    };
}

const Csm::csmChar* Cubism2ParamBridge::MapParamName(const Csm::csmChar* cubism2Name)
{
    for (auto s_mapping : s_mappings)
    {
        if (strcmp(cubism2Name, s_mapping.cubism2) == 0)
        {
            return s_mapping.cubism3;
        }
    }
    // No mapping found — return as-is (model may use Cubism 3+ names already)
    return cubism2Name;
}

Csm::CubismIdHandle Cubism2ParamBridge::GetMappedId(const Csm::csmChar* cubism2Name)
{
    return Csm::CubismFramework::GetIdManager()->GetId(MapParamName(cubism2Name));
}
