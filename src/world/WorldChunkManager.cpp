#include "pch.h"
#include "WorldChunkManager.h"
#include <fstream>
#include <filesystem>
#include <iostream>

namespace fw {

struct ChunkFileHeader {
    char magic[4] = {'F', 'W', 'C', 'K'};
    uint32_t version = 1;
    int32_t cx = 0;
    int32_t cz = 0;
    uint32_t sizeX = 16;
    uint32_t sizeY = 128;
    uint32_t sizeZ = 16;
};

bool WorldChunkManager::SaveChunk(int cx, int cz, const VoxelChunkComponent& chunkData) const {
    try {
        std::filesystem::create_directories(m_saveDir);
        std::string filename = m_saveDir + "/chunk_" + std::to_string(cx) + "_" + std::to_string(cz) + ".bin";
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) return false;

        ChunkFileHeader header;
        header.cx = cx;
        header.cz = cz;

        file.write(reinterpret_cast<const char*>(&header), sizeof(ChunkFileHeader));
        file.write(reinterpret_cast<const char*>(chunkData.blocks), sizeof(chunkData.blocks));
        file.write(reinterpret_cast<const char*>(chunkData.light), sizeof(chunkData.light));
        file.close();
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[WorldChunkManager] Errore salvataggio chunk (" << cx << ", " << cz << "): " << e.what() << "\n";
        return false;
    }
}

bool WorldChunkManager::LoadChunk(int cx, int cz, VoxelChunkComponent& chunkData) const {
    std::string filename = m_saveDir + "/chunk_" + std::to_string(cx) + "_" + std::to_string(cz) + ".bin";
    if (!std::filesystem::exists(filename)) return false;

    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;

    ChunkFileHeader header;
    if (!file.read(reinterpret_cast<char*>(&header), sizeof(ChunkFileHeader))) return false;
    
    // Validazione Header per struttura solida e compatibile
    if (header.magic[0] != 'F' || header.magic[1] != 'W' || 
        header.magic[2] != 'C' || header.magic[3] != 'K') {
        std::cerr << "[WorldChunkManager] File chunk obsoleto o corrotto (magic errato): " << filename << "\n";
        return false; // Il sistema lo rigenererà da zero
    }
    
    if (header.version != 1 || header.sizeX != 16 || header.sizeY != 128 || header.sizeZ != 16) {
        std::cerr << "[WorldChunkManager] Versione o dimensioni chunk incompatibili: " << filename << "\n";
        return false;
    }

    file.read(reinterpret_cast<char*>(chunkData.blocks), sizeof(chunkData.blocks));
    file.read(reinterpret_cast<char*>(chunkData.light), sizeof(chunkData.light));
    file.close();
    
    chunkData.cx = cx;
    chunkData.cz = cz;
    chunkData.isGenerated = true;
    return true;
}

void WorldChunkManager::SaveAllChunks(entt::registry& registry) const {
    auto view = registry.view<VoxelChunkComponent>();
    int savedCount = 0;
    for (auto entity : view) {
        const auto& chunk = view.get<VoxelChunkComponent>(entity);
        if (chunk.isGenerated) {
            SaveChunk(chunk.cx, chunk.cz, chunk);
            savedCount++;
        }
    }
    std::cout << "[WorldChunkManager] Salvati " << savedCount << " chunk su disco (" << m_saveDir << ").\n";
}

} // namespace fw
