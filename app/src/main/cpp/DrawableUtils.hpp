#pragma once

#include <string>
#include <CubismFramework.hpp>
#include <Model/CubismModel.hpp>

/// Find drawable index by ID string. Returns -1 if not found.
inline Csm::csmInt32 FindDrawableIndex(Csm::CubismModel* model, const std::string& id) {
    using namespace Csm;
    const CubismId* drawId = CubismFramework::GetIdManager()->GetId(id.c_str());
    csmInt32 count = model->GetDrawableCount();
    for (csmInt32 d = 0; d < count; d++) {
        if (model->GetDrawableId(d) == drawId) return d;
    }
    return -1;
}
