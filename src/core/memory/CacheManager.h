#pragma once
#include <iostream>
#include <cstdint>

struct SharedContext;

namespace fw {

enum class CachePolicy {
    FullFlush,         // Clear GPU chunk render buffers, reset staging ring buffers, flush DMA queues
    IsolatedPreview,   // Safely detach main world, clear GPU render buffers, attach preview world
    HotReloadMaterial, // Re-upload Vulkan GPU descriptor/texture arrays for a specific block ID
    KeepState          // No cache invalidation needed
};

class CacheManager {
public:
    CacheManager() = default;
    ~CacheManager() = default;

    void Initialize(SharedContext* context);

    // Core cache flush operations
    void FlushGpuRenderCaches(SharedContext* context);
    void FlushCpuTransientCaches(SharedContext* context);
    void SyncMaterialGpuCache(uint8_t blockId, SharedContext* context);
};

} // namespace fw
