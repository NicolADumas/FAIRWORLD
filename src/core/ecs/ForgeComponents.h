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

struct ForgeMaterial {
    Vec3  baseColor        = {0.8f, 0.8f, 0.8f};
    float metallic         = 0.0f;
    float roughness        = 0.5f;
    
    // --- Indispensabili ---
    float emissiveStrength = 0.0f;
    Vec3  emissiveColor    = {0.0f, 0.0f, 0.0f};
    float normalIntensity  = 1.0f; // Utile per scalare il dettaglio della Normal Map
    float alphaCutoff      = 0.5f; // Threshold per il discard nello shader
    
    // --- Altamente Raccomandati ---
    float aoStrength         = 1.0f; // Moltiplicatore per l'Ambient Occlusion
    float clearcoat          = 0.0f; // Vernice/Smalto trasparente (0.0 = assente, 1.0 = massima)
    float clearcoatRoughness = 0.0f; // Rugosità del solo strato di smalto
    float transmission       = 0.0f; // Trasparenza PBR (vetro, liquidi)
    float ior                = 1.5f; // Indice di Rifrazione (1.0 aria, 1.33 acqua, 1.5 vetro)
    
    // Texture Index array (-1.0f = usa baseColor)
    float textureIndex = -1.0f;
    
    // --- Fisica (Jolt) ---
    float mass               = 1.0f;  // Massa del materiale in kg (se 0.0f il blocco è statico/cinematico)
    float friction           = 0.5f;  // Attrito (0.0 scivoloso, 1.0 massima aderenza)
    float restitution        = 0.0f;  // Bounciness (Rimbalzo)
    
    // --- Iniezione Minivoxel ---
    uint8_t fallbackBlockId = 1; // Default to Grass/Stone ID per l'iniezione in Fairworld
};

struct ForgeMaterialPalette {
    ForgeMaterial materials[256];
    
    ForgeMaterialPalette() {
        // Inizializza con colori di default per evitare il bianco assoluto
        for (int i = 0; i < 256; ++i) {
            float hue = (float)i / 255.0f;
            // Un semplice gradiente HSV to RGB semplificato per i materiali base
            materials[i].baseColor = {0.8f, 0.8f, 0.8f};
            materials[i].roughness = 0.8f;
        }
    }
};

// Mesh element structures (Trivially Copyable dove possibile)
struct Vertex {
    Vec3 position;    // 12
    Vec4 color;       // 16
    Vec2 roughMetal;  // 8
    float texIndex;   // 4
    Vec3 normal;      // 12
    float ao;         // 4
    float light;      // 4
    float emissive;   // 4
};

struct Face {
    std::vector<int> indices;
    Vec3 faceNormal;
    int matIndex = 0;
};

enum class MeshType { Standard, Chunk, Prefab, Editor };

struct MeshComponent {
    std::string name;
    MeshType type = MeshType::Standard;
    std::vector<Vertex> vertices;
    std::vector<Face> faces;
    VramAllocation vramAlloc;
    float colorOverride[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // RGBA, se A > 0.5f il vertex shader ignora il colore del vertice
    
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
    Obsidian      = 16,
    OutOfBounds = 255,
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
    
    // Indica se i dati procedurali sono già stati generati
    bool isGenerated = false;
};

// Tag Component: indica che il VoxelChunkComponent associato è stato modificato
// e la sua mesh deve essere rigenerata dal Job System.
struct ChunkDirtyComponent {
    bool pendingJob = false; // Se true, un Job è già in esecuzione per questo chunk
    bool needsRebuild = false; // Se true, è stato modificato di nuovo MENTRE il Job era in esecuzione
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

// ─────────────────────────────────────────────────────────────────────────────
// COMPONENTI GIUNTI E CINEMATICA (PHYSICS LAB)
// ─────────────────────────────────────────────────────────────────────────────

enum class JointType { Hinge, Pivot, Universal, BallAndSocket, Fixed };

struct JointComponent {
    entt::entity parentEntity = entt::null;
    entt::entity childEntity = entt::null;
    
    JointType type = JointType::Hinge;
    
    // Matrice di offset locale rispetto al parent
    Mat4 localRestTransform = Mat4::identity();
    
    // Asse di rotazione (es. per Hinge)
    Vec3 axis = {1.0f, 0.0f, 0.0f};
    
    float limitMin = -3.14159f;
    float limitMax = 3.14159f;
};

struct MotorControllerComponent {
    float targetAngle = 0.0f; // Target gestito dall'animazione
    
    // Guadagni PID
    float Kp = 10.0f;
    float Ki = 0.0f;
    float Kd = 1.0f;
    
    float maxTorque = 100.0f; // Coppia massima erogabile
    
    // Stato interno del PID (aggiornato dal Fixed Timestep Loop)
    float integralError = 0.0f;
    float previousError = 0.0f;
};

// Lock-Free Command Queue element (per ImGui -> Physics Thread)
struct SetPidCommand {
    entt::entity entity;
    float Kp;
    float Ki;
    float Kd;
    float maxTorque;
};

} // namespace fw
