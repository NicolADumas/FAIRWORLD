#include "pch.h"
#include "SphericalLOD.h"
#include "GameWorld.h"
#include "JobSystem.h"
#include "../app/AssetManager.h"
#include "MapWorldGenerator.h"
#include "BlockRegistry.h"

namespace fw {

void SphericalLODSystem::UpdateLODTree(ChunkNode& node, const glm::vec3& playerPos, GameWorld* world, JobSystem* jobs, AssetManager* assets, const std::vector<MapRegion>& activeRegions, const glm::mat4& viewProj, class BlockRegistry* blockReg) {
    // --- FRUSTUM CULLING ---
    glm::vec4 planes[6];
    planes[0] = glm::vec4(viewProj[0][3] + viewProj[0][0], viewProj[1][3] + viewProj[1][0], viewProj[2][3] + viewProj[2][0], viewProj[3][3] + viewProj[3][0]); // Left
    planes[1] = glm::vec4(viewProj[0][3] - viewProj[0][0], viewProj[1][3] - viewProj[1][0], viewProj[2][3] - viewProj[2][0], viewProj[3][3] - viewProj[3][0]); // Right
    planes[2] = glm::vec4(viewProj[0][3] + viewProj[0][1], viewProj[1][3] + viewProj[1][1], viewProj[2][3] + viewProj[2][1], viewProj[3][3] + viewProj[3][1]); // Bottom
    planes[3] = glm::vec4(viewProj[0][3] - viewProj[0][1], viewProj[1][3] - viewProj[1][1], viewProj[2][3] - viewProj[2][1], viewProj[3][3] - viewProj[3][1]); // Top
    planes[4] = glm::vec4(viewProj[0][3] + viewProj[0][2], viewProj[1][3] + viewProj[1][2], viewProj[2][3] + viewProj[2][2], viewProj[3][3] + viewProj[3][2]); // Near
    planes[5] = glm::vec4(viewProj[0][3] - viewProj[0][2], viewProj[1][3] - viewProj[1][2], viewProj[2][3] - viewProj[2][2], viewProj[3][3] - viewProj[3][2]); // Far
    
    bool isVisible = true;
    for (int i = 0; i < 6; i++) {
        float len = glm::length(glm::vec3(planes[i]));
        planes[i] /= len;
        
        float distance = glm::dot(glm::vec3(planes[i]), node.centerPos) + planes[i].w;
        if (distance < -node.boundsRadius) {
            isVisible = false;
            break;
        }
    }
    
    // Se il nodo è fuori dal Frustum, distruggiamo la sua mesh (se esiste) e compattiamo i figli per risparmiare memoria
    if (!isVisible) {
        if (node.isSplit) {
            MergeNode(node, world);
        }
        if (node.targetEntity != entt::null && !node.isGenerating) {
            world->DestroyEntity(node.targetEntity);
            node.targetEntity = entt::null;
        }
        return; // Interrompiamo l'aggiornamento per questo ramo
    }

    float distance = glm::length(node.centerPos - playerPos);
    
    // Genera la mesh se non esiste e non stiamo già generando
    if (node.targetEntity == entt::null && !node.isGenerating && !node.isSplit) {
        RequestMeshGeneration(&node, world, jobs, assets, activeRegions, blockReg);
    }
    
    // Siamo vicini e possiamo ancora dividere? Dividiamo.
    if (distance < GetThresholdForLOD(node.lodLevel, node.boundsRadius) && node.lodLevel > 0) {
        if (!node.isSplit) {
            SplitNode(node, world, jobs, assets, activeRegions, blockReg);
            // Non distruggiamo subito il nodo genitore per evitare buchi, lo nasconderemo quando i figli sono pronti.
        }
        // Aggiorna ricorsivamente i figli
        for (auto& child : node.children) {
            if (child) UpdateLODTree(*child, playerPos, world, jobs, assets, activeRegions, viewProj, blockReg);
        }
    } 
    // Ci siamo allontanati? Uniamo i figli e puliamo la memoria.
    else if (node.isSplit && distance >= GetThresholdForLOD(node.lodLevel, node.boundsRadius)) {
        MergeNode(node, world);
        // Richiediamo la generazione del nodo genitore
        if (node.targetEntity == entt::null && !node.isGenerating) {
            RequestMeshGeneration(&node, world, jobs, assets, activeRegions, blockReg);
        }
    }
}

void SphericalLODSystem::SplitNode(ChunkNode& node, GameWorld* world, JobSystem* jobs, AssetManager* assets, const std::vector<MapRegion>& activeRegions, class BlockRegistry* blockReg) {
    node.isSplit = true;
    
    glm::vec3 m0 = glm::normalize(node.p00 + node.p10) * m_planetRadius;
    glm::vec3 m1 = glm::normalize(node.p01 + node.p11) * m_planetRadius;
    glm::vec3 m2 = glm::normalize(node.p00 + node.p01) * m_planetRadius;
    glm::vec3 m3 = glm::normalize(node.p10 + node.p11) * m_planetRadius;
    glm::vec3 center = glm::normalize(node.p00 + node.p11) * m_planetRadius;
    
    float newRadius = node.boundsRadius * 0.5f;
    int nextLod = node.lodLevel - 1;
    
    node.children[0] = std::make_unique<ChunkNode>(glm::normalize(node.p00 + center) * m_planetRadius, newRadius, nextLod, node.p00, m0, m2, center);
    node.children[1] = std::make_unique<ChunkNode>(glm::normalize(m0 + m3) * m_planetRadius, newRadius, nextLod, m0, node.p10, center, m3);
    node.children[2] = std::make_unique<ChunkNode>(glm::normalize(m2 + center) * m_planetRadius, newRadius, nextLod, m2, center, node.p01, m1);
    node.children[3] = std::make_unique<ChunkNode>(glm::normalize(center + node.p11) * m_planetRadius, newRadius, nextLod, center, m3, m1, node.p11);
}

void SphericalLODSystem::MergeNode(ChunkNode& node, GameWorld* world) {
    node.isSplit = false;
    for (int i = 0; i < 4; ++i) {
        if (node.children[i]) {
            if (node.children[i]->targetEntity != entt::null) {
                world->DestroyEntity(node.children[i]->targetEntity);
            }
            node.children[i].reset();
        }
    }
}

void SphericalLODSystem::RequestMeshGeneration(ChunkNode* node, GameWorld* world, JobSystem* jobs, AssetManager* assets, const std::vector<MapRegion>& activeRegions, class BlockRegistry* blockReg) {
    node->isGenerating = true;
    
    glm::vec3 p00 = node->p00;
    glm::vec3 p10 = node->p10;
    glm::vec3 p01 = node->p01;
    glm::vec3 p11 = node->p11;
    float planetRadius = m_planetRadius;
    
    std::string meshName = "LOD_" + std::to_string(node->lodLevel) + "_" + std::to_string(reinterpret_cast<uintptr_t>(node));
    
    if (node->targetEntity == entt::null) {
        node->targetEntity = world->CreateEmptyEntity(meshName);
        fw::TransformComponent trans;
        world->GetRegistry().emplace<fw::TransformComponent>(node->targetEntity, trans);
    }
    entt::entity target = node->targetEntity;
    
    // Per l'esecuzione asincrona, facciamo una copia dei dati di base per thread-safety
    std::vector<MapRegion> safeRegions = activeRegions;
    
    // NOTA BENE: NON catturiamo 'node' come raw pointer, perché 'MergeNode' potrebbe distruggerlo nel thread principale prima che il job finisca!
    jobs->Execute([world, assets, target, meshName, p00, p10, p01, p11, planetRadius, safeRegions, blockReg]() {
        MeshComponent mesh;
        mesh.name = meshName;
        mesh.type = fw::MeshType::Chunk; // Set to Chunk so MapRenderer draws it!
        
        const int RESOLUTION = 16;
        std::vector<glm::vec3> positions;
        std::vector<glm::vec3> normals;
        std::vector<glm::vec4> colors;
        std::vector<uint32_t> materials; // Array per gli ID dei materiali (1=Erba, 2=Sabbia, 3=Pietra, 4=Acqua, 5=Neve)
        std::vector<float> emissives; // Nuova array per glow olografico
        
        positions.reserve((RESOLUTION + 1) * (RESOLUTION + 1));
        normals.reserve((RESOLUTION + 1) * (RESOLUTION + 1));
        colors.reserve((RESOLUTION + 1) * (RESOLUTION + 1));
        materials.reserve((RESOLUTION + 1) * (RESOLUTION + 1));
        emissives.reserve((RESOLUTION + 1) * (RESOLUTION + 1));
        
        for (int y = 0; y <= RESOLUTION; ++y) {
            float v = (float)y / RESOLUTION;
            for (int x = 0; x <= RESOLUTION; ++x) {
                float u = (float)x / RESOLUTION;
                
                glm::vec3 p0 = glm::mix(p00, p10, u);
                glm::vec3 p1 = glm::mix(p01, p11, u);
                glm::vec3 p = glm::mix(p0, p1, v);
                
                glm::vec3 normal = glm::normalize(p);
                
                // DATA-DRIVEN: Calcoliamo l'influenza delle regioni tramite Grid Mapping esatto e distanza angolare (Fallback)
                MapRegion activeRegion;
                activeRegion.seed = 12345;
                activeRegion.gravityModifier = 1.0f;
                activeRegion.perlinFrequency = 0.005f; // Base
                
                // --- GRID MAPPING LOGIC (Legge Sferica Esatta) ---
                int N_lato = (int)std::ceil((glm::pi<float>() * planetRadius) / (2.0f * 32.0f));
                if (N_lato < 1) N_lato = 1;
                
                int face = -1;
                float cx = 0.0f, cy = 0.0f;
                float ax = std::abs(normal.x);
                float ay = std::abs(normal.y);
                float az = std::abs(normal.z);

                if (az >= ax && az >= ay) {
                    if (normal.z > 0) { face = 0; cx = normal.x / normal.z; cy = normal.y / normal.z; } // +Z
                    else              { face = 1; cx = normal.x / normal.z; cy = -normal.y / normal.z; } // -Z
                } else if (ax >= ay && ax >= az) {
                    if (normal.x > 0) { face = 2; cx = -normal.z / normal.x; cy = normal.y / normal.x; } // +X
                    else              { face = 3; cx = -normal.z / normal.x; cy = -normal.y / normal.x; } // -X
                } else {
                    if (normal.y > 0) { face = 4; cx = normal.x / normal.y; cy = -normal.z / normal.y; } // +Y
                    else              { face = 5; cx = -normal.x / normal.y; cy = -normal.z / normal.y; } // -Y
                }
                
                int gridCol = std::clamp((int)std::floor((cx + 1.0f) * 0.5f * N_lato), 0, N_lato - 1);
                int gridRow = std::clamp((int)std::floor((1.0f - cy) * 0.5f * N_lato), 0, N_lato - 1);
                
                bool foundGridAligned = false;
                
                // Prima passata: Cerca una regione allineata alla griglia esatta (Tabella Excel)
                for (const auto& r : safeRegions) {
                    if (r.isGridAligned && r.faceIndex == face && r.gridX == gridCol && r.gridY == gridRow) {
                        activeRegion = r;
                        foundGridAligned = true;
                        break;
                    }
                }
                
                float maxInfluence = -1.0f;
                if (foundGridAligned) {
                    maxInfluence = 1.0f; // Copertura perfetta per il rendering seamless
                } else {
                    // Seconda passata: Fallback ai chunk posizionati liberamente (Raycast manuale a pennello)
                    for (const auto& r : safeRegions) {
                        if (r.isGridAligned) continue; // Salta quelli rigidi
                        
                        float pitch = glm::radians(r.eulerAngles.x);
                        float yaw = glm::radians(r.eulerAngles.y);
                        glm::vec3 rCenterNormal(cos(pitch) * cos(yaw), sin(pitch), cos(pitch) * sin(yaw));
                        float dotProduct = glm::dot(normal, rCenterNormal);
                        float threshold = cos(r.angularRadius);
                        
                        if (dotProduct > threshold) {
                            float influence = (dotProduct - threshold) / (1.0f - threshold);
                            if (influence > maxInfluence) {
                                maxInfluence = influence;
                                activeRegion = r;
                            }
                        }
                    }
                }

                if (maxInfluence < 0.0f) {
                    // Rendering Olografico / Wireframe per zone non dipinte
                    float height = planetRadius;
                    positions.push_back(normal * height);
                    normals.push_back(normal);
                    
                    // Crea griglia olografica basata su lat/lon sferica
                    float uGrid = atan2(normal.z, normal.x) * 40.0f;
                    float vGrid = asin(normal.y) * 40.0f;
                    bool isLine = (fmod(std::abs(uGrid), 1.0f) < 0.05f || fmod(std::abs(vGrid), 1.0f) < 0.05f);
                    
                    colors.push_back(isLine ? glm::vec4(0.0f, 0.8f, 1.0f, 1.0f) : glm::vec4(0.02f, 0.05f, 0.1f, 1.0f));
                    materials.push_back(0); // Nessun materiale
                    emissives.push_back(isLine ? 1.0f : 0.0f);
                } else {
                    // Generazione Voxel Reale
                    float noiseVal = MapWorldGenerator::SampleSphericalNoise(normal, activeRegion, activeRegion.perlinFrequency);
                    float height = planetRadius + (noiseVal * planetRadius * 0.05f * activeRegion.gravityModifier);
                    
                    positions.push_back(normal * height);
                    normals.push_back(normal);
                    
                    float latitude = asin(normal.y);
                    float latTemp = 1.0f - std::abs(latitude) / (glm::pi<float>() / 2.0f);
                    float tempNoise = (MapWorldGenerator::SampleSphericalNoise(normal, activeRegion, 0.001f) + 1.0f) * 0.5f;
                    float humNoise = (MapWorldGenerator::SampleSphericalNoise(normal, activeRegion, 0.003f) + 1.0f) * 0.5f;
                    float tempFinal = (tempNoise * 0.5f) + (latTemp * 0.5f);
                    float relHeight = std::clamp((height - planetRadius) / (planetRadius * 0.05f), 0.0f, 1.0f);
                    
                    const ::BiomeDef* biome = MapWorldGenerator::EvaluateBiome(tempFinal, humNoise, relHeight, assets);
                    glm::vec4 color(0.3f, 0.8f, 0.3f, 1.0f);
                    
                    // INIZIALIZZA col blocco deciso dall'utente nella UI (Tabella/Template)!
                    uint32_t matId = activeRegion.surfaceBlockId; 
                    
                    uint8_t idSand = 5;
                    uint8_t idWater = 6;
                    uint8_t idSnow = 7;
                    uint8_t idStone = 3;
                    uint8_t idGrass = 1;
                    
                    if (blockReg) {
                        idSand = blockReg->GetBlock("fairworld:sand").id;
                        idWater = blockReg->GetBlock("fairworld:water").id;
                        idStone = blockReg->GetBlock("fairworld:stone").id;
                        idGrass = blockReg->GetBlock("fairworld:grass").id;
                        idSnow = blockReg->GetBlock("fairworld:snow").id;
                        if (idSnow == 0) idSnow = 7; // Fallback
                    }

                    // Colore approssimativo per debug se non ci sono texture valide
                    if (matId == idSand) color = glm::vec4(0.8f, 0.7f, 0.4f, 1.0f); // Sabbia
                    else if (matId == idWater) color = glm::vec4(0.1f, 0.3f, 0.8f, 1.0f); // Acqua
                    else if (matId == idSnow) color = glm::vec4(0.9f, 0.9f, 0.95f, 1.0f); // Neve
                    else if (matId == idStone) color = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f); // Pietra
                    
                    // Elevation based materials (Stone for mountains, Snow for peaks)
                    float elevation = height - planetRadius;
                    
                    // Sovrascrivi con Neve/Pietra SOLO se l'utente aveva messo Erba/Sabbia di base
                    if (matId == idGrass || matId == idSand) {
                        if (elevation > planetRadius * 0.02f) {
                            matId = idStone; // Pietra per le montagne
                        }
                        if (elevation > planetRadius * 0.04f) {
                            matId = idSnow; // Neve per le alte vette
                        }
                    }
                    
                    if (activeRegion.type == MapRegionType::Ocean || height < planetRadius + 0.1f) {
                        // Forza il livello del mare perfettamente piatto e l'ID acqua
                        positions.back() = normal * (planetRadius + 0.1f);
                        color = glm::vec4(0.1f, 0.3f, 0.8f, 1.0f);
                        matId = idWater; // Acqua
                    }
                    
                    float latDeg = glm::degrees(latitude);
                    float em = 0.0f;
                    // Equator and Poles markers
                    if (std::abs(latDeg) < 0.8f) { // Equatore
                        color = glm::vec4(1.0f, 0.2f, 0.2f, 1.0f);
                        em = 1.0f;
                    } else if (latDeg > 88.0f) { // Polo Nord
                        color = glm::vec4(0.2f, 0.2f, 1.0f, 1.0f);
                        em = 1.0f;
                    } else if (latDeg < -88.0f) { // Polo Sud
                        color = glm::vec4(0.2f, 1.0f, 0.2f, 1.0f);
                        em = 1.0f;
                    }
                    
                    colors.push_back(color);
                    materials.push_back(matId);
                    emissives.push_back(em);
                }
            }
        }
        
        mesh.vertices.reserve(RESOLUTION * RESOLUTION * 6);
        for (int y = 0; y < RESOLUTION; ++y) {
            for (int x = 0; x < RESOLUTION; ++x) {
                int i00 = y * (RESOLUTION + 1) + x;
                int i10 = i00 + 1;
                int i01 = (y + 1) * (RESOLUTION + 1) + x;
                int i11 = i01 + 1;
                
                auto addVertex = [&](int idx) {
                    Vertex vtx;
                    vtx.position = {positions[idx].x, positions[idx].y, positions[idx].z};
                    vtx.normal = {normals[idx].x, normals[idx].y, normals[idx].z};
                    vtx.color = {colors[idx].r, colors[idx].g, colors[idx].b, colors[idx].a};
                    vtx.roughMetal = {0.8f, 0.0f};
                    vtx.materialID = materials[idx];
                    vtx.ao = 1.0f;
                    vtx.light = 1.0f;
                    vtx.emissive = emissives[idx];
                    mesh.vertices.push_back(vtx);
                };
                
                addVertex(i00); addVertex(i10); addVertex(i01);
                addVertex(i10); addVertex(i11); addVertex(i01);
            }
        }
        
        world->EnqueueDeferredMesh(meshName, glm::vec3(0), std::move(mesh), nullptr, target, true);
    });
}

} // namespace fw
