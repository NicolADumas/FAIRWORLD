#include "pch.h"
#include "BiomeSystems.h"
#include "BiomeComponents.h"
#include "ForgeComponents.h"
#include "PerlinNoise.h"
#include <cmath>

#include "BlockRegistry.h"

namespace fw {

void BiomeTerrainSystem::Update(entt::registry& registry, int maxChunksPerFrame, BlockRegistry* blockRegistry) {
    auto view = registry.view<fw::VoxelChunkComponent, BiomeDataComponent, TerrainGenTag>();
    
    int processed = 0;
    
    // Query BlockRegistry dynamically via string IDs
    uint8_t idAir = 0;
    uint8_t idGrass = 255;
    uint8_t idDirt = 255;
    uint8_t idStone = 255;
    uint8_t idSand = 255;
    uint8_t idWater = 255;
    
    if (blockRegistry) {
        idAir = blockRegistry->GetBlock("fairworld:air").id;
        idGrass = blockRegistry->GetBlock("fairworld:grass").id;
        idDirt = blockRegistry->GetBlock("fairworld:dirt").id;
        idStone = blockRegistry->GetBlock("fairworld:stone").id;
        idSand = blockRegistry->GetBlock("fairworld:sand").id;
        idWater = blockRegistry->GetBlock("fairworld:water").id;
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

        // Estrarre coordinate chunk dalla posizione
        int cx = chunk.cx;
        int cz = chunk.cz;

        for (int x = 0; x < 16; ++x) {
            // Creiamo istanze statiche del PerlinNoise per i vari fattori
            static PerlinNoise terrainNoiseGen(12345);
            static PerlinNoise tempNoiseGen(54321);
            static PerlinNoise humNoiseGen(98765);

            for (int z = 0; z < 16; ++z) {
                float worldX = cx * 16.0f + x;
                float worldZ = cz * 16.0f + z;

                // Altitudine base (fluttuante in modo continuo e morbido!)
                float terrainVal = terrainNoiseGen.octaveNoise(worldX * biome.baseRegion.perlinFrequency, 0.0, worldZ * biome.baseRegion.perlinFrequency, 4, 0.5);
                float baseHeight = 25.0f + (terrainVal * 25.0f * biome.baseRegion.gravityModifier);
                
                if (biome.baseRegion.type == fw::MapRegionType::Ocean) {
                    baseHeight = 8.0f + (terrainVal * 5.0f); // Oceano profondo
                }
                
                float finalHeight = baseHeight;
                uint8_t surfaceBlock = biome.baseRegion.surfaceBlockId;
                uint8_t subsurfaceBlock = biome.baseRegion.subsurfaceBlockId;
                fw::MapRegionType colBiome = biome.baseRegion.type;
                float minSdf = 9999.0f;
                
                // --- SDF BLENDING IBRIDO ---
                // Calcoliamo la posizione sferica "approssimativa" della colonna (x, z) 
                // basandoci su chunkCenterWorld (che e' sulla superficie della sfera)
                glm::vec3 colNormal = glm::normalize(biome.chunkCenterWorld);
                // NOTA: Per un chunk flat 16x16 l'errore angolare sui bordi e' minuscolo,
                // ma per precisione assoluta si potrebbe sfalsare colNormal con x e z. 
                // Usiamo chunkCenterWorld per semplicita' architetturale e consistenza col LOD.
                
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
                        
                        sdf = angle - r.angularRadius; // SDF sferico
                        blendDistance = 0.08f;
                    } else {
                        // SDF 2D su griglia piana
                        // Convertiamo rectMin e rectMax in coordinate mondo
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
                            sdf = dist - halfW; // assumiamo halfW = halfH per il cerchio
                        } else {
                            // Rectangle SDF
                            glm::vec2 d = glm::abs(glm::vec2(worldX - centerX, worldZ - centerZ)) - glm::vec2(halfW, halfH);
                            sdf = glm::length(glm::max(d, glm::vec2(0.0f))) + std::min(std::max(d.x, d.y), 0.0f);
                        }
                        
                        blendDistance = 12.0f; // 12 blocchi di sfumatura
                    }
                    
                    if (sdf < blendDistance) {
                        float rTerrainVal = terrainNoiseGen.octaveNoise(worldX * r.perlinFrequency, 0.0, worldZ * r.perlinFrequency, 4, 0.5);
                        float rHeight = 25.0f + (rTerrainVal * 25.0f * r.gravityModifier);
                        
                        if (r.type == fw::MapRegionType::Ocean) {
                            rHeight = 8.0f + (rTerrainVal * 5.0f); // Oceano profondo
                        }
                        
                        // Blending tramite smoothstep
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
                
                int height = (int)finalHeight;
                
                // Popolamento blocchi
                for (int y = 0; y < 128; ++y) {
                    if (y < height - 3) {
                        chunk.blocks[x][y][z] = idStone;
                    } else if (y < height) {
                        chunk.blocks[x][y][z] = subsurfaceBlock;
                    } else if (y == height) {
                        chunk.blocks[x][y][z] = surfaceBlock;
                    } else if (y <= 16) { // Livello del mare globale
                        chunk.blocks[x][y][z] = idWater;
                    } else {
                        chunk.blocks[x][y][z] = idAir;
                    }
                    chunk.light[x][y][z] = 255; 
                }
            }
        }

        // Il terreno base e' finito. Passa il testimone al Decorator System.
        chunk.isGenerated = true;
        registry.remove<TerrainGenTag>(entity);
        registry.emplace<DecoratorGenTag>(entity);
    }
}

void BiomeDecoratorSystem::Update(entt::registry& registry, int maxChunksPerFrame, BlockRegistry* blockRegistry) {
    auto view = registry.view<fw::VoxelChunkComponent, BiomeDataComponent, DecoratorGenTag>();
    
    int processed = 0;
    
    uint8_t idAir = 0;
    uint8_t idWater = 255;
    uint8_t idGrass = 255;
    uint8_t idWood = 255;
    uint8_t idLeaves = 255;
    
    if (blockRegistry) {
        idAir = blockRegistry->GetBlock("fairworld:air").id;
        idWater = blockRegistry->GetBlock("fairworld:water").id;
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

        // Alberi solo su erba (Forest) e un po' su Tundra, no Desert
        bool canHaveTrees = false;
        for (int lx = 0; lx < 16; ++lx) {
            for (int lz = 0; lz < 16; ++lz) {
                for (int ly = 127; ly >= 0; --ly) {
                    if (chunk.blocks[lx][ly][lz] == idGrass) canHaveTrees = true;
                }
            }
        }

        if (canHaveTrees) {
            // Rimuove i blocchi in eccesso se si scontra con blocchi vicini (Semplificato)
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
                    
                    // Add some randomness
                    if (surfaceY > 0 && (rand() % 100) < 40) {
                        treePositions.push_back({x, surfaceY, z});
                    }
                }
            }
            
            for (auto p : treePositions) {
                int tx = p.x; int ty = p.y; int tz = p.z;
                // Tronco
                for (int h = 1; h <= 4; ++h) {
                    if (ty + h < 128) {
                        chunk.blocks[tx][ty + h][tz] = idWood;
                    }
                }
                // Chioma
                for (int lx = tx - 2; lx <= tx + 2; ++lx) {
                    for (int lz = tz - 2; lz <= tz + 2; ++lz) {
                        for (int ly = ty + 3; ly <= ty + 5; ++ly) {
                            if (lx >= 0 && lx < 16 && lz >= 0 && lz < 16 && ly < 128) {
                                if (chunk.blocks[lx][ly][lz] == 0) {
                                    chunk.blocks[lx][ly][lz] = idLeaves;
                                }
                            }
                        }
                    }
                }
            }
        }
        // Il chunk e' completamente pronto!
        chunk.isGenerated = true; // Flag per renderlo "ufficiale" per ForgeWorld
        registry.remove<DecoratorGenTag>(entity);
        
        // Ordina alla pipeline di rendering di costruire la mesh finale
        registry.emplace_or_replace<fw::ChunkDirtyComponent>(entity);
    }
}

} // namespace fw
