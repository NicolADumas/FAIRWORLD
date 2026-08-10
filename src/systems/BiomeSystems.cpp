#include "pch.h"
#include "BiomeSystems.h"
#include "BiomeComponents.h"
#include "ForgeComponents.h"
#include "../utils/PerlinNoise.h"
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

                // Calcolo rumore di base (Altitudine)
                // Usiamo un rumore a frequenza media per le colline
                float terrainVal = terrainNoiseGen.octaveNoise(worldX * 0.008f, 0.0, worldZ * 0.008f, 4, 0.5);
                // Usiamo un rumore a frequenza bassissima per le montagne/continenti
                float continentVal = terrainNoiseGen.octaveNoise(worldX * 0.002f, 0.0, worldZ * 0.002f, 3, 0.4);

                // Calcolo Temperatura e Umidità (Mappe a frequenza molto bassa)
                float tempVal = tempNoiseGen.octaveNoise(worldX * 0.003f, 0.0, worldZ * 0.003f, 3, 0.5);
                float humVal = humNoiseGen.octaveNoise(worldX * 0.004f, 0.0, worldZ * 0.004f, 3, 0.5);

                // Altitudine base (fluttuante in modo continuo e morbido!)
                float baseHeight = 25.0f + (continentVal * 40.0f) + (terrainVal * 15.0f);
                int height = (int)baseHeight;

                // Determinazione del Bioma (Whittaker diagram semplificato)
                fw::MapRegionType colBiome = biome.type;
                uint8_t surfaceBlock = biome.surfaceBlockId;
                uint8_t subsurfaceBlock = biome.subsurfaceBlockId;

                if (!biome.isCustomMapped) {
                    if (baseHeight <= 16.0f) {
                        // Sotto o al livello del mare: Oceano / Spiaggia
                        colBiome = fw::MapRegionType::Ocean;
                        surfaceBlock = idSand;
                        subsurfaceBlock = idSand;
                    } else {
                        if (tempVal > 0.6f) {
                            if (humVal < 0.4f) {
                                colBiome = fw::MapRegionType::Desert;
                                surfaceBlock = idSand;
                                subsurfaceBlock = idSand;
                            } else {
                                colBiome = fw::MapRegionType::Forest;
                                surfaceBlock = idGrass;
                                subsurfaceBlock = idDirt;
                            }
                        } else if (tempVal < 0.35f) {
                            colBiome = fw::MapRegionType::Tundra;
                            surfaceBlock = idSand; // Sand/Snow
                            subsurfaceBlock = idStone;
                        } else {
                            colBiome = fw::MapRegionType::Forest;
                            surfaceBlock = idGrass;
                            subsurfaceBlock = idDirt;
                        }
                    }
                } else {
                    // Se la regione e' specificamente dipinta come Oceano, forza l'altezza ad essere sotto il livello del mare
                    if (colBiome == fw::MapRegionType::Ocean) {
                        baseHeight = 10.0f + (terrainVal * 5.0f); // Abbassa il fondale oceanico
                        height = (int)baseHeight;
                    }
                }
                
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

        // Decorazioni extra
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                int surfaceY = 0;
                for (int y = 127; y >= 0; --y) {
                    if (chunk.blocks[x][y][z] != idAir && chunk.blocks[x][y][z] != idWater) {
                        surfaceY = y;
                        break;
                    }
                }

                // Se la superficie e' erba, possiamo spawnare un albero (Bioma Foresta)
                if (surfaceY > 16 && surfaceY < 120 && chunk.blocks[x][surfaceY][z] == idGrass) {
                    // Costruisci albero
                    chunk.blocks[x][surfaceY+1][z] = idWood;
                    chunk.blocks[x][surfaceY+2][z] = idWood;
                    chunk.blocks[x][surfaceY+3][z] = idWood;
                    
                    for (int lx = -1; lx <= 1; ++lx) {
                        for (int lz = -1; lz <= 1; ++lz) {
                            chunk.blocks[x+lx][surfaceY+3][z+lz] = idLeaves;
                            chunk.blocks[x+lx][surfaceY+4][z+lz] = idLeaves;
                        }
                    }
                    chunk.blocks[x][surfaceY+5][z] = idLeaves; // Punta foglie
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
