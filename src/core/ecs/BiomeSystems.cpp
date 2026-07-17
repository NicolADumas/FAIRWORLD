#include "pch.h"
#include "BiomeSystems.h"
#include "BiomeComponents.h"
#include "ForgeComponents.h"
#include "../utils/PerlinNoise.h"
#include <cmath>

namespace fw {

void BiomeTerrainSystem::Update(entt::registry& registry, int maxChunksPerFrame) {
    auto view = registry.view<fw::VoxelChunkComponent, BiomeDataComponent, TerrainGenTag>();
    
    int processed = 0;
    for (auto entity : view) {
        if (processed >= maxChunksPerFrame) break;

        auto& chunk = view.get<fw::VoxelChunkComponent>(entity);
        const auto& biome = view.get<BiomeDataComponent>(entity);

        // Estrarre coordinate chunk dalla posizione
        int cx = chunk.cx;
        int cz = chunk.cz;

        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                float worldX = cx * 16.0f + x;
                float worldZ = cz * 16.0f + z;

                // 1. Calcolo altezza basato sul bioma
                float baseHeight = 30.0f;
                float noiseVal = 0.0f;
                
                // Creiamo un'istanza statica del PerlinNoise (seed fisso per coerenza)
                static PerlinNoise noiseGen(12345);
                
                if (biome.type == fw::MapRegionType::Ocean) {
                    baseHeight = 10.0f;
                    noiseVal = noiseGen.octaveNoise(worldX * 0.02f, 0.0, worldZ * 0.02f, 2, 0.5) * 5.0f;
                } else if (biome.type == fw::MapRegionType::Desert) {
                    baseHeight = 22.0f;
                    noiseVal = noiseGen.octaveNoise(worldX * 0.015f, 0.0, worldZ * 0.015f, 3, 0.3) * 15.0f;
                } else if (biome.type == fw::MapRegionType::Volcano) {
                    baseHeight = 50.0f;
                    noiseVal = noiseGen.octaveNoise(worldX * 0.04f, 0.0, worldZ * 0.04f, 6, 0.6) * 40.0f;
                } else if (biome.type == fw::MapRegionType::Tundra) {
                    baseHeight = 35.0f;
                    noiseVal = noiseGen.octaveNoise(worldX * 0.03f, 0.0, worldZ * 0.03f, 4, 0.5) * 20.0f;
                } else { // Forest & Default
                    baseHeight = 30.0f;
                    noiseVal = noiseGen.octaveNoise(worldX * 0.025f, 0.0, worldZ * 0.025f, 4, 0.45) * 25.0f;
                }
                
                int height = (int)(baseHeight + noiseVal);
                
                // Popolamento blocchi
                for (int y = 0; y < 128; ++y) {
                    if (y < height - 3) {
                        chunk.blocks[x][y][z] = 2; // Stone (Core)
                    } else if (y < height) {
                        chunk.blocks[x][y][z] = biome.subsurfaceBlockId;
                    } else if (y == height) {
                        chunk.blocks[x][y][z] = biome.surfaceBlockId;
                    } else if (y <= 16 && biome.type == fw::MapRegionType::Ocean) {
                        chunk.blocks[x][y][z] = 4; // Water
                    } else {
                        chunk.blocks[x][y][z] = 0; // Air
                    }
                    chunk.light[x][y][z] = 255; 
                }
                // Salvataggio altezza massima superficiale in un array se servisse ai decoratori
                // Ma per ora ricalcoleremo o cercheremo dall'alto in basso.
            }
        }

        // Il terreno base e' finito. Passa il testimone al Decorator System.
        chunk.isGenerated = true;
        registry.remove<TerrainGenTag>(entity);
        registry.emplace<DecoratorGenTag>(entity);
        
        processed++;
    }
}

void BiomeDecoratorSystem::Update(entt::registry& registry, int maxChunksPerFrame) {
    auto view = registry.view<fw::VoxelChunkComponent, BiomeDataComponent, DecoratorGenTag>();
    
    int processed = 0;
    for (auto entity : view) {
        if (processed >= maxChunksPerFrame) break;

        auto& chunk = view.get<fw::VoxelChunkComponent>(entity);
        const auto& biome = view.get<BiomeDataComponent>(entity);

        int cx = chunk.cx;
        int cz = chunk.cz;

        // Decorazioni (Alberi per la foresta)
        if (biome.type == fw::MapRegionType::Forest) {
            for (int x = 2; x < 13; ++x) {
                for (int z = 2; z < 13; ++z) {
                    float worldX = cx * 16.0f + x;
                    float worldZ = cz * 16.0f + z;

                    int randVal = (int)(std::abs(std::sin(worldX * 12.3f + worldZ * 45.6f)) * 1000.0f);
                    if (randVal % 100 < 2) { 
                        // Trova la Y della superficie (scendendo da 127)
                        int surfaceY = 0;
                        for (int y = 127; y >= 0; --y) {
                            if (chunk.blocks[x][y][z] != 0 && chunk.blocks[x][y][z] != 4) { // Ignora aria e acqua
                                surfaceY = y;
                                break;
                            }
                        }

                        if (surfaceY > 16 && surfaceY < 120) {
                            // Costruisci albero
                            chunk.blocks[x][surfaceY+1][z] = 12; // Legno
                            chunk.blocks[x][surfaceY+2][z] = 12;
                            chunk.blocks[x][surfaceY+3][z] = 12;
                            
                            for (int lx = -1; lx <= 1; ++lx) {
                                for (int lz = -1; lz <= 1; ++lz) {
                                    chunk.blocks[x+lx][surfaceY+3][z+lz] = 13; // Foglie
                                    chunk.blocks[x+lx][surfaceY+4][z+lz] = 13;
                                }
                            }
                            chunk.blocks[x][surfaceY+5][z] = 13; // Punta
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

        processed++;
    }
}

} // namespace fw
