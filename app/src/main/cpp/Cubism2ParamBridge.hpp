#pragma once

#include <CubismFramework.hpp>
#include <Id/CubismId.hpp>
#include <Id/CubismIdManager.hpp>

namespace Cubism2ParamBridge {

    // Map a Cubism 2 parameter name to its Cubism 3+ equivalent.
    // Returns the original name if no mapping exists.
    const Csm::csmChar* MapParamName(const Csm::csmChar* cubism2Name);

    // Get a CubismId handle for a Cubism 2 parameter name.
    Csm::CubismIdHandle GetMappedId(const Csm::csmChar* cubism2Name);

}
