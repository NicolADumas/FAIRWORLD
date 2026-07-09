#include "pch.h"
#include "DimensionsManager.h"

namespace fw {

void DimensionsManager::SetBounds(int32_t minX, int32_t maxX, int32_t minZ, int32_t maxZ) {
    m_minX = minX;
    m_maxX = maxX;
    m_minZ = minZ;
    m_maxZ = maxZ;
}

void DimensionsManager::SetChunkMetadata(int32_t cx, int32_t cz, const ChunkMetadata& meta) {
    if (!IsOutOfBounds(cx, cz)) {
        m_chunksGrid[{cx, cz}] = meta;
    }
}

bool DimensionsManager::IsOutOfBounds(int32_t cx, int32_t cz) const {
    return cx < m_minX || cx > m_maxX || cz < m_minZ || cz > m_maxZ;
}

const ChunkMetadata* DimensionsManager::GetChunkMetadata(int32_t cx, int32_t cz) const {
    auto it = m_chunksGrid.find({cx, cz});
    if (it != m_chunksGrid.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace fw
