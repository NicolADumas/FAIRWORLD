#include "pch.h"
#include "MapWorldGenerator.h"
#include "ForgeWorld.h"
#include "../app/AssetManager.h"
#include <iostream>
#include "BiomeComponents.h"

// Mock per PerlinNoise se non esiste ancora un header isolato
namespace fw {
    class PerlinNoise {
    public:
        PerlinNoise(uint32_t seed) {}
        float FractalNoise3D(float x, float y, float z, int octaves) {
            return 0.0f; // Mock temporaneo
        }
    };
}

namespace fw {

void MapWorldGenerator::Generate(const MapDocument& doc, int planetIndex, ForgeWorld& targetWorld, fw::JobSystem* jobs) {
    if (planetIndex < 0 || planetIndex >= (int)doc.planets.size()) return;
    
    const PlanetMap& planet = doc.planets[planetIndex];

    // TODO: Usare jobs->Execute() quando JobSystem e' esposto
    // Per ora facciamo generazione sincrona per collaudo
    std::cout << "[MapWorldGenerator] Inizio generazione voxel per " << planet.name << "...\n";
    
    // Crea il DimensionsManager al volo in base ai dati della mappa
    fw::DimensionsManager dimManager;
    dimManager.SetBounds(planet.minX, planet.maxX, planet.minZ, planet.maxZ);
    for (const auto& co : planet.chunkOverrides) {
        dimManager.SetChunkMetadata(co.coord.x, co.coord.z, co.meta);
    }
    
    for (int cz = dimManager.GetMinZ(); cz <= dimManager.GetMaxZ(); ++cz) {
        for (int cx = dimManager.GetMinX(); cx <= dimManager.GetMaxX(); ++cx) {
            
            const ChunkMetadata* meta = dimManager.GetChunkMetadata(cx, cz);
            ChunkMetadata defaultMeta;
            const ChunkMetadata& activeMeta = meta ? *meta : defaultMeta;

            // Salta i chunk etichettati come OuterBoundary se non vogliamo che contengano MicroVoxel 
            // (potremmo instanziare mesh low-poly per l'orizzonte, ma qui stiamo generando VoxelComponent)
            if (activeMeta.type == ChunkType::OuterBoundary) {
                continue;
            }

            std::string chunkName = "WorldChunk_" + std::to_string(cx) + "_" + std::to_string(cz);
            entt::entity chunkEnt = targetWorld.CreateChunkEntity(chunkName, {cx * 16.0f, 0.0f, cz * 16.0f});
            auto& chunk = targetWorld.GetRegistry().get<fw::VoxelChunkComponent>(chunkEnt);
            // 1. Trova la regione dominante per questo chunk
            uint8_t surfBlock = 1; // Grass fallback
            uint8_t subBlock = 3;  // Dirt fallback
            fw::MapRegionType regionType = fw::MapRegionType::Forest;
            
            for (int i = (int)planet.regions.size() - 1; i >= 0; --i) {
                const auto& r = planet.regions[i];
                if (cx >= r.rectMin.x && cx <= r.rectMax.x && 
                    cz >= r.rectMin.y && cz <= r.rectMax.y) {
                    surfBlock = r.surfaceBlockId;
                    subBlock = r.subsurfaceBlockId;
                    regionType = r.type;
                    break; // Prende l'ultima regione inserita (top z-index)
                }
            }

            // Inizializza i componenti ECS per la pipeline asincrona
            fw::BiomeDataComponent biomeData;
            biomeData.type = regionType;
            biomeData.surfaceBlockId = surfBlock;
            biomeData.subsurfaceBlockId = subBlock;
            
            targetWorld.GetRegistry().emplace<fw::BiomeDataComponent>(chunkEnt, biomeData);
            targetWorld.GetRegistry().emplace<fw::TerrainGenTag>(chunkEnt);
            
            // NON markiamo come dirty ne' generato qui. Ci penseranno i sistemi ECS.
        }
    }
    
    std::cout << "[MapWorldGenerator] Generazione completata con successo!\n";
}

float MapWorldGenerator::SampleSphericalNoise(const glm::vec3& normal, const MapRegion& regionInfo, float frequency) {
    PerlinNoise noiseGen(regionInfo.seed);
    
    // Usa parametri esistenti in MapRegion o default se mancanti
    float noiseScale = frequency * 100.0f; 
    int octaves = 4;
    float heightMultiplier = 5.0f;
    
    float noiseVal = noiseGen.FractalNoise3D(
        normal.x * noiseScale, 
        normal.y * noiseScale, 
        normal.z * noiseScale, 
        octaves
    );
    
    return noiseVal * heightMultiplier;
}

const ::BiomeDef* MapWorldGenerator::EvaluateBiome(float temp, float humidity, float height, AssetManager* assets) {
    if (!assets) return nullptr;
    
    const auto& biomes = assets->GetBiomes();
    if (biomes.empty()) return nullptr;
    
    // Ritorna il primo bioma che soddisfa i criteri ambientali
    for (const auto& b : biomes) {
        if (temp >= b.minTemperature && temp <= b.maxTemperature &&
            humidity >= b.minHumidity && humidity <= b.maxHumidity &&
            height >= b.minHeight && height <= b.maxHeight) {
            return &b;
        }
    }
    
    // Fallback al primo bioma se nessuno match
    return &biomes[0];
}

} // namespace fw
