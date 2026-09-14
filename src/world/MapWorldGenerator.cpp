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
                
                fw::MapRegion baseR;
                baseR.eulerAngles = inst.eulerAngles;
                baseR.angularRadius = inst.angularRadius;
                baseR.isGridAligned = inst.isGridAligned;
                baseR.faceIndex = inst.faceIndex;
                baseR.gridX = baseX;
                baseR.gridY = baseZ;
                baseR.rectMin = glm::ivec2(baseX, baseZ);
                baseR.rectMax = glm::ivec2(baseX, baseZ);
                baseR.type = tmpl.baseType;
                baseR.gravityModifier = tmpl.baseGravityModifier;
                baseR.perlinFrequency = tmpl.basePerlinFrequency;
                baseR.surfaceBlockId = tmpl.baseSurfaceBlockId;
                baseR.subsurfaceBlockId = tmpl.baseSubsurfaceBlockId;
                combinedRegions.push_back(baseR);
                
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
        biomeData.planetSize = planet.planetSize;
        biomeData.isFlat = planet.isFlat;
        biomeData.chunkCenterWorld = pos;
        biomeData.baseTerrain = planet.baseTerrain;
        
        for (auto it = combinedRegions.begin(); it != combinedRegions.end(); ++it) {
            if (it->isGridAligned) {
                // Aggiungi un margine di 1 tile per l'SDF blending sui chunk adiacenti
                int margin = 1; 
                if (it->faceIndex == face && local_cx >= it->gridX - margin && local_cx <= it->gridX + margin &&
                    local_cz >= it->gridY - margin && local_cz <= it->gridY + margin) {
                    biomeData.overlappingRegions.push_back(*it);
                    biomeData.isCustomMapped = true;
                }
            } else if (it->angularRadius > 0.0f) {
                // Free-floating Spherical region
                float R = fw::PlanetMath::GetPlanetRadius(planet.planetSize);
                float rRadius = it->angularRadius * R;
                float pitch = glm::radians(it->eulerAngles.x);
                float yaw = glm::radians(it->eulerAngles.y);
                glm::vec3 rCenter = glm::vec3(cos(pitch) * cos(yaw), sin(pitch), cos(pitch) * sin(yaw)) * R;
                
                // Mappa la posizione (piatta o sferica) sulla sfera logica per calcolare la distanza
                glm::vec3 checkPos = pos;
                if (planet.isFlat) {
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

        biomeData.surfaceBlockId = biomeData.baseTerrain.surfaceBlock;
        biomeData.subsurfaceBlockId = biomeData.baseTerrain.subsurfaceBlock;
        
        targetWorld.GetRegistry().emplace_or_replace<fw::BiomeDataComponent>(chunkEnt, biomeData);
        targetWorld.GetRegistry().emplace_or_replace<fw::TerrainGenTag>(chunkEnt);
        
        switch (biomeData.baseTerrain.biome) {
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

    if (!planet.isFlat) {
        int N = fw::PlanetMath::GetEditorCanvasExtents(planet.planetSize);
        int stride = fw::PlanetMath::GetFaceResolution(planet.planetSize);
        
        for (int face = 0; face < 6; ++face) {
            for (int cy = -N; cy <= N; ++cy) {
                for (int cx = -N; cx <= N; ++cx) {
                    int face_col = face % 3;
                    int face_row = face / 3;
                    int global_cx = face_col * stride + (cx + N);
                    int global_cz = face_row * stride + (cy + N);
                    
                    glm::vec3 spherePos;
                    glm::quat q;
                    if (GetSphericalChunkTransform(planet.planetSize, global_cx, global_cz, spherePos, q)) {
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
        bool hasOnlyBackground = true;
        for (const auto& r : combinedRegions) {
            if (!r.isBackgroundFill) {
                hasOnlyBackground = false;
                break;
            }
        }
        
        for (int cz = dimManager.GetMinZ(); cz <= dimManager.GetMaxZ(); ++cz) {
            for (int cx = dimManager.GetMinX(); cx <= dimManager.GetMaxX(); ++cx) {
                bool shouldSpawn = false;
                
                if (combinedRegions.empty() || hasOnlyBackground) {
                    shouldSpawn = true; // Fallback for completely empty maps or maps with only background
                } else {
                    for (const auto& r : combinedRegions) {
                        if (r.isBackgroundFill) continue;
                        float margin = 2.0f; // Margine per overlapping
                        if (cx >= r.rectMin.x - margin && cx <= r.rectMax.x + margin &&
                            cz >= r.rectMin.y - margin && cz <= r.rectMax.y + margin) {
                            shouldSpawn = true;
                            break;
                        }
                    }
                }
                
                if (shouldSpawn) {
                    generateChunk(cx, cz, cx, cz, -1, glm::vec3(cx * 16.0f, 0.0f, cz * 16.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
                }
            }
        }
    }
    
    std::cout << "[MapWorldGenerator] Generazione chunk completata.\n";
}

bool MapWorldGenerator::GetSphericalChunkTransform(PlanetSize pSize, int global_cx, int global_cz, glm::vec3& outPos, glm::quat& outRot) {
    int N = fw::PlanetMath::GetEditorCanvasExtents(pSize); // Es: 5 per Medium
    float R = fw::PlanetMath::GetPlanetRadius(pSize);      // Es: 88.0f
    float S = fw::PlanetMath::CHUNK_WORLD_SIZE;            // Rigorosamente 16.0f

    int stride = fw::PlanetMath::GetFaceResolution(pSize); // Es: 11
    
    // Reverse mapping using 3x2 grid (Face 0..5)
    int face_col = global_cx / stride;
    int face_row = global_cz / stride;

    if (face_col >= 0 && face_col < 3 && face_row >= 0 && face_row < 2) {
        int face = face_col + face_row * 3;
        
        // Offset dal centro della faccia (da -N a +N)
        int cx = (global_cx % stride) - N; 
        int cy = (global_cz % stride) - N;

        // Proiezione cubica base (piano 3D)
        glm::vec3 localPos(0.0f);
        if (face == 0) localPos = glm::vec3(cx * S, cy * S, R);         // +Z
        else if (face == 1) localPos = glm::vec3(-cx * S, cy * S, -R);  // -Z
        else if (face == 2) localPos = glm::vec3(R, cy * S, -cx * S);   // +X
        else if (face == 3) localPos = glm::vec3(-R, cy * S, cx * S);   // -X
        else if (face == 4) localPos = glm::vec3(cx * S, R, -cy * S);   // +Y
        else if (face == 5) localPos = glm::vec3(cx * S, -R, cy * S);   // -Y
        
        // Normalizzazione (proiezione sferica) e riscalamento sul raggio esatto
        glm::vec3 normal = glm::normalize(localPos);
        outPos = normal * R;

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
    return false;
}

bool MapWorldGenerator::GetTrueSphericalPosition(PlanetSize pSize, bool isFlat, int global_cx, int global_cz, float local_x, float local_y, float local_z, glm::vec3& outWorldPos) {
    if (isFlat) {
        outWorldPos = glm::vec3(global_cx * 16.0f + local_x, local_y, global_cz * 16.0f + local_z);
        return true;
    }

    int N = fw::PlanetMath::GetEditorCanvasExtents(pSize);
    float R = fw::PlanetMath::GetPlanetRadius(pSize);
    float S = fw::PlanetMath::CHUNK_WORLD_SIZE;
    int stride = fw::PlanetMath::GetFaceResolution(pSize);
    
    int face_col = global_cx / stride;
    int face_row = global_cz / stride;

    if (face_col >= 0 && face_col < 3 && face_row >= 0 && face_row < 2) {
        int face = face_col + face_row * 3;
        int cx = (global_cx % stride) - N;
        int cy = (global_cz % stride) - N;

        float dx = (local_x - 8.0f);
        float dz = (local_z - 8.0f);
        
        float faceX = cx * S + dx;
        float faceY = cy * S + dz;

        glm::vec3 localPos(0.0f);
        if (face == 0) localPos = glm::vec3(faceX, faceY, R);         // +Z
        else if (face == 1) localPos = glm::vec3(-faceX, faceY, -R);  // -Z
        else if (face == 2) localPos = glm::vec3(R, faceY, -faceX);   // +X
        else if (face == 3) localPos = glm::vec3(-R, faceY, faceX);   // -X
        else if (face == 4) localPos = glm::vec3(faceX, R, -faceY);   // +Y
        else if (face == 5) localPos = glm::vec3(faceX, -R, faceY);   // -Y
        
        glm::vec3 normal = glm::normalize(localPos);
        float radiusAtY = R + (local_y - 25.0f);
        
        outWorldPos = normal * radiusAtY;
        return true;
    }
    return false;
}

void MapWorldGenerator::WorldToVoxelCoord(PlanetSize pSize, bool isFlat, const glm::vec3& worldPos, float& out_flatX, float& out_localY, float& out_flatZ) {
    if (isFlat) {
        out_flatX = worldPos.x + 8.0f;
        out_localY = worldPos.y;
        out_flatZ = worldPos.z + 8.0f;
        return;
    }

    float distance = glm::length(worldPos);
    float R = fw::PlanetMath::GetPlanetRadius(pSize);
    out_localY = (distance - R) + 25.0f;

    if (distance < 0.001f) {
        out_flatX = 0; out_flatZ = 0; return;
    }
    
    glm::vec3 normal = worldPos / distance;
    glm::vec3 absNormal = glm::abs(normal);
    int face = 0;
    if (absNormal.z >= absNormal.x && absNormal.z >= absNormal.y) face = normal.z > 0 ? 0 : 1;
    else if (absNormal.x >= absNormal.y && absNormal.x >= absNormal.z) face = normal.x > 0 ? 2 : 3;
    else face = normal.y > 0 ? 4 : 5;

    float factor = R / fw::PlanetMath::CHUNK_WORLD_SIZE;
    float local_cx = 0, local_cy = 0;

    if (face == 0) { local_cx = (normal.x / normal.z) * factor; local_cy = (normal.y / normal.z) * factor; }
    else if (face == 1) { local_cx = (normal.x / -normal.z) * -factor; local_cy = (normal.y / -normal.z) * factor; }
    else if (face == 2) { local_cx = (normal.z / normal.x) * -factor; local_cy = (normal.y / normal.x) * factor; }
    else if (face == 3) { local_cx = (normal.z / -normal.x) * factor; local_cy = (normal.y / -normal.x) * factor; }
    else if (face == 4) { local_cx = (normal.x / normal.y) * factor; local_cy = (normal.z / normal.y) * -factor; }
    else if (face == 5) { local_cx = (normal.x / -normal.y) * factor; local_cy = (normal.z / -normal.y) * factor; }

    int stride = fw::PlanetMath::GetFaceResolution(pSize);
    int face_col = face % 3;
    int face_row = face / 3;

    int N = fw::PlanetMath::GetEditorCanvasExtents(pSize);
    float global_cx_continuous = local_cx + N + face_col * stride;
    float global_cy_continuous = local_cy + N + face_row * stride;

    out_flatX = global_cx_continuous * 16.0f + 8.0f;
    out_flatZ = global_cy_continuous * 16.0f + 8.0f;
}

void MapWorldGenerator::GetChunkCoordFromPosition(PlanetSize pSize, bool isFlat, const glm::vec3& worldPos, int& out_cx, int& out_cz) {
    if (isFlat) {
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

    float R = fw::PlanetMath::GetPlanetRadius(pSize);
    float factor = R / fw::PlanetMath::CHUNK_WORLD_SIZE;
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

    int N = fw::PlanetMath::GetEditorCanvasExtents(pSize);
    cx = std::clamp(cx, -N, N);
    cy = std::clamp(cy, -N, N);

    int stride = fw::PlanetMath::GetFaceResolution(pSize);
    out_cx = (cx + N) + (face % 3) * stride;
    out_cz = (cy + N) + (face / 3) * stride;
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
