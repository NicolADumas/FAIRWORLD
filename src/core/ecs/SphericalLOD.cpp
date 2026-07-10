#include "pch.h"
#include "SphericalLOD.h"
#include "ForgeWorld.h"
#include "JobSystem.h"
#include "../app/AssetManager.h"
#include "MapWorldGenerator.h"

namespace fw {

void SphericalLODSystem::UpdateLODTree(ChunkNode& node, const glm::vec3& playerPos, ForgeWorld* world, JobSystem* jobs, AssetManager* assets, const PlanetMap* planetInfo) {
    float distance = glm::length(node.centerPos - playerPos);
    
    // Genera la mesh se non esiste e non stiamo già generando
    if (node.targetEntity == entt::null && !node.isGenerating && !node.isSplit) {
        RequestMeshGeneration(&node, world, jobs, assets, planetInfo);
    }
    
    // Siamo vicini e possiamo ancora dividere? Dividiamo.
    if (distance < GetThresholdForLOD(node.lodLevel, node.boundsRadius) && node.lodLevel > 0) {
        if (!node.isSplit) {
            SplitNode(node, world, jobs, assets, planetInfo);
            // Nascondiamo il nodo genitore distruggendolo per liberare memoria
            if (node.targetEntity != entt::null) {
                world->DestroyEntity(node.targetEntity);
                node.targetEntity = entt::null;
            }
        }
        // Aggiorna ricorsivamente i figli
        for (auto& child : node.children) {
            if (child) UpdateLODTree(*child, playerPos, world, jobs, assets, planetInfo);
        }
    } 
    // Ci siamo allontanati? Uniamo i figli e puliamo la memoria.
    else if (node.isSplit && distance >= GetThresholdForLOD(node.lodLevel, node.boundsRadius)) {
        MergeNode(node, world);
        // Richiediamo la generazione del nodo genitore
        if (node.targetEntity == entt::null && !node.isGenerating) {
            RequestMeshGeneration(&node, world, jobs, assets, planetInfo);
        }
    }
}

void SphericalLODSystem::SplitNode(ChunkNode& node, ForgeWorld* world, JobSystem* jobs, AssetManager* assets, const PlanetMap* planetInfo) {
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

void SphericalLODSystem::MergeNode(ChunkNode& node, ForgeWorld* world) {
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

void SphericalLODSystem::RequestMeshGeneration(ChunkNode* node, ForgeWorld* world, JobSystem* jobs, AssetManager* assets, const PlanetMap* planetInfo) {
    node->isGenerating = true;
    
    glm::vec3 p00 = node->p00;
    glm::vec3 p10 = node->p10;
    glm::vec3 p01 = node->p01;
    glm::vec3 p11 = node->p11;
    float planetRadius = m_planetRadius;
    
    std::string meshName = "LOD_" + std::to_string(node->lodLevel) + "_" + std::to_string(reinterpret_cast<uintptr_t>(node));
    
    if (node->targetEntity == entt::null) {
        node->targetEntity = world->CreateEmptyEntity(meshName);
    }
    entt::entity target = node->targetEntity;
    
    // Per l'esecuzione asincrona, facciamo una copia dei dati di base per thread-safety
    std::vector<MapRegion> safeRegions;
    if (planetInfo) safeRegions = planetInfo->regions;
    
    // NOTA BENE: NON catturiamo 'node' come raw pointer, perché 'MergeNode' potrebbe distruggerlo nel thread principale prima che il job finisca!
    jobs->Execute([world, assets, target, meshName, p00, p10, p01, p11, planetRadius, safeRegions]() {
        MeshComponent mesh;
        mesh.name = meshName;
        
        const int RESOLUTION = 16;
        std::vector<glm::vec3> positions;
        std::vector<glm::vec3> normals;
        std::vector<glm::vec4> colors;
        
        positions.reserve((RESOLUTION + 1) * (RESOLUTION + 1));
        normals.reserve((RESOLUTION + 1) * (RESOLUTION + 1));
        colors.reserve((RESOLUTION + 1) * (RESOLUTION + 1));
        
        for (int y = 0; y <= RESOLUTION; ++y) {
            float v = (float)y / RESOLUTION;
            for (int x = 0; x <= RESOLUTION; ++x) {
                float u = (float)x / RESOLUTION;
                
                glm::vec3 p0 = glm::mix(p00, p10, u);
                glm::vec3 p1 = glm::mix(p01, p11, u);
                glm::vec3 p = glm::mix(p0, p1, v);
                
                glm::vec3 normal = glm::normalize(p);
                
                // DATA-DRIVEN: Calcoliamo l'influenza delle regioni
                MapRegion activeRegion;
                activeRegion.seed = 12345;
                activeRegion.gravityModifier = 1.0f;
                activeRegion.perlinFrequency = 0.005f; // Base
                
                // NOTA: Con la migrazione alle coordinate Chunk (rectMin/rectMax), il calcolo
                // dell'influenza per la macro-sfera visiva (SphericalLOD) richiede una conversione 
                // da coordinate geografiche (lat/long) a Chunk. 
                // Per ora, applichiamo un rumore di base uniforme per il pianeta LOD in lontananza.
                
                float latitude = asin(normal.y); // Necessario per il bioma
                
                // Usa i dati della regione per la generazione procedurale!
                float noiseVal = MapWorldGenerator::SampleSphericalNoise(normal, activeRegion, activeRegion.perlinFrequency);
                float height = planetRadius + (noiseVal * planetRadius * 0.05f * activeRegion.gravityModifier);
                
                positions.push_back(normal * height);
                normals.push_back(normal);
                
                // Temperatura influenzata dalla latitudine (poli freddi, equatore caldo)
                float latTemp = 1.0f - std::abs(latitude) / (glm::pi<float>() / 2.0f);
                
                float tempNoise = (MapWorldGenerator::SampleSphericalNoise(normal, activeRegion, 0.001f) + 1.0f) * 0.5f;
                float humNoise = (MapWorldGenerator::SampleSphericalNoise(normal, activeRegion, 0.003f) + 1.0f) * 0.5f;
                
                float tempFinal = (tempNoise * 0.5f) + (latTemp * 0.5f);
                float relHeight = std::clamp((height - planetRadius) / (planetRadius * 0.05f), 0.0f, 1.0f);
                
                const ::BiomeDef* biome = MapWorldGenerator::EvaluateBiome(tempFinal, humNoise, relHeight, assets);
                
                glm::vec4 color(0.3f, 0.8f, 0.3f, 1.0f);
                if (biome) {
                    if (biome->name.find("Deserto") != std::string::npos || activeRegion.type == MapRegionType::Desert) color = glm::vec4(0.8f, 0.7f, 0.4f, 1.0f);
                    else if (biome->name.find("Oceano") != std::string::npos || activeRegion.type == MapRegionType::Ocean) color = glm::vec4(0.1f, 0.3f, 0.8f, 1.0f);
                    else if (biome->name.find("Neve") != std::string::npos || activeRegion.type == MapRegionType::Tundra) color = glm::vec4(0.9f, 0.9f, 0.95f, 1.0f);
                    else if (activeRegion.type == MapRegionType::Volcano) color = glm::vec4(0.8f, 0.2f, 0.2f, 1.0f);
                }
                
                if (height < planetRadius + 0.1f) {
                    positions.back() = normal * (planetRadius + 0.1f);
                    color = glm::vec4(0.1f, 0.3f, 0.8f, 1.0f);
                }
                
                colors.push_back(color);
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
                    vtx.texIndex = 0;
                    vtx.ao = 1.0f;
                    vtx.light = 1.0f;
                    vtx.emissive = 0.0f;
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
