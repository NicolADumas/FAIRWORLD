#include "pch.h"
#include "BiomeSystems.h"
#include "BiomeComponents.h"
#include "ForgeComponents.h"
#include "PerlinNoise.h"
#include <cmath>
#include <algorithm>

#include "BlockRegistry.h"
#include "MapWorldGenerator.h"

namespace fw {

namespace {
    struct SdfResult {
        float height;
        uint8_t surfaceBlock;
        uint8_t subsurfaceBlock;
        fw::MapRegionType dominantBiome;
    };

    SdfResult EvaluateSDF(const fw::BiomeDataComponent& biome, float worldX, float worldZ, glm::vec3 noisePos, const PerlinNoise& terrainNoiseGen) {
        float freq = biome.baseRegion.perlinFrequency;
        float terrainVal = terrainNoiseGen.octaveNoise(noisePos.x * freq, noisePos.y * freq, noisePos.z * freq, 4, 0.5);
        float baseHeight = 25.0f + (terrainVal * 25.0f * biome.baseRegion.gravityModifier);
        
        if (biome.baseRegion.type == fw::MapRegionType::Ocean) {
            baseHeight = 8.0f + (terrainVal * 5.0f); // Oceano profondo
        }
        
        float finalHeight = baseHeight;
        uint8_t surfaceBlock = biome.baseRegion.surfaceBlockId;
        uint8_t subsurfaceBlock = biome.baseRegion.subsurfaceBlockId;
        fw::MapRegionType colBiome = biome.baseRegion.type;
        
        glm::vec3 colNormal = glm::normalize(biome.chunkCenterWorld);
        
        for (const auto& r : biome.overlappingRegions) {
            float sdf = 0.0f;
            float blendDistance = 0.0f;
            
            if (biome.planetRadius > 0.0f && r.angularRadius > 0.0f) {
                float pitch = glm::radians(r.eulerAngles.x);
                float yaw = glm::radians(r.eulerAngles.y);
                glm::vec3 rCenterNormal(cos(pitch) * cos(yaw), sin(pitch), cos(pitch) * sin(yaw));
                
                float dotProduct = glm::dot(colNormal, rCenterNormal);
                dotProduct = std::clamp(dotProduct, -1.0f, 1.0f);
                float angle = acos(dotProduct); 
                
                sdf = angle - r.angularRadius;
                blendDistance = 0.08f;
            } else {
                float rMinX = r.rectMin.x * 16.0f;
                float rMinZ = r.rectMin.y * 16.0f;
                float rMaxX = (r.rectMax.x + 1) * 16.0f;
                float rMaxZ = (r.rectMax.y + 1) * 16.0f;
                
                float centerX = (rMinX + rMaxX) * 0.5f;
                float centerZ = (rMinZ + rMaxZ) * 0.5f;
                float halfW = (rMaxX - rMinX) * 0.5f;
                float halfH = (rMaxZ - rMinZ) * 0.5f;
                
                if (r.shape == fw::RegionShape::Circle) {
                    float dist = glm::distance(glm::vec2(worldX, worldZ), glm::vec2(centerX, centerZ));
                    sdf = dist - halfW;
                } else {
                    glm::vec2 d = glm::abs(glm::vec2(worldX - centerX, worldZ - centerZ)) - glm::vec2(halfW, halfH);
                    sdf = glm::length(glm::max(d, glm::vec2(0.0f))) + std::min(std::max(d.x, d.y), 0.0f);
                }
                blendDistance = 12.0f;
            }
            
            if (sdf < blendDistance) {
                float freq = r.perlinFrequency;
                float rTerrainVal = terrainNoiseGen.octaveNoise(noisePos.x * freq, noisePos.y * freq, noisePos.z * freq, 4, 0.5);
                float rHeight = 25.0f + (rTerrainVal * 25.0f * r.gravityModifier);
                
                if (r.type == fw::MapRegionType::Ocean) {
                    rHeight = 8.0f + (rTerrainVal * 5.0f);
                }
                
                float influence = std::clamp(1.0f - (sdf / blendDistance), 0.0f, 1.0f);
                influence = influence * influence * (3.0f - 2.0f * influence);
                finalHeight = glm::mix(finalHeight, rHeight, influence);
                
                if (influence > 0.5f) {
                    surfaceBlock = r.surfaceBlockId;
                    subsurfaceBlock = r.subsurfaceBlockId;
                    colBiome = r.type;
                }
            }
        }
        return { finalHeight, surfaceBlock, subsurfaceBlock, colBiome };
    }
}

// --- DISPATCHER TERRAIN ---
void BiomeTerrainSystem::Update(entt::registry& registry, int maxChunksPerFrame, BlockRegistry* blockRegistry) {
    ForestTerrainSystem::Update(registry, maxChunksPerFrame, blockRegistry);
    DesertTerrainSystem::Update(registry, maxChunksPerFrame, blockRegistry);
    OceanTerrainSystem::Update(registry, maxChunksPerFrame, blockRegistry);
    VolcanoTerrainSystem::Update(registry, maxChunksPerFrame, blockRegistry);
    TundraTerrainSystem::Update(registry, maxChunksPerFrame, blockRegistry);
    FlatTerrainSystem::Update(registry, maxChunksPerFrame, blockRegistry);
    // TODO: Aggiungere gli altri biomi quando implementati
}

// --- FOREST TERRAIN ---
void ForestTerrainSystem::Update(entt::registry& registry, int maxChunksPerFrame, BlockRegistry* blockRegistry) {
    auto view = registry.view<fw::VoxelChunkComponent, BiomeDataComponent, TerrainGenTag, ForestBiomeTag>();
    
    int processed = 0;
    uint8_t idAir = 0, idStone = 255, idWater = 255;
    if (blockRegistry) {
        idAir = blockRegistry->GetBlock("fairworld:air").id;
        idStone = blockRegistry->GetBlock("fairworld:stone").id;
        idWater = blockRegistry->GetBlock("fairworld:water").id;
    }
    
    std::vector<entt::entity> toProcess;
    for (auto entity : view) {
        if (processed >= maxChunksPerFrame) break;
        toProcess.push_back(entity);
        processed++;
    }

    static PerlinNoise terrainNoiseGen(12345);
    static PerlinNoise caveNoiseGen(11111);

    for (auto entity : toProcess) {
        auto& chunk = registry.get<fw::VoxelChunkComponent>(entity);
        const auto& biome = registry.get<BiomeDataComponent>(entity);

        int cx = chunk.cx;
        int cz = chunk.cz;

        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                float worldX = cx * 16.0f + x;
                float worldZ = cz * 16.0f + z;
                
                glm::vec3 noisePos(worldX, 0.0f, worldZ);
                if (biome.planetRadius > 0.0f) {
                    fw::MapWorldGenerator::GetTrueSphericalPosition(biome.planetRadius, cx, cz, (float)x, 0.0f, (float)z, noisePos);
                }
                
                SdfResult sdf = EvaluateSDF(biome, worldX, worldZ, noisePos, terrainNoiseGen);
                int height = (int)sdf.height;
                
                for (int y = 0; y < 128; ++y) {
                    bool isCave = false;
                    
                    if (y > 4 && y < height - 4) {
                        float caveFreq = 0.05f; 
                        float caveDensity = caveNoiseGen.octaveNoise(noisePos.x * caveFreq, (noisePos.y + y) * caveFreq * 1.5f, noisePos.z * caveFreq, 3, 0.5f);
                        if (caveDensity > 0.55f) {
                            isCave = true;
                        }
                    }

                    if (isCave) {
                        if (y <= 16) chunk.blocks[x][y][z] = idWater;
                        else chunk.blocks[x][y][z] = idAir;
                    } else {
                        if (y < height - 3) chunk.blocks[x][y][z] = idStone;
                        else if (y < height) chunk.blocks[x][y][z] = sdf.subsurfaceBlock;
                        else if (y == height) chunk.blocks[x][y][z] = sdf.surfaceBlock;
                        else if (y <= 16) chunk.blocks[x][y][z] = idWater;
                        else chunk.blocks[x][y][z] = idAir;
                    }
                    chunk.light[x][y][z] = 255; 
                }
            }
        }
        chunk.isGenerated = true;
        registry.remove<TerrainGenTag>(entity);
        registry.emplace<DecoratorGenTag>(entity);
    }
}

// --- DESERT TERRAIN ---
void DesertTerrainSystem::Update(entt::registry& registry, int maxChunksPerFrame, BlockRegistry* blockRegistry) {
    auto view = registry.view<fw::VoxelChunkComponent, BiomeDataComponent, TerrainGenTag, DesertBiomeTag>();
    
    int processed = 0;
    uint8_t idAir = 0, idStone = 255, idWater = 255;
    if (blockRegistry) {
        idAir = blockRegistry->GetBlock("fairworld:air").id;
        idStone = blockRegistry->GetBlock("fairworld:stone").id;
        idWater = blockRegistry->GetBlock("fairworld:water").id;
    }
    
    std::vector<entt::entity> toProcess;
    for (auto entity : view) {
        if (processed >= maxChunksPerFrame) break;
        toProcess.push_back(entity);
        processed++;
    }

    static PerlinNoise terrainNoiseGen(12345);
    // Niente caverne d'acqua, ma magari caverne vuote di sabbia (sandstone)
    static PerlinNoise caveNoiseGen(22222);

    for (auto entity : toProcess) {
        auto& chunk = registry.get<fw::VoxelChunkComponent>(entity);
        const auto& biome = registry.get<BiomeDataComponent>(entity);

        int cx = chunk.cx;
        int cz = chunk.cz;

        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                float worldX = cx * 16.0f + x;
                float worldZ = cz * 16.0f + z;
                
                glm::vec3 noisePos(worldX, 0.0f, worldZ);
                if (biome.planetRadius > 0.0f) {
                    fw::MapWorldGenerator::GetTrueSphericalPosition(biome.planetRadius, cx, cz, (float)x, 0.0f, (float)z, noisePos);
                }
                
                SdfResult sdf = EvaluateSDF(biome, worldX, worldZ, noisePos, terrainNoiseGen);
                int height = (int)sdf.height;
                
                for (int y = 0; y < 128; ++y) {
                    bool isCave = false;
                    
                    if (y > 4 && y < height - 6) {
                        float caveFreq = 0.04f; // Caverne più larghe nel deserto
                        float caveDensity = caveNoiseGen.octaveNoise(noisePos.x * caveFreq, (noisePos.y + y) * caveFreq * 1.5f, noisePos.z * caveFreq, 3, 0.5f);
                        if (caveDensity > 0.60f) {
                            isCave = true;
                        }
                    }

                    if (isCave) {
                        chunk.blocks[x][y][z] = idAir; // Niente acqua nel deserto
                    } else {
                        // Strato di sabbia molto più spesso
                        if (y < height - 6) chunk.blocks[x][y][z] = idStone;
                        else if (y < height) chunk.blocks[x][y][z] = sdf.subsurfaceBlock;
                        else if (y == height) chunk.blocks[x][y][z] = sdf.surfaceBlock;
                        else chunk.blocks[x][y][z] = idAir; // Niente oceano
                    }
                    chunk.light[x][y][z] = 255; 
                }
            }
        }
        chunk.isGenerated = true;
        registry.remove<TerrainGenTag>(entity);
        registry.emplace<DecoratorGenTag>(entity);
    }
}

// --- OCEAN TERRAIN ---
void OceanTerrainSystem::Update(entt::registry& registry, int maxChunksPerFrame, BlockRegistry* blockRegistry) {
    auto view = registry.view<fw::VoxelChunkComponent, BiomeDataComponent, TerrainGenTag, OceanBiomeTag>();
    
    int processed = 0;
    uint8_t idAir = 0, idStone = 255, idWater = 255;
    if (blockRegistry) {
        idAir = blockRegistry->GetBlock("fairworld:air").id;
        idStone = blockRegistry->GetBlock("fairworld:stone").id;
        idWater = blockRegistry->GetBlock("fairworld:water").id;
    }
    
    std::vector<entt::entity> toProcess;
    for (auto entity : view) {
        if (processed >= maxChunksPerFrame) break;
        toProcess.push_back(entity);
        processed++;
    }

    static PerlinNoise terrainNoiseGen(12345);

    for (auto entity : toProcess) {
        auto& chunk = registry.get<fw::VoxelChunkComponent>(entity);
        const auto& biome = registry.get<BiomeDataComponent>(entity);

        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                float worldX = chunk.cx * 16.0f + x;
                float worldZ = chunk.cz * 16.0f + z;
                
                glm::vec3 noisePos(worldX, 0.0f, worldZ);
                if (biome.planetRadius > 0.0f) {
                    fw::MapWorldGenerator::GetTrueSphericalPosition(biome.planetRadius, chunk.cx, chunk.cz, (float)x, 0.0f, (float)z, noisePos);
                }
                
                SdfResult sdf = EvaluateSDF(biome, worldX, worldZ, noisePos, terrainNoiseGen);
                int height = (int)sdf.height;
                
                for (int y = 0; y < 128; ++y) {
                    // Nessuna caverna nell'oceano
                    if (y < height - 3) chunk.blocks[x][y][z] = idStone;
                    else if (y < height) chunk.blocks[x][y][z] = sdf.subsurfaceBlock;
                    else if (y == height) chunk.blocks[x][y][z] = sdf.surfaceBlock;
                    else if (y <= 20) chunk.blocks[x][y][z] = idWater; // Oceano più alto
                    else chunk.blocks[x][y][z] = idAir;
                    
                    chunk.light[x][y][z] = 255; 
                }
            }
        }
        chunk.isGenerated = true;
        registry.remove<TerrainGenTag>(entity);
        registry.emplace<DecoratorGenTag>(entity);
    }
}

// --- TUNDRA TERRAIN ---
void TundraTerrainSystem::Update(entt::registry& registry, int maxChunksPerFrame, BlockRegistry* blockRegistry) {
    auto view = registry.view<fw::VoxelChunkComponent, BiomeDataComponent, TerrainGenTag, TundraBiomeTag>();
    int processed = 0;
    uint8_t idAir = 0, idStone = 255, idWater = 255;
    if (blockRegistry) {
        idAir = blockRegistry->GetBlock("fairworld:air").id;
        idStone = blockRegistry->GetBlock("fairworld:stone").id;
        idWater = blockRegistry->GetBlock("fairworld:water").id;
    }
    
    std::vector<entt::entity> toProcess;
    for (auto entity : view) {
        if (processed >= maxChunksPerFrame) break;
        toProcess.push_back(entity);
        processed++;
    }

    static PerlinNoise terrainNoiseGen(12345);
    
    for (auto entity : toProcess) {
        auto& chunk = registry.get<fw::VoxelChunkComponent>(entity);
        const auto& biome = registry.get<BiomeDataComponent>(entity);

        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                float worldX = chunk.cx * 16.0f + x;
                float worldZ = chunk.cz * 16.0f + z;
                
                glm::vec3 noisePos(worldX, 0.0f, worldZ);
                if (biome.planetRadius > 0.0f) {
                    fw::MapWorldGenerator::GetTrueSphericalPosition(biome.planetRadius, chunk.cx, chunk.cz, (float)x, 0.0f, (float)z, noisePos);
                }
                
                SdfResult sdf = EvaluateSDF(biome, worldX, worldZ, noisePos, terrainNoiseGen);
                int height = (int)sdf.height;
                
                for (int y = 0; y < 128; ++y) {
                    if (y < height - 3) chunk.blocks[x][y][z] = idStone;
                    else if (y < height) chunk.blocks[x][y][z] = sdf.subsurfaceBlock;
                    else if (y == height) chunk.blocks[x][y][z] = sdf.surfaceBlock;
                    else if (y <= 16) chunk.blocks[x][y][z] = idWater; 
                    else chunk.blocks[x][y][z] = idAir;
                    chunk.light[x][y][z] = 255; 
                }
            }
        }
        chunk.isGenerated = true;
        registry.remove<TerrainGenTag>(entity);
        registry.emplace<DecoratorGenTag>(entity);
    }
}

// --- VOLCANO TERRAIN ---
void VolcanoTerrainSystem::Update(entt::registry& registry, int maxChunksPerFrame, BlockRegistry* blockRegistry) {
    auto view = registry.view<fw::VoxelChunkComponent, BiomeDataComponent, TerrainGenTag, VolcanoBiomeTag>();
    int processed = 0;
    uint8_t idAir = 0, idStone = 255, idWater = 255;
    if (blockRegistry) {
        idAir = blockRegistry->GetBlock("fairworld:air").id;
        idStone = blockRegistry->GetBlock("fairworld:stone").id;
        idWater = blockRegistry->GetBlock("fairworld:water").id; // Magari lava in futuro
    }
    
    std::vector<entt::entity> toProcess;
    for (auto entity : view) {
        if (processed >= maxChunksPerFrame) break;
        toProcess.push_back(entity);
        processed++;
    }

    static PerlinNoise terrainNoiseGen(12345);
    static PerlinNoise lavaTubeNoise(33333);
    
    for (auto entity : toProcess) {
        auto& chunk = registry.get<fw::VoxelChunkComponent>(entity);
        const auto& biome = registry.get<BiomeDataComponent>(entity);

        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                float worldX = chunk.cx * 16.0f + x;
                float worldZ = chunk.cz * 16.0f + z;
                
                glm::vec3 noisePos(worldX, 0.0f, worldZ);
                if (biome.planetRadius > 0.0f) {
                    fw::MapWorldGenerator::GetTrueSphericalPosition(biome.planetRadius, chunk.cx, chunk.cz, (float)x, 0.0f, (float)z, noisePos);
                }
                
                SdfResult sdf = EvaluateSDF(biome, worldX, worldZ, noisePos, terrainNoiseGen);
                int height = (int)sdf.height;
                
                for (int y = 0; y < 128; ++y) {
                    bool isLavaTube = false;
                    
                    if (y > 4 && y < height - 2) {
                        float caveFreq = 0.08f; 
                        float caveDensity = lavaTubeNoise.octaveNoise(noisePos.x * caveFreq, (noisePos.y + y) * caveFreq * 2.0f, noisePos.z * caveFreq, 3, 0.5f);
                        if (caveDensity > 0.60f) {
                            isLavaTube = true;
                        }
                    }

                    if (isLavaTube) {
                        if (y <= 12) chunk.blocks[x][y][z] = idWater; // Usiamo l'acqua come segnaposto finchè non avremo un ID lava
                        else chunk.blocks[x][y][z] = idAir;
                    } else {
                        if (y < height - 3) chunk.blocks[x][y][z] = idStone;
                        else if (y < height) chunk.blocks[x][y][z] = sdf.subsurfaceBlock;
                        else if (y == height) chunk.blocks[x][y][z] = sdf.surfaceBlock;
                        else chunk.blocks[x][y][z] = idAir;
                    }
                    chunk.light[x][y][z] = 255; 
                }
            }
        }
        chunk.isGenerated = true;
        registry.remove<TerrainGenTag>(entity);
        registry.emplace<DecoratorGenTag>(entity);
    }
}

// ==========================================
// DECORATORS
// ==========================================

void BiomeDecoratorSystem::Update(entt::registry& registry, int maxChunksPerFrame, BlockRegistry* blockRegistry) {
    ForestDecoratorSystem::Update(registry, maxChunksPerFrame, blockRegistry);
    DesertDecoratorSystem::Update(registry, maxChunksPerFrame, blockRegistry);
    FlatDecoratorSystem::Update(registry, maxChunksPerFrame, blockRegistry);

    // Fallback generico per tutti gli altri biomi (Ocean, Tundra, Volcano, City, Dungeon, Portal)
    // Se sono rimasti incastrati con DecoratorGenTag, li passiamo a Dirty per generare la mesh
    auto view = registry.view<fw::VoxelChunkComponent, DecoratorGenTag>();
    std::vector<entt::entity> toProcess;
    for (auto entity : view) {
        toProcess.push_back(entity);
    }
    for (auto entity : toProcess) {
        registry.remove<DecoratorGenTag>(entity);
        registry.emplace_or_replace<ChunkDirtyComponent>(entity);
    }
}

void ForestDecoratorSystem::Update(entt::registry& registry, int maxChunksPerFrame, BlockRegistry* blockRegistry) {
    auto view = registry.view<fw::VoxelChunkComponent, BiomeDataComponent, DecoratorGenTag, ForestBiomeTag>();
    
    int processed = 0;
    uint8_t idAir = 0, idGrass = 255, idWood = 255, idLeaves = 255;
    if (blockRegistry) {
        idAir = blockRegistry->GetBlock("fairworld:air").id;
        idGrass = blockRegistry->GetBlock("fairworld:grass").id;
        idWood = blockRegistry->GetBlock("fairworld:wood").id;
        idLeaves = blockRegistry->GetBlock("fairworld:leaves").id;
    }

    std::vector<entt::entity> toProcess;
    for (auto entity : view) {
        if (processed >= maxChunksPerFrame) break;
        toProcess.push_back(entity);
        processed++;
    }

    for (auto entity : toProcess) {
        auto& chunk = registry.get<fw::VoxelChunkComponent>(entity);

        bool canHaveTrees = false;
        for (int lx = 0; lx < 16; ++lx) {
            for (int lz = 0; lz < 16; ++lz) {
                for (int ly = 127; ly >= 0; --ly) {
                    if (chunk.blocks[lx][ly][lz] == idGrass) canHaveTrees = true;
                }
            }
        }

        if (canHaveTrees) {
            std::vector<glm::ivec3> treePositions;
            for (int x = 4; x < 12; x += 6) {
                for (int z = 4; z < 12; z += 6) {
                    int surfaceY = 0;
                    for (int y = 127; y >= 0; --y) {
                        if (chunk.blocks[x][y][z] == idGrass) {
                            surfaceY = y;
                            break;
                        }
                    }
                    if (surfaceY > 0 && (rand() % 100) < 40) {
                        treePositions.push_back({x, surfaceY, z});
                    }
                }
            }
            
            for (auto p : treePositions) {
                int tx = p.x; int ty = p.y; int tz = p.z;
                for (int h = 1; h <= 4; ++h) {
                    if (ty + h < 128) chunk.blocks[tx][ty + h][tz] = idWood;
                }
                for (int lx = tx - 2; lx <= tx + 2; ++lx) {
                    for (int lz = tz - 2; lz <= tz + 2; ++lz) {
                        for (int ly = ty + 3; ly <= ty + 5; ++ly) {
                            if (lx >= 0 && lx < 16 && lz >= 0 && lz < 16 && ly < 128) {
                                if (chunk.blocks[lx][ly][lz] == 0) chunk.blocks[lx][ly][lz] = idLeaves;
                            }
                        }
                    }
                }
            }
        }
        registry.remove<DecoratorGenTag>(entity);
        registry.emplace_or_replace<ChunkDirtyComponent>(entity);
    }
}

void DesertDecoratorSystem::Update(entt::registry& registry, int maxChunksPerFrame, BlockRegistry* blockRegistry) {
    auto view = registry.view<fw::VoxelChunkComponent, BiomeDataComponent, DecoratorGenTag, DesertBiomeTag>();
    
    int processed = 0;
    std::vector<entt::entity> toProcess;
    for (auto entity : view) {
        if (processed >= maxChunksPerFrame) break;
        toProcess.push_back(entity);
        processed++;
    }

    // Nel deserto attualmente non facciamo decorazioni complesse (forse cactus in futuro)
    for (auto entity : toProcess) {
        registry.remove<DecoratorGenTag>(entity);
        registry.emplace_or_replace<ChunkDirtyComponent>(entity);
    }
}

// --- FLAT TERRAIN ---
void FlatTerrainSystem::Update(entt::registry& registry, int maxChunksPerFrame, BlockRegistry* blockRegistry) {
    auto view = registry.view<fw::VoxelChunkComponent, BiomeDataComponent, TerrainGenTag, FlatBiomeTag>();
    
    int processed = 0;
    uint8_t idAir = 0;
    uint8_t idSurface = 1; // Default
    uint8_t idSubsurface = 1; // Default
    
    if (blockRegistry) {
        idAir = blockRegistry->GetBlock("fairworld:air").id;
    }
    
    std::vector<entt::entity> toProcess;
    for (auto entity : view) {
        if (processed >= maxChunksPerFrame) break;
        toProcess.push_back(entity);
        processed++;
    }

    for (auto entity : toProcess) {
        auto& chunk = registry.get<fw::VoxelChunkComponent>(entity);
        const auto& biome = registry.get<BiomeDataComponent>(entity);

        idSurface = biome.surfaceBlockId;
        idSubsurface = biome.subsurfaceBlockId;

        int cx = chunk.cx;
        int cz = chunk.cz;

        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                // Altezza piatta costante (e.g. 25, o potremmo usare gravityModifier per variare l'altezza)
                int height = 25; 
                
                for (int y = 0; y < 128; ++y) {
                    if (y < height) {
                        chunk.blocks[x][y][z] = idSubsurface;
                    } else if (y == height) {
                        chunk.blocks[x][y][z] = idSurface;
                    } else {
                        chunk.blocks[x][y][z] = idAir;
                    }
                    chunk.light[x][y][z] = 255; 
                }
            }
        }
        chunk.isGenerated = true;
        registry.remove<TerrainGenTag>(entity);
        registry.emplace<DecoratorGenTag>(entity);
    }
}

void FlatDecoratorSystem::Update(entt::registry& registry, int maxChunksPerFrame, BlockRegistry* blockRegistry) {
    auto view = registry.view<fw::VoxelChunkComponent, BiomeDataComponent, DecoratorGenTag, FlatBiomeTag>();
    
    int processed = 0;
    std::vector<entt::entity> toProcess;
    for (auto entity : view) {
        if (processed >= maxChunksPerFrame) break;
        toProcess.push_back(entity);
        processed++;
    }

    for (auto entity : toProcess) {
        // Nessun albero nel bioma flat, è completamente vuoto
        registry.remove<DecoratorGenTag>(entity);
        registry.emplace<ChunkDirtyComponent>(entity);
    }
}

} // namespace fw
