#pragma once
#include <entt/entt.hpp>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <filesystem>
#include <iostream>
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
        std::cout << "[WorldChunkManager] Tentativo di eliminazione cache disco in: " << m_saveDir << "\n";
        try {
            if (std::filesystem::exists(m_saveDir)) {
                int count = 0;
                for (const auto& entry : std::filesystem::directory_iterator(m_saveDir)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".bin") {
                        std::error_code ec;
                        if (std::filesystem::remove(entry.path(), ec)) {
                            count++;
                        } else {
                            std::cerr << "[WorldChunkManager] Impossibile eliminare " << entry.path() << ": " << ec.message() << "\n";
                        }
                    }
                }
                std::cout << "[WorldChunkManager] Cache pulita! " << count << " file .bin eliminati con successo.\n";
            } else {
                std::cout << "[WorldChunkManager] Nessuna cache trovata in " << m_saveDir << ".\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "[WorldChunkManager] ERRORE durante la pulizia della cache: " << e.what() << "\n";
        }
    }

private:
    std::string m_saveDir = "saves/world";
    std::unordered_map<uint64_t, entt::entity> m_activeChunks;
};

} // namespace fw
