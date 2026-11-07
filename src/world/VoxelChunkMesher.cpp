#include "pch.h"

#include "world/VoxelChunkMesher.h"

#include "MaterialRegistry.h"
#include "world/MapWorldGenerator.h"
#include "world/PlanetaryMeshGenerator.h"
#include "world/VoxelFaceUV.h"

#include <cmath>

namespace fw {

VoxelChunkCpuMesh VoxelChunkMesher::Build(
    const VoxelChunkComponent& chunk,
    const std::array<const VoxelChunkComponent*, 9>& neighbors,
    const MaterialRegistry& materials,
    bool isSpherical,
    PlanetSize planetSize) {
    VoxelChunkCpuMesh result;
    result.sourceChunkX = chunk.cx;
    result.sourceChunkZ = chunk.cz;
    result.sourcePlanetCoord = chunk.planetCoord;
    result.vertices.reserve(16384);

    auto getBlock = [&chunk, &neighbors](int x, int y, int z) -> uint8_t {
        if (y < 0 || y >= CHUNK_HEIGHT) return 0;
        if (x >= 0 && x < CHUNK_SIZE && z >= 0 && z < CHUNK_SIZE) {
            return chunk.blocks[x][y][z];
        }

        int nX = 1;
        if (x < 0) { nX = 0; x += CHUNK_SIZE; }
        else if (x >= CHUNK_SIZE) { nX = 2; x -= CHUNK_SIZE; }

        int nZ = 1;
        if (z < 0) { nZ = 0; z += CHUNK_SIZE; }
        else if (z >= CHUNK_SIZE) { nZ = 2; z -= CHUNK_SIZE; }

        const VoxelChunkComponent* targetChunk = neighbors[static_cast<size_t>(nX * 3 + nZ)];
        if (targetChunk) return targetChunk->blocks[x][y][z];
        return 0;
    };

    auto getLight = [&chunk](int x, int y, int z) -> float {
        if (y < 0 || y >= CHUNK_HEIGHT) return 1.0f;
        if (x >= 0 && x < CHUNK_SIZE && z >= 0 && z < CHUNK_SIZE) {
            return chunk.light[x][y][z] / 255.0f;
        }
        return 1.0f;
    };

    auto calcAO = [](bool side1, bool side2, bool corner) -> float {
        if (side1 && side2) return 0.25f;
        return 1.0f - (side1 + side2 + corner) * 0.25f;
    };

    auto shouldDrawFace = [](uint8_t thisBlock, uint8_t neighborBlock) -> bool {
        if (neighborBlock == 0) return true;
        if (thisBlock == neighborBlock) return false;
        if (neighborBlock == 6 || neighborBlock == 8 || neighborBlock == 13) return true; // Acqua, Foglie, Ghiaccio
        return false;
    };

    glm::vec3 chunkPos(0.0f);
    glm::quat chunkRot(1.0f, 0.0f, 0.0f, 0.0f);
    if (isSpherical) {
        MapWorldGenerator::GetSphericalChunkTransform(planetSize, chunk.cx, chunk.cz, chunkPos, chunkRot);
    }

    auto getVertexPos = [&chunk, isSpherical, planetSize, &chunkPos, &chunkRot](float vx, float vy, float vz) -> Vec3 {
        if (chunk.planetCoord.planet.IsValid()) {
            return {vx, vy, vz};
        }
        if (!isSpherical) return {vx, vy, vz};
        glm::vec3 trueWorldPos;
        if (MapWorldGenerator::GetTrueSphericalPosition(
                planetSize, !isSpherical, chunk.cx, chunk.cz, vx, vy, vz, trueWorldPos)) {
            glm::vec3 local = glm::inverse(chunkRot) * (trueWorldPos - chunkPos);
            return {local.x, local.y, local.z};
        }
        return {vx, vy, vz};
    };

    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int y = 0; y < CHUNK_HEIGHT; ++y) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                const uint8_t block = chunk.blocks[x][y][z];
                if (block == 0) continue;
                ++result.solidVoxels;

                const float px = static_cast<float>(x);
                const float py = static_cast<float>(y);
                const float pz = static_cast<float>(z);
                const auto& mat = materials.GetMaterial(block);
                const Vec4 color = {mat.baseColorFallback.x, mat.baseColorFallback.y, mat.baseColorFallback.z, 1.0f};
                const float emissive = mat.emissiveStrength;
                const uint32_t materialID = block;

                // Top (+Y)
                if (shouldDrawFace(block, getBlock(x, y + 1, z))) {
                    const float light = getLight(x, y + 1, z);
                    const float ao00 = calcAO(getBlock(x-1, y+1, z), getBlock(x, y+1, z-1), getBlock(x-1, y+1, z-1));
                    const float ao10 = calcAO(getBlock(x+1, y+1, z), getBlock(x, y+1, z-1), getBlock(x+1, y+1, z-1));
                    const float ao11 = calcAO(getBlock(x+1, y+1, z), getBlock(x, y+1, z+1), getBlock(x+1, y+1, z+1));
                    const float ao01 = calcAO(getBlock(x-1, y+1, z), getBlock(x, y+1, z+1), getBlock(x-1, y+1, z+1));
                    result.vertices.push_back({getVertexPos(px-0.5f, py+0.5f, pz-0.5f), color, VoxelFaceUV(glm::vec3(-0.5f, 0.5f, -0.5f), glm::vec3(0,1,0)), materialID, {0,1,0}, ao00, light, emissive});
                    result.vertices.push_back({getVertexPos(px+0.5f, py+0.5f, pz-0.5f), color, VoxelFaceUV(glm::vec3(0.5f, 0.5f, -0.5f), glm::vec3(0,1,0)), materialID, {0,1,0}, ao10, light, emissive});
                    result.vertices.push_back({getVertexPos(px+0.5f, py+0.5f, pz+0.5f), color, VoxelFaceUV(glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0,1,0)), materialID, {0,1,0}, ao11, light, emissive});
                    result.vertices.push_back({getVertexPos(px-0.5f, py+0.5f, pz-0.5f), color, VoxelFaceUV(glm::vec3(-0.5f, 0.5f, -0.5f), glm::vec3(0,1,0)), materialID, {0,1,0}, ao00, light, emissive});
                    result.vertices.push_back({getVertexPos(px+0.5f, py+0.5f, pz+0.5f), color, VoxelFaceUV(glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0,1,0)), materialID, {0,1,0}, ao11, light, emissive});
                    result.vertices.push_back({getVertexPos(px-0.5f, py+0.5f, pz+0.5f), color, VoxelFaceUV(glm::vec3(-0.5f, 0.5f, 0.5f), glm::vec3(0,1,0)), materialID, {0,1,0}, ao01, light, emissive});
                }
                // Bottom (-Y)
                if (shouldDrawFace(block, getBlock(x, y - 1, z))) {
                    const float light = getLight(x, y - 1, z);
                    result.vertices.push_back({getVertexPos(px-0.5f, py-0.5f, pz+0.5f), color, VoxelFaceUV(glm::vec3(-0.5f, -0.5f, 0.5f), glm::vec3(0,-1,0)), materialID, {0,-1,0}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px+0.5f, py-0.5f, pz+0.5f), color, VoxelFaceUV(glm::vec3(0.5f, -0.5f, 0.5f), glm::vec3(0,-1,0)), materialID, {0,-1,0}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px+0.5f, py-0.5f, pz-0.5f), color, VoxelFaceUV(glm::vec3(0.5f, -0.5f, -0.5f), glm::vec3(0,-1,0)), materialID, {0,-1,0}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px-0.5f, py-0.5f, pz+0.5f), color, VoxelFaceUV(glm::vec3(-0.5f, -0.5f, 0.5f), glm::vec3(0,-1,0)), materialID, {0,-1,0}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px+0.5f, py-0.5f, pz-0.5f), color, VoxelFaceUV(glm::vec3(0.5f, -0.5f, -0.5f), glm::vec3(0,-1,0)), materialID, {0,-1,0}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px-0.5f, py-0.5f, pz-0.5f), color, VoxelFaceUV(glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(0,-1,0)), materialID, {0,-1,0}, 1.0f, light, emissive});
                }
                // Left (-X)
                if (shouldDrawFace(block, getBlock(x - 1, y, z))) {
                    const float light = getLight(x - 1, y, z);
                    result.vertices.push_back({getVertexPos(px-0.5f, py-0.5f, pz-0.5f), color, VoxelFaceUV(glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(-1,0,0)), materialID, {-1,0,0}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px-0.5f, py+0.5f, pz-0.5f), color, VoxelFaceUV(glm::vec3(-0.5f, 0.5f, -0.5f), glm::vec3(-1,0,0)), materialID, {-1,0,0}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px-0.5f, py+0.5f, pz+0.5f), color, VoxelFaceUV(glm::vec3(-0.5f, 0.5f, 0.5f), glm::vec3(-1,0,0)), materialID, {-1,0,0}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px-0.5f, py-0.5f, pz-0.5f), color, VoxelFaceUV(glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(-1,0,0)), materialID, {-1,0,0}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px-0.5f, py+0.5f, pz+0.5f), color, VoxelFaceUV(glm::vec3(-0.5f, 0.5f, 0.5f), glm::vec3(-1,0,0)), materialID, {-1,0,0}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px-0.5f, py-0.5f, pz+0.5f), color, VoxelFaceUV(glm::vec3(-0.5f, -0.5f, 0.5f), glm::vec3(-1,0,0)), materialID, {-1,0,0}, 1.0f, light, emissive});
                }
                // Right (+X)
                if (shouldDrawFace(block, getBlock(x + 1, y, z))) {
                    const float light = getLight(x + 1, y, z);
                    result.vertices.push_back({getVertexPos(px+0.5f, py-0.5f, pz+0.5f), color, VoxelFaceUV(glm::vec3(0.5f, -0.5f, 0.5f), glm::vec3(1,0,0)), materialID, {1,0,0}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px+0.5f, py+0.5f, pz+0.5f), color, VoxelFaceUV(glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1,0,0)), materialID, {1,0,0}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px+0.5f, py+0.5f, pz-0.5f), color, VoxelFaceUV(glm::vec3(0.5f, 0.5f, -0.5f), glm::vec3(1,0,0)), materialID, {1,0,0}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px+0.5f, py-0.5f, pz+0.5f), color, VoxelFaceUV(glm::vec3(0.5f, -0.5f, 0.5f), glm::vec3(1,0,0)), materialID, {1,0,0}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px+0.5f, py+0.5f, pz-0.5f), color, VoxelFaceUV(glm::vec3(0.5f, 0.5f, -0.5f), glm::vec3(1,0,0)), materialID, {1,0,0}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px+0.5f, py-0.5f, pz-0.5f), color, VoxelFaceUV(glm::vec3(0.5f, -0.5f, -0.5f), glm::vec3(1,0,0)), materialID, {1,0,0}, 1.0f, light, emissive});
                }
                // Front (+Z)
                if (shouldDrawFace(block, getBlock(x, y, z + 1))) {
                    const float light = getLight(x, y, z + 1);
                    result.vertices.push_back({getVertexPos(px-0.5f, py-0.5f, pz+0.5f), color, VoxelFaceUV(glm::vec3(-0.5f, -0.5f, 0.5f), glm::vec3(0,0,1)), materialID, {0,0,1}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px+0.5f, py-0.5f, pz+0.5f), color, VoxelFaceUV(glm::vec3(0.5f, -0.5f, 0.5f), glm::vec3(0,0,1)), materialID, {0,0,1}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px+0.5f, py+0.5f, pz+0.5f), color, VoxelFaceUV(glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0,0,1)), materialID, {0,0,1}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px-0.5f, py-0.5f, pz+0.5f), color, VoxelFaceUV(glm::vec3(-0.5f, -0.5f, 0.5f), glm::vec3(0,0,1)), materialID, {0,0,1}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px+0.5f, py+0.5f, pz+0.5f), color, VoxelFaceUV(glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0,0,1)), materialID, {0,0,1}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px-0.5f, py+0.5f, pz+0.5f), color, VoxelFaceUV(glm::vec3(-0.5f, 0.5f, 0.5f), glm::vec3(0,0,1)), materialID, {0,0,1}, 1.0f, light, emissive});
                }
                // Back (-Z)
                if (shouldDrawFace(block, getBlock(x, y, z - 1))) {
                    const float light = getLight(x, y, z - 1);
                    result.vertices.push_back({getVertexPos(px+0.5f, py-0.5f, pz-0.5f), color, VoxelFaceUV(glm::vec3(0.5f, -0.5f, -0.5f), glm::vec3(0,0,-1)), materialID, {0,0,-1}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px-0.5f, py-0.5f, pz-0.5f), color, VoxelFaceUV(glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(0,0,-1)), materialID, {0,0,-1}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px-0.5f, py+0.5f, pz-0.5f), color, VoxelFaceUV(glm::vec3(-0.5f, 0.5f, -0.5f), glm::vec3(0,0,-1)), materialID, {0,0,-1}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px+0.5f, py-0.5f, pz-0.5f), color, VoxelFaceUV(glm::vec3(0.5f, -0.5f, -0.5f), glm::vec3(0,0,-1)), materialID, {0,0,-1}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px-0.5f, py+0.5f, pz-0.5f), color, VoxelFaceUV(glm::vec3(-0.5f, 0.5f, -0.5f), glm::vec3(0,0,-1)), materialID, {0,0,-1}, 1.0f, light, emissive});
                    result.vertices.push_back({getVertexPos(px+0.5f, py+0.5f, pz-0.5f), color, VoxelFaceUV(glm::vec3(0.5f, 0.5f, -0.5f), glm::vec3(0,0,-1)), materialID, {0,0,-1}, 1.0f, light, emissive});
                }
            }
        }
    }

    if (chunk.planetCoord.planet.IsValid()) {
        PlanetaryMeshGenerator::ConvertToPlanetaryPositions(result.vertices, chunk.planetCoord, planetSize);
    }

    // Keep the legacy triangle geometry and per-vertex attributes intact while making
    // the non-indexed stream's winding agree with its outward normals.
    for (size_t i = 0; i + 2 < result.vertices.size(); i += 3) {
        const auto& a = result.vertices[i];
        const auto& b = result.vertices[i + 1];
        const auto& c = result.vertices[i + 2];
        const glm::vec3 pa(a.position.x, a.position.y, a.position.z);
        const glm::vec3 pb(b.position.x, b.position.y, b.position.z);
        const glm::vec3 pc(c.position.x, c.position.y, c.position.z);
        const glm::vec3 na(a.normal.x, a.normal.y, a.normal.z);
        const glm::vec3 nb(b.normal.x, b.normal.y, b.normal.z);
        const glm::vec3 nc(c.normal.x, c.normal.y, c.normal.z);
        const glm::vec3 geometricNormal = glm::cross(pb - pa, pc - pa);
        if (glm::length(geometricNormal) > 1.0e-7f && glm::dot(geometricNormal, na + nb + nc) < 0.0f) {
            std::swap(result.vertices[i + 1], result.vertices[i + 2]);
        }
    }

    result.indices.reserve(result.vertices.size());
    for (size_t index = 0; index < result.vertices.size(); ++index) {
        result.indices.push_back(static_cast<uint32_t>(index));
    }
    return result;
}

} // namespace fw
