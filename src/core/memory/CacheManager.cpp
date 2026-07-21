#include "pch.h"
#include "CacheManager.h"
#include "SharedContext.h"
#include "RenderManager.h"
#include "FAIRWORLD.h"
#include "ForgeWorld.h"
#include "MaterialRegistry.h"
#include <iostream>

namespace fw {

void CacheManager::Initialize(SharedContext* context) {
    std::cout << "[CacheManager] Inizializzazione Gestore Cache CPU & GPU...\n";
}

void CacheManager::FlushGpuRenderCaches(SharedContext* context) {
    if (!context) return;
    if (context->engine && context->engine->GetRenderManager()) {
        std::cout << "[CacheManager] Invalidation cache GPU (Vulkan Chunk Buffers & Staging Ring)...\n";
        context->engine->GetRenderManager()->InvalidateForgeCache();
    }
}

void CacheManager::FlushCpuTransientCaches(SharedContext* context) {
    if (!context) return;
    std::cout << "[CacheManager] Invalidation cache CPU (Deferred Meshes & Arenas)...\n";
}

void CacheManager::SyncMaterialGpuCache(uint8_t blockId, SharedContext* context) {
    if (!context) return;
    std::cout << "[CacheManager] Sincronizzazione GPU Material Cache per BlockID " << (int)blockId << "...\n";
    if (context->engine && context->engine->GetRenderManager() && context->materialRegistry) {
        const auto& mat = context->materialRegistry->GetMaterial(blockId);
        if (!mat.albedoPath.empty()) {
            context->engine->GetRenderManager()->LoadPBRTextureFromFile(mat.albedoPath, blockId, RenderManager::PBRTextureType::ALBEDO);
        }
        if (!mat.normalPath.empty()) {
            context->engine->GetRenderManager()->LoadPBRTextureFromFile(mat.normalPath, blockId, RenderManager::PBRTextureType::NORMAL);
        }
        if (!mat.ormPath.empty()) {
            context->engine->GetRenderManager()->LoadPBRTextureFromFile(mat.ormPath, blockId, RenderManager::PBRTextureType::ORM);
        }
        context->engine->GetRenderManager()->InvalidateForgeCache();
    }
}

} // namespace fw
