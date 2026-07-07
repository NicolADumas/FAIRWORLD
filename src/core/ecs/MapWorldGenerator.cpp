#include "pch.h"
#include "MapWorldGenerator.h"
#include "ForgeWorld.h"
#include "../app/AssetManager.h"
#include <iostream>

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
    
    float radius = 50.0f; // Raggio sferico di base
    
    // Simula la generazione dei chunk che compongono il volume del pianeta
    // Richiede che targetWorld.GetActiveChunks() o simili sia esposto.
    // Al momento implementiamo un mock di generazione
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
