#include "pch.h"
#include "PlanetMapperCompiler.h"
#include "FAIRWORLD.h"
#include "RenderManager.h"
#include "vulkan/ChunkCullingTypes.h"
#include "BlockRegistry.h"
#include "JobSystem.h"

PlanetMapperCompiler::PlanetMapperCompiler() {}

void PlanetMapperCompiler::Update(SharedContext* context, int activePlanetIndex) {
    if (!context || !context->projectManager || !context->engine) return;
    auto& doc = context->projectManager->GetDocumentMutable();

    if (doc.planets.empty() || activePlanetIndex < 0 || activePlanetIndex >= (int)doc.planets.size()) return;
    auto& currentPlanet = doc.planets[activePlanetIndex];

    if (currentPlanet.structuralDirty) {
        CompileEverything(context, currentPlanet);
        currentPlanet.structuralDirty = false;
        currentPlanet.dirtyChunkIndices.clear();
    } else {
        for (uint32_t idx : currentPlanet.dirtyChunkIndices) {
            CompileChunk(context, currentPlanet, idx);
        }
        currentPlanet.dirtyChunkIndices.clear();
    }
}

void PlanetMapperCompiler::CompileEverything(SharedContext* context, fw::PlanetMap& planet) {
    planet.regions.clear();
    auto& doc = context->projectManager->GetDocumentMutable();

    for (const auto& inst : planet.chunkInstances) {
        if (!inst.isActive) continue;

        for (const auto& tmpl : doc.terrainLibrary) {
                if (tmpl.id == inst.templateId) {
                    fw::MapRegion baseRegion;
                    baseRegion.eulerAngles = inst.eulerAngles;
                    baseRegion.angularRadius = inst.angularRadius;
                    baseRegion.isGridAligned = inst.isGridAligned;
                    baseRegion.faceIndex = inst.faceIndex;
                    baseRegion.gridX = inst.gridX;
                    baseRegion.gridY = inst.gridY;
                    baseRegion.type = tmpl.baseType;
                    baseRegion.perlinFrequency = tmpl.basePerlinFrequency;
                    baseRegion.gravityModifier = tmpl.baseGravityModifier;
                    baseRegion.seed = tmpl.seed;
                    baseRegion.surfaceBlockId = tmpl.baseSurfaceBlockId;
                    baseRegion.subsurfaceBlockId = tmpl.baseSubsurfaceBlockId;

                    if (inst.isGridAligned && inst.gridX != -1 && inst.gridY != -1) {
                        int radiusTiles = (int)std::max(1.0f, inst.angularRadius * 10.0f);
                        baseRegion.rectMin = glm::ivec2(inst.gridX - radiusTiles, inst.gridY - radiusTiles);
                        baseRegion.rectMax = glm::ivec2(inst.gridX + radiusTiles, inst.gridY + radiusTiles);
                    }
                    planet.regions.push_back(baseRegion);

                    for (const auto& sub : tmpl.subRegions) {
                        fw::MapRegion projectedSub = sub;
                        if (inst.isGridAligned && inst.gridX != -1 && inst.gridY != -1) {
                            projectedSub.rectMin += glm::ivec2(inst.gridX, inst.gridY);
                            projectedSub.rectMax += glm::ivec2(inst.gridX, inst.gridY);
                        }
                        planet.regions.push_back(projectedSub);
                    }
                    break;
                }
            }
        }

    if (context->engine && context->engine->GetRenderManager()) {
        auto* rm = context->engine->GetRenderManager();

        std::vector<fw::MapRegionGPU> gpuRegions;
        gpuRegions.reserve(planet.regions.size());
        int N_latoForRegion = fw::PlanetMath::GetFaceResolution(planet.planetSize);
        for (const auto& r : planet.regions) {
            fw::MapRegionGPU gr{};
            float pitch = glm::radians(r.eulerAngles.x);
            float yaw   = glm::radians(r.eulerAngles.y);
            gr.centerNormal   = glm::vec3(cos(pitch) * cos(yaw), sin(pitch), cos(pitch) * sin(yaw));
            gr.angularRadius  = r.isGridAligned ? 0.0f : r.angularRadius; // 0 = usa il test grid, >0 usa test sferico
            if (r.isGridAligned && N_latoForRegion > 0) {
                // Converti tile coords in UV normalizzato [0, 1] per il compute shader
                float N = (float)N_latoForRegion;
                gr.rectMinMax = glm::vec4(
                    (float)r.rectMin.x / N,
                    (float)r.rectMin.y / N,
                    (float)(r.rectMax.x) / N,
                    (float)(r.rectMax.y) / N
                );
            } else {
                gr.rectMinMax = glm::vec4((float)r.rectMin.x, (float)r.rectMin.y,
                                          (float)r.rectMax.x, (float)r.rectMax.y);
            }
            gr.shapeType      = (uint32_t)r.shape;
            gr.biomeType      = (uint32_t)r.type;
            gr.perlinFreq     = r.perlinFrequency;
            gr.gravityMod     = r.gravityModifier;
            gr.isGridAligned  = r.isGridAligned ? 1u : 0u;
            gr.faceIndex      = (uint32_t)r.faceIndex;
            gr.surfaceBlock   = r.surfaceBlockId;
            gr.subsurfaceBlock = r.subsurfaceBlockId;
            gpuRegions.push_back(gr);
        }

        std::vector<ChunkData> gpuChunks;
        gpuChunks.reserve(planet.chunkInstances.size());
        
        int N_lato = fw::PlanetMath::GetFaceResolution(planet.planetSize);
        float R = fw::PlanetMath::GetPlanetRadius(planet.planetSize);
        
        auto getFacePos = [](int face, float u, float v) -> glm::vec3 {
            switch (face) {
                case 0: return glm::vec3(u, v, 1.0f);
                case 1: return glm::vec3(-u, v, -1.0f);
                case 2: return glm::vec3(1.0f, v, -u);
                case 3: return glm::vec3(-1.0f, v, u);
                case 4: return glm::vec3(u, 1.0f, -v);
                case 5: return glm::vec3(u, -1.0f, v);
            }
            return glm::vec3(0.0f);
        };
        
        for (int i = 0; i < (int)planet.chunkInstances.size(); ++i) {
            const auto& inst = planet.chunkInstances[i];
            if (!inst.isActive) continue;
            
            float u0 = (inst.gridX) / (float)N_lato * 2.0f - 1.0f;
            float u1 = (inst.gridX + 1) / (float)N_lato * 2.0f - 1.0f;
            float v0 = 1.0f - (inst.gridY + 1) / (float)N_lato * 2.0f;
            float v1 = 1.0f - (inst.gridY) / (float)N_lato * 2.0f;
            
            ChunkData cd{};
            cd.p00 = glm::vec4(getFacePos(inst.faceIndex, u0, v0) * R, 0.0f);
            cd.p10 = glm::vec4(getFacePos(inst.faceIndex, u1, v0) * R, 0.0f);
            cd.p01 = glm::vec4(getFacePos(inst.faceIndex, u0, v1) * R, 0.0f);
            cd.p11 = glm::vec4(getFacePos(inst.faceIndex, u1, v1) * R, 0.0f);
            cd.center = glm::vec3(cd.p00 + cd.p10 + cd.p01 + cd.p11) * 0.25f;
            cd.radius = glm::distance(glm::vec3(cd.p00), cd.center);
            cd.chunkID = (uint32_t)i;
            cd._pad0 = 0; cd._pad1 = 0; cd._pad2 = 0;
            gpuChunks.push_back(cd);
        }

        rm->UploadTerrainData(gpuChunks, gpuRegions, R, planet.baseTerrain);
    }
}

void PlanetMapperCompiler::CompileChunk(SharedContext* context, fw::PlanetMap& planet, uint32_t chunkIndex) {
    if (chunkIndex >= planet.chunkInstances.size()) return;
    
    auto* rm = context->engine->GetRenderManager();
    if (!rm) return;
    
    const auto& inst = planet.chunkInstances[chunkIndex];
    if (!inst.isActive) return;
    
    int N_lato = fw::PlanetMath::GetFaceResolution(planet.planetSize);
    float R = fw::PlanetMath::GetPlanetRadius(planet.planetSize);
    
    auto getFacePos = [](int face, float u, float v) -> glm::vec3 {
        switch (face) {
            case 0: return glm::vec3(u, v, 1.0f);
            case 1: return glm::vec3(-u, v, -1.0f);
            case 2: return glm::vec3(1.0f, v, -u);
            case 3: return glm::vec3(-1.0f, v, u);
            case 4: return glm::vec3(u, 1.0f, -v);
            case 5: return glm::vec3(u, -1.0f, v);
        }
        return glm::vec3(0.0f);
    };
    
    float u0 = (inst.gridX) / (float)N_lato * 2.0f - 1.0f;
    float u1 = (inst.gridX + 1) / (float)N_lato * 2.0f - 1.0f;
    float v0 = 1.0f - (inst.gridY + 1) / (float)N_lato * 2.0f;
    float v1 = 1.0f - (inst.gridY) / (float)N_lato * 2.0f;
    
    ChunkData cd{};
    cd.p00 = glm::vec4(getFacePos(inst.faceIndex, u0, v0) * R, 0.0f);
    cd.p10 = glm::vec4(getFacePos(inst.faceIndex, u1, v0) * R, 0.0f);
    cd.p01 = glm::vec4(getFacePos(inst.faceIndex, u0, v1) * R, 0.0f);
    cd.p11 = glm::vec4(getFacePos(inst.faceIndex, u1, v1) * R, 0.0f);
    cd.center = glm::vec3(cd.p00 + cd.p10 + cd.p01 + cd.p11) * 0.25f;
    cd.radius = glm::distance(glm::vec3(cd.p00), cd.center);
    cd.chunkID = chunkIndex;
    cd._pad0 = 0; cd._pad1 = 0; cd._pad2 = 0;
    
    rm->UpdateTerrainChunk(chunkIndex, cd);
}
