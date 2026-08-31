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
        
        // Trova la coordinata chunk 2D partendo da una posizione 3D
        static void GetChunkCoordFromPosition(float planetRadius, const glm::vec3& worldPos, int& out_cx, int& out_cz);
        
        // Mappa esattamente una posizione globale nel mondo sferico alle coordinate flat del mesher
        static void WorldToVoxelCoord(float planetRadius, const glm::vec3& worldPos, float& out_flatX, float& out_localY, float& out_flatZ);
        
        static bool GetTrueSphericalPosition(float planetRadius, int global_cx, int global_cz, float local_x, float local_y, float local_z, glm::vec3& outWorldPos);
        
        // Cerca il bioma più adatto date le variabili ambientali attuali
        static const ::BiomeDef* EvaluateBiome(float temp, float humidity, float height, AssetManager* assets);
    
        // Converte le coordinate cartesiane in un valore di altezza sferica locale
        static float SampleSphericalNoise(const glm::vec3& normal, const MapRegion& regionInfo, float frequency = 1.0f);
    };
}
