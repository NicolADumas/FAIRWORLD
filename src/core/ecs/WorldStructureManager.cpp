#include "pch.h"
#include "WorldStructureManager.h"
#include "ForgeComponents.h"
#include "Components.h"
#include "SharedContext.h"
#include "MaterialRegistry.h"
#include "VramSlabAllocator.h"
#include "VulkanDmaManager.h"
#include "JobSystem.h"
#include <fstream>
#include <iostream>
#include <filesystem>

namespace fw {

struct FWBlockHeader {
    char magic[4] = {'F', 'W', 'B', 'K'};
    uint32_t version = 3; // RLE + Pivot + PlacementMode
    uint32_t voxelCount = 0;
    int32_t pivotX = 0;
    int32_t pivotY = 0;
    int32_t pivotZ = 0;
    uint32_t compressedSize = 0;
    uint8_t placementMode = 0; // 0 = Prefab, 1 = Voxel Injection
};

struct RLEChunk {
    uint8_t count;
    uint8_t blockId;
};

bool WorldStructureManager::SaveStructure(const std::string& name, const std::unordered_map<glm::ivec3, StructureBlock>& blocks, uint8_t placementMode, int pivotX, int pivotY, int pivotZ) {
    if (name.empty()) return false;
    
    std::filesystem::create_directories("assets/blocks");
    std::string path = "assets/blocks/" + name + ".fwblock";
    std::ofstream file(path, std::ios::binary);
    
    if (!file.is_open()) {
        std::cerr << "[WorldStructureManager] Errore salvataggio file: " << path << "\n";
        return false;
    }
    
    FWBlockHeader header;
    header.placementMode = placementMode;
    header.pivotX = pivotX;
    header.pivotY = pivotY;
    header.pivotZ = pivotZ;
    header.voxelCount = (uint32_t)blocks.size();

    if (blocks.empty()) {
        std::cerr << "[WorldStructureManager] Struttura vuota. Salvataggio annullato per: " << name << "\n";
        file.close();
        return false;
    }

    // Compressione RLE
    std::vector<RLEChunk> compressedBlocks;
    uint8_t currentBlock = 0;
    uint8_t currentCount = 0;
    bool first = true;

    for (const auto& pair : blocks) {
        uint8_t block = (uint8_t)pair.second.type;
        if (first) {
            currentBlock = block;
            currentCount = 1;
            first = false;
        } else if (block == currentBlock && currentCount < 255) {
            currentCount++;
        } else {
            compressedBlocks.push_back({currentCount, currentBlock});
            currentBlock = block;
            currentCount = 1;
        }
    }
    if (currentCount > 0) {
        compressedBlocks.push_back({currentCount, currentBlock});
    }

    header.compressedSize = (uint32_t)(compressedBlocks.size() * sizeof(RLEChunk));
    
    file.write(reinterpret_cast<const char*>(&header), sizeof(FWBlockHeader));
    char dummyPalette[256 * 48] = {0};
    file.write(dummyPalette, sizeof(dummyPalette));
    file.write(reinterpret_cast<const char*>(compressedBlocks.data()), header.compressedSize);

    file.close();
    std::cout << "[WorldStructureManager] Struttura salvata con successo in: " << path << " (Voxel: " << header.voxelCount << ", Compresso a " << header.compressedSize << " bytes)\n";
    return true;
}

entt::entity WorldStructureManager::LoadStructureAsPrefab(entt::registry& registry, SharedContext* context, const std::string& name, const fw::Vec3& position) {
    std::string path = "assets/blocks/" + name + ".fwblock";
    if (!std::filesystem::exists(path)) {
        path = name; // Probabilmente è già un path completo
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[WorldStructureManager] Errore caricamento file: " << path << "\n";
        return entt::null;
    }

    FWBlockHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(FWBlockHeader));
    if (header.magic[0] != 'F' || header.magic[1] != 'W' || header.magic[2] != 'B' || header.magic[3] != 'K') {
        std::cerr << "[WorldStructureManager] File non valido o versione non supportata: " << path << "\n";
        return entt::null;
    }

    if (header.voxelCount == 0) {
        std::cerr << "[WorldStructureManager] Attenzione: la struttura (Prefab) " << name << " è vuota (0 voxel). Caricamento interrotto.\n";
        return entt::null;
    }

    char dummyPalette[256 * 48] = {0};
    file.read(dummyPalette, sizeof(dummyPalette));

    auto chunkData = std::make_shared<VoxelChunkComponent>();
    memset(chunkData->blocks, 0, sizeof(chunkData->blocks));
    memset(chunkData->light, 255, sizeof(chunkData->light));

    if (header.compressedSize > 0) {
        std::vector<RLEChunk> compressedBlocks(header.compressedSize / sizeof(RLEChunk));
        file.read(reinterpret_cast<char*>(compressedBlocks.data()), header.compressedSize);

        int blockIndex = 0;
        for (const auto& rle : compressedBlocks) {
            for (int i = 0; i < rle.count; ++i) {
                if (blockIndex < 16 * 128 * 16) {
                    int x = blockIndex / (128 * 16);
                    int y = (blockIndex / 16) % 128;
                    int z = blockIndex % 16;
                    chunkData->blocks[x][y][z] = rle.blockId;
                    blockIndex++;
                }
            }
        }
    }
    file.close();

    entt::entity prefabEntity = registry.create();
    auto& trans = registry.emplace<TransformComponent>(prefabEntity);
    trans.location = position - fw::Vec3{(float)header.pivotX, (float)header.pivotY, (float)header.pivotZ};
    trans.scale = fw::Vec3{0.0625f, 0.0625f, 0.0625f}; // Scale 1/16 per microvoxel

    auto& meta = registry.emplace<MetadataComponent>(prefabEntity);
    meta.name = "Prefab_" + name;

    auto& mesh = registry.emplace<MeshComponent>(prefabEntity);
    mesh.name = "Prefab_" + name;
    mesh.colorOverride[3] = 0.0f;

    registry.emplace<VoxelChunkComponent>(prefabEntity, *chunkData);

    // CALCOLO MASS PROPERTIES PROCEDURALE
    MassPropertiesComponent massProps;
    float totalMass = 0.0f;
    glm::vec3 com(0.0f);

    for (int x = 0; x < 16; ++x) {
        for (int y = 0; y < 128; ++y) {
            for (int z = 0; z < 16; ++z) {
                uint8_t block = chunkData->blocks[x][y][z];
                if (block != 0) {
                    float m = 10.0f;
                    totalMass += m;
                    com += glm::vec3(x, y, z) * m;
                }
            }
        }
    }

    if (totalMass > 0.0f) {
        com /= totalMass;
        massProps.mass = totalMass;
        massProps.centerOfMass = com;

        float Ixx = 0, Iyy = 0, Izz = 0;
        float Ixy = 0, Ixz = 0, Iyz = 0;

        for (int x = 0; x < 16; ++x) {
            for (int y = 0; y < 128; ++y) {
                for (int z = 0; z < 16; ++z) {
                    uint8_t block = chunkData->blocks[x][y][z];
                    if (block != 0) {
                        float m = 10.0f;
                        glm::vec3 r = glm::vec3(x, y, z) - com;

                        Ixx += m * (r.y * r.y + r.z * r.z);
                        Iyy += m * (r.x * r.x + r.z * r.z);
                        Izz += m * (r.x * r.x + r.y * r.y);

                        Ixy -= m * (r.x * r.y);
                        Ixz -= m * (r.x * r.z);
                        Iyz -= m * (r.y * r.z);
                    }
                }
            }
        }

        massProps.inertiaTensor[0] = glm::vec3(Ixx, Ixy, Ixz);
        massProps.inertiaTensor[1] = glm::vec3(Ixy, Iyy, Iyz);
        massProps.inertiaTensor[2] = glm::vec3(Ixz, Iyz, Izz);
    } else {
        massProps.mass = 1.0f;
        massProps.inertiaTensor = glm::mat3(1.0f);
    }

    registry.emplace<MassPropertiesComponent>(prefabEntity, massProps);

    std::cout << "[WorldStructureManager] Struttura " << name << " caricata come Prefab a (" << position.x << ", " << position.y << ", " << position.z << ").\n";
    return prefabEntity;
}

bool WorldStructureManager::SaveStructureJSON(const std::string& name, const std::unordered_map<glm::ivec3, StructureBlock>& blocks, uint8_t placementMode, int pivotX, int pivotY, int pivotZ) {
    if (name.empty()) return false;

    std::filesystem::create_directories("assets/blocks");
    std::string path = "assets/blocks/" + name + ".json";
    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "{\n  \"name\": \"" << name << "\",\n";
    file << "  \"placementMode\": " << (int)placementMode << ",\n";
    file << "  \"pivot\": [" << pivotX << ", " << pivotY << ", " << pivotZ << "],\n";
    file << "  \"voxelCount\": " << blocks.size() << "\n}\n";
    file.close();
    return true;
}

} // namespace fw
