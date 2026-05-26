#include "pch.h"
#include "World.h"
#include "AIAssistant.h"
#include "PerlinNoise.h"

// Offset per le 6 facce di un cubo (in ordine: +Y, -Y, -X, +X, -Z, +Z)
static const glm::vec3 FACE_VERTS[6][4] = {
    {{0,1,0},{1,1,0},{1,1,1},{0,1,1}}, // Top
    {{0,0,1},{1,0,1},{1,0,0},{0,0,0}}, // Bottom
    {{0,0,1},{0,1,1},{0,1,0},{0,0,0}}, // Left
    {{1,0,0},{1,1,0},{1,1,1},{1,0,1}}, // Right
    {{0,0,0},{0,1,0},{1,1,0},{1,0,0}}, // Front
    {{1,0,1},{1,1,1},{0,1,1},{0,0,1}}, // Back
};

// Direzione del vicino per ogni faccia
static const glm::ivec3 FACE_DIRS[6] = {
    { 0, 1, 0}, { 0,-1, 0}, {-1, 0, 0},
    { 1, 0, 0}, { 0, 0,-1}, { 0, 0, 1},
};

World::World() {
}

void World::InitWorld() {
    m_chunks.clear();
    // Genera 16x16 chunk (da -8 a +7 in X e Z)
    for (int cx = -8; cx < 8; cx++) {
        for (int cz = -8; cz < 8; cz++) {
            auto chunk = std::make_unique<Chunk>(cx, cz);
            GenerateChunk(chunk.get());
            ChunkCoord coord{cx, cz};
            m_chunks[coord] = std::move(chunk);
        }
    }
}

void World::GenerateChunk(Chunk* chunk) {
    PerlinNoise pn(12345); // Seed hardcoded per ora
    
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            double gx = chunk->cx * CHUNK_SIZE + x;
            double gz = chunk->cz * CHUNK_SIZE + z;
            
            // Rumore combinato per colline morbide
            double n = pn.noise(gx * 0.03, 0, gz * 0.03);
            double n2 = pn.noise(gx * 0.1, 0, gz * 0.1) * 0.5;
            int height = 20 + (int)((n + n2) * 12);
            
            for (int y = 0; y < CHUNK_HEIGHT; y++) {
                if (y < height - 4) {
                    chunk->blocks[x][y][z] = (uint8_t)BlockType::Stone;
                } else if (y < height) {
                    chunk->blocks[x][y][z] = (uint8_t)BlockType::Dirt;
                } else if (y == height) {
                    if (y < 16) chunk->blocks[x][y][z] = (uint8_t)BlockType::Sand;
                    else chunk->blocks[x][y][z] = (uint8_t)BlockType::Grass;
                } else if (y < 16) {
                    chunk->blocks[x][y][z] = (uint8_t)BlockType::Water;
                } else {
                    chunk->blocks[x][y][z] = (uint8_t)BlockType::Air;
                }
            }
        }
    }
}

Chunk* World::GetChunk(int cx, int cz) {
    ChunkCoord coord{cx, cz};
    auto it = m_chunks.find(coord);
    if (it != m_chunks.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool World::IsInBounds(int x, int y, int z) const {
    if (y < 0 || y >= CHUNK_HEIGHT) return false;
    int cx = (int)floor((float)x / CHUNK_SIZE);
    int cz = (int)floor((float)z / CHUNK_SIZE);
    ChunkCoord coord{cx, cz};
    return m_chunks.find(coord) != m_chunks.end();
}

BlockType World::GetBlock(int x, int y, int z) const {
    if (y < 0 || y >= CHUNK_HEIGHT) return BlockType::Air;
    int cx = (int)floor((float)x / CHUNK_SIZE);
    int cz = (int)floor((float)z / CHUNK_SIZE);
    ChunkCoord coord{cx, cz};
    
    auto it = m_chunks.find(coord);
    if (it == m_chunks.end()) return BlockType::Air;
    
    int lx = x - cx * CHUNK_SIZE;
    int lz = z - cz * CHUNK_SIZE;
    return (BlockType)it->second->blocks[lx][y][lz];
}

void World::SetBlock(int x, int y, int z, BlockType type) {
    if (y < 0 || y >= CHUNK_HEIGHT) return;
    int cx = (int)floor((float)x / CHUNK_SIZE);
    int cz = (int)floor((float)z / CHUNK_SIZE);
    ChunkCoord coord{cx, cz};
    
    auto it = m_chunks.find(coord);
    if (it != m_chunks.end()) {
        int lx = x - cx * CHUNK_SIZE;
        int lz = z - cz * CHUNK_SIZE;
        if (it->second->blocks[lx][y][lz] != (uint8_t)type) {
            it->second->blocks[lx][y][lz] = (uint8_t)type;
            it->second->isDirty = true;
            
            // Mark neighboring chunks as dirty if placed on chunk border
            if (lx == 0) { auto n = GetChunk(cx - 1, cz); if (n) n->isDirty = true; }
            if (lx == CHUNK_SIZE - 1) { auto n = GetChunk(cx + 1, cz); if (n) n->isDirty = true; }
            if (lz == 0) { auto n = GetChunk(cx, cz - 1); if (n) n->isDirty = true; }
            if (lz == CHUNK_SIZE - 1) { auto n = GetChunk(cx, cz + 1); if (n) n->isDirty = true; }
        }
    }
}

constexpr glm::vec3 World::BlockColor(BlockType t) {
    switch (t) {
        case BlockType::Grass:       return {0.10f, 0.85f, 0.30f};
        case BlockType::Dirt:        return {0.55f, 0.35f, 0.15f};
        case BlockType::Stone:       return {0.50f, 0.50f, 0.50f};
        case BlockType::Wood:        return {0.58f, 0.40f, 0.20f};
        case BlockType::Sand:        return {0.85f, 0.80f, 0.55f};
        case BlockType::Water:       return {0.10f, 0.40f, 0.90f};
        case BlockType::Lava:        return {0.95f, 0.35f, 0.05f};
        case BlockType::Leaves:      return {0.15f, 0.45f, 0.10f};
        case BlockType::MobSpawner:  return {0.45f, 0.10f, 0.70f};
        case BlockType::LightSource: return {1.00f, 0.85f, 0.20f};
        default:                     return {1.0f,  0.0f,  1.0f};
    }
}

void World::AddFace(int x, int y, int z, int face, const glm::vec3& color, float texIndex, std::vector<Vertex>& outVerts, std::vector<uint32_t>& outIndices, bool lowerY) {
    uint32_t baseIndex = (uint32_t)outVerts.size();
    glm::vec3 origin((float)x, (float)y, (float)z);
    const glm::vec2 UVS[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

    for (int i = 0; i < 4; i++) {
        glm::vec3 pos = origin + FACE_VERTS[face][i];
        if (lowerY && pos.y == origin.y + 1) {
            pos.y -= 0.15f;
        }
        outVerts.push_back({ pos, color, UVS[i], texIndex });
    }

    outIndices.push_back(baseIndex + 0);
    outIndices.push_back(baseIndex + 1);
    outIndices.push_back(baseIndex + 2);
    outIndices.push_back(baseIndex + 0);
    outIndices.push_back(baseIndex + 2);
    outIndices.push_back(baseIndex + 3);
}

std::vector<ChunkCoord> World::BuildDirtyChunks() {
    std::vector<ChunkCoord> updated;
    
    for (auto& pair : m_chunks) {
        Chunk* chunk = pair.second.get();
        if (!chunk->isDirty) continue;
        
        chunk->vertices.clear();
        chunk->indices.clear();
        
        int cx = chunk->cx;
        int cz = chunk->cz;
        
        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            for (int y = 0; y < CHUNK_HEIGHT; y++) {
                for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                    BlockType type = (BlockType)chunk->blocks[lx][y][lz];
                    if (type == BlockType::Air) continue;

                    int gx = cx * CHUNK_SIZE + lx;
                    int gz = cz * CHUNK_SIZE + lz;
                    glm::vec3 color = BlockColor(type);

                    for (int face = 0; face < 6; face++) {
                        glm::ivec3 neighbor = glm::ivec3(gx, y, gz) + FACE_DIRS[face];
                        BlockType neighType = GetBlock(neighbor.x, neighbor.y, neighbor.z);
                        
                        bool drawFace = false;
                        if (neighType == BlockType::Air) drawFace = true;
                        else if (type != BlockType::Water && type != BlockType::Lava && (neighType == BlockType::Water || neighType == BlockType::Lava)) drawFace = true;

                        if (drawFace) {
                            glm::vec3 faceColor = color;
                            if (face >= 2) faceColor *= 0.85f;
                            if (face == 1) faceColor *= 0.70f;
                            
                            bool isLiquid = (type == BlockType::Water || type == BlockType::Lava);
                            bool lowerY = false;
                            if (isLiquid && face == 0) { 
                                BlockType above = GetBlock(gx, y + 1, gz);
                                if (above != BlockType::Water && above != BlockType::Lava) {
                                    lowerY = true;
                                }
                            }

                            AddFace(gx, y, gz, face, faceColor, (float)type, chunk->vertices, chunk->indices, lowerY);
                        }
                    }
                }
            }
        }
        
        chunk->isDirty = false;
        chunk->isMeshEmpty = chunk->indices.empty();
        updated.push_back(pair.first);
    }
    
    return updated;
}

void World::BuildGhostMesh(const std::vector<GhostBlock>& ghosts) {
    m_ghostVertices.clear();
    m_ghostIndices.clear();
    glm::vec3 ghostColor(0.2f, 0.8f, 1.0f); 

    for (const auto& ghost : ghosts) {
        int x = ghost.pos.x;
        int y = ghost.pos.y;
        int z = ghost.pos.z;

        for (int face = 0; face < 6; face++) {
            glm::ivec3 neighbor = glm::ivec3(x, y, z) + FACE_DIRS[face];
            if (GetBlock(neighbor.x, neighbor.y, neighbor.z) == BlockType::Air) {
                AddFace(x, y, z, face, ghostColor, (float)ghost.type, m_ghostVertices, m_ghostIndices, false);
            }
        }
    }
}
