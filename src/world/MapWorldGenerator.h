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
        
        // Calcola la trasformazione sferica (posizione e rotazione) per un chunk dato il raggio del pianeta e le coordinate globali.
        // Restituisce false se il chunk si trova al di fuori dell'area di superficie sferica (pianeta finito).
        static bool GetSphericalChunkTransform(float planetRadius, int global_cx, int global_cz, glm::vec3& outPos, glm::quat& outRot);
        
        // Trova la coordinata chunk 2D (global_cx, global_cz) partendo da una posizione sferica 3D (utile per il giocatore)
        static void GetChunkCoordFromPosition(float planetRadius, const glm::vec3& worldPos, int& out_cx, int& out_cz);
        
        // Cerca il bioma più adatto date le variabili ambientali attuali
        static const ::BiomeDef* EvaluateBiome(float temp, float humidity, float height, AssetManager* assets);
    
        // Converte le coordinate cartesiane in un valore di altezza sferica locale
        static float SampleSphericalNoise(const glm::vec3& normal, const MapRegion& regionInfo, float frequency = 1.0f);
    };
}
