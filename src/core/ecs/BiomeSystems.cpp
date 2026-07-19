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
                        surfaceBlock = 8; // Sand (assumiamo 8 = Sand, se non c'è userà fallback)
                        subsurfaceBlock = 3; // Stone o Sand
                    } else {
                        if (tempVal > 0.6f) {
                            if (humVal < 0.4f) {
                                colBiome = fw::MapRegionType::Desert;
                                surfaceBlock = 8; // Sand
                                subsurfaceBlock = 8;
                            } else {
                                colBiome = fw::MapRegionType::Forest; // Jungle (ma usiamo Forest per ora)
                                surfaceBlock = 1;
                                subsurfaceBlock = 2;
                            }
                        } else if (tempVal < 0.35f) {
                            colBiome = fw::MapRegionType::Tundra;
                            surfaceBlock = 9; // Snow (assumiamo 9 = Snow)
                            subsurfaceBlock = 3; // Stone o frozen dirt
                        } else {
                            colBiome = fw::MapRegionType::Forest;
                            surfaceBlock = 1;
                            subsurfaceBlock = 2;
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
                        chunk.blocks[x][y][z] = 2; // Stone (Core)
                    } else if (y < height) {
                        chunk.blocks[x][y][z] = subsurfaceBlock;
                    } else if (y == height) {
                        chunk.blocks[x][y][z] = surfaceBlock;
                    } else if (y <= 16) { // Livello del mare globale
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

                    // Se la superficie e' erba (1), possiamo spawnare un albero (Bioma Foresta)
                    if (surfaceY > 16 && surfaceY < 120 && chunk.blocks[x][surfaceY][z] == 1) {
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

        // Il chunk e' completamente pronto!
        chunk.isGenerated = true; // Flag per renderlo "ufficiale" per ForgeWorld
        registry.remove<DecoratorGenTag>(entity);
        
        // Ordina alla pipeline di rendering di costruire la mesh finale
        registry.emplace_or_replace<fw::ChunkDirtyComponent>(entity);

        processed++;
    }
}

} // namespace fw
