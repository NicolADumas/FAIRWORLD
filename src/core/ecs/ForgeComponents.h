#pragma once
#include "ForgeMath.h"
#include <string>
#include <vector>
#include "VramSlabAllocator.h"

namespace fw {

// ─────────────────────────────────────────────────────────────────────────────
// COMPONENTI ECS BASE PER FORGE
// ─────────────────────────────────────────────────────────────────────────────

struct TransformComponent {
    Vec3 location = {0.0f, 0.0f, 0.0f};
    Vec3 rotation = {0.0f, 0.0f, 0.0f}; // Euler XYZ radians
    Vec3 scale    = {1.0f, 1.0f, 1.0f};

    Mat4 worldMatrix() const {
        return Mat4::translate(location)
             * Mat4::rotateZ(rotation.z)
             * Mat4::rotateY(rotation.y)
             * Mat4::rotateX(rotation.x)
             * Mat4::scale(scale);
    }
};

struct PBRMaterialComponent {
    Vec3  baseColor        = {0.8f, 0.8f, 0.8f};
    float metallic         = 0.0f;
    float roughness        = 0.5f;
    float ao               = 1.0f;
    float emissiveStrength = 0.0f;
    Vec3  emissiveColor    = {0.0f, 0.0f, 0.0f};
    
    // Shader reference / Texture handles (to be integrated with Vulkan/Assets)
    std::string albedoMap;
    std::string normalMap;
};

// Mesh element structures (Trivially Copyable dove possibile)
struct Vertex {
    Vec3 position;
    Vec3 color;
    Vec2 uv;
    float texIndex;
    Vec3 normal;
    float ao;     // Ambient Occlusion value (0.0 to 1.0)
    float light;  // Light value (0.0 to 1.0)
};

struct Face {
    std::vector<int> indices;
    Vec3 faceNormal;
    int matIndex = 0;
};

struct MeshComponent {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<Face> faces;
    VramAllocation vramAlloc;
    
    AABB bounds() const {
        AABB bb;
        for(const auto& v : vertices) {
            bb.expand(v.position);
        }
        return bb;
    }
};

struct LightComponent {
    enum Type { POINT, SUN, SPOT, AREA } type = POINT;
    Vec3  color = {1.0f, 1.0f, 1.0f};
    float power = 10.0f;
    float radius = 0.0f;
    bool  castShadow = true;
};

struct CameraComponent {
    float fov = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    bool isActive = false;
};

struct MetadataComponent {
    std::string name;
    bool visible = true;
    bool selected = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// COMPONENTI VOXEL/CHUNK
// ─────────────────────────────────────────────────────────────────────────────

// Tipi di blocco legacy adattati a ForgeWorld
enum class BlockType : uint8_t {
    Air         = 0,
    Grass       = 1,
    Dirt        = 2,
    Stone       = 3,
    Wood        = 4,
    Sand        = 5,
    Water       = 6,
    Lava        = 7,
    Leaves      = 8,
    MobSpawner  = 9,
    LightSource = 10,
    Mushroom    = 11,
    Ore         = 12,
    Ice         = 13,
    StargateFrame = 14,
    StargatePortal = 15,
};

static constexpr int CHUNK_SIZE = 16;
static constexpr int CHUNK_HEIGHT = 128;

struct VoxelChunkComponent {
    // Coordinate del chunk (es. cx=0, cz=0)
    int cx = 0;
    int cz = 0;
    
    // Array tridimensionale dei blocchi
    // L'allocazione di questa struct verrà gestita in futuro dal PoolAllocator in O(1)
    uint8_t blocks[CHUNK_SIZE][CHUNK_HEIGHT][CHUNK_SIZE] = {};
    
    // Array tridimensionale dell'illuminazione (es. 4 bit luce solare, 4 bit luce locale, per ora usiamo 8 bit semplici)
    uint8_t light[CHUNK_SIZE][CHUNK_HEIGHT][CHUNK_SIZE] = {};
    
    // Dati ingegneristici aggregati
    double temperature = 293.15; // Kelvin
    double internalEnergy = 0.0; // Joule
    double heatCapacity = 0.0;   // J/K
};

// Tag Component: indica che il VoxelChunkComponent associato è stato modificato
// e la sua mesh deve essere rigenerata dal Job System.
struct ChunkDirtyComponent {
    bool pendingJob = false; // Se true, un Job è già in esecuzione per questo chunk
};

// ─────────────────────────────────────────────────────────────────────────────
// COMPONENTI PORTALI E STREAMING NON-EUCLIDEO
// ─────────────────────────────────────────────────────────────────────────────

struct VolumeComponent {
    float radius = 100.0f; // Raggio d'azione (es. distanza in cui innesca il caricamento)
};

struct PortalComponent {
    entt::entity targetPortal = entt::null; // Entità dell'altro lato del portale
    uint32_t targetWorldID = 0;             // ID del mondo/dimensione di destinazione
    
    // Matrice calcolata M_teleport = target.worldMatrix * inverse(this.worldMatrix)
    Mat4 mTeleport = Mat4::identity();
    bool isActive = true;
};

} // namespace fw
