#pragma once
#include "MapDocument.h"
class AssetManager;
struct BiomeDef;

namespace fw {
    class GameWorld;
    class ForgeWorld;
    class JobSystem;

    class MapWorldGenerator {
    public:
        // Prende il MapDocument e popola un'istanza GameWorld con i chunk
        // basati su campionamento spaziale 3D e rumore di Perlin.
        static void Generate(const MapDocument& doc, int planetIndex, GameWorld& targetWorld, fw::JobSystem* jobs);
        
        // Cerca il bioma più adatto date le variabili ambientali attuali
        static const ::BiomeDef* EvaluateBiome(float temp, float humidity, float height, AssetManager* assets);
    
        // Converte le coordinate cartesiane in un valore di altezza sferica locale
        static float SampleSphericalNoise(const glm::vec3& normal, const MapRegion& regionInfo, float frequency = 1.0f);
    };
}
