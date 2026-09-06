#include "pch.h"
#include "MapWorldGenerator.h"
#include "ForgeWorld.h"
#include "AssetManager.h"
#include "BlockRegistry.h"
#include <iostream>
#include "BiomeComponents.h"
#include <unordered_map>

#include "PerlinNoise.h"

#include "GameWorld.h"
#include <glm/gtx/quaternion.hpp>

namespace fw {

void MapWorldGenerator::Generate(const MapDocument& doc, int planetIndex, GameWorld& targetWorld, fw::JobSystem* jobs, float limitRadius, glm::vec3 focusPos) {
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
    
    std::vector<fw::MapRegion> combinedRegions = planet.regions;
    for (const auto& inst : planet.chunkInstances) {
        for (const auto& tmpl : doc.terrainLibrary) {
            if (tmpl.id == inst.templateId) {
                int baseX = inst.gridX;
                int baseZ = inst.gridY;
                for (const auto& sub : tmpl.subRegions) {
                    fw::MapRegion projected = sub;
                    projected.faceIndex = inst.faceIndex;
                    projected.isGridAligned = inst.isGridAligned;
                    projected.rectMin += glm::ivec2(baseX, baseZ);
                    projected.rectMax += glm::ivec2(baseX, baseZ);
                    combinedRegions.push_back(projected);
                }
                break;
            }
        }
    }

    auto generateChunk = [&](int global_cx, int global_cz, int local_cx, int local_cz, int face, const glm::vec3& pos, const glm::quat& rot) {
        const ChunkMetadata* meta = dimManager.GetChunkMetadata(global_cx, global_cz);
        ChunkMetadata defaultMeta;
        const ChunkMetadata& activeMeta = meta ? *meta : defaultMeta;

        if (activeMeta.type == ChunkType::OuterBoundary) return;

        entt::entity chunkEnt = targetWorld.GetChunkManager().GetChunkEntity(global_cx, global_cz);
        if (chunkEnt == entt::null || !targetWorld.GetRegistry().valid(chunkEnt)) {
            std::string chunkName = "WorldChunk_" + std::to_string(global_cx) + "_" + std::to_string(global_cz);
            chunkEnt = targetWorld.CreateChunkEntity(chunkName, {pos.x, pos.y, pos.z});
        }
        
        // APPLICA LA ROTAZIONE SFERICA E LA GERARCHIA
        if (targetWorld.GetRegistry().all_of<fw::TransformComponent>(chunkEnt)) {
            auto& trans = targetWorld.GetRegistry().get<fw::TransformComponent>(chunkEnt);
            trans.rotation = {rot.x, rot.y, rot.z, rot.w};
            trans.parent = targetWorld.GetPlanetEntity();
        }

        auto& chunk = targetWorld.GetRegistry().get<fw::VoxelChunkComponent>(chunkEnt);
        
        fw::BiomeDataComponent biomeData;
        biomeData.planetRadius = planet.planetRadius;
        biomeData.chunkCenterWorld = pos;
        
        for (auto it = combinedRegions.begin(); it != combinedRegions.end(); ++it) {
            if (it->isGridAligned) {
                if (it->faceIndex == face && it->gridX == local_cx && it->gridY == local_cz) {
                    biomeData.hasBaseRegion = true;
                    biomeData.baseRegion = *it;
                    biomeData.isCustomMapped = true;
                }
            } else if (it->angularRadius > 0.0f) {
                // Free-floating Spherical region
                float R = planet.planetRadius > 0.0f ? planet.planetRadius : 50.0f; // Fallback radius se forzato piatto
                float rRadius = it->angularRadius * R;
                float pitch = glm::radians(it->eulerAngles.x);
                float yaw = glm::radians(it->eulerAngles.y);
                glm::vec3 rCenter = glm::vec3(cos(pitch) * cos(yaw), sin(pitch), cos(pitch) * sin(yaw)) * R;
                
                // Mappa la posizione (piatta o sferica) sulla sfera logica per calcolare la distanza
                glm::vec3 checkPos = pos;
                if (planet.planetRadius <= 0.0f) {
                    float pyaw = pos.x / R;
                    float ppitch = pos.z / R;
                    checkPos = glm::vec3(cos(ppitch) * cos(pyaw), sin(ppitch), cos(ppitch) * sin(pyaw)) * R;
                }
                
                float chunkRadius = 32.0f; // Safe margin
                if (glm::distance(checkPos, rCenter) - chunkRadius <= rRadius) {
                    biomeData.overlappingRegions.push_back(*it);
                    biomeData.isCustomMapped = true;
                }
            } else {
                // Flat 2D region (rectMin, rectMax)
                float test_cx = (it->faceIndex != -1) ? (float)local_cx : (float)global_cx;
                float test_cz = (it->faceIndex != -1) ? (float)local_cz : (float)global_cz;
                
                // Add margin for SDF blending overlap
                float margin = 2.0f; 
                if (test_cx >= it->rectMin.x - margin && test_cx <= it->rectMax.x + margin &&
                    test_cz >= it->rectMin.y - margin && test_cz <= it->rectMax.y + margin) {
                    biomeData.overlappingRegions.push_back(*it);
                    biomeData.isCustomMapped = true;
                }
            }
        }

        if (!biomeData.hasBaseRegion) {
            biomeData.baseRegion.type = fw::MapRegionType::Forest; 
            uint8_t idGrass = 1;
            uint8_t idDirt = 3;
            float grav = 1.0f;
            float perlin = 0.03f;
            if (!doc.terrainLibrary.empty()) {
                const auto& defaultTmpl = doc.terrainLibrary[0];
                idGrass = defaultTmpl.baseSurfaceBlockId;
                idDirt = defaultTmpl.baseSubsurfaceBlockId;
                grav = defaultTmpl.baseGravityModifier;
                perlin = defaultTmpl.basePerlinFrequency;
            }
            biomeData.baseRegion.surfaceBlockId = idGrass;
            biomeData.baseRegion.subsurfaceBlockId = idDirt;
            biomeData.baseRegion.gravityModifier = grav;
            biomeData.baseRegion.perlinFrequency = perlin;
            biomeData.surfaceBlockId = idGrass;
            biomeData.subsurfaceBlockId = idDirt;
        } else {
            biomeData.surfaceBlockId = biomeData.baseRegion.surfaceBlockId;
            biomeData.subsurfaceBlockId = biomeData.baseRegion.subsurfaceBlockId;
        }
        
        targetWorld.GetRegistry().emplace_or_replace<fw::BiomeDataComponent>(chunkEnt, biomeData);
        targetWorld.GetRegistry().emplace_or_replace<fw::TerrainGenTag>(chunkEnt);
        
        switch (biomeData.baseRegion.type) {
            case fw::MapRegionType::Forest:  targetWorld.GetRegistry().emplace_or_replace<fw::ForestBiomeTag>(chunkEnt); break;
            case fw::MapRegionType::Desert:  targetWorld.GetRegistry().emplace_or_replace<fw::DesertBiomeTag>(chunkEnt); break;
            case fw::MapRegionType::Tundra:  targetWorld.GetRegistry().emplace_or_replace<fw::TundraBiomeTag>(chunkEnt); break;
            case fw::MapRegionType::Ocean:   targetWorld.GetRegistry().emplace_or_replace<fw::OceanBiomeTag>(chunkEnt); break;
            case fw::MapRegionType::Volcano: targetWorld.GetRegistry().emplace_or_replace<fw::VolcanoBiomeTag>(chunkEnt); break;
            case fw::MapRegionType::City:    targetWorld.GetRegistry().emplace_or_replace<fw::CityBiomeTag>(chunkEnt); break;
            case fw::MapRegionType::Dungeon: targetWorld.GetRegistry().emplace_or_replace<fw::DungeonBiomeTag>(chunkEnt); break;
            case fw::MapRegionType::Portal:  targetWorld.GetRegistry().emplace_or_replace<fw::PortalBiomeTag>(chunkEnt); break;
            case fw::MapRegionType::Flat:    targetWorld.GetRegistry().emplace_or_replace<fw::FlatBiomeTag>(chunkEnt); break;
        }
    };

    if (planet.planetRadius > 0.0f) {
        // Rapporto di incremento (Fasizzazione modulare) per curvare i chunk senza lasciare buchi.
        // Più il pianeta è piccolo, più l'angolo è acuto e i chunk 16x16 piatti si divaricano.
        // Utilizziamo una funzione continua modulata in base alla grandezza del pianeta:
        // - Pianeti piccoli (R=50) -> overlap più aggressivo (~0.85) per coprire i buchi
        // - Pianeti giganti (R=1000+) -> overlap vicino a 1.0 (0.98) quasi piatti
        float overlapFactor = 0.80f + (planet.planetRadius / 2500.0f);
        if (overlapFactor > 0.98f) overlapFactor = 0.98f;
        if (overlapFactor < 0.80f) overlapFactor = 0.80f;

        float S = 16.0f * overlapFactor; 
        
        float R = planet.planetRadius;
        int N = (int)std::ceil((glm::pi<float>() * R) / (2.0f * S));
        if (N < 1) N = 1;

        int stride = N * 2 + 1;
        for (int face = 0; face < 6; ++face) {
            for (int cy = -N; cy <= N; ++cy) {
                for (int cx = -N; cx <= N; ++cx) {
                    int face_col = face % 3;
                    int face_row = face / 3;
                    int global_cx = face_col * stride + (cx + N);
                    int global_cz = face_row * stride + (cy + N);
                    
                    glm::vec3 spherePos;
                    glm::quat q;
                    if (GetSphericalChunkTransform(planet.planetRadius, global_cx, global_cz, spherePos, q)) {
                        if (limitRadius > 0.0f) {
                            float dist = glm::distance(focusPos, spherePos);
                            if (dist > limitRadius) continue;
                        }
                        generateChunk(global_cx, global_cz, cx, cy, face, spherePos, q);
                    }
                }
            }
        }
    } else {
        for (int cz = dimManager.GetMinZ(); cz <= dimManager.GetMaxZ(); ++cz) {
            for (int cx = dimManager.GetMinX(); cx <= dimManager.GetMaxX(); ++cx) {
                generateChunk(cx, cz, cx, cz, -1, glm::vec3(cx * 16.0f, 0.0f, cz * 16.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            }
        }
    }
    
    std::cout << "[MapWorldGenerator] Generazione chunk completata.\n";
}

bool MapWorldGenerator::GetSphericalChunkTransform(float planetRadius, int global_cx, int global_cz, glm::vec3& outPos, glm::quat& outRot) {
    if (planetRadius <= 0.0f) {
        outPos = glm::vec3(global_cx * 16.0f, 0.0f, global_cz * 16.0f);
        outRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        return true;
    }

    float overlapFactor = 0.80f + (planetRadius / 2500.0f);
    if (overlapFactor > 0.98f) overlapFactor = 0.98f;
    if (overlapFactor < 0.80f) overlapFactor = 0.80f;

    float S = 16.0f * overlapFactor; 
    int N = (int)std::ceil((glm::pi<float>() * planetRadius) / (2.0f * S));
    if (N < 1) N = 1;

    int stride = N * 2 + 1;
    
    // Reverse mapping using 3x2 grid
    int face_col = global_cx / stride;
    int face_row = global_cz / stride;

    if (face_col >= 0 && face_col < 3 && face_row >= 0 && face_row < 2) {
        int face = face_col + face_row * 3;
        int cx = (global_cx % stride) - N;
        int cy = (global_cz % stride) - N;

        glm::vec3 localPos(0.0f);
        if (face == 0) localPos = glm::vec3(cx * S, cy * S, planetRadius);         // +Z
        else if (face == 1) localPos = glm::vec3(-cx * S, cy * S, -planetRadius);  // -Z
        else if (face == 2) localPos = glm::vec3(planetRadius, cy * S, -cx * S);   // +X
        else if (face == 3) localPos = glm::vec3(-planetRadius, cy * S, cx * S);   // -X
        else if (face == 4) localPos = glm::vec3(cx * S, planetRadius, -cy * S);   // +Y
        else if (face == 5) localPos = glm::vec3(cx * S, -planetRadius, cy * S);   // -Y
        
        glm::vec3 normal = glm::normalize(localPos);
        outPos = normal * planetRadius;

        glm::vec3 forwardBase;
        if (face == 0 || face == 1 || face == 2 || face == 3) {
            forwardBase = glm::vec3(0, 1, 0);
        } else if (face == 4) {
            forwardBase = glm::vec3(0, 0, -1);
        } else if (face == 5) {
            forwardBase = glm::vec3(0, 0, 1);
        }

        glm::vec3 forward = glm::normalize(forwardBase - normal * glm::dot(forwardBase, normal));
        glm::vec3 right = glm::normalize(glm::cross(forward, normal));

        glm::mat3 rotMat(right, normal, forward);
        outRot = glm::quat_cast(rotMat);
        return true;
    }
    return false; // Fuori dalla sfera
}

bool MapWorldGenerator::GetTrueSphericalPosition(float planetRadius, int global_cx, int global_cz, float local_x, float local_y, float local_z, glm::vec3& outWorldPos) {
    if (planetRadius <= 0.0f) {
        outWorldPos = glm::vec3(global_cx * 16.0f + local_x - 8.0f, local_y, global_cz * 16.0f + local_z - 8.0f);
        return true;
    }

    float overlapFactor = 0.80f + (planetRadius / 2500.0f);
    if (overlapFactor > 0.98f) overlapFactor = 0.98f;
    if (overlapFactor < 0.80f) overlapFactor = 0.80f;

    float S = 16.0f * overlapFactor; 
    int N = (int)std::ceil((glm::pi<float>() * planetRadius) / (2.0f * S));
    if (N < 1) N = 1;

    int stride = N * 2 + 1;
    
    int face_col = global_cx / stride;
    int face_row = global_cz / stride;

    if (face_col >= 0 && face_col < 3 && face_row >= 0 && face_row < 2) {
        int face = face_col + face_row * 3;
        int cx = (global_cx % stride) - N;
        int cy = (global_cz % stride) - N;

        // Offset dal centro del chunk. Nota: il centro del chunk per il greedy mesher è a (8, 0, 8),
        // ma noi assumiamo che il mesher generi i vertici nell'intervallo [0, 16].
        // Il GetSphericalChunkTransform usa il centro per il posizionamento.
        // Convertiamo local_x e local_z in un offset proporzionale alla spaziatura S.
        float dx = (local_x - 8.0f) * (S / 16.0f);
        float dz = (local_z - 8.0f) * (S / 16.0f);
        
        float faceX = cx * S + dx;
        float faceY = cy * S + dz;

        glm::vec3 localPos(0.0f);
        if (face == 0) localPos = glm::vec3(faceX, faceY, planetRadius);         // +Z
        else if (face == 1) localPos = glm::vec3(-faceX, faceY, -planetRadius);  // -Z
        else if (face == 2) localPos = glm::vec3(planetRadius, faceY, -faceX);   // +X
        else if (face == 3) localPos = glm::vec3(-planetRadius, faceY, faceX);   // -X
        else if (face == 4) localPos = glm::vec3(faceX, planetRadius, -faceY);   // +Y
        else if (face == 5) localPos = glm::vec3(faceX, -planetRadius, faceY);   // -Y
        
        glm::vec3 normal = glm::normalize(localPos);
        
        // Mappiamo Y in elevazione: Y=25 è il raggio del pianeta, ogni unità in Y aggiunge 1 metro.
        // Possiamo adattare questo in base a come vogliamo centrare la sfera.
        // In PlayState il default spawn Y è planetRadius + 150 (se non c'è punto di spawn).
        // SphericalLOD usa "planetRadius + (rTerrainVal * ...)" per l'altezza, dove planetRadius è circa il livello dell'acqua.
        float radiusAtY = planetRadius + (local_y - 25.0f);
        
        outWorldPos = normal * radiusAtY;
        return true;
    }
    return false; // Fuori dalla sfera
}

void MapWorldGenerator::WorldToVoxelCoord(float planetRadius, const glm::vec3& worldPos, float& out_flatX, float& out_localY, float& out_flatZ) {
    if (planetRadius <= 0.0f) {
        out_flatX = worldPos.x + 8.0f;
        out_localY = worldPos.y;
        out_flatZ = worldPos.z + 8.0f;
        return;
    }

    float distance = glm::length(worldPos);
    out_localY = (distance - planetRadius) + 25.0f;

    if (distance < 0.001f) {
        out_flatX = 0; out_flatZ = 0; return;
    }
    
    glm::vec3 normal = worldPos / distance;
    glm::vec3 absNormal = glm::abs(normal);
    int face = 0;
    if (absNormal.z >= absNormal.x && absNormal.z >= absNormal.y) face = normal.z > 0 ? 0 : 1;
    else if (absNormal.x >= absNormal.y && absNormal.x >= absNormal.z) face = normal.x > 0 ? 2 : 3;
    else face = normal.y > 0 ? 4 : 5;

    float overlapFactor = 0.80f + (planetRadius / 2500.0f);
    if (overlapFactor > 0.98f) overlapFactor = 0.98f;
    if (overlapFactor < 0.80f) overlapFactor = 0.80f;

    float S = 16.0f * overlapFactor;
    int N = (int)std::ceil((glm::pi<float>() * planetRadius) / (2.0f * S));
    if (N < 1) N = 1;
    
    float factor = planetRadius / S;
    float local_cx = 0, local_cy = 0;

    if (face == 0) { local_cx = (normal.x / normal.z) * factor; local_cy = (normal.y / normal.z) * factor; }
    else if (face == 1) { local_cx = (normal.x / -normal.z) * -factor; local_cy = (normal.y / -normal.z) * factor; }
    else if (face == 2) { local_cx = (normal.z / normal.x) * -factor; local_cy = (normal.y / normal.x) * factor; }
    else if (face == 3) { local_cx = (normal.z / -normal.x) * factor; local_cy = (normal.y / -normal.x) * factor; }
    else if (face == 4) { local_cx = (normal.x / normal.y) * factor; local_cy = (normal.z / normal.y) * -factor; }
    else if (face == 5) { local_cx = (normal.x / -normal.y) * factor; local_cy = (normal.z / -normal.y) * factor; }

    int stride = N * 2 + 1;
    int face_col = face % 3;
    int face_row = face / 3;

    float global_cx_continuous = local_cx + face_col * stride;
    float global_cy_continuous = local_cy + face_row * stride;

    out_flatX = global_cx_continuous * 16.0f + 8.0f;
    out_flatZ = global_cy_continuous * 16.0f + 8.0f;
}

void MapWorldGenerator::GetChunkCoordFromPosition(float planetRadius, const glm::vec3& worldPos, int& out_cx, int& out_cz) {
    if (planetRadius <= 0.0f) {
        out_cx = (int)std::floor(worldPos.x / 16.0f);
        out_cz = (int)std::floor(worldPos.z / 16.0f);
        return;
    }

    glm::vec3 normal = glm::normalize(worldPos);
    glm::vec3 absNormal = glm::abs(normal);
    int face = 0;
    if (absNormal.z >= absNormal.x && absNormal.z >= absNormal.y) {
        face = normal.z > 0 ? 0 : 1;
    } else if (absNormal.x >= absNormal.y && absNormal.x >= absNormal.z) {
        face = normal.x > 0 ? 2 : 3;
    } else {
        face = normal.y > 0 ? 4 : 5;
    }

    float overlapFactor = 0.80f + (planetRadius / 2500.0f);
    if (overlapFactor > 0.98f) overlapFactor = 0.98f;
    if (overlapFactor < 0.80f) overlapFactor = 0.80f;

    float S = 16.0f * overlapFactor;
    int N = (int)std::ceil((glm::pi<float>() * planetRadius) / (2.0f * S));
    if (N < 1) N = 1;

    float factor = planetRadius / S;
    float local_cx = 0, local_cy = 0;

    if (face == 0) { local_cx = (normal.x / normal.z) * factor; local_cy = (normal.y / normal.z) * factor; }
    else if (face == 1) { local_cx = (normal.x / -normal.z) * -factor; local_cy = (normal.y / -normal.z) * factor; }
    else if (face == 2) { local_cx = (normal.z / normal.x) * -factor; local_cy = (normal.y / normal.x) * factor; }
    else if (face == 3) { local_cx = (normal.z / -normal.x) * factor; local_cy = (normal.y / -normal.x) * factor; }
    else if (face == 4) { local_cx = (normal.x / normal.y) * factor; local_cy = (normal.z / normal.y) * -factor; }
    else if (face == 5) { local_cx = (normal.x / -normal.y) * factor; local_cy = (normal.z / -normal.y) * factor; }

    int cx = 0, cy = 0;
    if (face == 0) { cx = (int)std::floor(local_cx); cy = (int)std::ceil(local_cy); }
    else if (face == 1) { cx = (int)std::ceil(local_cx); cy = (int)std::floor(local_cy); }
    else if (face == 2) { cx = (int)std::ceil(local_cx); cy = (int)std::floor(local_cy); }
    else if (face == 3) { cx = (int)std::floor(local_cx); cy = (int)std::ceil(local_cy); }
    else if (face == 4) { cx = (int)std::floor(local_cx); cy = (int)std::ceil(local_cy); }
    else if (face == 5) { cx = (int)std::floor(local_cx); cy = (int)std::ceil(local_cy); }

    cx = std::clamp(cx, -N, N);
    cy = std::clamp(cy, -N, N);

    out_cx = cx + (face % 3) * (N * 2 + 1);
    out_cz = cy + (face / 3) * (N * 2 + 1);
}

float MapWorldGenerator::SampleSphericalNoise(const glm::vec3& normal, const MapRegion& regionInfo, float frequency) {
    thread_local std::unordered_map<uint32_t, ::PerlinNoise> noiseCache;
    if (noiseCache.find(regionInfo.seed) == noiseCache.end()) {
        noiseCache.emplace(regionInfo.seed, ::PerlinNoise(regionInfo.seed));
    }
    const ::PerlinNoise& noiseGen = noiseCache[regionInfo.seed];
    
    // Parametri base ereditati dal template
    float noiseScale = frequency * 100.0f; 
    int octaves = 4;
    float persistence = 0.5f;
    float heightMultiplier = 5.0f;
    
    // --- FORME GEOLOGICHE SPECIFICHE PER TIPO DI TERRENO ---
    switch (regionInfo.type) {
        case MapRegionType::Desert:
            // Deserto: Dune sabbiose morbide e larghe (pochi ottavi, scale più grande)
            octaves = 2;
            persistence = 0.35f;
            noiseScale *= 0.7f;
            break;
        case MapRegionType::Ocean:
            // Oceano: Superficie estremamente piatta
            octaves = 1;
            heightMultiplier = 0.8f;
            break;
        case MapRegionType::Volcano:
            // Vulcano: Montagne molto alte, frastagliate e appuntite
            octaves = 6;
            persistence = 0.65f;
            heightMultiplier = 15.0f;
            noiseScale *= 1.2f;
            break;
        case MapRegionType::Tundra:
            // Tundra/Ghiacciaio: Terreno ruvido, solcato e freddo
            octaves = 5;
            persistence = 0.55f;
            noiseScale *= 1.4f;
            heightMultiplier = 8.0f;
            break;
        case MapRegionType::City:
        case MapRegionType::Portal:
            // Zone Artificiali: Terreno appiattito per costruire
            octaves = 2;
            heightMultiplier = 1.0f;
            break;
        case MapRegionType::Forest:
        default:
            // Foresta/Base: Colline morbide standard
            octaves = 4;
            persistence = 0.5f;
            break;
    }
    
    float noiseVal = noiseGen.octaveNoise(
        normal.x * noiseScale, 
        normal.y * noiseScale, 
        normal.z * noiseScale, 
        octaves,
        persistence
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
