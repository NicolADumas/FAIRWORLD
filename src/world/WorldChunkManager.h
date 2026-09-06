#pragma once
#include <entt/entt.hpp>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <filesystem>
#include "ForgeComponents.h"

namespace fw {

class WorldChunkManager {
public:
    WorldChunkManager() = default;
    ~WorldChunkManager() = default;

    void SetSaveDirectory(const std::string& dir) { m_saveDir = dir; }
    const std::string& GetSaveDirectory() const { return m_saveDir; }

    bool SaveChunk(int cx, int cz, const VoxelChunkComponent& chunkData) const;
    bool LoadChunk(int cx, int cz, VoxelChunkComponent& chunkData) const;
    void SaveAllChunks(entt::registry& registry) const;

    void RegisterChunkEntity(int cx, int cz, entt::entity entity) {
        uint64_t key = (static_cast<uint64_t>(cx) << 32) | (static_cast<uint32_t>(cz));
        m_activeChunks[key] = entity;
    }

    entt::entity GetChunkEntity(int cx, int cz) const {
        uint64_t key = (static_cast<uint64_t>(cx) << 32) | (static_cast<uint32_t>(cz));
        auto it = m_activeChunks.find(key);
        if (it != m_activeChunks.end()) return it->second;
        return entt::null;
    }

    void Clear() {
        m_activeChunks.clear();
    }

    void ClearDiskCache() const {
        try {
            if (std::filesystem::exists(m_saveDir)) {
                std::filesystem::remove_all(m_saveDir);
                std::filesystem::create_directories(m_saveDir);
            }
        } catch (...) {
            // Ignora eventuali errori di file system (es. permessi)
        }
    }

private:
    std::string m_saveDir = "saves/world";
    std::unordered_map<uint64_t, entt::entity> m_activeChunks;
};

} // namespace fw
