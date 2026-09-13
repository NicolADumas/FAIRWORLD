#pragma once
#include "SharedContext.h"
#include "WorldProjectManager.h"
#include <vector>

namespace fw {
    struct PlanetMap;
}

class PlanetMapperCompiler {
public:
    PlanetMapperCompiler();
    ~PlanetMapperCompiler() = default;

    // Compila i dati del pianeta e chiama l'upload (totale o parziale) al RenderManager
    void Update(SharedContext* context, int activePlanetIndex);

private:
    void CompileEverything(SharedContext* context, fw::PlanetMap& planet);
    void CompileChunk(SharedContext* context, fw::PlanetMap& planet, uint32_t chunkIndex);
};
