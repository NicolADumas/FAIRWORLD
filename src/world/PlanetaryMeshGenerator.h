#pragma once

#include <vector>
#include "components/ForgeComponents.h"
#include "core/utils/PlanetMath.h"

namespace fw {

class PlanetaryMeshGenerator {
public:
    static void ConvertToPlanetaryPositions(
        std::vector<Vertex>& inOutVertices,
        const fw::PlanetChunkCoord& planetCoord,
        fw::PlanetSize planetSize
    );
};

} // namespace fw
