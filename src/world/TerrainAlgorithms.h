#pragma once

#include "MapDocument.h"
#include "TerrainSolver.h"

namespace fw {

struct TerrainAlgorithmContext {
    const TerrainGenerationContext& context;
    const ResolvedTerrainRules& rules;
    TerrainWorkspace& workspace;
    
    // Add access to the TerrainSolver for helper functions if necessary
    const class TerrainSolver* solver;
};

// Stateless Generator Functions
void GeneratePlains(const TerrainAlgorithmContext& ctx);
void GenerateMountains(const TerrainAlgorithmContext& ctx);
void GenerateDunes(const TerrainAlgorithmContext& ctx);
void GenerateHills(const TerrainAlgorithmContext& ctx); // Fallback for Hills if needed

} // namespace fw
