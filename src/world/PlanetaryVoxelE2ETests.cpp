#include "pch.h"

#include "BlockRegistry.h"
#include "MaterialRegistry.h"
#include "components/BiomeComponents.h"
#include "components/ForgeComponents.h"
#include "components/PlanetComponents.h"
#include "core/app/SharedContext.h"
#include "core/utils/PlanetMath.h"
#include "hardware/DeviceManager.h"
#include "systems/PhysicsEngine.h"
#include "systems/RaycastSystem.h"
#include "systems/Systems.h"
#include "systems/TerrainSolverSystem.h"
#include "world/CubeSphereMapping.h"
#include "world/GameWorld.h"
#include "world/MapDocument.h"
#include "world/MapWorldGenerator.h"
#include "world/PlanetGridCompatibility.h"
#include "world/PlanetaryChunkNeighborhood.h"
#include "world/PlanetRadialMapping.h"
#include "world/PlanetaryMeshGenerator.h"
#include "world/PlanetarySpawnPolicy.h"
#include "world/TerrainSolver.h"
#include "world/terrain/validation/TerrainValidation.h"
#include "world/noise/Noise.h"
#include "systems/PlanetaryCameraFrame.h"
#include "systems/PlanetaryMovementMath.h"
#include <cstdlib>
#include "world/VoxelChunkMesher.h"

#include <cmath>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace {

bool IsFinite(const glm::vec2& value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool TestUnifiedPlanetRadialMapping(size_t& checkedCells, float& maxRoundTripError) {
    constexpr float planetRadius = 128.0f;
    constexpr float tolerance = 2.0e-4f;
    checkedCells = 0;
    maxRoundTripError = 0.0f;
    bool pass = true;

    // Forward/inverse layer mapping, including negative layers and sub-cell values.
    for (int layer = -2; layer <= 2; ++layer) {
        for (float localY : {0.0f, 0.5f, 63.5f, 64.0f, 127.5f}) {
            const float g = fw::PlanetRadialMapping::GetGlobalRadialOffset(layer, localY);
            const float radius = fw::PlanetRadialMapping::GetRadialDistance(g, planetRadius);
            const float inverseG = fw::PlanetRadialMapping::GetGlobalRadialOffsetFromDistance(
                radius, planetRadius);
            int inverseLayer = 0;
            float inverseLocalY = 0.0f;
            fw::PlanetRadialMapping::GetLayerAndLocalY(inverseG, inverseLayer, inverseLocalY);
            const float error = std::max(std::abs(inverseG - g),
                std::abs(static_cast<float>(inverseLayer - layer)) + std::abs(inverseLocalY - localY));
            maxRoundTripError = std::max(maxRoundTripError, error);
            pass = pass && inverseLayer == layer && std::abs(inverseLocalY - localY) <= tolerance &&
                std::abs(inverseG - g) <= tolerance;
        }
    }

    // A voxel's integer localY is its center. Verify every cell center, every
    // upper face, and the immediately representable samples around each face.
    for (int localY = 0; localY < fw::ChunkDimensions::VoxelsY; ++localY) {
        const float centerRadius = fw::PlanetRadialMapping::GetCellCenterRadius(0, localY, planetRadius);
        const float centerCoord = fw::PlanetRadialMapping::GetVoxelCellCoordinateFromDistance(
            centerRadius, planetRadius);
        const float outerRadius = fw::PlanetRadialMapping::GetCellOuterRadius(0, localY, planetRadius);
        const float belowRadius = std::nextafter(outerRadius, -std::numeric_limits<float>::infinity());
        const float aboveRadius = std::nextafter(outerRadius, std::numeric_limits<float>::infinity());
        const float belowCoord = fw::PlanetRadialMapping::GetVoxelCellCoordinateFromDistance(
            belowRadius, planetRadius);
        const float boundaryCoord = fw::PlanetRadialMapping::GetVoxelCellCoordinateFromDistance(
            outerRadius, planetRadius);
        const float aboveCoord = fw::PlanetRadialMapping::GetVoxelCellCoordinateFromDistance(
            aboveRadius, planetRadius);

        pass = pass && std::floor(centerCoord) == static_cast<float>(localY) &&
            std::floor(belowCoord) == static_cast<float>(localY) &&
            std::floor(boundaryCoord) == static_cast<float>(localY + 1) &&
            std::floor(aboveCoord) == static_cast<float>(localY + 1) &&
            std::abs(centerCoord - (static_cast<float>(localY) + 0.5f)) <= tolerance &&
            std::abs(outerRadius - centerRadius - 0.5f * fw::ChunkDimensions::VoxelWorldSize) <= tolerance;
        ++checkedCells;
    }

    const float firstInnerFace = fw::PlanetRadialMapping::GetRadialDistance(
        fw::PlanetRadialMapping::GetGlobalRadialOffset(0, -0.5f), planetRadius);
    const float lastOuterFace = fw::PlanetRadialMapping::GetCellOuterRadius(
        0, fw::ChunkDimensions::VoxelsY - 1, planetRadius);
    pass = pass && std::floor(fw::PlanetRadialMapping::GetVoxelCellCoordinateFromDistance(
        firstInnerFace, planetRadius)) == 0.0f &&
        std::floor(fw::PlanetRadialMapping::GetVoxelCellCoordinateFromDistance(
            lastOuterFace, planetRadius)) == static_cast<float>(fw::ChunkDimensions::VoxelsY);

    return pass && checkedCells == static_cast<size_t>(fw::ChunkDimensions::VoxelsY);
}

struct ColliderVolumeAudit {
    size_t solidSamples = 0;
    size_t pendingSamples = 0;
    size_t outOfBoundsSamples = 0;
    float maximumRadialPenetration = 0.0f;
    glm::ivec3 deepestVoxel{-1};
    glm::vec3 deepestSamplePosition{0.0f};
    float deepestHeightOffset = 0.0f;
    float deepestRingScale = 0.0f;
    int deepestAngleIndex = -1;
};

ColliderVolumeAudit AuditDensePlanetaryCollider(
    const fw::GameWorld& world, const RigidBody& body, fw::PlanetSize planetSize) {
    ColliderVolumeAudit audit;
    glm::vec3 up;
    if (!fw::PlanetaryCameraFrame::Normalize(body.position, up)) return audit;

    fw::PlanetaryCameraFrame::State frame{
        body.planetaryCollisionFrameUp,
        body.planetaryCollisionFrameForward,
        body.planetaryCollisionFrameInitialized};
    if (!fw::PlanetaryCameraFrame::TransportTo(frame, up)) return audit;
    glm::vec3 right;
    if (!fw::PlanetaryCameraFrame::Normalize(glm::cross(frame.forward, up), right)) return audit;

    const float radius = fw::PlanetMath::GetPlanetRadius(planetSize);
    constexpr int angularSamples = 16;
    const float radialRings[] = {0.0f, 0.5f, 0.95f};
    const int heightSteps = static_cast<int>(std::ceil(body.height / 0.1f));
    for (int iy = 0; iy <= heightSteps; ++iy) {
        const float yOffset = body.height * static_cast<float>(iy) / static_cast<float>(heightSteps);
        for (float ringScale : radialRings) {
            const float ringRadius = body.radius * ringScale;
            const int samples = ringScale == 0.0f ? 1 : angularSamples;
            for (int angleIndex = 0; angleIndex < samples; ++angleIndex) {
                const float angle = 6.28318530718f * static_cast<float>(angleIndex) /
                    static_cast<float>(angularSamples);
                const glm::vec3 samplePosition = body.position + up * yOffset +
                    right * (ringRadius * std::cos(angle)) +
                    frame.forward * (ringRadius * std::sin(angle));
                const fw::WorldVoxelQueryResult query = world.QueryBlockCartesian(samplePosition);
                if (query.status == fw::WorldVoxelQueryStatus::SolidVoxel) {
                    ++audit.solidSamples;
                    const float sampleRadius = glm::length(samplePosition);
                    const float outerRadius = fw::PlanetRadialMapping::GetCellOuterRadius(
                        0, query.voxelCoord.y, radius);
                    const float penetration = std::max(0.0f, outerRadius - sampleRadius);
                    if (penetration > audit.maximumRadialPenetration) {
                        audit.maximumRadialPenetration = penetration;
                        audit.deepestVoxel = query.voxelCoord;
                        audit.deepestSamplePosition = samplePosition;
                        audit.deepestHeightOffset = yOffset;
                        audit.deepestRingScale = ringScale;
                        audit.deepestAngleIndex = angleIndex;
                    }
                } else if (query.status == fw::WorldVoxelQueryStatus::TerrainPending) {
                    ++audit.pendingSamples;
                } else if (query.status == fw::WorldVoxelQueryStatus::OutOfBounds) {
                    ++audit.outOfBoundsSamples;
                }
            }
        }
    }
    return audit;
}

bool IsSolidOnProductionCollisionProbeSet(
    const fw::GameWorld& world, const RigidBody& body, const glm::vec3& localUp,
    const glm::vec3& localForward, const glm::vec3& localRight) {
    const float r = body.radius;
    const float ySteps[] = {0.0f, 0.1f, body.height * 0.5f, body.height - 0.1f};
    std::array<glm::vec2, 17> offsets = {{
        {0, 0}, {r, 0}, {-r, 0}, {0, r}, {0, -r},
        {r * 0.707f, r * 0.707f}, {-r * 0.707f, r * 0.707f},
        {r * 0.707f, -r * 0.707f}, {-r * 0.707f, -r * 0.707f}
    }};
    for (int sample = 0; sample < 8; ++sample) {
        const float angle = 6.28318530718f * (static_cast<float>(sample) + 0.5f) / 8.0f;
        offsets[9 + sample] = glm::vec2(r * 0.95f * std::cos(angle), r * 0.95f * std::sin(angle));
    }
    for (const float y : ySteps) {
        for (const auto& offset : offsets) {
            const glm::vec3 sample = body.position + localUp * y + localRight * offset.x +
                localForward * offset.y;
            if (world.QueryBlockCartesian(sample).status == fw::WorldVoxelQueryStatus::SolidVoxel) return true;
        }
    }
    return false;
}

struct TinySurfaceProfile {
    int faceSide = 0;
    int flatRunStartX = -1;
    int flatRunZ = -1;
    int flatRunLength = 0;
    int flatRunTopY = -1;
    int stepX = -1;
    int stepZ = -1;
    int stepDelta = 0;
    int flatPatchX = -1;
    int flatPatchZ = -1;
    int flatPatchSide = 0;
    int flatPatchTopY = -1;
    int minimumTopY = std::numeric_limits<int>::max();
    int maximumTopY = std::numeric_limits<int>::min();
};

int GetTopSolidVoxel(const fw::GameWorld& world, int x, int z) {
    for (int y = fw::ChunkDimensions::VoxelsY - 1; y >= 0; --y) {
        const fw::BlockType block = world.GetBlockFlat(x, y, z);
        if (block != fw::BlockType::Air && block != fw::BlockType::Water &&
            block != fw::BlockType::Lava && block != fw::BlockType::StargatePortal &&
            block != fw::BlockType::OutOfBounds) return y;
    }
    return -1;
}

TinySurfaceProfile InspectTinyPositiveZSurface(const fw::GameWorld& world) {
    TinySurfaceProfile profile;
    profile.faceSide = static_cast<int>(fw::PlanetMath::GetFaceResolution(fw::PlanetSize::Tiny)) *
        fw::ChunkDimensions::VoxelsX;
    std::vector<int> top(static_cast<size_t>(profile.faceSide * profile.faceSide), -1);
    auto at = [&](int x, int z) -> int& {
        return top[static_cast<size_t>(z * profile.faceSide + x)];
    };
    for (int z = 0; z < profile.faceSide; ++z) {
        for (int x = 0; x < profile.faceSide; ++x) {
            at(x, z) = GetTopSolidVoxel(world, x, z);
            if (at(x, z) >= 0) {
                profile.minimumTopY = std::min(profile.minimumTopY, at(x, z));
                profile.maximumTopY = std::max(profile.maximumTopY, at(x, z));
            }
        }
    }
    for (int z = 1; z + 1 < profile.faceSide; ++z) {
        int runStart = 1;
        for (int x = 2; x < profile.faceSide; ++x) {
            if (x < profile.faceSide - 1 && at(x, z) == at(runStart, z) && at(x, z) >= 0) continue;
            const int runEnd = x - 1;
            const int runLength = runEnd - runStart + 1;
            if (runLength > profile.flatRunLength) {
                profile.flatRunStartX = runStart;
                profile.flatRunZ = z;
                profile.flatRunLength = runLength;
                profile.flatRunTopY = at(runStart, z);
            }
            if (x < profile.faceSide - 1 && at(x, z) >= 0 && at(x + 1, z) >= 0) {
                const int delta = at(x + 1, z) - at(x, z);
                if (std::abs(delta) > std::abs(profile.stepDelta)) {
                    profile.stepX = x;
                    profile.stepZ = z;
                    profile.stepDelta = delta;
                }
            }
            runStart = x;
        }
    }
    for (int side = 7; side >= 3 && profile.flatPatchSide == 0; side -= 2) {
        for (int z = 1; z + side < profile.faceSide && profile.flatPatchSide == 0; ++z) {
            for (int x = 1; x + side < profile.faceSide; ++x) {
                const int topY = at(x, z);
                if (topY < 0) continue;
                bool uniform = true;
                for (int dz = 0; dz < side && uniform; ++dz) {
                    for (int dx = 0; dx < side; ++dx) {
                        if (at(x + dx, z + dz) != topY) { uniform = false; break; }
                    }
                }
                if (uniform) {
                    profile.flatPatchX = x;
                    profile.flatPatchZ = z;
                    profile.flatPatchSide = side;
                    profile.flatPatchTopY = topY;
                    break;
                }
            }
        }
    }
    return profile;
}

glm::vec3 TinyPositiveZDirectionAtGrid(float flatX, float flatZ) {
    const float radius = fw::PlanetMath::GetPlanetRadius(fw::PlanetSize::Tiny);
    const float base = static_cast<float>(fw::PlanetMath::GetEditorCanvasExtents(fw::PlanetSize::Tiny) *
        fw::ChunkDimensions::VoxelsX + fw::ChunkDimensions::VoxelsX / 2);
    const glm::vec2 chart((flatX - base) / radius, (flatZ - base) / radius);
    return fw::CubeSphereMapping::FaceChartToDirection(fw::CubeFace::PositiveZ, chart);
}

bool TryGetRadialSolidSurface(
    const fw::GameWorld& world, const glm::vec3& direction, fw::PlanetSize planetSize,
    float& outSurfaceRadius, glm::ivec3* outVoxel = nullptr) {
    glm::vec3 unitDirection;
    if (!fw::PlanetaryCameraFrame::Normalize(direction, unitDirection)) return false;
    const float planetRadius = fw::PlanetMath::GetPlanetRadius(planetSize);
    float flatX = 0.0f, localY = 0.0f, flatZ = 0.0f;
    fw::MapWorldGenerator::WorldToVoxelCoord(
        planetSize, false, unitDirection * planetRadius, flatX, localY, flatZ);
    const int voxelX = static_cast<int>(std::floor(flatX));
    const int voxelZ = static_cast<int>(std::floor(flatZ));
    for (int y = fw::ChunkDimensions::VoxelsY - 1; y >= 0; --y) {
        const fw::BlockType block = world.GetBlockFlat(voxelX, y, voxelZ);
        if (block == fw::BlockType::OutOfBounds) return false;
        if (block == fw::BlockType::Air || block == fw::BlockType::Water ||
            block == fw::BlockType::Lava || block == fw::BlockType::StargatePortal) continue;
        outSurfaceRadius = fw::PlanetRadialMapping::GetCellOuterRadius(0, y, planetRadius);
        if (outVoxel) *outVoxel = glm::ivec3(voxelX, y, voxelZ);
        return true;
    }
    return false;
}

struct LocomotionAuditResult {
    bool spawned = false;
    bool settledGrounded = false;
    bool endedGrounded = false;
    bool pending = false;
    bool crossedFace = false;
    int groundedLosses = 0;
    int groundedTicks = 0;
    int tangentialBlocks = 0;
    float maximumPenetration = 0.0f;
    float maximumPositiveRadialVelocity = 0.0f;
    float finalClearance = std::numeric_limits<float>::quiet_NaN();
    float finalRadialSurfaceClearance = std::numeric_limits<float>::quiet_NaN();
    float finalBodyRadius = std::numeric_limits<float>::quiet_NaN();
    int startFace = -1;
    int endFace = -1;
    size_t denseSolidSamples = 0;
    fw::PlanetarySpawnResolveStatus finalSpawnStatus = fw::PlanetarySpawnResolveStatus::InvalidInput;
};

bool HasP03aFootSupport(const fw::GameWorld& world, const RigidBody& body);

LocomotionAuditResult RunTinySurfaceLocomotion(
    fw::GameWorld& world, const glm::vec3& startDirection, const glm::vec3& travelDirection,
    bool diagonal, const std::vector<float>& timesteps, float durationSeconds) {
    LocomotionAuditResult result;
    if (timesteps.empty() || durationSeconds <= 0.0f) return result;

    DeviceManager deviceManager;
    deviceManager.requireFreeCursor = false;
    SharedContext context{};
    context.forgeWorld = &world;
    context.deviceManager = &deviceManager;
    entt::registry registry;
    const entt::entity entity = registry.create();
    auto& player = registry.emplace<PlayerControllerComponent>(entity);
    player.planetarySpawnPending = false;
    auto& camera = registry.emplace<CameraComponent>(entity);
    auto& transform = registry.emplace<TransformComponent>(entity);
    auto& body = registry.emplace<RigidBodyComponent>(entity).body;
    body.mass = 70.0f;

    float surfaceRadius = 0.0f;
    if (world.TryResolvePlanetarySpawn(startDirection, 0.11f, body.radius,
            body.position, &surfaceRadius) != fw::PlanetarySpawnResolveStatus::Ready) return result;
    result.spawned = true;

    const glm::vec3 up = glm::normalize(body.position);
    const auto frame = fw::PlanetaryCameraFrame::MakeInitial(up);
    camera.planetaryFrameUp = frame.up;
    camera.planetaryFrameForward = frame.forward;
    camera.planetaryFrameInitialized = frame.initialized;
    fw::PlanetaryCameraFrame::Basis baseBasis;
    glm::vec3 tangent = fw::PlanetaryCameraFrame::ProjectToTangent(travelDirection, up);
    if (!fw::PlanetaryCameraFrame::Normalize(tangent, tangent) ||
        !fw::PlanetaryCameraFrame::BuildBasis(frame, 0.0f, 0.0f, baseBasis)) return result;
    // BuildBasis rotates the reference forward by -yaw around local Up.
    // Use the inverse sign here so the CPU fixture follows the requested route.
    camera.yaw = -glm::degrees(std::atan2(glm::dot(tangent, baseBasis.right),
                                         glm::dot(tangent, baseBasis.forward)));

    fw::PhysicsSystem physics;
    fw::CameraSyncSystem cameraSync;
    fw::CameraSystem cameraSystem;
    transform.x = body.position.x;
    transform.y = body.position.y + up.y * body.eyeOffset;
    transform.z = body.position.z + up.z * body.eyeOffset;

    // Settle from the spawn clearance using the production physics path.
    for (int tick = 0; tick < 120 && !body.isGrounded; ++tick) {
        physics.Update(registry, &context, PhysicsEngine::FIXED_DT);
    }
    result.settledGrounded = body.isGrounded;
    if (!result.settledGrounded) return result;

    auto faceOf = [](const glm::vec3& direction) {
        fw::CubeFace face;
        glm::vec2 uv;
        fw::CubeSphereMapping::DirectionToFaceUV(direction, face, uv);
        return static_cast<int>(face);
    };
    result.startFace = faceOf(glm::normalize(body.position));
    float elapsed = 0.0f;
    size_t dtIndex = 0;
    bool wasGrounded = body.isGrounded;
    bool penetrationLogged = false;
    const float cycleDuration = std::accumulate(timesteps.begin(), timesteps.end(), 0.0f);
    if (!std::isfinite(cycleDuration) || cycleDuration <= 0.0f) return result;
    const size_t cycles = static_cast<size_t>(std::max(1.0f, std::round(durationSeconds / cycleDuration)));
    const size_t stepCount = cycles * timesteps.size();
    const bool traceDiagonal = diagonal && std::getenv("FAIRWORLD_P03D2_TRACE_DIAGONAL") != nullptr;
    for (size_t step = 0; step < stepCount; ++step) {
        const float dt = timesteps[dtIndex++ % timesteps.size()];
        if (!std::isfinite(dt) || dt <= 0.0f) return result;
        deviceManager.GetInput().isJumping = false;
        deviceManager.GetInput().moveForward = 1.0f;
        deviceManager.GetInput().moveRight = diagonal ? 1.0f : 0.0f;
        const glm::vec3 positionBefore = body.position;
        const glm::vec3 velocityBefore = body.velocity;
        const bool groundedBefore = body.isGrounded;
        cameraSync.Update(registry, &context, dt);
        fw::PlayerMovementSystem movement;
        movement.Update(registry, &context, dt);
        physics.Update(registry, &context, dt);
        cameraSystem.Update(registry, &context, dt);

        if (wasGrounded && !body.isGrounded) ++result.groundedLosses;
        wasGrounded = body.isGrounded;
        if (body.isGrounded) ++result.groundedTicks;
        if (body.dbg_tangentialSolidBlock) ++result.tangentialBlocks;
        result.pending = result.pending || body.dbg_terrainPending;
        const glm::vec3 currentUp = glm::normalize(body.position);
        result.maximumPositiveRadialVelocity = std::max(result.maximumPositiveRadialVelocity,
            glm::max(0.0f, glm::dot(body.velocity, currentUp)));
        const ColliderVolumeAudit volume = AuditDensePlanetaryCollider(world, body, fw::PlanetSize::Tiny);
        result.denseSolidSamples += volume.solidSamples;
        result.maximumPenetration = std::max(result.maximumPenetration, volume.maximumRadialPenetration);
        if (traceDiagonal) {
            glm::vec3 traceUp(0.0f);
            fw::PlanetaryCameraFrame::Normalize(body.position, traceUp);
            const auto footQuery = world.QueryBlockCartesian(body.position - traceUp * 0.02f);
            const auto sweepQuery = world.QueryBlockCartesian(body.position + traceUp * 0.9f);
            const char* transitionReason = body.dbg_radialGroundContact ? "radial-solid-contact" :
                (body.dbg_tangentialSolidBlock ? "tangential-solid-block" :
                 (body.dbg_terrainPending ? "terrain-pending" :
                  (groundedBefore && !body.isGrounded ? "grounded-cleared-without-support" : "no-ground-transition")));
            std::cout << "[P03D2][DIAGONAL_TICK] tick=" << step << " dt=" << dt
                      << " grounded=" << groundedBefore << ':' << body.isGrounded
                      << " position=" << body.position.x << ',' << body.position.y << ',' << body.position.z
                      << " velocity=" << body.velocity.x << ',' << body.velocity.y << ',' << body.velocity.z
                      << " up=" << traceUp.x << ',' << traceUp.y << ',' << traceUp.z
                      << " footSupport=" << HasP03aFootSupport(world, body)
                      << " footQuery=" << static_cast<unsigned>(footQuery.status) << ':'
                      << footQuery.voxelCoord.x << ',' << footQuery.voxelCoord.y << ',' << footQuery.voxelCoord.z
                      << " supportVoxel=" << body.dbg_groundVoxelCoord.x << ','
                      << body.dbg_groundVoxelCoord.y << ',' << body.dbg_groundVoxelCoord.z
                      << " lastQuery=" << static_cast<unsigned>(body.dbg_lastPlanetarySample) << ':'
                      << body.dbg_lastLookupCoord.x << ',' << body.dbg_lastLookupCoord.y << ','
                      << body.dbg_lastLookupCoord.z << ':' << body.dbg_lastLookupSolid
                      << " probeQuery=" << static_cast<unsigned>(sweepQuery.status) << ':'
                      << sweepQuery.voxelCoord.x << ',' << sweepQuery.voxelCoord.y << ',' << sweepQuery.voxelCoord.z
                      << " contacts=" << body.dbg_voxelContacts << " air=" << body.dbg_airSamples
                      << " pending=" << body.dbg_terrainPendingSamples
                      << " oob=" << body.dbg_outOfBoundsSamples
                      << " radialContact=" << body.dbg_radialGroundContact
                      << " tangentialBlock=" << body.dbg_tangentialSolidBlock
                      << " againstWall=" << body.isAgainstWall
                      << " penetration=" << volume.maximumRadialPenetration
                      << " transitionReason=" << transitionReason << '\n';
        }
        if ((groundedBefore && !body.isGrounded) ||
            (volume.maximumRadialPenetration > 0.005f && !penetrationLogged)) {
            penetrationLogged = penetrationLogged || volume.maximumRadialPenetration > 0.005f;
            const auto deepestQuery = volume.deepestVoxel.x >= 0
                ? world.QueryBlockCartesian(volume.deepestSamplePosition)
                : fw::WorldVoxelQueryResult{};
            glm::vec3 auditUp;
            fw::PlanetaryCameraFrame::Normalize(body.position, auditUp);
            fw::PlanetaryCameraFrame::State auditFrame{
                body.planetaryCollisionFrameUp, body.planetaryCollisionFrameForward,
                body.planetaryCollisionFrameInitialized};
            fw::PlanetaryCameraFrame::TransportTo(auditFrame, auditUp);
            glm::vec3 auditRight;
            fw::PlanetaryCameraFrame::Normalize(glm::cross(auditFrame.forward, auditUp), auditRight);
            const bool coveredByProductionProbes = IsSolidOnProductionCollisionProbeSet(
                world, body, auditUp, auditFrame.forward, auditRight);
            float localSolidSurface = 0.0f;
            glm::ivec3 localSurfaceVoxel{-1};
            const bool hasLocalSolidSurface = TryGetRadialSolidSurface(
                world, auditUp, fw::PlanetSize::Tiny, localSolidSurface, &localSurfaceVoxel);
            std::cout << "[D1.2.19d.6][LOCOMOTION_TRANSITION] dt=" << dt
                      << " grounded=" << groundedBefore << ':' << body.isGrounded
                      << " posBefore=" << positionBefore.x << ',' << positionBefore.y << ',' << positionBefore.z
                      << " posAfter=" << body.position.x << ',' << body.position.y << ',' << body.position.z
                      << " radialBefore=" << glm::dot(velocityBefore, glm::normalize(positionBefore))
                      << " radialAfter=" << glm::dot(body.velocity, glm::normalize(body.position))
                      << " velocityBefore=" << velocityBefore.x << ',' << velocityBefore.y << ',' << velocityBefore.z
                      << " velocityAfter=" << body.velocity.x << ',' << body.velocity.y << ',' << body.velocity.z
                      << " radialGroundContact=" << body.dbg_radialGroundContact
                      << " tangentialSolidBlock=" << body.dbg_tangentialSolidBlock
                      << " againstWall=" << body.isAgainstWall
                      << " groundVoxel=" << body.dbg_groundVoxelCoord.x << ','
                      << body.dbg_groundVoxelCoord.y << ',' << body.dbg_groundVoxelCoord.z
                      << " denseSolid=" << volume.solidSamples
                      << " densePending=" << volume.pendingSamples
                      << " denseOob=" << volume.outOfBoundsSamples
                      << " maxPenetration=" << volume.maximumRadialPenetration
                      << " deepestVoxel=" << volume.deepestVoxel.x << ','
                      << volume.deepestVoxel.y << ',' << volume.deepestVoxel.z
                      << " deepestSample=" << volume.deepestSamplePosition.x << ','
                      << volume.deepestSamplePosition.y << ',' << volume.deepestSamplePosition.z
                      << " deepestLocalSample=" << volume.deepestHeightOffset << ','
                      << volume.deepestRingScale << ',' << volume.deepestAngleIndex
                      << " coveredByProductionProbes=" << coveredByProductionProbes
                      << " deepestStatus=" << static_cast<unsigned>(deepestQuery.status)
                      << " radialSurface=" << hasLocalSolidSurface << ':' << localSolidSurface << ':'
                      << localSurfaceVoxel.x << ',' << localSurfaceVoxel.y << ',' << localSurfaceVoxel.z
                      << " radialSurfaceClearance=" << (hasLocalSolidSurface
                          ? glm::length(body.position) - localSolidSurface
                          : std::numeric_limits<float>::quiet_NaN())
                      << " terrainPending=" << body.dbg_terrainPending << '\n';
        }
        if (body.dbg_terrainPending) break;
        elapsed += dt;
    }

    result.endFace = faceOf(glm::normalize(body.position));
    result.crossedFace = result.startFace != result.endFace;
    result.endedGrounded = body.isGrounded;
    result.finalBodyRadius = glm::length(body.position);
    glm::vec3 finalDirection;
    glm::vec3 resolvedBodyPosition(0.0f);
    float finalSurface = 0.0f;
    if (fw::PlanetaryCameraFrame::Normalize(body.position, finalDirection) &&
        (result.finalSpawnStatus = world.TryResolvePlanetarySpawn(finalDirection, 0.11f, body.radius,
            resolvedBodyPosition, &finalSurface)) == fw::PlanetarySpawnResolveStatus::Ready) {
        result.finalClearance = glm::length(body.position) - finalSurface;
    }
    float radialSurface = 0.0f;
    if (TryGetRadialSolidSurface(world, body.position, fw::PlanetSize::Tiny, radialSurface)) {
        result.finalRadialSurfaceClearance = glm::length(body.position) - radialSurface;
    }
    return result;
}

bool TestTinyLocomotionContinuity(fw::GameWorld& world) {
    const float fixedDt = PhysicsEngine::FIXED_DT;
    const glm::vec3 positiveZ(0.0f, 0.0f, 1.0f);
    const glm::vec3 east(1.0f, 0.0f, 0.0f);
    const glm::vec3 diagonal = glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f));
    const TinySurfaceProfile profile = InspectTinyPositiveZSurface(world);
    std::cout << "[D1.2.19d.6][SURFACE_PROFILE] faceSide=" << profile.faceSide
              << " topRange=" << profile.minimumTopY << ':' << profile.maximumTopY
              << " longestFlatRun=" << profile.flatRunLength << " flatStart="
              << profile.flatRunStartX << ',' << profile.flatRunZ << " flatY=" << profile.flatRunTopY
              << " flatPatch=" << profile.flatPatchX << ',' << profile.flatPatchZ << ':'
              << profile.flatPatchSide << " patchY=" << profile.flatPatchTopY
              << " largestAdjacentStep=" << profile.stepDelta << " stepAt="
              << profile.stepX << ',' << profile.stepZ << '\n';

    const LocomotionAuditResult fixedWalk = RunTinySurfaceLocomotion(
        world, positiveZ, east, false, {fixedDt}, 1.0f);
    const LocomotionAuditResult diagonalWalk = RunTinySurfaceLocomotion(
        world, positiveZ, diagonal, true, {fixedDt}, 1.0f);
    const LocomotionAuditResult variableWalk = RunTinySurfaceLocomotion(
        world, positiveZ, east, false, {1.0f / 120.0f, 1.0f / 30.0f, fixedDt}, 1.0f);
    LocomotionAuditResult flatRun{};
    LocomotionAuditResult flatPatchWalk{};
    if (profile.flatPatchSide >= 3) {
        const float centerX = static_cast<float>(profile.flatPatchX) +
            static_cast<float>(profile.flatPatchSide) * 0.5f;
        const float centerZ = static_cast<float>(profile.flatPatchZ) +
            static_cast<float>(profile.flatPatchSide) * 0.5f;
        const glm::vec3 patchStart = TinyPositiveZDirectionAtGrid(centerX, centerZ);
        const glm::vec3 patchNext = TinyPositiveZDirectionAtGrid(centerX + 0.5f, centerZ);
        const glm::vec3 patchTangent = fw::PlanetaryCameraFrame::ProjectToTangent(patchNext, patchStart);
        flatPatchWalk = RunTinySurfaceLocomotion(world, patchStart, patchTangent, false, {fixedDt}, 0.4f);
    }
    if (profile.flatRunLength >= 4) {
        const glm::vec3 flatStart = TinyPositiveZDirectionAtGrid(
            static_cast<float>(profile.flatRunStartX + 1), static_cast<float>(profile.flatRunZ));
        const glm::vec3 flatNext = TinyPositiveZDirectionAtGrid(
            static_cast<float>(profile.flatRunStartX + 2), static_cast<float>(profile.flatRunZ));
        const glm::vec3 flatTangent = fw::PlanetaryCameraFrame::ProjectToTangent(flatNext, flatStart);
        flatRun = RunTinySurfaceLocomotion(world, flatStart, flatTangent, false, {fixedDt}, 0.75f);
    }
    LocomotionAuditResult stepRun{};
    if (profile.stepX >= 0) {
        const glm::vec3 stepStart = TinyPositiveZDirectionAtGrid(
            static_cast<float>(profile.stepX) + 0.5f, static_cast<float>(profile.stepZ) + 0.5f);
        const glm::vec3 stepNext = TinyPositiveZDirectionAtGrid(
            static_cast<float>(profile.stepX) + 1.5f, static_cast<float>(profile.stepZ) + 0.5f);
        const glm::vec3 stepTangent = fw::PlanetaryCameraFrame::ProjectToTangent(stepNext, stepStart);
        stepRun = RunTinySurfaceLocomotion(world, stepStart, stepTangent, false, {fixedDt}, 1.25f);
    }
    const glm::vec3 edgeStart = fw::CubeSphereMapping::FaceUVToDirection(
        fw::CubeFace::PositiveZ, glm::vec2(0.98f, 0.5f));
    const LocomotionAuditResult edgeWalk = RunTinySurfaceLocomotion(
        world, edgeStart, east, false, {fixedDt}, 1.5f);

    auto print = [](const char* name, const LocomotionAuditResult& result) {
        std::cout << "[D1.2.19d.6][LOCOMOTION] case=" << name
                  << " spawned=" << result.spawned
                  << " settledGrounded=" << result.settledGrounded
                  << " groundedTicks=" << result.groundedTicks
                  << " groundedLosses=" << result.groundedLosses
                  << " tangentialBlocks=" << result.tangentialBlocks
                  << " endedGrounded=" << result.endedGrounded
                  << " pending=" << result.pending
                  << " crossedFace=" << result.crossedFace << ':' << result.startFace << ':' << result.endFace
                  << " denseSolidSamples=" << result.denseSolidSamples
                  << " maxPenetration=" << result.maximumPenetration
                  << " maxPositiveRadialVelocity=" << result.maximumPositiveRadialVelocity
                  << " finalBodyRadius=" << result.finalBodyRadius
                  << " finalClearance=" << result.finalClearance
                  << " finalRadialSurfaceClearance=" << result.finalRadialSurfaceClearance
                  << " finalSpawnStatus=" << static_cast<unsigned>(result.finalSpawnStatus) << '\n';
    };
    print("fixed_walk", fixedWalk);
    print("diagonal", diagonalWalk);
    print("variable_timestep", variableWalk);
    print("uniform_fixture_patch", flatPatchWalk);
    print("fixture_flat_run", flatRun);
    print("fixture_step", stepRun);
    print("cube_face_edge", edgeWalk);

    // Report hard violations separately from expected Grounded transitions:
    // a step or edge may legitimately remove support, but pending terrain or
    // measurable solid penetration in the collider is never accepted.
    constexpr float numericalPenetrationTolerance = 0.005f;
    const auto safe = [&](const LocomotionAuditResult& result) {
        return result.spawned && result.settledGrounded && !result.pending &&
            result.maximumPenetration <= numericalPenetrationTolerance;
    };
    const bool flatPatchStable = profile.flatPatchSide >= 3 && flatPatchWalk.spawned &&
        flatPatchWalk.settledGrounded && flatPatchWalk.endedGrounded &&
        flatPatchWalk.groundedLosses == 0 && !flatPatchWalk.pending &&
        flatPatchWalk.maximumPenetration <= numericalPenetrationTolerance;
    std::cout << (flatPatchStable ? "[PASS]" : "[FAIL]")
              << " short walk on a measured uniform 3x3+ voxel surface patch remains grounded\n";
    const bool pass = safe(fixedWalk) && safe(diagonalWalk) && safe(variableWalk) &&
        safe(flatPatchWalk) && flatPatchStable && safe(flatRun) && safe(stepRun) && safe(edgeWalk);
    std::cout << (pass ? "[PASS]" : "[FAIL]")
              << " Tiny fixed/diagonal/variable-timestep/face-edge locomotion has no pending terrain or significant solid overlap\n";
    return pass;
}

uint64_t HashFloat(uint64_t hash, float value) {
    uint32_t bits = std::bit_cast<uint32_t>(value);
    for (int byte = 0; byte < 4; ++byte) {
        hash ^= static_cast<uint8_t>((bits >> (byte * 8)) & 0xffu);
        hash *= 1099511628211ull;
    }
    return hash;
}

uint64_t HashVertex(uint64_t hash, const fw::Vertex& vertex) {
    hash = HashFloat(hash, vertex.position.x);
    hash = HashFloat(hash, vertex.position.y);
    hash = HashFloat(hash, vertex.position.z);
    hash = HashFloat(hash, vertex.roughMetal.x);
    hash = HashFloat(hash, vertex.roughMetal.y);
    hash = HashFloat(hash, vertex.normal.x);
    hash = HashFloat(hash, vertex.normal.y);
    hash = HashFloat(hash, vertex.normal.z);
    hash ^= vertex.materialID;
    hash *= 1099511628211ull;
    return hash;
}

bool TestP03D3DeterministicFaceMapping(
    uint64_t& outInputDigest, uint64_t& outDirectionDigest, size_t& outSamples) {
    constexpr std::array<fw::PlanetSize, 6> sizes = {
        fw::PlanetSize::Tiny, fw::PlanetSize::Micro, fw::PlanetSize::Small,
        fw::PlanetSize::Medium, fw::PlanetSize::Large, fw::PlanetSize::Huge
    };
    outInputDigest = 1469598103934665603ull;
    outDirectionDigest = 1469598103934665603ull;
    outSamples = 0;
    bool valid = true;
    const bool traceMapping = std::getenv("FAIRWORLD_P03D3_TRACE_MAPPING") != nullptr;
    const auto addSample = [&](int face, int resolution, const glm::vec2& uv) {
        const glm::vec3 direction = fw::CubeSphereMapping::FaceUVToDirection(
            static_cast<fw::CubeFace>(face), uv);
        const float length = glm::length(direction);
        if (!std::isfinite(direction.x) || !std::isfinite(direction.y) ||
            !std::isfinite(direction.z) || !std::isfinite(length) ||
            std::abs(length - 1.0f) > 2.0e-6f) {
            valid = false;
        }
        outInputDigest ^= static_cast<uint64_t>(face);
        outInputDigest *= 1099511628211ull;
        outInputDigest ^= static_cast<uint64_t>(resolution);
        outInputDigest *= 1099511628211ull;
        outInputDigest = HashFloat(outInputDigest, uv.x);
        outInputDigest = HashFloat(outInputDigest, uv.y);
        outDirectionDigest ^= static_cast<uint64_t>(face);
        outDirectionDigest *= 1099511628211ull;
        outDirectionDigest ^= static_cast<uint64_t>(resolution);
        outDirectionDigest *= 1099511628211ull;
        outDirectionDigest = HashFloat(outDirectionDigest, direction.x);
        outDirectionDigest = HashFloat(outDirectionDigest, direction.y);
        outDirectionDigest = HashFloat(outDirectionDigest, direction.z);
        if (traceMapping) {
            const glm::vec3 cube = fw::CubeSphereMapping::FaceUVToCubeDir(
                static_cast<fw::CubeFace>(face), uv);
            const float q = (cube.x * cube.x + cube.y * cube.y) + cube.z * cube.z;
            const float inverseLength = glm::inversesqrt(q);
            const auto bits = [](float value) { return std::bit_cast<uint32_t>(value); };
            std::cout << "[P03D3][MAPTRACE] i=" << outSamples << " face=" << face
                      << " n=" << resolution << " uv=" << std::hex
                      << bits(uv.x) << ',' << bits(uv.y)
                      << " cube=" << bits(cube.x) << ',' << bits(cube.y) << ',' << bits(cube.z)
                      << " q=" << bits(q) << " inv=" << bits(inverseLength)
                      << " dir=" << bits(direction.x) << ',' << bits(direction.y) << ','
                      << bits(direction.z) << std::dec << '\n';
        }
        ++outSamples;
    };

    for (fw::PlanetSize size : sizes) {
        const int resolution = static_cast<int>(fw::PlanetMath::GetFaceResolution(size));
        for (int face = 0; face < 6; ++face) {
            addSample(face, resolution, {0.0f, 0.0f});
            addSample(face, resolution, {1.0f, 1.0f});
            for (int row = 0; row < resolution; ++row) {
                for (int col = 0; col < resolution; ++col) {
                    addSample(face, resolution, fw::CubeSphereMapping::GridToUV(col, row, resolution));
                }
            }
            for (int boundary = 1; boundary < resolution; ++boundary) {
                const float edge = static_cast<float>(boundary) / static_cast<float>(resolution);
                const float below = std::nextafter(edge, -std::numeric_limits<float>::infinity());
                const float above = std::nextafter(edge, std::numeric_limits<float>::infinity());
                for (float u : {below, edge, above}) addSample(face, resolution, {u, 0.5f});
                for (float v : {below, edge, above}) addSample(face, resolution, {0.5f, v});
            }
        }
    }
    return valid;
}

bool TestLegacySixFaceWinding(fw::MaterialRegistry& materials, size_t& legacyOpposedFaces, size_t& correctedTriangles) {
    struct LegacyFace {
        glm::vec3 a;
        glm::vec3 b;
        glm::vec3 c;
        glm::vec3 normal;
    };
    const std::array<LegacyFace, 6> oldFaces = {{
        {{-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, {0, 1, 0}},
        {{-0.5f,-0.5f,  0.5f}, {0.5f,-0.5f,  0.5f}, {0.5f,-0.5f,-0.5f}, {0,-1, 0}},
        {{-0.5f,-0.5f, -0.5f}, {-0.5f,0.5f,-0.5f}, {-0.5f,0.5f,0.5f}, {-1,0,0}},
        {{ 0.5f,-0.5f,  0.5f}, { 0.5f,0.5f, 0.5f}, { 0.5f,0.5f,-0.5f}, { 1,0,0}},
        {{-0.5f,-0.5f,  0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f,0.5f, 0.5f}, {0,0,1}},
        {{ 0.5f,-0.5f, -0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f,0.5f,-0.5f}, {0,0,-1}}
    }};
    for (const auto& face : oldFaces) {
        if (glm::dot(glm::cross(face.b - face.a, face.c - face.a), face.normal) < 0.0f) {
            ++legacyOpposedFaces;
            correctedTriangles += 2;
        }
    }

    fw::VoxelChunkComponent chunk{};
    chunk.cx = 0;
    chunk.cz = 0;
    chunk.planetCoord.planet = fw::PlanetID::Invalid();
    chunk.blocks[4][4][4] = 3;
    const std::array<const fw::VoxelChunkComponent*, 9> neighbors{};
    const auto mesh = fw::VoxelChunkMesher::Build(chunk, neighbors, materials, false, fw::PlanetSize::Tiny);
    if (mesh.vertices.size() != 36 || mesh.indices.size() != 36 || mesh.solidVoxels != 1) return false;

    std::array<size_t, 6> faceTriangleCounts{};
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const auto& a = mesh.vertices[mesh.indices[i]];
        const auto& b = mesh.vertices[mesh.indices[i + 1]];
        const auto& c = mesh.vertices[mesh.indices[i + 2]];
        const glm::vec3 pa(a.position.x, a.position.y, a.position.z);
        const glm::vec3 pb(b.position.x, b.position.y, b.position.z);
        const glm::vec3 pc(c.position.x, c.position.y, c.position.z);
        const glm::vec3 normal(a.normal.x, a.normal.y, a.normal.z);
        const float alignment = glm::dot(glm::cross(pb - pa, pc - pa), normal);
        if (!std::isfinite(alignment) || alignment <= 0.0f || a.materialID != 3 || b.materialID != 3 || c.materialID != 3) {
            return false;
        }
        size_t faceIndex = 0;
        if (normal.x < -0.5f) faceIndex = 0;
        else if (normal.x > 0.5f) faceIndex = 1;
        else if (normal.y < -0.5f) faceIndex = 2;
        else if (normal.y > 0.5f) faceIndex = 3;
        else if (normal.z < -0.5f) faceIndex = 4;
        else if (normal.z > 0.5f) faceIndex = 5;
        else return false;
        ++faceTriangleCounts[faceIndex];
        for (const auto* vertex : {&a, &b, &c}) {
            const glm::vec2 uv(vertex->roughMetal.x, vertex->roughMetal.y);
            if (!IsFinite(uv) || uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) return false;
        }
    }
    return legacyOpposedFaces == 4 && correctedTriangles == 8 &&
        std::all_of(faceTriangleCounts.begin(), faceTriangleCounts.end(), [](size_t count) { return count == 2; });
}

bool TestCubeSphereEdgesAndCorners(size_t& edgeSamples, size_t& cornerSamples, float& maxDirectionError) {
    constexpr std::array<float, 3> edgeParameters = {0.25f, 0.5f, 0.75f};
    constexpr std::array<glm::vec2, 4> corners = {
        glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f),
        glm::vec2(0.0f, 1.0f), glm::vec2(1.0f, 1.0f)
    };
    bool pass = true;
    auto check = [&](fw::CubeFace face, const glm::vec2& uv, bool corner) {
        const glm::vec3 direction = fw::CubeSphereMapping::FaceUVToDirection(face, uv);
        fw::CubeFace mappedFace{};
        glm::vec2 mappedUV{};
        fw::CubeSphereMapping::DirectionToFaceUV(direction, mappedFace, mappedUV);
        const glm::vec3 roundTrip = fw::CubeSphereMapping::FaceUVToDirection(mappedFace, mappedUV);
        const float error = glm::length(direction - roundTrip);
        maxDirectionError = std::max(maxDirectionError, error);
        if (!std::isfinite(error) || std::abs(glm::length(direction) - 1.0f) > 1.0e-5f || error > 1.0e-5f) pass = false;
        fw::CubeFace tieFace{};
        glm::vec2 tieUV{};
        fw::CubeSphereMapping::DirectionToFaceUV(direction, tieFace, tieUV);
        if (tieFace != mappedFace || glm::length(tieUV - mappedUV) > 1.0e-6f) pass = false;
        if (corner) ++cornerSamples;
        else ++edgeSamples;
    };

    for (int face = 0; face < 6; ++face) {
        const auto cubeFace = static_cast<fw::CubeFace>(face);
        for (float t : edgeParameters) {
            check(cubeFace, glm::vec2(0.0f, t), false);
            check(cubeFace, glm::vec2(1.0f, t), false);
            check(cubeFace, glm::vec2(t, 0.0f), false);
            check(cubeFace, glm::vec2(t, 1.0f), false);
        }
        for (const auto& uv : corners) check(cubeFace, uv, true);
    }
    return pass;
}

size_t ApplyWindingPassForBenchmark(std::vector<fw::Vertex>& vertices) {
    size_t swaps = 0;
    for (size_t i = 0; i + 2 < vertices.size(); i += 3) {
        const auto& a = vertices[i];
        const auto& b = vertices[i + 1];
        const auto& c = vertices[i + 2];
        const glm::vec3 pa(a.position.x, a.position.y, a.position.z);
        const glm::vec3 pb(b.position.x, b.position.y, b.position.z);
        const glm::vec3 pc(c.position.x, c.position.y, c.position.z);
        const glm::vec3 na(a.normal.x, a.normal.y, a.normal.z);
        const glm::vec3 nb(b.normal.x, b.normal.y, b.normal.z);
        const glm::vec3 nc(c.normal.x, c.normal.y, c.normal.z);
        const glm::vec3 geometricNormal = glm::cross(pb - pa, pc - pa);
        if (glm::length(geometricNormal) > 1.0e-7f && glm::dot(geometricNormal, na + nb + nc) < 0.0f) {
            std::swap(vertices[i + 1], vertices[i + 2]);
            ++swaps;
        }
    }
    return swaps;
}

bool TestWindingAcrossPlanetaryDeformation(size_t& preCorrectionMismatches, size_t& correctionSwaps,
    size_t& degenerateTriangles) {
    struct FaceQuad { glm::vec3 a, b, c, d, normal; };
    const std::array<FaceQuad, 6> faces = {{
        {{-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {0, 1, 0}},
        {{-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}, {0,-1, 0}},
        {{-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f}, {-1,0,0}},
        {{ 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}, { 1,0,0}},
        {{-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {0,0, 1}},
        {{ 0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, {0,0,-1}}
    }};

    for (int planetFace = 0; planetFace < 6; ++planetFace) {
        std::vector<fw::Vertex> triangles;
        triangles.reserve(36);
        for (const auto& face : faces) {
            const std::array<glm::vec3, 6> points = {face.a, face.b, face.c, face.a, face.c, face.d};
            for (const glm::vec3& point : points) {
                fw::Vertex vertex{};
                vertex.position = {point.x, point.y, point.z};
                vertex.normal = {face.normal.x, face.normal.y, face.normal.z};
                vertex.materialID = 3;
                triangles.push_back(vertex);
            }
        }
        const fw::PlanetChunkCoord coord{{1}, static_cast<fw::CubeFace>(planetFace), 1, 1, 1};
        fw::PlanetaryMeshGenerator::ConvertToPlanetaryPositions(triangles, coord, fw::PlanetSize::Tiny);
        for (size_t i = 0; i + 2 < triangles.size(); i += 3) {
            const auto& a = triangles[i]; const auto& b = triangles[i + 1]; const auto& c = triangles[i + 2];
            const glm::vec3 pa(a.position.x, a.position.y, a.position.z);
            const glm::vec3 pb(b.position.x, b.position.y, b.position.z);
            const glm::vec3 pc(c.position.x, c.position.y, c.position.z);
            const glm::vec3 na(a.normal.x, a.normal.y, a.normal.z);
            const glm::vec3 nb(b.normal.x, b.normal.y, b.normal.z);
            const glm::vec3 nc(c.normal.x, c.normal.y, c.normal.z);
            const glm::vec3 geometric = glm::cross(pb - pa, pc - pa);
            if (!std::isfinite(glm::length(geometric)) || glm::length(geometric) <= 1.0e-7f) {
                ++degenerateTriangles;
                continue;
            }
            if (glm::dot(geometric, na + nb + nc) < 0.0f) ++preCorrectionMismatches;
        }
        correctionSwaps += ApplyWindingPassForBenchmark(triangles);
        for (size_t i = 0; i + 2 < triangles.size(); i += 3) {
            const auto& a = triangles[i]; const auto& b = triangles[i + 1]; const auto& c = triangles[i + 2];
            const glm::vec3 pa(a.position.x, a.position.y, a.position.z);
            const glm::vec3 pb(b.position.x, b.position.y, b.position.z);
            const glm::vec3 pc(c.position.x, c.position.y, c.position.z);
            const glm::vec3 normal(a.normal.x, a.normal.y, a.normal.z);
            if (glm::dot(glm::cross(pb - pa, pc - pa), normal) <= 0.0f) return false;
        }
    }
    return preCorrectionMismatches == 48 && correctionSwaps == 48 && degenerateTriangles == 0;
}

bool ValidateFixture(const fw::MapDocument& document, std::string& reason) {
    if (document.planets.size() != 1) {
        reason = "fixture deve contenere un solo pianeta";
        return false;
    }

    const auto& planet = document.planets.front();
    if (planet.planetSize != fw::PlanetSize::Tiny || planet.isFlat) {
        reason = "fixture non e' PlanetSize::Tiny sferico (isFlat deve essere false)";
        return false;
    }
    if (document.terrainLibrary.size() != 1 || document.terrainLibrary.front().id != "default_terrain" ||
        document.terrainLibrary.front().planetSize != fw::PlanetSize::Tiny) {
        reason = "template default_terrain Tiny non trovato";
        return false;
    }
    if (planet.chunkInstances.size() != 1) {
        reason = "fixture deve contenere esattamente una istanza";
        return false;
    }

    const auto& instance = planet.chunkInstances.front();
    if (!instance.isActive || instance.templateId != "default_terrain" || instance.faceIndex != 0 ||
        instance.gridX != 1 || instance.gridY != 1 || !instance.isGridAligned) {
        reason = "istanza attiva default_terrain non corrisponde a face 0 / grid 1,1";
        return false;
    }
    if (planet.spawnPoints.empty()) {
        reason = "fixture privo di spawn planetario";
        return false;
    }

    const auto& spawn = planet.spawnPoints.front();
    const float radius = fw::PlanetMath::GetPlanetRadius(planet.planetSize);
    if (spawn.faceIndex < 0 || spawn.faceIndex > 5 || !std::isfinite(radius) || radius <= 0.0f ||
        !std::isfinite(spawn.localX) || !std::isfinite(spawn.localZ) || !std::isfinite(spawn.heightOffset)) {
        reason = "coordinate spawn non valide";
        return false;
    }
    const glm::vec3 direction = fw::CubeSphereMapping::FaceChartToDirection(
        spawn.faceIndex, glm::vec2(spawn.localX / radius, spawn.localZ / radius));
    if (!std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z) ||
        std::abs(glm::length(direction) - 1.0f) > 1.0e-4f || radius + spawn.heightOffset <= 0.0f) {
        reason = "direzione o quota spawn non valida";
        return false;
    }
    return true;
}

bool ValidateLegacyChunkCreation(SharedContext& context) {
    fw::GameWorld legacyWorld;
    legacyWorld.Initialize(&context);
    auto& registry = legacyWorld.GetRegistry();
    auto& geometry = registry.get<fw::PlanetGeometryComponent>(legacyWorld.GetPlanetEntity());
    geometry.isLogicalSphere = false;

    // Preserve the legacy Cartesian conversion, including its C++ truncation behavior for negatives.
    const entt::entity first = legacyWorld.CreateChunkEntity("LegacyChunk_2_-1", {32.0f, 0.0f, -17.0f});
    const entt::entity second = legacyWorld.CreateChunkEntity("LegacyChunk_3_-1", {48.0f, 0.0f, -17.0f});
    if (!registry.valid(first) || !registry.valid(second) || first == second ||
        !registry.all_of<fw::VoxelChunkComponent>(first) || !registry.all_of<fw::VoxelChunkComponent>(second)) {
        return false;
    }
    const auto& firstChunk = registry.get<fw::VoxelChunkComponent>(first);
    const auto& secondChunk = registry.get<fw::VoxelChunkComponent>(second);
    return firstChunk.cx == 2 && firstChunk.cz == -1 && secondChunk.cx == 3 && secondChunk.cz == -1 &&
        legacyWorld.GetChunkManager().GetChunkEntity(2, -1) == first &&
        legacyWorld.GetChunkManager().GetChunkEntity(3, -1) == second &&
        legacyWorld.GetChunkManager().GetLoadedChunkCount() == 2;
}

bool TestPlanetarySpawnResolution(fw::GameWorld& world, size_t& samples, size_t& colliderChecks,
    float& minimumClearance) {
    struct SpawnSample { int face; glm::vec2 uv; };
    std::vector<SpawnSample> cases;
    for (int face = 0; face < 6; ++face) {
        cases.push_back({face, {0.5f, 0.5f}});
        cases.push_back({face, {0.0001f, 0.5f}});
        cases.push_back({face, {0.9999f, 0.5f}});
        cases.push_back({face, {0.5f, 0.0001f}});
        cases.push_back({face, {0.5f, 0.9999f}});
        cases.push_back({face, {0.0f, 0.0f}});
        cases.push_back({face, {1.0f, 0.0f}});
        cases.push_back({face, {0.0f, 1.0f}});
        cases.push_back({face, {1.0f, 1.0f}});
    }

    constexpr float colliderRadius = 0.3f;
    constexpr float colliderHeight = 1.8f;
    constexpr float authoredOffset = 0.0f;
    constexpr float expectedClearance = 0.11f;
    constexpr float baseProbe[] = {0.1f, colliderHeight * 0.5f, colliderHeight - 0.1f};
    const std::array<glm::vec2, 9> footprint = {{
        {0.0f, 0.0f}, {colliderRadius, 0.0f}, {-colliderRadius, 0.0f},
        {0.0f, colliderRadius}, {0.0f, -colliderRadius},
        {colliderRadius * 0.707f, colliderRadius * 0.707f},
        {-colliderRadius * 0.707f, colliderRadius * 0.707f},
        {colliderRadius * 0.707f, -colliderRadius * 0.707f},
        {-colliderRadius * 0.707f, -colliderRadius * 0.707f}
    }};
    minimumClearance = std::numeric_limits<float>::infinity();

    for (const SpawnSample& sample : cases) {
        const glm::vec3 direction = fw::CubeSphereMapping::FaceUVToDirection(
            static_cast<fw::CubeFace>(sample.face), sample.uv);
        glm::vec3 bodyPosition(0.0f), repeatedPosition(0.0f);
        float surfaceRadius = 0.0f;
        if (world.TryResolvePlanetarySpawn(direction, authoredOffset, colliderRadius, bodyPosition) !=
                fw::PlanetarySpawnResolveStatus::Ready ||
            world.TryResolvePlanetarySpawn(direction, authoredOffset, colliderRadius, repeatedPosition) !=
                fw::PlanetarySpawnResolveStatus::Ready ||
            glm::length(bodyPosition - repeatedPosition) > 1.0e-5f ||
            glm::length(glm::normalize(bodyPosition) - glm::normalize(direction)) > 1.0e-5f) {
            return false;
        }
        if (world.TryResolvePlanetarySpawn(direction, authoredOffset, colliderRadius,
                bodyPosition, &surfaceRadius) != fw::PlanetarySpawnResolveStatus::Ready ||
            std::abs(glm::length(bodyPosition) - surfaceRadius - expectedClearance) > 1.0e-4f) {
            return false;
        }

        const glm::vec3 up = glm::normalize(direction);
        glm::vec3 right = glm::cross(up, glm::vec3(0.0f, 1.0f, 0.0f));
        if (glm::length(right) < 0.01f) right = glm::cross(up, glm::vec3(1.0f, 0.0f, 0.0f));
        right = glm::normalize(right);
        const glm::vec3 forward = glm::normalize(glm::cross(right, up));
        for (float y : baseProbe) {
            for (const glm::vec2& offset : footprint) {
                const glm::vec3 probe = bodyPosition + up * y + right * offset.x + forward * offset.y;
                const auto block = world.GetBlockCartesian(probe);
                if (block != fw::BlockType::Air && block != fw::BlockType::Water &&
                    block != fw::BlockType::Lava && block != fw::BlockType::StargatePortal &&
                    block != fw::BlockType::OutOfBounds) {
                    return false;
                }
                ++colliderChecks;
            }
        }
        minimumClearance = std::min(minimumClearance, glm::length(bodyPosition) - surfaceRadius);
        ++samples;
    }
    return samples == cases.size() && colliderChecks == cases.size() * 27;
}

bool CameraBasisValid(const fw::PlanetaryCameraFrame::Basis& basis) {
    constexpr float tolerance = 2.0e-4f;
    return fw::PlanetaryCameraFrame::IsFinite(basis.forward) &&
        fw::PlanetaryCameraFrame::IsFinite(basis.right) && fw::PlanetaryCameraFrame::IsFinite(basis.up) &&
        std::abs(glm::length(basis.forward) - 1.0f) < tolerance &&
        std::abs(glm::length(basis.right) - 1.0f) < tolerance &&
        std::abs(glm::length(basis.up) - 1.0f) < tolerance &&
        std::abs(glm::dot(basis.forward, basis.right)) < tolerance &&
        std::abs(glm::dot(basis.forward, basis.up)) < tolerance &&
        std::abs(glm::dot(basis.right, basis.up)) < tolerance;
}

bool TestPlanetaryCameraFrame(size_t& orientationSteps, float& maxStepAngle,
    float& closedLoopForwardError, float& cubeEdgeMaxJump) {
    using fw::PlanetaryCameraFrame::Basis;
    using fw::PlanetaryCameraFrame::State;
    constexpr float pi = 3.14159265358979323846f;
    bool pass = true;

    const State initial = fw::PlanetaryCameraFrame::MakeInitial({0.0f, 1.0f, 0.0f});
    Basis initialBasis;
    pass = pass && initial.initialized && fw::PlanetaryCameraFrame::BuildBasis(initial, 0.0f, 0.0f, initialBasis);
    for (int i = 0; i < 16; ++i) {
        Basis stationary;
        pass = pass && fw::PlanetaryCameraFrame::BuildBasis(initial, 0.0f, 0.0f, stationary) &&
            glm::length(stationary.forward - initialBasis.forward) < 1.0e-6f &&
            glm::length(stationary.up - initialBasis.up) < 1.0e-6f;
    }

    float yaw = 0.0f, pitch = 0.0f;
    pass = pass && fw::PlanetaryCameraFrame::ApplyLookDelta(yaw, pitch, 25.0f, 0.0f, 1.0f) &&
        yaw > 0.0f && std::abs(pitch) < 1.0e-6f;
    Basis yawPositive;
    pass = pass && fw::PlanetaryCameraFrame::BuildBasis(initial, yaw, pitch, yawPositive) &&
        glm::length(yawPositive.forward - initialBasis.forward) > 0.1f;
    pass = pass && fw::PlanetaryCameraFrame::ApplyLookDelta(yaw, pitch, -25.0f, 0.0f, 1.0f) &&
        std::abs(yaw) < 1.0e-5f;
    pass = pass && fw::PlanetaryCameraFrame::ApplyLookDelta(yaw, pitch, 0.0f, -2000.0f, 1.0f) && pitch == 89.0f;
    pass = pass && fw::PlanetaryCameraFrame::ApplyLookDelta(yaw, pitch, 0.0f, 2000.0f, 1.0f) && pitch == -89.0f;
    float wrappedYaw = 179.0f;
    pass = pass && fw::PlanetaryCameraFrame::ApplyLookDelta(wrappedYaw, pitch, 5.0f, 0.0f, 1.0f) &&
        wrappedYaw < -170.0f;

    auto runClosedEquator = [&](State& result) {
        result = fw::PlanetaryCameraFrame::MakeInitial({1.0f, 0.0f, 0.0f});
        Basis previous;
        if (!fw::PlanetaryCameraFrame::BuildBasis(result, 0.0f, 0.0f, previous)) return false;
        const glm::vec3 initialForward = previous.forward;
        constexpr int steps = 720;
        for (int i = 1; i <= steps; ++i) {
            const float angle = (2.0f * pi * static_cast<float>(i)) / static_cast<float>(steps);
            const glm::vec3 nextUp(std::cos(angle), std::sin(angle), 0.0f);
            if (!fw::PlanetaryCameraFrame::TransportTo(result, nextUp)) return false;
            Basis current;
            if (!fw::PlanetaryCameraFrame::BuildBasis(result, 0.0f, 0.0f, current) || !CameraBasisValid(current)) return false;
            const float stepDot = glm::clamp(glm::dot(previous.forward, current.forward), -1.0f, 1.0f);
            const float stepAngle = std::acos(stepDot);
            maxStepAngle = std::max(maxStepAngle, stepAngle);
            if (stepDot < 0.99f) return false;
            previous = current;
            ++orientationSteps;
        }
        closedLoopForwardError = std::max(closedLoopForwardError, glm::length(previous.forward - initialForward));
        return glm::length(result.up - glm::vec3(1.0f, 0.0f, 0.0f)) < 2.0e-4f &&
            glm::length(previous.forward - initialForward) < 2.0e-4f;
    };

    State equatorA, equatorB;
    pass = pass && runClosedEquator(equatorA) && runClosedEquator(equatorB) &&
        glm::length(equatorA.forward - equatorB.forward) < 1.0e-6f;

    // Pass through both poles on a great-circle meridian, including exact pole samples.
    State poleFrame = fw::PlanetaryCameraFrame::MakeInitial({-1.0f, 0.0f, 0.0f});
    Basis previousPoleBasis;
    pass = pass && fw::PlanetaryCameraFrame::BuildBasis(poleFrame, 0.0f, 0.0f, previousPoleBasis);
    constexpr int poleSteps = 720;
    for (int i = 1; i <= poleSteps; ++i) {
        const float angle = -0.5f * pi + (2.0f * pi * static_cast<float>(i)) / static_cast<float>(poleSteps);
        const glm::vec3 nextUp(std::sin(angle), std::cos(angle), 0.0f);
        pass = pass && fw::PlanetaryCameraFrame::TransportTo(poleFrame, nextUp);
        Basis current;
        pass = pass && fw::PlanetaryCameraFrame::BuildBasis(poleFrame, 0.0f, 0.0f, current) && CameraBasisValid(current);
        if (glm::dot(previousPoleBasis.forward, current.forward) < 0.99f) pass = false;
        previousPoleBasis = current;
    }

    // The exact antiparallel normal is ambiguous; the contract preserves the
    // previous heading as the deterministic half-turn axis.
    State antiparallel = fw::PlanetaryCameraFrame::MakeInitial({0.0f, 1.0f, 0.0f});
    const glm::vec3 beforeAntiparallel = antiparallel.forward;
    pass = pass && fw::PlanetaryCameraFrame::TransportTo(antiparallel, {0.0f, -1.0f, 0.0f}) &&
        glm::dot(beforeAntiparallel, antiparallel.forward) > 0.9999f;
    Basis antiparallelBasis;
    pass = pass && fw::PlanetaryCameraFrame::BuildBasis(antiparallel, 0.0f, 0.0f, antiparallelBasis) &&
        CameraBasisValid(antiparallelBasis);

    const std::array<glm::vec3, 7> faceRoute = {{
        {0, 0, 1}, {1, 0, 0}, {0, 1, 0}, {0, 0, -1}, {-1, 0, 0}, {0, -1, 0}, {0, 0, 1}
    }};
    State edgeFrame = fw::PlanetaryCameraFrame::MakeInitial(faceRoute.front());
    Basis previousEdgeBasis;
    pass = pass && fw::PlanetaryCameraFrame::BuildBasis(edgeFrame, 0.0f, 0.0f, previousEdgeBasis);
    fw::CubeFace previousFace = fw::CubeFace::PositiveZ;
    size_t faceTransitions = 0;
    constexpr int edgeSteps = 32;
    for (size_t segment = 0; segment + 1 < faceRoute.size(); ++segment) {
        for (int step = 1; step <= edgeSteps; ++step) {
            const float t = static_cast<float>(step) / static_cast<float>(edgeSteps);
            const glm::vec3 interpolated = glm::normalize(glm::mix(faceRoute[segment], faceRoute[segment + 1], t));
            fw::CubeFace mappedFace{};
            glm::vec2 mappedUV{};
            fw::CubeSphereMapping::DirectionToFaceUV(interpolated, mappedFace, mappedUV);
            const glm::vec3 edgeNormal = fw::CubeSphereMapping::FaceUVToDirection(mappedFace, mappedUV);
            pass = pass && glm::length(edgeNormal - interpolated) < 2.0e-5f &&
                fw::PlanetaryCameraFrame::TransportTo(edgeFrame, edgeNormal);
            if (mappedFace != previousFace) ++faceTransitions;
            previousFace = mappedFace;

            Basis current;
            pass = pass && fw::PlanetaryCameraFrame::BuildBasis(edgeFrame, 0.0f, 0.0f, current) && CameraBasisValid(current);
            const float jump = glm::clamp(glm::dot(previousEdgeBasis.forward, current.forward), -1.0f, 1.0f);
            cubeEdgeMaxJump = std::max(cubeEdgeMaxJump, std::acos(jump));
            if (jump < 0.0f) pass = false;
            previousEdgeBasis = current;
        }
    }

    return pass && orientationSteps == 1440 && maxStepAngle < 0.02f &&
        closedLoopForwardError < 2.0e-4f && faceTransitions >= 5 && cubeEdgeMaxJump < 0.1f;
}

bool TestPlanetaryMovementMath() {
    const glm::vec2 diagonal = fw::PlanetaryMovementMath::NormalizePlanarInput({1.0f, 1.0f});
    const glm::vec2 analog = fw::PlanetaryMovementMath::NormalizePlanarInput({0.5f, 0.5f});
    const glm::vec2 reverse = fw::PlanetaryMovementMath::NormalizePlanarInput({-1.0f, -1.0f});
    const glm::vec2 nonFinite = fw::PlanetaryMovementMath::NormalizePlanarInput(
        {std::numeric_limits<float>::quiet_NaN(), 1.0f});
    float radialSpeed = 0.0f;
    glm::vec3 tangentVelocity(0.0f);
    const glm::vec3 up = glm::normalize(glm::vec3(1.0f, 2.0f, -3.0f));
    const glm::vec3 sourceTangent = glm::normalize(glm::cross(up, glm::vec3(0.0f, 1.0f, 0.0f))) * 3.0f;
    const glm::vec3 velocity = up * -8.0f + sourceTangent;
    const bool splitValid = fw::PlanetaryMovementMath::SplitRadialVelocity(
        velocity, up, radialSpeed, tangentVelocity);
    const glm::vec3 clamped = fw::PlanetaryMovementMath::ClampInwardRadialSpeed(velocity, up, 5.0f);
    float clampedRadial = 0.0f;
    glm::vec3 clampedTangent(0.0f);
    const bool clampValid = fw::PlanetaryMovementMath::SplitRadialVelocity(
        clamped, up, clampedRadial, clampedTangent);
    const glm::vec3 forward(0.0f, 0.0f, -1.0f);
    const glm::vec3 right(1.0f, 0.0f, 0.0f);
    const glm::vec3 moveForward = fw::PlanetaryMovementMath::DesiredTangentVelocity(forward, right, {0, 1}, 5.0f);
    const glm::vec3 moveBackward = fw::PlanetaryMovementMath::DesiredTangentVelocity(forward, right, {0, -1}, 5.0f);
    const glm::vec3 moveLeft = fw::PlanetaryMovementMath::DesiredTangentVelocity(forward, right, {-1, 0}, 5.0f);
    const glm::vec3 moveRight = fw::PlanetaryMovementMath::DesiredTangentVelocity(forward, right, {1, 0}, 5.0f);
    const glm::vec3 moveDiagonal = fw::PlanetaryMovementMath::DesiredTangentVelocity(forward, right, {1, 1}, 5.0f);
    const std::array<bool, 13> checks = {{
        glm::length(diagonal) <= 1.000001f && std::abs(glm::length(diagonal) - 1.0f) < 1.0e-6f,
        glm::length(analog - glm::vec2(0.5f, 0.5f)) < 1.0e-6f,
        glm::length(reverse + diagonal) < 1.0e-6f,
        glm::length(nonFinite) == 0.0f,
        splitValid && clampValid,
        std::abs(radialSpeed + 8.0f) < 1.0e-5f,
        std::abs(clampedRadial + 5.0f) < 1.0e-5f,
        glm::length(tangentVelocity - clampedTangent) < 1.0e-5f,
        std::abs(glm::dot(tangentVelocity, up)) < 1.0e-5f,
        glm::length(moveForward - forward * 5.0f) < 1.0e-6f,
        glm::length(moveBackward + forward * 5.0f) < 1.0e-6f,
        glm::length(moveLeft + right * 5.0f) < 1.0e-6f,
        glm::length(moveRight - right * 5.0f) < 1.0e-6f && std::abs(glm::length(moveDiagonal) - 5.0f) < 1.0e-5f
    }};
    const bool pass = std::all_of(checks.begin(), checks.end(), [](bool value) { return value; });
    std::cout << "[OBSERVED] movement math diagonalSpeed=" << glm::length(moveDiagonal)
              << " radialBefore=" << radialSpeed << " radialAfterClamp=" << clampedRadial
              << " tangentChange=" << glm::length(tangentVelocity - clampedTangent)
              << " nonFiniteInputOutput=" << glm::length(nonFinite) << '\n';
    if (!pass) for (size_t i = 0; i < checks.size(); ++i) if (!checks[i]) std::cout << "[FAIL] movement math assertion " << i << '\n';
    return pass;
}

bool TestPlanetaryPhysics(fw::GameWorld& world, const glm::vec3& spawnDirection,
                          float& settledClearance, float& maximumJumpHeight,
                          float& maxCollisionFrameStep, size_t& physicsFaceTransitions,
                          size_t& physicsCellTransitions) {
    constexpr float fixedDt = 1.0f / 60.0f;
    PhysicsEngine physics;
    bool pass = true;

    RigidBody probe;
    probe.position = glm::normalize(spawnDirection) * 140.0f;
    const glm::vec3 probeUp = glm::normalize(probe.position);
    const glm::vec3 tangent = glm::normalize(glm::cross(probeUp, glm::vec3(0, 1, 0)));
    probe.velocity = tangent * 30.0f - probeUp * 100.0f;
    physics.StepSimulation(probe, 0.0f, world);
    const float radialSpeedAfterClamp = glm::dot(probe.velocity, probeUp);
    const glm::vec3 tangentAfterClamp = probe.velocity - probeUp * radialSpeedAfterClamp;
    pass = pass && std::isfinite(radialSpeedAfterClamp) && radialSpeedAfterClamp >= -55.0001f &&
        std::abs(glm::length(tangentAfterClamp) - 30.0f) < 1.0e-3f;
    std::cout << "[DIAG] radial clamp=" << pass << " speed=" << radialSpeedAfterClamp << '\n';

    probe.velocity = glm::vec3(0.0f);
    physics.StepSimulation(probe, fixedDt, world);
    pass = pass && fw::PlanetaryCameraFrame::IsFinite(probe.dbg_gravityAccel) &&
        glm::dot(probe.dbg_gravityAccel, glm::normalize(probe.position)) < 0.0f &&
        glm::dot(probe.velocity, glm::normalize(probe.position)) < 0.0f;
    std::cout << "[DIAG] inward gravity=" << pass << " accel=" << glm::dot(probe.dbg_gravityAccel, glm::normalize(probe.position)) << '\n';

    // Exercise the same spherical collision-frame state used by the real solver
    // at poles, across cube-face directions, and around a closed equatorial loop.
    const std::array<glm::vec3, 7> route = {{
        {0, 0, 1}, {1, 0, 0}, {0, 1, 0}, {0, 0, -1}, {-1, 0, 0}, {0, -1, 0}, {0, 0, 1}
    }};
    RigidBody frameBody;
    frameBody.velocity = glm::vec3(0.0f);
    glm::vec3 previousForward(0.0f);
    bool havePrevious = false;
    for (size_t segment = 0; segment + 1 < route.size(); ++segment) {
        for (int step = 1; step <= 32; ++step) {
            const float t = static_cast<float>(step) / 32.0f;
            const glm::vec3 direction = glm::normalize(glm::mix(route[segment], route[segment + 1], t));
            frameBody.position = direction * 140.0f;
            physics.StepSimulation(frameBody, 0.0f, world);
            const glm::vec3 currentForward = frameBody.planetaryCollisionFrameForward;
            if (!frameBody.planetaryCollisionFrameInitialized || !fw::PlanetaryCameraFrame::IsFinite(currentForward)) {
                pass = false;
                continue;
            }
            if (havePrevious) {
                const float dot = glm::clamp(glm::dot(previousForward, currentForward), -1.0f, 1.0f);
                maxCollisionFrameStep = std::max(maxCollisionFrameStep, std::acos(dot));
                if (dot < 0.95f) pass = false;
            }
            previousForward = currentForward;
            havePrevious = true;
        }
    }
    frameBody.position = glm::vec3(0.0f, 0.0f, -140.0f);
    physics.StepSimulation(frameBody, 0.0f, world);
    pass = pass && glm::dot(previousForward, frameBody.planetaryCollisionFrameForward) > 0.95f;
    std::cout << "[DIAG] collision frame=" << pass << " antiparallelDot="
              << glm::dot(previousForward, frameBody.planetaryCollisionFrameForward)
              << " maxStep=" << maxCollisionFrameStep << '\n';

    // Fixed-step replay must be deterministic; a half-sized variable step must
    // remain finite and close to the same short free-fall trajectory.
    RigidBody replayA, replayB, variableStep;
    replayA.position = replayB.position = variableStep.position = glm::vec3(0.0f, 0.0f, 140.0f);
    for (int i = 0; i < 120; ++i) {
        physics.StepSimulation(replayA, fixedDt, world);
        physics.StepSimulation(replayB, fixedDt, world);
    }
    for (int i = 0; i < 240; ++i) physics.StepSimulation(variableStep, fixedDt * 0.5f, world);
    const float fixedDeterminismError = glm::length(replayA.position - replayB.position);
    const float variableStepError = glm::length(replayA.position - variableStep.position);
    pass = pass && fixedDeterminismError < 1.0e-6f && variableStepError < 0.15f &&
        fw::PlanetaryCameraFrame::IsFinite(variableStep.position) &&
        fw::PlanetaryCameraFrame::IsFinite(variableStep.velocity);
    std::cout << "[DIAG] timestep=" << pass << " fixedError=" << fixedDeterminismError
              << " variableError=" << variableStepError << '\n';

    // Move along the spherical surface from the +Z face toward +X, crossing the
    // face edge and at least one logical grid cell using real voxel collision queries.
    constexpr float theta = 40.0f * 3.14159265358979323846f / 180.0f;
    const glm::vec3 traverseDirection(std::sin(theta), 0.0f, std::cos(theta));
    RigidBody traversal;
    if (world.TryResolvePlanetarySpawn(traverseDirection, 0.11f, traversal.radius, traversal.position) !=
        fw::PlanetarySpawnResolveStatus::Ready) return false;
    traversal.velocity = glm::normalize(glm::vec3(std::cos(theta), 0.0f, -std::sin(theta))) * 5.0f;
    fw::CubeFace lastFace{};
    glm::vec2 lastUV(0.0f);
    fw::CubeSphereMapping::DirectionToFaceUV(glm::normalize(traversal.position), lastFace, lastUV);
    const int resolution = fw::PlanetMath::GetFaceResolution(fw::PlanetSize::Tiny);
    int lastCol = 0, lastRow = 0;
    fw::CubeSphereMapping::UVToGrid(lastUV, resolution, lastCol, lastRow);
    for (int i = 0; i < 300; ++i) {
        physics.StepSimulation(traversal, fixedDt, world);
        if (!fw::PlanetaryCameraFrame::IsFinite(traversal.position) ||
            !fw::PlanetaryCameraFrame::IsFinite(traversal.velocity)) pass = false;
        fw::CubeFace currentFace{};
        glm::vec2 currentUV(0.0f);
        fw::CubeSphereMapping::DirectionToFaceUV(glm::normalize(traversal.position), currentFace, currentUV);
        int currentCol = 0, currentRow = 0;
        fw::CubeSphereMapping::UVToGrid(currentUV, resolution, currentCol, currentRow);
        if (currentFace != lastFace) ++physicsFaceTransitions;
        if (currentFace != lastFace || currentCol != lastCol || currentRow != lastRow) ++physicsCellTransitions;
        lastFace = currentFace;
        lastCol = currentCol;
        lastRow = currentRow;
    }
    pass = pass && physicsFaceTransitions > 0;
    std::cout << "[DIAG] face traversal=" << pass << " transitions=" << physicsFaceTransitions << '\n';

    constexpr float innerTheta = 17.8f * 3.14159265358979323846f / 180.0f;
    const glm::vec3 innerDirection(std::sin(innerTheta), 0.0f, std::cos(innerTheta));
    RigidBody chunkTraversal;
    if (world.TryResolvePlanetarySpawn(innerDirection, 0.11f, chunkTraversal.radius, chunkTraversal.position) !=
        fw::PlanetarySpawnResolveStatus::Ready) return false;
    chunkTraversal.velocity = glm::normalize(glm::vec3(std::cos(innerTheta), 0.0f, -std::sin(innerTheta))) * 5.0f;
    fw::CubeFace chunkFace{};
    glm::vec2 chunkUV(0.0f);
    fw::CubeSphereMapping::DirectionToFaceUV(glm::normalize(chunkTraversal.position), chunkFace, chunkUV);
    int chunkCol = 0, chunkRow = 0;
    fw::CubeSphereMapping::UVToGrid(chunkUV, resolution, chunkCol, chunkRow);
    const auto startingCell = std::make_tuple(chunkFace, chunkCol, chunkRow);
    for (int i = 0; i < 90; ++i) {
        physics.StepSimulation(chunkTraversal, fixedDt, world);
        fw::CubeFace currentFace{};
        glm::vec2 currentUV(0.0f);
        fw::CubeSphereMapping::DirectionToFaceUV(glm::normalize(chunkTraversal.position), currentFace, currentUV);
        int currentCol = 0, currentRow = 0;
        fw::CubeSphereMapping::UVToGrid(currentUV, resolution, currentCol, currentRow);
        if (currentFace != lastFace || currentCol != lastCol || currentRow != lastRow) {
            // Include this second actual traversal's grid crossings in the report.
            ++physicsCellTransitions;
        }
        lastFace = currentFace;
        lastCol = currentCol;
        lastRow = currentRow;
        if (!fw::PlanetaryCameraFrame::IsFinite(chunkTraversal.position) ||
            !fw::PlanetaryCameraFrame::IsFinite(chunkTraversal.velocity)) pass = false;
    }
    fw::CubeFace endingFace{};
    glm::vec2 endingUV(0.0f);
    fw::CubeSphereMapping::DirectionToFaceUV(glm::normalize(chunkTraversal.position), endingFace, endingUV);
    int endingCol = 0, endingRow = 0;
    fw::CubeSphereMapping::UVToGrid(endingUV, resolution, endingCol, endingRow);
    const bool crossedGridCell = std::make_tuple(endingFace, endingCol, endingRow) != startingCell;
    glm::vec3 traversalSurfacePosition(0.0f);
    float traversalSurfaceRadius = 0.0f;
    const auto traversalSurfaceStatus = world.TryResolvePlanetarySpawn(
        glm::normalize(chunkTraversal.position), 0.11f, chunkTraversal.radius,
        traversalSurfacePosition, &traversalSurfaceRadius);
    const float traversalClearance = glm::length(chunkTraversal.position) - traversalSurfaceRadius;
    std::cout << "[OBSERVED] physics grid route startFace=" << static_cast<int>(std::get<0>(startingCell))
              << " startCell=" << std::get<1>(startingCell) << ',' << std::get<2>(startingCell)
              << " endFace=" << static_cast<int>(endingFace) << " endCell=" << endingCol << ',' << endingRow
              << " startDirection=" << innerDirection.x << ',' << innerDirection.y << ',' << innerDirection.z
              << " endDirection=" << glm::normalize(chunkTraversal.position).x << ','
              << glm::normalize(chunkTraversal.position).y << ',' << glm::normalize(chunkTraversal.position).z
              << " finalRadius=" << glm::length(chunkTraversal.position)
              << " localSurfaceRadius=" << traversalSurfaceRadius << " clearance=" << traversalClearance
              << " grounded=" << chunkTraversal.isGrounded
              << " wall=" << chunkTraversal.isAgainstWall << '\n';
    if (crossedGridCell) ++physicsCellTransitions;
    pass = pass && crossedGridCell && physicsCellTransitions > physicsFaceTransitions &&
        traversalSurfaceStatus == fw::PlanetarySpawnResolveStatus::Ready && traversalClearance >= -0.005f;
    std::cout << "[DIAG] grid traversal=" << pass << " crossed=" << crossedGridCell
              << " transitions=" << physicsCellTransitions << '\n';

    glm::vec3 resolvedStart(0.0f);
    float surfaceRadius = 0.0f;
    if (world.TryResolvePlanetarySpawn(spawnDirection, 0.11f, 0.3f, resolvedStart, &surfaceRadius) !=
        fw::PlanetarySpawnResolveStatus::Ready) return false;

    RigidBody standing;
    standing.position = resolvedStart;
    standing.radius = 0.3f;
    standing.height = 1.8f;
    bool groundedObserved = false;
    for (int i = 0; i < 1200; ++i) {
        physics.StepSimulation(standing, fixedDt, world);
        if (!fw::PlanetaryCameraFrame::IsFinite(standing.position) ||
            !fw::PlanetaryCameraFrame::IsFinite(standing.velocity) ||
            glm::length(standing.velocity) > 100.0f) pass = false;
        groundedObserved = groundedObserved || standing.isGrounded;
    }
    settledClearance = glm::length(standing.position) - surfaceRadius;
    std::cout << "[OBSERVED] physics settle startRadius=" << glm::length(resolvedStart)
              << " surfaceRadius=" << surfaceRadius << " finalRadius=" << glm::length(standing.position)
              << " groundedObserved=" << groundedObserved << " groundedFinal=" << standing.isGrounded
              << " radialVelocity=" << glm::dot(standing.velocity, glm::normalize(standing.position))
              << " centerProbeBlock=" << static_cast<unsigned>(world.GetBlockCartesian(
                    standing.position + glm::normalize(standing.position) * 0.1f)) << '\n';
    pass = pass && groundedObserved && standing.isGrounded && settledClearance >= -0.005f;
    std::cout << "[DIAG] ground contact=" << pass << " grounded=" << standing.isGrounded
              << " clearance=" << settledClearance << '\n';

    const glm::vec3 jumpUp = glm::normalize(standing.position);
    const float beforeJump = glm::length(standing.position);
    standing.velocity = jumpUp * 5.5f;
    standing.isGrounded = true;
    for (int i = 0; i < 1200; ++i) {
        physics.StepSimulation(standing, fixedDt, world);
        maximumJumpHeight = std::max(maximumJumpHeight,
            glm::length(standing.position) - beforeJump);
        if (!fw::PlanetaryCameraFrame::IsFinite(standing.position) ||
            !fw::PlanetaryCameraFrame::IsFinite(standing.velocity)) pass = false;
    }
    pass = pass && maximumJumpHeight > 0.25f && standing.isGrounded &&
        glm::length(standing.position) - surfaceRadius >= -0.005f;
    std::cout << "[DIAG] jump/landing=" << pass << " jumpHeight=" << maximumJumpHeight
              << " finalGrounded=" << standing.isGrounded
              << " finalClearance=" << glm::length(standing.position) - surfaceRadius << '\n';

    return pass;
}

struct P03CollisionWitness {
    bool found = false;
    size_t candidates = 0;
    glm::vec3 startPosition{0.0f};
    glm::vec3 finalPosition{0.0f};
    glm::vec3 initialVelocity{0.0f};
    glm::ivec3 contactVoxel{-1};
    int probes = 0;
    int bottomSupportProbes = 0;
};

// Search the loaded Tiny fixture for a downward sweep that marks Grounded from
// a voxel intersecting only the upper/perimeter part of the cylinder, with no
// solid voxel under the foot footprint. This is characterization only.
P03CollisionWitness FindP03FalseGroundWitness(fw::GameWorld& world) {
    constexpr float dt = 1.0f / 60.0f;
    P03CollisionWitness witness{};
    for (int angleStep = 0; angleStep <= 100; ++angleStep) {
        const float theta = (20.0f + 0.5f * angleStep) * 3.14159265358979323846f / 180.0f;
        const glm::vec3 direction(std::sin(theta), 0.0f, std::cos(theta));
        for (int radiusStep = 0; radiusStep <= 80; ++radiusStep) {
            const float radius = 27.0f + 0.25f * radiusStep;
            RigidBody body{};
            body.position = direction * radius;
            body.velocity = -direction * 1.0f;
            body.dbg_countCollisionProbes = true;
            const glm::vec3 start = body.position;
            PhysicsEngine physics;
            ++witness.candidates;
            physics.StepSimulation(body, dt, world);
            if (!body.isGrounded || !body.dbg_radialGroundContact) continue;

            glm::vec3 up;
            if (!fw::PlanetaryCameraFrame::Normalize(body.position, up)) continue;
            fw::PlanetaryCameraFrame::State frame{
                body.planetaryCollisionFrameUp, body.planetaryCollisionFrameForward,
                body.planetaryCollisionFrameInitialized};
            if (!fw::PlanetaryCameraFrame::TransportTo(frame, up)) continue;
            glm::vec3 right;
            if (!fw::PlanetaryCameraFrame::Normalize(glm::cross(frame.forward, up), right)) continue;
            bool solidBelowFoot = false;
            int bottomQueries = 0;
            constexpr int footProbeCount = 17;
            std::array<glm::vec2, footProbeCount> offsets{};
            offsets[0] = {0.0f, 0.0f};
            const float r = body.radius;
            offsets[1] = {r, 0}; offsets[2] = {-r, 0}; offsets[3] = {0, r}; offsets[4] = {0, -r};
            offsets[5] = {r * 0.707f, r * 0.707f}; offsets[6] = {-r * 0.707f, r * 0.707f};
            offsets[7] = {r * 0.707f, -r * 0.707f}; offsets[8] = {-r * 0.707f, -r * 0.707f};
            for (int sample = 0; sample < 8; ++sample) {
                const float angle = 6.28318530718f * (static_cast<float>(sample) + 0.5f) / 8.0f;
                offsets[9 + sample] = {r * 0.95f * std::cos(angle), r * 0.95f * std::sin(angle)};
            }
            for (const auto& offset : offsets) {
                ++bottomQueries;
                const glm::vec3 belowFoot = body.position - up * 0.02f +
                    right * offset.x + frame.forward * offset.y;
                if (world.QueryBlockCartesian(belowFoot).status == fw::WorldVoxelQueryStatus::SolidVoxel) {
                    solidBelowFoot = true;
                    break;
                }
            }
            if (!solidBelowFoot) {
                witness.found = true;
                witness.startPosition = start;
                witness.finalPosition = body.position;
                witness.initialVelocity = -direction;
                witness.contactVoxel = body.dbg_groundVoxelCoord;
                witness.probes = body.dbg_collisionProbeCount;
                witness.bottomSupportProbes = bottomQueries;
                return witness;
            }
        }
    }
    return witness;
}

bool RunP03CollisionProbeMatrix(fw::GameWorld& world) {
    constexpr float dt = 1.0f / 60.0f;
    struct Scenario {
        const char* name;
        glm::vec3 direction;
        float clearance;
        float radialSpeed;
        float tangentSpeed;
        int ticks;
    };
    const glm::vec3 corner = glm::normalize(glm::vec3(1.0f));
    const float edgeAngle = 44.5f * 3.14159265358979323846f / 180.0f;
    const glm::vec3 seamDirection(std::sin(edgeAngle), 0.0f, std::cos(edgeAngle));
    const float boundaryU = 0.5f + 1.0f / static_cast<float>(fw::PlanetMath::GetFaceResolution(fw::PlanetSize::Tiny));
    const glm::vec3 cellEdge = fw::CubeSphereMapping::FaceUVToDirection(
        fw::CubeFace::PositiveZ, {boundaryU, 0.5f});
    const std::array<Scenario, 14> scenarios = {{
        {"idle_+Z", {0,0,1}, 0.11f, 0.0f, 0.0f, 30},
        {"radial_fall", {0,0,1}, 1.0f, -1.0f, 0.0f, 30},
        {"tangent_walk", {0,0,1}, 0.11f, 0.0f, 5.0f, 30},
        {"diagonal", {0,0,1}, 0.11f, 0.0f, 7.0710678f, 30},
        {"voxel_edge", cellEdge, 0.11f, -0.5f, 5.0f, 20},
        {"cross_face", seamDirection, 0.11f, 0.0f, 5.0f, 30},
        {"cube_corner", corner, 0.11f, -0.25f, 2.0f, 20},
        {"high_speed_60", {1,0,0}, 0.11f, 0.0f, 60.0f, 1},
        {"high_speed_200", {-1,0,0}, 0.11f, -5.0f, 200.0f, 1},
        {"face_+Y", {0,1,0}, 0.11f, 0.0f, 1.0f, 10},
        {"face_-Y", {0,-1,0}, 0.11f, 0.0f, 1.0f, 10},
        {"face_-Z", {0,0,-1}, 0.11f, 0.0f, 1.0f, 10},
        {"face_+X", {1,0,0}, 0.11f, -0.5f, 1.0f, 10},
        {"face_-X", {-1,0,0}, 0.11f, -0.5f, 1.0f, 10}
    }};
    bool valid = true;
    size_t totalProbes = 0;
    int maximumProbes = 0;
    uint64_t maximumTickNanoseconds = 0;
    float maximumDensePenetration = 0.0f;
    for (const auto& scenario : scenarios) {
        glm::vec3 spawn(0.0f);
        const auto spawnStatus = world.TryResolvePlanetarySpawn(
            scenario.direction, scenario.clearance, 0.3f, spawn);
        if (spawnStatus != fw::PlanetarySpawnResolveStatus::Ready) {
            std::cout << "[P03][NOT RUN] " << scenario.name << " spawnStatus="
                      << static_cast<unsigned>(spawnStatus) << '\n';
            valid = false;
            continue;
        }
        glm::vec3 up = glm::normalize(spawn);
        glm::vec3 tangent = glm::cross(up, glm::vec3(0.0f, 1.0f, 0.0f));
        if (glm::length(tangent) < 1.0e-4f) tangent = glm::cross(up, glm::vec3(1.0f, 0.0f, 0.0f));
        tangent = glm::normalize(tangent);
        RigidBody body{};
        body.position = spawn;
        body.velocity = up * scenario.radialSpeed + tangent * scenario.tangentSpeed;
        body.dbg_countCollisionProbes = true;
        size_t scenarioProbes = 0;
        int scenarioMaximum = 0;
        uint64_t tickMaximumNs = 0;
        uint64_t scenarioTotalNs = 0;
        uint64_t scenarioQueryNs = 0;
        size_t recoveryProbes = 0;
        size_t refinementProbes = 0;
        float scenarioMaximumPenetration = 0.0f;
        for (int tick = 0; tick < scenario.ticks; ++tick) {
            const auto start = std::chrono::steady_clock::now();
            PhysicsEngine physics;
            physics.StepSimulation(body, dt, world);
            const auto elapsed = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start).count());
            scenarioProbes += static_cast<size_t>(std::max(0, body.dbg_collisionProbeCount));
            scenarioMaximum = std::max(scenarioMaximum, body.dbg_collisionProbeCount);
            tickMaximumNs = std::max(tickMaximumNs, elapsed);
            scenarioTotalNs += elapsed;
            scenarioQueryNs += body.dbg_collisionQueryNanoseconds;
            recoveryProbes += static_cast<size_t>(body.dbg_initialRecoveryProbeCount);
            refinementProbes += static_cast<size_t>(body.dbg_contactRefinementProbeCount);
            valid = valid && fw::PlanetaryCameraFrame::IsFinite(body.position) &&
                fw::PlanetaryCameraFrame::IsFinite(body.velocity);
        }
        const ColliderVolumeAudit volume = AuditDensePlanetaryCollider(world, body, fw::PlanetSize::Tiny);
        scenarioMaximumPenetration = volume.maximumRadialPenetration;
        maximumDensePenetration = std::max(maximumDensePenetration, scenarioMaximumPenetration);
        valid = valid && scenarioMaximumPenetration <= 0.005f;
        totalProbes += scenarioProbes;
        maximumProbes = std::max(maximumProbes, scenarioMaximum);
        maximumTickNanoseconds = std::max(maximumTickNanoseconds, tickMaximumNs);
        std::cout << "[P03][CASE] " << scenario.name << " ticks=" << scenario.ticks
                  << " probesTotal=" << scenarioProbes << " probesPerTickMax=" << scenarioMaximum
                  << " maxTickNs=" << tickMaximumNs << " totalNs=" << scenarioTotalNs
                  << " queryNs=" << scenarioQueryNs
                  << " solverNsApprox=" << (scenarioTotalNs > scenarioQueryNs ? scenarioTotalNs - scenarioQueryNs : 0)
                  << " recoveryProbes=" << recoveryProbes << " refinementProbes=" << refinementProbes
                  << " grounded=" << body.isGrounded
                  << " maxDensePenetration=" << scenarioMaximumPenetration
                  << " radialClearance=" << (glm::length(body.position) - glm::length(spawn))
                  << " finalPosition=" << body.position.x << ',' << body.position.y << ','
                  << body.position.z << '\n';
    }
    std::cout << "[P03][BENCH] scenarios=" << scenarios.size() << " probesTotal=" << totalProbes
              << " probesPerTickMax=" << maximumProbes << " worstDebugTickNs="
              << maximumTickNanoseconds << " maxDensePenetration=" << maximumDensePenetration << '\n';
    return valid;
}

bool RunP03DeepOverlapDiagnostic(fw::GameWorld& world) {
    glm::vec3 spawn(0.0f);
    float surfaceRadius = 0.0f;
    const glm::vec3 direction = fw::CubeSphereMapping::FaceUVToDirection(
        fw::CubeFace::PositiveZ, {0.5f, 0.5f});
    if (world.TryResolvePlanetarySpawn(direction, 0.11f, 0.3f, spawn, &surfaceRadius) !=
        fw::PlanetarySpawnResolveStatus::Ready) {
        std::cout << "[P03][NOT RUN] deep_overlap no valid fixture spawn\n";
        return false;
    }
    RigidBody body{};
    body.position = direction * (surfaceRadius - 1.0f);
    body.dbg_countCollisionProbes = true;
    PhysicsEngine physics;
    physics.StepSimulation(body, PhysicsEngine::FIXED_DT, world);
    const ColliderVolumeAudit volume = AuditDensePlanetaryCollider(world, body, fw::PlanetSize::Tiny);
    const float radialVelocity = glm::dot(body.velocity, glm::normalize(body.position));
    std::cout << "[P03][OBSERVED] deep_overlap startRadius=" << surfaceRadius - 1.0f
              << " finalRadius=" << glm::length(body.position)
              << " measuredSurfaceRadius=" << surfaceRadius
              << " remainingRadialPenetration=" << volume.maximumRadialPenetration
              << " denseSolidSamples=" << volume.solidSamples
              << " probes=" << body.dbg_collisionProbeCount
              << " queryNs=" << body.dbg_collisionQueryNanoseconds
              << " recoveryProbes=" << body.dbg_initialRecoveryProbeCount
              << " refinementProbes=" << body.dbg_contactRefinementProbeCount
              << " grounded=" << body.isGrounded
              << " unresolved=" << body.dbg_initialPenetrationUnresolved
              << " radialVelocity=" << radialVelocity
              << " position=" << body.position.x << ',' << body.position.y << ',' << body.position.z << '\n';
    return !body.dbg_initialPenetrationUnresolved && volume.maximumRadialPenetration <= 0.005f &&
        radialVelocity <= 0.02f;
}

bool InitializeP03aSyntheticPlanet(SharedContext& context, fw::GameWorld& world,
                                  bool fillRadialSurface, uint32_t planetValue) {
    world.Initialize(&context);
    context.forgeWorld = &world;
    auto& registry = world.GetRegistry();
    const entt::entity planet = world.GetPlanetEntity();
    if (!registry.valid(planet) || !registry.all_of<fw::PlanetGeometryComponent>(planet)) return false;
    auto& geometry = registry.get<fw::PlanetGeometryComponent>(planet);
    geometry.planetSize = fw::PlanetSize::Tiny;
    geometry.isLogicalSphere = true;
    constexpr int n = 3;
    const fw::PlanetID id{planetValue};
    for (int face = 0; face < 6; ++face) {
        for (int row = 0; row < n; ++row) {
            for (int col = 0; col < n; ++col) {
                const fw::PlanetChunkCoord coord{id, static_cast<fw::CubeFace>(face), col, row, 0};
                int keyX = 0, keyZ = 0;
                if (!fw::planet_grid_compat::TryPlanetChunkCoordToLegacyNet(coord, n, keyX, keyZ)) return false;
                const entt::entity chunkEntity = world.CreateChunkEntity(
                    "P03aSynthetic", {0.0f, 0.0f, 0.0f}, keyX, keyZ);
                if (chunkEntity == entt::null || !registry.valid(chunkEntity)) return false;
                auto& chunk = registry.get<fw::VoxelChunkComponent>(chunkEntity);
                chunk.planetCoord = coord;
                chunk.isGenerated = true;
                if (fillRadialSurface) {
                    for (int x = 0; x < fw::ChunkDimensions::VoxelsX; ++x)
                        for (int z = 0; z < fw::ChunkDimensions::VoxelsZ; ++z)
                            chunk.blocks[x][80][z] = static_cast<uint8_t>(fw::BlockType::Stone);
                }
            }
        }
    }
    return world.GetLoadedChunkCount() == 54;
}

bool SetP03aVoxelAtWorldPosition(fw::GameWorld& world, const glm::vec3& position, uint8_t blockId) {
    float flatX = 0.0f, localY = 0.0f, flatZ = 0.0f;
    fw::MapWorldGenerator::WorldToVoxelCoord(
        fw::PlanetSize::Tiny, false, position, flatX, localY, flatZ);
    if (!std::isfinite(flatX) || !std::isfinite(localY) || !std::isfinite(flatZ)) return false;
    const int voxelX = static_cast<int>(std::floor(flatX));
    const int voxelY = static_cast<int>(std::floor(localY));
    const int voxelZ = static_cast<int>(std::floor(flatZ));
    if (voxelX < 0 || voxelZ < 0 || voxelY < 0 || voxelY >= fw::ChunkDimensions::VoxelsY) return false;
    const int chunkX = voxelX / fw::ChunkDimensions::VoxelsX;
    const int chunkZ = voxelZ / fw::ChunkDimensions::VoxelsZ;
    const entt::entity entity = world.GetChunkManager().GetChunkEntity(chunkX, chunkZ);
    auto& registry = world.GetRegistry();
    if (entity == entt::null || !registry.valid(entity) || !registry.all_of<fw::VoxelChunkComponent>(entity)) return false;
    auto& chunk = registry.get<fw::VoxelChunkComponent>(entity);
    const int localX = voxelX - chunkX * fw::ChunkDimensions::VoxelsX;
    const int localZ = voxelZ - chunkZ * fw::ChunkDimensions::VoxelsZ;
    chunk.blocks[localX][voxelY][localZ] = blockId;
    return true;
}

bool SetP03aVoxelByCoord(fw::GameWorld& world, const glm::ivec3& voxelCoord, uint8_t blockId) {
    if (voxelCoord.x < 0 || voxelCoord.z < 0 || voxelCoord.y < 0 ||
        voxelCoord.y >= fw::ChunkDimensions::VoxelsY) return false;
    const int chunkX = voxelCoord.x / fw::ChunkDimensions::VoxelsX;
    const int chunkZ = voxelCoord.z / fw::ChunkDimensions::VoxelsZ;
    const entt::entity entity = world.GetChunkManager().GetChunkEntity(chunkX, chunkZ);
    auto& registry = world.GetRegistry();
    if (entity == entt::null || !registry.valid(entity) || !registry.all_of<fw::VoxelChunkComponent>(entity)) return false;
    auto& chunk = registry.get<fw::VoxelChunkComponent>(entity);
    chunk.blocks[voxelCoord.x - chunkX * fw::ChunkDimensions::VoxelsX]
        [voxelCoord.y]
        [voxelCoord.z - chunkZ * fw::ChunkDimensions::VoxelsZ] = blockId;
    return true;
}

bool HasP03aFootSupport(const fw::GameWorld& world, const RigidBody& body) {
    glm::vec3 up;
    if (!fw::PlanetaryCameraFrame::Normalize(body.position, up)) return false;
    fw::PlanetaryCameraFrame::State frame{
        body.planetaryCollisionFrameUp, body.planetaryCollisionFrameForward,
        body.planetaryCollisionFrameInitialized};
    if (!fw::PlanetaryCameraFrame::TransportTo(frame, up)) return false;
    glm::vec3 right;
    if (!fw::PlanetaryCameraFrame::Normalize(glm::cross(frame.forward, up), right)) return false;
    const float r = body.radius;
    std::array<glm::vec2, 17> offsets{{
        {0,0},{r,0},{-r,0},{0,r},{0,-r},
        {r*.707f,r*.707f},{-r*.707f,r*.707f},{r*.707f,-r*.707f},{-r*.707f,-r*.707f},
        {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}};
    for (int i = 0; i < 8; ++i) {
        const float angle = 6.28318530718f * (static_cast<float>(i) + 0.5f) / 8.0f;
        offsets[9 + i] = {r * 0.95f * std::cos(angle), r * 0.95f * std::sin(angle)};
    }
    for (const auto& offset : offsets) {
        const glm::vec3 sample = body.position - up * 0.02f +
            right * offset.x + frame.forward * offset.y;
        if (world.QueryBlockCartesian(sample).status == fw::WorldVoxelQueryStatus::SolidVoxel) return true;
    }
    return false;
}

size_t ClearP03aFootprint(fw::GameWorld& world, const RigidBody& body) {
    glm::vec3 up;
    if (!fw::PlanetaryCameraFrame::Normalize(body.position, up)) return 0;
    fw::PlanetaryCameraFrame::State frame{
        body.planetaryCollisionFrameUp, body.planetaryCollisionFrameForward,
        body.planetaryCollisionFrameInitialized};
    if (!fw::PlanetaryCameraFrame::TransportTo(frame, up)) return 0;
    glm::vec3 right;
    if (!fw::PlanetaryCameraFrame::Normalize(glm::cross(frame.forward, up), right)) return 0;
    const float r = body.radius;
    std::array<glm::vec2, 17> offsets{{
        {0,0},{r,0},{-r,0},{0,r},{0,-r},
        {r*.707f,r*.707f},{-r*.707f,r*.707f},{r*.707f,-r*.707f},{-r*.707f,-r*.707f},
        {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}};
    for (int i = 0; i < 8; ++i) {
        const float angle = 6.28318530718f * (static_cast<float>(i) + 0.5f) / 8.0f;
        offsets[9 + i] = {r * 0.95f * std::cos(angle), r * 0.95f * std::sin(angle)};
    }
    std::set<std::tuple<int,int,int>> cleared;
    for (const auto& offset : offsets) {
        const glm::vec3 sample = body.position - up * 0.02f +
            right * offset.x + frame.forward * offset.y;
        const auto query = world.QueryBlockCartesian(sample);
        if (query.status == fw::WorldVoxelQueryStatus::SolidVoxel &&
            cleared.emplace(query.voxelCoord.x, query.voxelCoord.y, query.voxelCoord.z).second) {
            SetP03aVoxelByCoord(world, query.voxelCoord, 0);
        }
    }
    return cleared.size();
}

bool TestP03aCollisionContracts() {
    constexpr float fixedDt = 1.0f / 60.0f;
    constexpr uint8_t stone = static_cast<uint8_t>(fw::BlockType::Stone);
    const glm::vec3 up(0.0f, 0.0f, 1.0f);
    const float planetRadius = fw::PlanetMath::GetPlanetRadius(fw::PlanetSize::Tiny);
    const float surfaceRadius = planetRadius + 16.5f; // top face of synthetic radial voxel layer 80
    bool allPass = true;

    // 1. Flat radial support / 2. single-cell support (edge/corner).
    {
        SharedContext context{};
        fw::GameWorld world;
        if (!InitializeP03aSyntheticPlanet(context, world, true, 0x50334101u)) return false;
        PhysicsEngine physics;
        RigidBody flat{};
        flat.position = up * (surfaceRadius + 0.11f);
        bool grounded = false;
        for (int tick = 0; tick < 240; ++tick) {
            physics.StepSimulation(flat, fixedDt, world);
            grounded = grounded || flat.isGrounded;
        }
        const bool footSupport = HasP03aFootSupport(world, flat);
        const bool flatPass = grounded && flat.isGrounded && footSupport && !flat.dbg_terrainPending;
        std::cout << "[P03A][CASE] flat_floor expected=Grounded observed=" << flat.isGrounded
                  << " support=" << footSupport << " contacts=" << flat.dbg_voxelContacts
                  << " finalRadius=" << glm::length(flat.position) << " PASS=" << flatPass << '\n';
        allPass = allPass && flatPass;

        glm::vec3 edgePosition(0.0f);
        if (world.TryResolvePlanetarySpawn(up, 0.11f, 0.3f, edgePosition) !=
            fw::PlanetarySpawnResolveStatus::Ready) return false;
        // Remove one actual supporting voxel found by the same footprint used
        // by HasP03aFootSupport; the coordinate chart's central cell can be Air
        // while a perimeter sample owns the physical contact at a seam.
        glm::vec3 edgeUp;
        fw::PlanetaryCameraFrame::Normalize(flat.position, edgeUp);
        const auto edgeFrameInit = fw::PlanetaryCameraFrame::MakeInitial(edgeUp);
        glm::vec3 edgeRight = glm::normalize(glm::cross(edgeFrameInit.forward, edgeUp));
        bool removedEdgeSupport = false;
        size_t removedEdgeVoxels = 0;
        for (int sample = 0; sample < 17; ++sample) {
            glm::vec2 offset(0.0f);
            if (sample >= 1 && sample <= 4) {
                const glm::vec2 cardinals[] = {{.3f,0},{-.3f,0},{0,.3f},{0,-.3f}};
                offset = cardinals[sample - 1];
            } else if (sample >= 5 && sample <= 8) {
                const glm::vec2 diagonals[] = {{.2121f,.2121f},{-.2121f,.2121f},{.2121f,-.2121f},{-.2121f,-.2121f}};
                offset = diagonals[sample - 5];
            } else if (sample >= 9) {
                const float angle = 6.28318530718f * (static_cast<float>(sample - 9) + .5f) / 8.0f;
                offset = { .285f * std::cos(angle), .285f * std::sin(angle) };
            }
            const glm::vec3 samplePosition = flat.position - edgeUp * .02f +
                edgeRight * offset.x + edgeFrameInit.forward * offset.y;
            const auto support = world.QueryBlockCartesian(samplePosition);
            if (support.status == fw::WorldVoxelQueryStatus::SolidVoxel)
                removedEdgeVoxels += SetP03aVoxelByCoord(world, support.voxelCoord, 0) ? 1u : 0u;
        }
        removedEdgeSupport = removedEdgeVoxels > 0;
        if (!removedEdgeSupport) {
            std::cout << "[P03A][FIXTURE_FAIL] no removable support voxel in foot footprint\n";
            return false;
        }
        RigidBody edge{};
        edge.position = edgePosition;
        for (int tick = 0; tick < 120; ++tick) physics.StepSimulation(edge, fixedDt, world);
        const bool edgeFoot = HasP03aFootSupport(world, edge);
        const bool edgePass = !edge.isGrounded || edgeFoot;
        std::cout << "[P03A][CASE] floor_edge expected=GroundedOnlyWithFootSupport observed="
                  << edge.isGrounded << " support=" << edgeFoot << " PASS=" << edgePass << '\n';
        allPass = allPass && edgePass;
    }

    // 3. A single synthetic voxel must intersect only the upper collision
    // samples. Validate every required query before asking the solver to run.
    {
        SharedContext context{};
        fw::GameWorld world;
        if (!InitializeP03aSyntheticPlanet(context, world, false, 0x50334102u)) return false;
        RigidBody body{};
        const float planetRadius = fw::PlanetMath::GetPlanetRadius(fw::PlanetSize::Tiny);
        constexpr int solidLocalY = 80;
        const float solidCenterRadius = fw::PlanetRadialMapping::GetCellCenterRadius(
            0, solidLocalY, planetRadius);
        if (!SetP03aVoxelAtWorldPosition(world, up * solidCenterRadius, stone)) return false;

        const float expectedContactBaseRadius = solidCenterRadius - (body.height - 0.1f) + 0.4f;
        const glm::vec3 expectedContactBase = up * expectedContactBaseRadius;
        const auto contactFrame = fw::PlanetaryCameraFrame::MakeInitial(up);
        glm::vec3 contactRight(0.0f);
        if (!fw::PlanetaryCameraFrame::Normalize(glm::cross(contactFrame.forward, up), contactRight)) return false;
        std::array<glm::vec2, 17> probeOffsets{{
            {0,0},{body.radius,0},{-body.radius,0},{0,body.radius},{0,-body.radius},
            {body.radius*.707f,body.radius*.707f},{-body.radius*.707f,body.radius*.707f},
            {body.radius*.707f,-body.radius*.707f},{-body.radius*.707f,-body.radius*.707f},
            {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}};
        for (int sample = 0; sample < 8; ++sample) {
            const float angle = 6.28318530718f * (static_cast<float>(sample) + 0.5f) / 8.0f;
            probeOffsets[9 + sample] = {body.radius * 0.95f * std::cos(angle),
                                        body.radius * 0.95f * std::sin(angle)};
        }
        const auto upperSample = world.QueryBlockCartesian(
            expectedContactBase + up * (body.height - 0.1f));
        bool lowerSamplesAir = true;
        bool lowerSamplesResolved = true;
        // The base and near-foot production probe rows must be clear. The
        // half-height row is intentionally in the upper torso and may be the
        // first colliding row; the top row is independently confirmed solid.
        constexpr std::array<float, 2> lowerHeights{{0.0f, 0.1f}};
        for (float height : lowerHeights) {
            for (const auto& offset : probeOffsets) {
                const auto lower = world.QueryBlockCartesian(expectedContactBase + up * height +
                    contactRight * offset.x + contactFrame.forward * offset.y);
                lowerSamplesAir = lowerSamplesAir && lower.status == fw::WorldVoxelQueryStatus::Air;
                lowerSamplesResolved = lowerSamplesResolved &&
                    lower.status != fw::WorldVoxelQueryStatus::TerrainPending &&
                    lower.status != fw::WorldVoxelQueryStatus::OutOfBounds &&
                    lower.status != fw::WorldVoxelQueryStatus::InvalidInput;
            }
        }
        RigidBody expectedContactBody = body;
        expectedContactBody.position = expectedContactBase;
        expectedContactBody.planetaryCollisionFrameUp = contactFrame.up;
        expectedContactBody.planetaryCollisionFrameForward = contactFrame.forward;
        expectedContactBody.planetaryCollisionFrameInitialized = contactFrame.initialized;
        const bool footSamplesAir = !HasP03aFootSupport(world, expectedContactBody);
        const bool fixturePreconditions = lowerSamplesAir && lowerSamplesResolved &&
            footSamplesAir &&
            upperSample.status == fw::WorldVoxelQueryStatus::SolidVoxel &&
            upperSample.voxelCoord.y == solidLocalY;
        std::cout << "[P03D2][UPPER_ONLY_PRECHECK] lowerAir=" << lowerSamplesAir
                  << " lowerResolved=" << lowerSamplesResolved
                  << " upperStatus=" << static_cast<unsigned>(upperSample.status)
                  << " upperVoxel=" << upperSample.voxelCoord.x << ',' << upperSample.voxelCoord.y
                  << ',' << upperSample.voxelCoord.z << " expectedY=" << solidLocalY
                  << " bodyHeight=" << body.height << " expectedBaseRadius=" << expectedContactBaseRadius
                  << " solidCenterRadius=" << solidCenterRadius
                  << " footSamplesAir=" << footSamplesAir << '\n';
        if (!fixturePreconditions) {
            std::cout << "[P03D2][UPPER_ONLY] NOT VERIFIED: synthetic fixture preconditions failed\n";
            return false;
        }

        body.position = up * (expectedContactBaseRadius + 2.0f);
        body.velocity = -up;
        PhysicsEngine physics;
        bool falseGrounded = false;
        int firstGroundedTick = -1;
        for (int tick = 0; tick < 360; ++tick) {
            physics.StepSimulation(body, fixedDt, world);
            const bool footSupport = HasP03aFootSupport(world, body);
            if (body.isGrounded) {
                firstGroundedTick = tick;
                falseGrounded = !footSupport;
                break;
            }
        }
        std::cout << "[P03D2][UPPER_ONLY] fixtureValid=" << fixturePreconditions
                  << " observedGrounded="
                  << body.isGrounded << " footSupport=" << HasP03aFootSupport(world, body)
                  << " falseGrounded=" << falseGrounded << " tick=" << firstGroundedTick
                  << " position=" << body.position.x << ',' << body.position.y << ',' << body.position.z
                  << " contacts=" << body.dbg_voxelContacts
                  << " radialContact=" << body.dbg_radialGroundContact
                  << " supportVoxel=" << body.dbg_groundVoxelCoord.x << ','
                  << body.dbg_groundVoxelCoord.y << ',' << body.dbg_groundVoxelCoord.z
                  << " lastSample=" << static_cast<unsigned>(body.dbg_lastPlanetarySample) << '\n';
        if (falseGrounded) {
            std::cout << "[P03D2][OBSERVED] false Grounded with no foot support; physics unchanged\n";
        } else if (fixturePreconditions && body.isGrounded && HasP03aFootSupport(world, body)) {
            std::cout << "[P03D2][NOT VERIFIED] candidate upper-body fixture reached ordinary foot support first; "
                         "no upper-body-only Grounded result was exercised\n";
        } else if (!body.isGrounded) {
            std::cout << "[P03D2][NOT VERIFIED] solver did not reach a Grounded result in the bounded fixture run\n";
        } else {
            std::cout << "[P03D2][NOT VERIFIED] unexpected fixture outcome; inspect precheck and contact trace\n";
        }
    }

    // 4. Support disappearing, 5. unavailable chunk and query classification.
    {
        SharedContext context{};
        fw::GameWorld world;
        if (!InitializeP03aSyntheticPlanet(context, world, true, 0x50334103u)) return false;
        PhysicsEngine physics;
        RigidBody body{};
        body.position = up * (surfaceRadius + 0.11f);
        for (int tick = 0; tick < 120; ++tick) physics.StepSimulation(body, fixedDt, world);
        const bool groundedBeforeRemoval = body.isGrounded;
        glm::vec3 localUp = glm::normalize(body.position);
        const size_t removedVoxels = ClearP03aFootprint(world, body);
        body.position = up * (surfaceRadius + 0.001f);
        body.velocity = -up;
        physics.StepSimulation(body, fixedDt, world);
        const bool disappearedPass = groundedBeforeRemoval && !body.isGrounded;
        std::cout << "[P03A][CASE] support_removed expected=Airborne observedGrounded="
                  << body.isGrounded << " priorGrounded=" << groundedBeforeRemoval
                  << " clearedVoxels=" << removedVoxels << " PASS=" << disappearedPass << '\n';
        allPass = allPass && disappearedPass;

        glm::vec3 pendingBodyPosition = up * (surfaceRadius + 0.001f);
        auto queryBelow = world.QueryBlockCartesian(pendingBodyPosition - up * 0.02f);
        if (queryBelow.voxelCoord.y < 0) return false;
        const int cx = queryBelow.voxelCoord.x / fw::ChunkDimensions::VoxelsX;
        const int cz = queryBelow.voxelCoord.z / fw::ChunkDimensions::VoxelsZ;
        const entt::entity centerEntity = world.GetChunkManager().GetChunkEntity(cx, cz);
        if (centerEntity == entt::null || !world.GetRegistry().valid(centerEntity)) return false;
        auto& centerChunk = world.GetRegistry().get<fw::VoxelChunkComponent>(centerEntity);
        centerChunk.isGenerated = false;
        const auto pendingQuery = world.QueryBlockCartesian(pendingBodyPosition - up * 0.02f);
        RigidBody pending{};
        pending.position = pendingBodyPosition;
        pending.velocity = -up;
        physics.StepSimulation(pending, fixedDt, world);
        const bool pendingPass = pendingQuery.status == fw::WorldVoxelQueryStatus::TerrainPending &&
            pending.dbg_terrainPending && !pending.isGrounded;
        std::cout << "[P03A][CASE] pending_chunk query=" << static_cast<unsigned>(pendingQuery.status)
                  << " pendingObserved=" << pending.dbg_terrainPending << " grounded=" << pending.isGrounded
                  << " PASS=" << pendingPass << '\n';
        allPass = allPass && pendingPass;

        centerChunk.isGenerated = true;
        const auto removedEntity = centerEntity;
        world.DestroyEntity(removedEntity);
        const auto missingQuery = world.QueryBlockCartesian(pendingBodyPosition - up * 0.02f);
        std::cout << "[P03A][CASE] missing_chunk query=" << static_cast<unsigned>(missingQuery.status)
                  << " collapsedWithPending=" << (missingQuery.status == pendingQuery.status)
                  << "\n";
        allPass = allPass && missingQuery.status == fw::WorldVoxelQueryStatus::TerrainPending &&
            missingQuery.status == pendingQuery.status;
    }

    // 6. Component presence versus logical-sphere dispatch in a legacy Cartesian scene.
    {
        SharedContext context{};
        fw::GameWorld world;
        world.Initialize(&context);
        auto& registry = world.GetRegistry();
        const auto planet = world.GetPlanetEntity();
        if (!registry.valid(planet) || !registry.all_of<fw::PlanetGeometryComponent>(planet)) return false;
        registry.get<fw::PlanetGeometryComponent>(planet).isLogicalSphere = false;
        const entt::entity floorEntity = world.CreateChunkEntity("P03aLegacyFloor", {0,0,0}, 0, 0);
        if (floorEntity == entt::null || !registry.valid(floorEntity)) return false;
        auto& floor = registry.get<fw::VoxelChunkComponent>(floorEntity);
        floor.isGenerated = true;
        for (int x = 7; x < 14; ++x) for (int z = 7; z < 14; ++z) floor.blocks[x][0][z] = stone;
        RigidBody body{};
        body.position = {10.5f, 1.1f, 10.5f};
        PhysicsEngine physics;
        for (int tick = 0; tick < 90; ++tick) physics.StepSimulation(body, fixedDt, world);
        const bool cartesianExpected = std::abs(body.position.x - 10.5f) < 0.05f &&
            std::abs(body.position.z - 10.5f) < 0.05f && body.position.y > 0.9f && body.isGrounded;
        std::cout << "[P03A][CASE] logicalSphere_false expected=CartesianVerticalSupport observed="
                  << body.position.x << ',' << body.position.y << ',' << body.position.z
                  << " grounded=" << body.isGrounded << " PASS=" << cartesianExpected << '\n';
        allPass = allPass && cartesianExpected;
    }

    // 7. Out-of-bounds, air and passable remain classifications, never false Grounded.
    {
        SharedContext context{};
        fw::GameWorld world;
        world.Initialize(&context);
        auto& geometry = world.GetRegistry().get<fw::PlanetGeometryComponent>(world.GetPlanetEntity());
        geometry.isLogicalSphere = true;
        geometry.planetSize = fw::PlanetSize::Huge;
        const auto air = world.QueryBlockCartesian(glm::vec3(0.0f, 0.0f, 10000.0f));
        const auto oob = world.QueryBlockCartesian(glm::vec3(0.0f));
        const auto invalid = world.QueryBlockCartesian(glm::vec3(
            std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f));
        const bool classificationsPass = air.status == fw::WorldVoxelQueryStatus::Air &&
            oob.status == fw::WorldVoxelQueryStatus::OutOfBounds &&
            invalid.status == fw::WorldVoxelQueryStatus::InvalidInput;
        std::cout << "[P03A][CASE] query_classes Air=" << static_cast<unsigned>(air.status)
                  << " OutOfBounds=" << static_cast<unsigned>(oob.status)
                  << " InvalidInput=" << static_cast<unsigned>(invalid.status)
                  << " PASS=" << classificationsPass << '\n';
        allPass = allPass && classificationsPass;
    }

    // 8-10. Cartesian wall, one-voxel step, and ceiling fixtures remain on the
    // legacy collision path when a PlanetGeometryComponent is non-logical.
    {
        SharedContext context{};
        fw::GameWorld world;
        world.Initialize(&context);
        auto& registry = world.GetRegistry();
        registry.get<fw::PlanetGeometryComponent>(world.GetPlanetEntity()).isLogicalSphere = false;
        const entt::entity floorEntity = world.CreateChunkEntity("P03aCartesianFixtures", {0,0,0}, 0, 0);
        if (floorEntity == entt::null || !registry.valid(floorEntity)) return false;
        auto& chunk = registry.get<fw::VoxelChunkComponent>(floorEntity);
        chunk.isGenerated = true;
        for (int x = 5; x < 16; ++x)
            for (int z = 5; z < 16; ++z)
                chunk.blocks[x][0][z] = stone;

        for (int y = 1; y <= 4; ++y) chunk.blocks[12][y][10] = stone;
        RigidBody wall{};
        wall.position = {10.5f, 1.1f, 10.5f};
        wall.velocity = {3.0f, 0.0f, 0.0f};
        PhysicsEngine physics;
        bool wallEncountered = false;
        float maximumWallX = wall.position.x;
        for (int tick = 0; tick < 90; ++tick) {
            physics.StepSimulation(wall, fixedDt, world);
            wallEncountered = wallEncountered || wall.isAgainstWall;
            maximumWallX = std::max(maximumWallX, wall.position.x);
        }
        const bool wallPass = wallEncountered && maximumWallX < 12.0f;
        std::cout << "[P03A][CASE] wall expected=blocked observedX=" << wall.position.x
                  << " maxX=" << maximumWallX << " encountered=" << wallEncountered
                  << " againstWallLastTick=" << wall.isAgainstWall << " PASS=" << wallPass << '\n';
        allPass = allPass && wallPass;

        chunk.blocks[12][1][12] = stone;
        RigidBody step{};
        step.position = {10.5f, 1.1f, 12.5f};
        step.velocity = {1.0f, 0.0f, 0.0f};
        for (int tick = 0; tick < 90; ++tick) physics.StepSimulation(step, fixedDt, world);
        const bool stepFinite = std::isfinite(step.position.x) && std::isfinite(step.position.y) &&
            std::isfinite(step.position.z) && step.position.x < 12.0f;
        std::cout << "[P03A][CASE] one_voxel_step expected=no penetration observed="
                  << step.position.x << ',' << step.position.y << ',' << step.position.z
                  << " grounded=" << step.isGrounded << " PASS=" << stepFinite << '\n';
        allPass = allPass && stepFinite;

        chunk.blocks[10][3][8] = stone;
        RigidBody ceiling{};
        ceiling.position = {10.5f, 1.1f, 8.5f};
        ceiling.velocity = {0.0f, 5.0f, 0.0f};
        float maximumCeilingY = ceiling.position.y;
        for (int tick = 0; tick < 30; ++tick) {
            physics.StepSimulation(ceiling, fixedDt, world);
            maximumCeilingY = std::max(maximumCeilingY, ceiling.position.y);
        }
        const bool ceilingPass = std::isfinite(ceiling.position.y) && maximumCeilingY < 3.0f;
        std::cout << "[P03A][CASE] ceiling expected=upward_motion_blocked observedY="
                  << ceiling.position.y << " maxY=" << maximumCeilingY
                  << " grounded=" << ceiling.isGrounded << " PASS=" << ceilingPass << '\n';
        allPass = allPass && ceilingPass;

        chunk.blocks[8][5][8] = static_cast<uint8_t>(fw::BlockType::Water);
        const auto passable = world.QueryBlockCartesian({8.0f, 5.0f, 8.0f});
        const bool passablePass = passable.status == fw::WorldVoxelQueryStatus::PassableVoxel;
        std::cout << "[P03A][CASE] passable_voxel status=" << static_cast<unsigned>(passable.status)
                  << " PASS=" << passablePass << '\n';
        allPass = allPass && passablePass;
    }
    return allPass;
}

// P03d.4 runner-only classification checks. Each synthetic contact is queried
// before the solver runs so a fixture that accidentally supplies foot support
// cannot be reported as an upper-body-only result.
bool TestP03d4GroundedContactScenarios() {
    constexpr float fixedDt = 1.0f / 60.0f;
    constexpr uint8_t stone = static_cast<uint8_t>(fw::BlockType::Stone);
    const glm::vec3 up(0.0f, 0.0f, 1.0f);
    const float planetRadius = fw::PlanetMath::GetPlanetRadius(fw::PlanetSize::Tiny);
    bool allPass = true;

    auto report = [](const char* scenario, const RigidBody& body, const glm::vec3& up,
                     bool fixtureValid, bool expectedGrounded, bool requireRadialContact) {
        const float radialVelocity = glm::dot(body.velocity, up);
        const float normalAlignment = glm::length(body.dbg_groundContactNormal) > 1.0e-6f
            ? glm::dot(glm::normalize(body.dbg_groundContactNormal), up) : 0.0f;
        const bool pass = fixtureValid && body.isGrounded == expectedGrounded &&
            (!requireRadialContact || (body.dbg_radialGroundContact && normalAlignment > 0.99f));
        std::cout << "[P03D4][CASE] " << scenario
                  << " fixtureValid=" << fixtureValid
                  << " expectedGrounded=" << expectedGrounded
                  << " observedGrounded=" << body.isGrounded
                  << " contacts=" << body.dbg_voxelContacts
                  << " radialContact=" << body.dbg_radialGroundContact
                  << " normalAlignment=" << normalAlignment
                  << " radialVelocity=" << radialVelocity
                  << " position=" << body.position.x << ',' << body.position.y << ',' << body.position.z
                  << " supportVoxel=" << body.dbg_groundVoxelCoord.x << ','
                  << body.dbg_groundVoxelCoord.y << ',' << body.dbg_groundVoxelCoord.z
                  << " queryState=" << static_cast<unsigned>(body.dbg_lastPlanetarySample)
                  << " PASS=" << pass << '\n';
        return pass;
    };
    auto footQueriesAirResolved = [](const fw::GameWorld& world, const RigidBody& body) {
        glm::vec3 localUp;
        if (!fw::PlanetaryCameraFrame::Normalize(body.position, localUp)) return false;
        fw::PlanetaryCameraFrame::State frame{
            body.planetaryCollisionFrameUp, body.planetaryCollisionFrameForward,
            body.planetaryCollisionFrameInitialized};
        if (!fw::PlanetaryCameraFrame::TransportTo(frame, localUp)) return false;
        glm::vec3 right;
        if (!fw::PlanetaryCameraFrame::Normalize(glm::cross(frame.forward, localUp), right)) return false;
        const float r = body.radius;
        const std::array<glm::vec2, 17> offsets = [&]() {
            std::array<glm::vec2, 17> values{{
                {0,0},{r,0},{-r,0},{0,r},{0,-r},
                {r*.707f,r*.707f},{-r*.707f,r*.707f},
                {r*.707f,-r*.707f},{-r*.707f,-r*.707f},
                {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}};
            for (int i = 0; i < 8; ++i) {
                const float angle = 6.28318530718f * (static_cast<float>(i) + 0.5f) / 8.0f;
                values[9 + i] = {r * 0.95f * std::cos(angle), r * 0.95f * std::sin(angle)};
            }
            return values;
        }();
        for (float y : {-0.02f, 0.0f, 0.1f}) {
            for (const auto& offset : offsets) {
                const auto query = world.QueryBlockCartesian(body.position + localUp * y +
                    right * offset.x + frame.forward * offset.y);
                if (query.status != fw::WorldVoxelQueryStatus::Air) return false;
            }
        }
        return true;
    };
    auto productionCylinderClear = [](const fw::GameWorld& world, const RigidBody& body) {
        glm::vec3 localUp;
        if (!fw::PlanetaryCameraFrame::Normalize(body.position, localUp)) return false;
        fw::PlanetaryCameraFrame::State frame{
            body.planetaryCollisionFrameUp, body.planetaryCollisionFrameForward,
            body.planetaryCollisionFrameInitialized};
        if (!fw::PlanetaryCameraFrame::TransportTo(frame, localUp)) return false;
        glm::vec3 right;
        if (!fw::PlanetaryCameraFrame::Normalize(glm::cross(frame.forward, localUp), right)) return false;
        const float r = body.radius;
        std::array<glm::vec2, 17> offsets{{
            {0,0},{r,0},{-r,0},{0,r},{0,-r},
            {r*.707f,r*.707f},{-r*.707f,r*.707f},
            {r*.707f,-r*.707f},{-r*.707f,-r*.707f},
            {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}};
        for (int i = 0; i < 8; ++i) {
            const float angle = 6.28318530718f * (static_cast<float>(i) + 0.5f) / 8.0f;
            offsets[9 + i] = {r * 0.95f * std::cos(angle), r * 0.95f * std::sin(angle)};
        }
        for (float y : {0.0f, 0.1f, body.height * 0.5f, body.height - 0.1f}) {
            for (const auto& offset : offsets) {
                if (world.QueryBlockCartesian(body.position + localUp * y +
                    right * offset.x + frame.forward * offset.y).status != fw::WorldVoxelQueryStatus::Air)
                    return false;
            }
        }
        return true;
    };

    // Valid radial foot support.
    {
        SharedContext context{};
        fw::GameWorld world;
        if (!InitializeP03aSyntheticPlanet(context, world, true, 0x503D401u)) return false;
        RigidBody body{};
        body.position = up * (planetRadius + 16.5f + 0.11f);
        PhysicsEngine physics;
        for (int tick = 0; tick < 120 && !body.isGrounded; ++tick)
            physics.StepSimulation(body, fixedDt, world);
        const bool fixtureValid = HasP03aFootSupport(world, body) && !body.dbg_terrainPending &&
            body.dbg_radialGroundContact;
        allPass = report("foot_support", body, up, fixtureValid, true, true) && allPass;
    }

    // Upper-body-only tangential contact: all production probes are clear at
    // the start; the wall is placed only at the top probe row and is reached
    // by tangential motion. No radial/downward sweep can turn it into foot
    // support during this scenario.
    {
        SharedContext context{};
        fw::GameWorld world;
        if (!InitializeP03aSyntheticPlanet(context, world, false, 0x503D402u)) return false;
        RigidBody body{};
        body.isFlying = true;
        body.position = up * (planetRadius + 14.0f);
        const auto frame = fw::PlanetaryCameraFrame::MakeInitial(up);
        body.planetaryCollisionFrameUp = frame.up;
        body.planetaryCollisionFrameForward = frame.forward;
        body.planetaryCollisionFrameInitialized = frame.initialized;
        const glm::vec3 obstacle = body.position + up * (body.height - 0.1f) + frame.forward * 2.0f;
        if (!SetP03aVoxelAtWorldPosition(world, obstacle, stone)) return false;
        const auto upperObstacle = world.QueryBlockCartesian(obstacle);
        const bool initialCylinderAir = productionCylinderClear(world, body);
        const bool footAirBefore = footQueriesAirResolved(world, body);
        const bool fixtureValid = initialCylinderAir && footAirBefore &&
            upperObstacle.status == fw::WorldVoxelQueryStatus::SolidVoxel &&
            !HasP03aFootSupport(world, body);
        std::cout << "[P03D4][PRECHECK] upper_body_only initialCylinderAir=" << initialCylinderAir
                  << " footAir=" << footAirBefore
                  << " obstacle=" << static_cast<unsigned>(upperObstacle.status) << ':'
                  << upperObstacle.voxelCoord.x << ',' << upperObstacle.voxelCoord.y << ','
                  << upperObstacle.voxelCoord.z << " footSupport=" << HasP03aFootSupport(world, body)
                  << " obstacleOffset=2.0,1.7\n";
        if (!fixtureValid) return false;
        body.velocity = frame.forward * 7.0f;
        PhysicsEngine physics;
        physics.StepSimulation(body, 0.2f, world);
        const bool noFootSupportAfter = !HasP03aFootSupport(world, body);
        const bool falseGrounded = body.isGrounded && !body.dbg_terrainPending && noFootSupportAfter;
        const bool hitUpperObstacle = body.dbg_tangentialSolidBlock && body.dbg_voxelContacts > 0;
        const bool upperVoxelMatched = body.dbg_lastLookupSolid &&
            body.dbg_lastLookupCoord == upperObstacle.voxelCoord;
        std::cout << "[P03D4][UPPER_ONLY_RESULT] fixtureValid=" << fixtureValid
                  << " footSupportAfter=" << !noFootSupportAfter
                  << " grounded=" << body.isGrounded
                  << " finalPosition=" << body.position.x << ',' << body.position.y << ',' << body.position.z
                  << " finalRadius=" << glm::length(body.position)
                  << " radialContact=" << body.dbg_radialGroundContact
                  << " upperObstacleHit=" << hitUpperObstacle
                  << " contactVoxel=" << body.dbg_lastLookupCoord.x << ','
                  << body.dbg_lastLookupCoord.y << ',' << body.dbg_lastLookupCoord.z
                  << " expectedVoxel=" << upperObstacle.voxelCoord.x << ','
                  << upperObstacle.voxelCoord.y << ',' << upperObstacle.voxelCoord.z
                  << " contactNormal=NA(radialGroundNormalOnly)"
                  << " falseGrounded=" << falseGrounded << '\n';
        allPass = fixtureValid && hitUpperObstacle && upperVoxelMatched && !body.isGrounded &&
            !body.dbg_terrainPending && body.dbg_outOfBoundsSamples == 0 && allPass;
    }

    // Side-wall-only: tangential movement hits an upper-body obstacle while
    // every independent foot probe is Air. Flying suppresses gravity only to
    // isolate the tangential contact; it does not change collision dispatch.
    {
        SharedContext context{};
        fw::GameWorld world;
        if (!InitializeP03aSyntheticPlanet(context, world, false, 0x503D403u)) return false;
        RigidBody body{};
        body.isFlying = true;
        body.position = up * (planetRadius + 14.0f);
        const auto frame = fw::PlanetaryCameraFrame::MakeInitial(up);
        body.planetaryCollisionFrameUp = frame.up;
        body.planetaryCollisionFrameForward = frame.forward;
        body.planetaryCollisionFrameInitialized = frame.initialized;
        const glm::vec3 wallPoint = body.position + up * 0.9f + frame.forward * 2.0f;
        if (!SetP03aVoxelAtWorldPosition(world, wallPoint, stone)) return false;
        const auto wall = world.QueryBlockCartesian(wallPoint);
        const bool initialClear = productionCylinderClear(world, body);
        bool footAir = footQueriesAirResolved(world, body) && !HasP03aFootSupport(world, body);
        body.velocity = frame.forward * 7.0f;
        PhysicsEngine physics;
        physics.StepSimulation(body, 0.2f, world);
        footAir = footAir && footQueriesAirResolved(world, body);
        const bool fixtureValid = initialClear && wall.status == fw::WorldVoxelQueryStatus::SolidVoxel && footAir &&
            !body.dbg_terrainPending && body.dbg_outOfBoundsSamples == 0;
        const bool wallHit = body.isAgainstWall || body.dbg_tangentialSolidBlock;
        std::cout << "[P03D4][PRECHECK] side_wall wall=" << static_cast<unsigned>(wall.status)
                  << ':' << wall.voxelCoord.x << ',' << wall.voxelCoord.y << ',' << wall.voxelCoord.z
                  << " footAir=" << footAir << "\n";
        const bool pass = fixtureValid && wallHit && !body.isGrounded;
        std::cout << "[P03D4][CASE] side_wall fixtureValid=" << fixtureValid
                  << " wallHit=" << wallHit << " grounded=" << body.isGrounded
                  << " tangentialBlock=" << body.dbg_tangentialSolidBlock
                  << " contactVoxel=" << body.dbg_lastLookupCoord.x << ',' << body.dbg_lastLookupCoord.y
                  << ',' << body.dbg_lastLookupCoord.z << " expectedVoxel=" << wall.voxelCoord.x << ','
                  << wall.voxelCoord.y << ',' << wall.voxelCoord.z
                  << " contactNormal=NA(radialGroundNormalOnly)"
                  << " PASS=" << (pass && body.dbg_lastLookupSolid && body.dbg_lastLookupCoord == wall.voxelCoord)
                  << '\n';
        allPass = pass && body.dbg_lastLookupSolid && body.dbg_lastLookupCoord == wall.voxelCoord && allPass;
    }

    // Ceiling-only: an outward radial impulse reaches an overhead voxel; it
    // must block motion without establishing support/Grounded.
    {
        SharedContext context{};
        fw::GameWorld world;
        if (!InitializeP03aSyntheticPlanet(context, world, false, 0x503D404u)) return false;
        RigidBody body{};
        body.isFlying = true;
        body.position = up * (planetRadius + 12.0f);
        const auto frame = fw::PlanetaryCameraFrame::MakeInitial(up);
        body.planetaryCollisionFrameUp = frame.up;
        body.planetaryCollisionFrameForward = frame.forward;
        body.planetaryCollisionFrameInitialized = frame.initialized;
        const glm::vec3 ceilingPoint = body.position + up * 2.5f;
        if (!SetP03aVoxelAtWorldPosition(world, ceilingPoint, stone)) return false;
        const auto ceiling = world.QueryBlockCartesian(ceilingPoint);
        const bool initialClear = productionCylinderClear(world, body);
        bool footAir = footQueriesAirResolved(world, body) && !HasP03aFootSupport(world, body);
        body.velocity = up * 4.0f;
        PhysicsEngine physics;
        physics.StepSimulation(body, 0.2f, world);
        footAir = footAir && footQueriesAirResolved(world, body);
        const bool fixtureValid = initialClear && ceiling.status == fw::WorldVoxelQueryStatus::SolidVoxel && footAir &&
            !body.dbg_terrainPending && body.dbg_outOfBoundsSamples == 0;
        const bool blocked = glm::dot(body.position - up * (planetRadius + 12.0f), up) < 0.8f;
        const bool pass = fixtureValid && blocked && !body.isGrounded;
        std::cout << "[P03D4][PRECHECK] ceiling solid=" << static_cast<unsigned>(ceiling.status)
                  << " footAir=" << footAir << '\n';
        std::cout << "[P03D4][CASE] ceiling fixtureValid=" << fixtureValid
                  << " blocked=" << blocked << " grounded=" << body.isGrounded
                  << " radialContact=" << body.dbg_radialGroundContact
                  << " contactVoxel=" << body.dbg_lastLookupCoord.x << ',' << body.dbg_lastLookupCoord.y
                  << ',' << body.dbg_lastLookupCoord.z << " expectedVoxel=" << ceiling.voxelCoord.x << ','
                  << ceiling.voxelCoord.y << ',' << ceiling.voxelCoord.z
                  << " contactNormal=NA(radialGroundNormalOnly)"
                  << " PASS=" << pass << '\n';
        allPass = pass && body.dbg_lastLookupSolid && body.dbg_lastLookupCoord == ceiling.voxelCoord && allPass;
    }

    // Empty world: gravity produces inward velocity, never Grounded.
    {
        SharedContext context{};
        fw::GameWorld world;
        if (!InitializeP03aSyntheticPlanet(context, world, false, 0x503D405u)) return false;
        RigidBody body{};
        body.position = up * (planetRadius + 10.0f);
        PhysicsEngine physics;
        physics.StepSimulation(body, fixedDt, world);
        const bool fixtureValid = body.dbg_voxelContacts == 0 && !body.dbg_terrainPending &&
            body.dbg_outOfBoundsSamples == 0 && body.dbg_airSamples > 0;
        const bool pass = fixtureValid && !body.isGrounded && glm::dot(body.velocity, up) < 0.0f;
        std::cout << "[P03D4][CASE] no_contact_falling fixtureValid=" << fixtureValid
                  << " grounded=" << body.isGrounded << " radialVelocity=" << glm::dot(body.velocity, up)
                  << " PASS=" << pass << '\n';
        allPass = pass && allPass;
    }

    // Support removal must clear Grounded on the next physics tick.
    {
        SharedContext context{};
        fw::GameWorld world;
        if (!InitializeP03aSyntheticPlanet(context, world, true, 0x503D406u)) return false;
        RigidBody body{};
        body.position = up * (planetRadius + 16.5f + 0.11f);
        PhysicsEngine physics;
        for (int tick = 0; tick < 120 && !body.isGrounded; ++tick)
            physics.StepSimulation(body, fixedDt, world);
        const bool groundedBefore = body.isGrounded && HasP03aFootSupport(world, body);
        const glm::ivec3 supportVoxel = body.dbg_groundVoxelCoord;
        const size_t removed = ClearP03aFootprint(world, body);
        const bool footAirAfterClear = !HasP03aFootSupport(world, body);
        body.velocity = -up * 0.1f;
        physics.StepSimulation(body, fixedDt, world);
        const bool pass = groundedBefore && removed > 0 && footAirAfterClear && !body.isGrounded &&
            !body.dbg_terrainPending;
        std::cout << "[P03D4][CASE] transition_unsupported groundedBefore=" << groundedBefore
                  << " removedVoxels=" << removed << " footAir=" << footAirAfterClear
                  << " formerSupportVoxel=" << supportVoxel.x << ',' << supportVoxel.y << ',' << supportVoxel.z
                  << " groundedAfter=" << body.isGrounded
                  << " contactNormal=NA(noContactAfterRemoval) PASS=" << pass << '\n';
        allPass = pass && allPass;
    }

    return allPass;
}

// Bounded runner-only search for a radial upper-row contact whose starting
// cylinder is clear and whose lower production rows remain Air at impact.
// The search never alters PhysicsEngine and reports NOT VERIFIED when no such
// voxel/grid configuration is found in the enumerated face/edge witnesses.
bool TestP03d5RadialUpperOnlyContactSearch() {
    constexpr float fixedDt = 1.0f / 60.0f;
    constexpr uint8_t stone = static_cast<uint8_t>(fw::BlockType::Stone);
    const float planetRadius = fw::PlanetMath::GetPlanetRadius(fw::PlanetSize::Tiny);
    const float voxelCenterRadius = fw::PlanetRadialMapping::GetCellCenterRadius(0, 80, planetRadius);
    const std::array<float, 4> probeHeights{{0.0f, 0.1f, 0.9f, 1.7f}};
    const std::array<float, 5> inwardSteps{{0.01f, 0.025f, 0.05f, 0.075f, 0.1f}};
    const std::array<glm::vec2, 9> chartSamples{{
        {0.5f,0.5f},{0.001f,0.5f},{0.999f,0.5f},{0.5f,0.001f},{0.5f,0.999f},
        {0.001f,0.001f},{0.999f,0.001f},{0.001f,0.999f},{0.999f,0.999f}}};
    size_t targetCells = 0;
    size_t clearStarts = 0;
    size_t upperSolidCandidates = 0;
    size_t upperOnlyCandidates = 0;
    size_t upperPredictionsWithPenetratingStart = 0;
    size_t upperPredictionsWithLowerSolid = 0;
    std::array<size_t, 4> upperPredictionsBlockedAtStartRow{};
    size_t unavailableCandidates = 0;

    SharedContext context{};
    fw::GameWorld world;
    if (!InitializeP03aSyntheticPlanet(context, world, false, 0x503D501u)) return false;

    auto makeOffsets = [](float radius) {
        std::array<glm::vec2, 17> offsets{{
            {0,0},{radius,0},{-radius,0},{0,radius},{0,-radius},
            {radius*.707f,radius*.707f},{-radius*.707f,radius*.707f},
            {radius*.707f,-radius*.707f},{-radius*.707f,-radius*.707f},
            {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}};
        for (int i = 0; i < 8; ++i) {
            const float angle = 6.28318530718f * (static_cast<float>(i) + 0.5f) / 8.0f;
            offsets[9 + i] = {radius * 0.95f * std::cos(angle), radius * 0.95f * std::sin(angle)};
        }
        return offsets;
    };

    bool searchCompleted = true;
    bool witnessExercised = false;
    bool falseGroundedObserved = false;
    bool firstPenetratingStartLogged = false;
    for (int face = 0; face < 6 && !falseGroundedObserved; ++face) {
        for (const glm::vec2& uv : chartSamples) {
            const glm::vec3 direction = fw::CubeSphereMapping::FaceUVToDirection(
                static_cast<fw::CubeFace>(face), uv);
            const auto frame = fw::PlanetaryCameraFrame::MakeInitial(direction);
            glm::vec3 localUp, localRight;
            if (!fw::PlanetaryCameraFrame::Normalize(direction, localUp) ||
                !fw::PlanetaryCameraFrame::Normalize(glm::cross(frame.forward, localUp), localRight)) {
                searchCompleted = false;
                break;
            }
            const glm::vec3 voxelCenter = direction * voxelCenterRadius;
            if (!SetP03aVoxelAtWorldPosition(world, voxelCenter, stone)) {
                searchCompleted = false;
                break;
            }
            const auto solidWitness = world.QueryBlockCartesian(voxelCenter);
            if (solidWitness.status != fw::WorldVoxelQueryStatus::SolidVoxel) {
                SetP03aVoxelAtWorldPosition(world, voxelCenter, 0);
                searchCompleted = false;
                break;
            }
            ++targetCells;
            const auto offsets = makeOffsets(0.3f);

            for (float upperHeight : {0.9f, 1.7f}) {
                for (int radialSample = -60; radialSample <= 60; ++radialSample) {
                    const float baseRadius = voxelCenterRadius - upperHeight +
                        static_cast<float>(radialSample) * 0.01f;
                    RigidBody initial{};
                    initial.isFlying = true; // isolate the inward radial sweep from gravity acceleration
                    initial.position = direction * baseRadius;
                    initial.planetaryCollisionFrameUp = frame.up;
                    initial.planetaryCollisionFrameForward = frame.forward;
                    initial.planetaryCollisionFrameInitialized = frame.initialized;

                    bool initialAllAir = true;
                    int initialBlockedRow = -1;
                    glm::ivec3 initialBlockedVoxel(-1);
                    for (size_t row = 0; row < probeHeights.size(); ++row) {
                        for (const glm::vec2& offset : offsets) {
                            const glm::vec3 point = initial.position + localUp * probeHeights[row] +
                                localRight * offset.x + frame.forward * offset.y;
                            const auto query = world.QueryBlockCartesian(point);
                            if (query.status != fw::WorldVoxelQueryStatus::Air) {
                                initialAllAir = false;
                                if (initialBlockedRow < 0 && query.status == fw::WorldVoxelQueryStatus::SolidVoxel) {
                                    initialBlockedRow = static_cast<int>(row);
                                    initialBlockedVoxel = query.voxelCoord;
                                }
                                if (query.status != fw::WorldVoxelQueryStatus::SolidVoxel)
                                    ++unavailableCandidates;
                            }
                        }
                    }
                    if (initialAllAir) ++clearStarts;

                    for (float inwardStep : inwardSteps) {
                        const glm::vec3 predictedBase = initial.position - localUp * inwardStep;
                        bool predictedLowerAir = true;
                        bool predictedUpperSolid = false;
                        bool predictedResolved = true;
                        int hitRow = -1;
                        glm::ivec3 hitVoxel(-1);
                        glm::vec2 hitOffset(0.0f);
                        for (size_t row = 0; row < probeHeights.size(); ++row) {
                            for (size_t sample = 0; sample < offsets.size(); ++sample) {
                                const auto query = world.QueryBlockCartesian(predictedBase + localUp * probeHeights[row] +
                                    localRight * offsets[sample].x + frame.forward * offsets[sample].y);
                                const bool resolved = query.status == fw::WorldVoxelQueryStatus::Air ||
                                    query.status == fw::WorldVoxelQueryStatus::SolidVoxel;
                                predictedResolved = predictedResolved && resolved;
                                if (row < 2 && query.status != fw::WorldVoxelQueryStatus::Air)
                                    predictedLowerAir = false;
                                if (row >= 2 && query.status == fw::WorldVoxelQueryStatus::SolidVoxel) {
                                    predictedUpperSolid = true;
                                    if (hitRow < 0) {
                                        hitRow = static_cast<int>(row);
                                        hitVoxel = query.voxelCoord;
                                        hitOffset = offsets[sample];
                                    }
                                }
                            }
                        }
                        if (!predictedResolved) ++unavailableCandidates;
                        if (predictedUpperSolid) {
                            ++upperSolidCandidates;
                            if (!predictedLowerAir) ++upperPredictionsWithLowerSolid;
                            if (!initialAllAir && initialBlockedRow >= 0 && initialBlockedRow < 4)
                                ++upperPredictionsBlockedAtStartRow[static_cast<size_t>(initialBlockedRow)];
                        }
                        if (!predictedResolved || !predictedLowerAir || !predictedUpperSolid) continue;
                        if (!initialAllAir) {
                            ++upperPredictionsWithPenetratingStart;
                            if (!firstPenetratingStartLogged) {
                                std::cout << "[P03D5][PRECONTACT_OCCUPANCY] face=" << face
                                          << " uv=" << uv.x << ',' << uv.y
                                          << " baseRadius=" << baseRadius << " step=" << inwardStep
                                          << " targetUpperRow=" << hitRow
                                          << " preexistingSolidRow=" << initialBlockedRow
                                          << " preexistingVoxel=" << initialBlockedVoxel.x << ','
                                          << initialBlockedVoxel.y << ',' << initialBlockedVoxel.z
                                          << " predictedLowerAir=1\n";
                                firstPenetratingStartLogged = true;
                            }
                            continue;
                        }
                        ++upperOnlyCandidates;
                        witnessExercised = true;

                        RigidBody body = initial;
                        const float dragFactor = 1.0f - 0.5f * fixedDt;
                        body.velocity = -localUp * (inwardStep / (fixedDt * dragFactor));
                        PhysicsEngine physics;
                        physics.StepSimulation(body, fixedDt, world);
                        bool footAirAfter = true;
                        bool footResolvedAfter = true;
                        glm::vec3 finalUp;
                        fw::PlanetaryCameraFrame::Normalize(body.position, finalUp);
                        fw::PlanetaryCameraFrame::State finalFrame{
                            body.planetaryCollisionFrameUp, body.planetaryCollisionFrameForward,
                            body.planetaryCollisionFrameInitialized};
                        fw::PlanetaryCameraFrame::TransportTo(finalFrame, finalUp);
                        glm::vec3 finalRight;
                        fw::PlanetaryCameraFrame::Normalize(glm::cross(finalFrame.forward, finalUp), finalRight);
                        for (float y : {0.0f, 0.1f}) {
                            for (const glm::vec2& offset : offsets) {
                                const auto query = world.QueryBlockCartesian(body.position + finalUp * y +
                                    finalRight * offset.x + finalFrame.forward * offset.y);
                                footAirAfter = footAirAfter && query.status == fw::WorldVoxelQueryStatus::Air;
                                footResolvedAfter = footResolvedAfter && (query.status == fw::WorldVoxelQueryStatus::Air ||
                                    query.status == fw::WorldVoxelQueryStatus::SolidVoxel);
                            }
                        }
                        const bool solverHitExpectedVoxel = body.dbg_lastLookupSolid &&
                            body.dbg_lastLookupCoord == hitVoxel;
                        falseGroundedObserved = body.isGrounded && footAirAfter && footResolvedAfter;
                        std::cout << "[P03D5][RADIAL_CANDIDATE] face=" << face << " uv=" << uv.x << ',' << uv.y
                                  << " baseRadius=" << baseRadius << " inwardStep=" << inwardStep
                                  << " row=" << hitRow << " probeOffset=" << hitOffset.x << ',' << hitOffset.y
                                  << " voxel=" << hitVoxel.x << ',' << hitVoxel.y << ',' << hitVoxel.z
                                  << " solverVoxel=" << body.dbg_lastLookupCoord.x << ','
                                  << body.dbg_lastLookupCoord.y << ',' << body.dbg_lastLookupCoord.z
                                  << " solverHitExpectedVoxel=" << solverHitExpectedVoxel
                                  << " lowerAirAtCandidate=1 footAirAfter=" << footAirAfter
                                  << " grounded=" << body.isGrounded
                                  << " radialGroundContact=" << body.dbg_radialGroundContact
                                  << " contactNormalAvailable=" << (body.dbg_radialGroundContact ? 1 : 0)
                                  << " pending=" << body.dbg_terrainPending
                                  << " outOfBounds=" << body.dbg_outOfBoundsSamples
                                  << " falseGrounded=" << falseGroundedObserved << '\n';
                        if (falseGroundedObserved) break;
                    }
                    if (falseGroundedObserved) break;
                }
                if (falseGroundedObserved) break;
            }
            SetP03aVoxelAtWorldPosition(world, voxelCenter, 0);
            if (falseGroundedObserved || !searchCompleted) break;
        }
    }

    std::cout << "[P03D5][SEARCH] faces=6 uvWitnessesPerFace=" << chartSamples.size()
              << " targetCells=" << targetCells << " clearStarts=" << clearStarts
              << " upperSolidPredictions=" << upperSolidCandidates
              << " upperPredictionsWithPenetratingStart=" << upperPredictionsWithPenetratingStart
              << " preblockedStartRows=" << upperPredictionsBlockedAtStartRow[0] << ','
              << upperPredictionsBlockedAtStartRow[1] << ',' << upperPredictionsBlockedAtStartRow[2] << ','
              << upperPredictionsBlockedAtStartRow[3]
              << " upperPredictionsWithLowerSolid=" << upperPredictionsWithLowerSolid
              << " lowerAirAndUpperSolidCandidates=" << upperOnlyCandidates
              << " unavailableQueries=" << unavailableCandidates
              << " witnessExercised=" << witnessExercised
              << " falseGrounded=" << falseGroundedObserved << '\n';
    if (falseGroundedObserved) {
        std::cout << "[P03D5][FAIL] reproducible radial false Grounded; production physics unchanged\n";
        return false;
    }
    if (upperOnlyCandidates == 0) {
        std::cout << "[P03D5][NOT VERIFIED] bounded radial search found no valid initial-clear / upper-solid / lower-Air fixture\n";
        return searchCompleted && targetCells == 54 && unavailableCandidates == 0;
    }
    std::cout << "[P03D5][OBSERVED] radial upper-only candidate(s) exercised; no false Grounded observed\n";
    return searchCompleted && targetCells == 54 && unavailableCandidates == 0;
}

bool TestPlanetaryGravityAnchoring(fw::GameWorld& world, const glm::vec3& direction) {
    constexpr float fixedDt = 1.0f / 60.0f;
    glm::vec3 surfaceSpawn(0.0f);
    float surfaceRadius = 0.0f;
    if (world.TryResolvePlanetarySpawn(direction, 0.11f, 0.3f, surfaceSpawn, &surfaceRadius) !=
        fw::PlanetarySpawnResolveStatus::Ready) return false;

    const fw::PlanetarySpawnPlan fallback = fw::MakePlanetaryFallbackSpawn(fw::PlanetSize::Tiny);
    const bool fallbackPolicyPass = fallback.pending && fallback.heightOffset >= 0.11f &&
        glm::length(fallback.temporaryBodyPosition) > fw::PlanetMath::GetPlanetRadius(fw::PlanetSize::Tiny) &&
        glm::dot(fallback.direction, glm::vec3(0.0f, 1.0f, 0.0f)) > 0.999f;

    DeviceManager deviceManager;
    deviceManager.requireFreeCursor = false;
    SharedContext context{};
    context.forgeWorld = &world;
    context.deviceManager = &deviceManager;

    entt::registry registry;
    const entt::entity entity = registry.create();
    auto& player = registry.emplace<PlayerControllerComponent>(entity);
    auto& transform = registry.emplace<TransformComponent>(entity);
    auto& camera = registry.emplace<CameraComponent>(entity);
    auto& body = registry.emplace<RigidBodyComponent>(entity).body;
    body.mass = 70.0f;
    body.position = direction * (surfaceRadius + 10.0f);
    const auto initialFrame = fw::PlanetaryCameraFrame::MakeInitial(direction);
    camera.planetaryFrameUp = initialFrame.up;
    camera.planetaryFrameForward = initialFrame.forward;
    camera.planetaryFrameInitialized = initialFrame.initialized;
    transform.x = body.position.x + direction.x * body.eyeOffset;
    transform.y = body.position.y + direction.y * body.eyeOffset;
    transform.z = body.position.z + direction.z * body.eyeOffset;

    fw::CameraSyncSystem cameraSync;
    fw::PlayerMovementSystem movement;
    fw::PhysicsSystem physics;
    fw::CameraSystem cameraSystem;
    bool gravityTowardCenterEveryTick = true;
    bool transformMatchesBodyEveryTick = true;
    bool groundedObserved = false;
    float radiusBeforeFall = glm::length(body.position);
    size_t fallTicks = 0;
    for (; fallTicks < 600; ++fallTicks) {
        cameraSync.Update(registry, &context, fixedDt);
        movement.Update(registry, &context, fixedDt);
        physics.Update(registry, &context, fixedDt);
        cameraSystem.Update(registry, &context, fixedDt);

        glm::vec3 up;
        if (!fw::PlanetaryCameraFrame::Normalize(body.position, up) ||
            glm::dot(body.dbg_gravityAccel, up) >= -1.0e-4f) {
            gravityTowardCenterEveryTick = false;
        }
        const glm::vec3 expectedCameraPosition = body.position + up * body.eyeOffset;
        const glm::vec3 actualCameraPosition(transform.x, transform.y, transform.z);
        if (glm::length(expectedCameraPosition - actualCameraPosition) > 1.0e-4f) {
            transformMatchesBodyEveryTick = false;
        }
        groundedObserved = groundedObserved || body.isGrounded;
        if (body.isGrounded) break;
    }

    const float settledClearance = glm::length(body.position) - surfaceRadius;
    // Deterministic no-input contact trace. This is test-only instrumentation:
    // it records collision-induced radial velocity separately from the velocity
    // predicted after gravity/drag, without changing production physics.
    RigidBody idleProbe = body;
    PhysicsEngine idleProbePhysics;
    bool idleContactStable = idleProbe.isGrounded;
    int firstIdleGroundedLossTick = -1;
    int maxCollisionRadialDeltaTick = -1;
    bool collisionRadialDeltaLogged = false;
    float maxCollisionRadialDelta = 0.0f;
    float maxPositivePostCollisionRadialVelocity = 0.0f;
    float maxIdlePenetration = 0.0f;
    const float referencePlanetRadius = fw::PlanetMath::GetPlanetRadius(fw::PlanetSize::Tiny);
    // dbg_lastLookupCoord is overwritten by later air probes in the same tick.
    // The solver captures the voxel that actually blocked downward motion in
    // dbg_groundVoxelCoord; validate that cell independently at the resolved
    // surface before comparing its outer radius.
    const glm::ivec3 supportVoxel = idleProbe.dbg_groundVoxelCoord;
    const bool supportCoordValid = idleProbe.dbg_radialGroundContact &&
        supportVoxel.y >= 0 && supportVoxel.y < fw::ChunkDimensions::VoxelsY;
    const float contactMeshTopRadius = supportCoordValid
        ? fw::PlanetRadialMapping::GetRadialDistance(
            fw::PlanetRadialMapping::GetGlobalRadialOffset(
                0, static_cast<float>(supportVoxel.y) + 0.5f), referencePlanetRadius)
        : std::numeric_limits<float>::quiet_NaN();
    const glm::vec3 contactDirection = glm::normalize(idleProbe.position);
    const auto supportProbe = supportCoordValid
        ? world.QueryBlockCartesian(contactDirection * (contactMeshTopRadius - 0.05f))
        : fw::WorldVoxelQueryResult{};
    const auto aboveSupportProbe = supportCoordValid
        ? world.QueryBlockCartesian(contactDirection * (contactMeshTopRadius + 0.05f))
        : fw::WorldVoxelQueryResult{};
    const bool supportVoxelConfirmed = supportProbe.status == fw::WorldVoxelQueryStatus::SolidVoxel &&
        supportProbe.voxelCoord.y == supportVoxel.y && aboveSupportProbe.status == fw::WorldVoxelQueryStatus::Air;
    const bool radialSurfaceAgreement = supportCoordValid && supportVoxelConfirmed &&
        std::abs(surfaceRadius - contactMeshTopRadius) <= 1.0e-4f;
    std::cout << "[D1.2.19d.4][CONTACT_BEGIN] grounded=" << idleProbe.isGrounded
              << " position=" << idleProbe.position.x << ',' << idleProbe.position.y << ',' << idleProbe.position.z
              << " radialVelocity=" << glm::dot(idleProbe.velocity, glm::normalize(idleProbe.position))
              << " lastVoxel=" << idleProbe.dbg_lastLookupCoord.x << ',' << idleProbe.dbg_lastLookupCoord.y << ','
              << idleProbe.dbg_lastLookupCoord.z << " lastSolid=" << idleProbe.dbg_lastLookupSolid
              << " supportVoxel=" << supportVoxel.x << ',' << supportVoxel.y << ',' << supportVoxel.z
              << " supportProbe=" << static_cast<unsigned>(supportProbe.status)
              << " supportProbeVoxel=" << supportProbe.voxelCoord.x << ','
              << supportProbe.voxelCoord.y << ',' << supportProbe.voxelCoord.z
              << " aboveSupportProbe=" << static_cast<unsigned>(aboveSupportProbe.status)
              << " supportVoxelConfirmed=" << supportVoxelConfirmed
              << " sampleStatus=" << static_cast<unsigned>(idleProbe.dbg_lastPlanetarySample)
              << " contacts=" << idleProbe.dbg_voxelContacts
              << " pending=" << idleProbe.dbg_terrainPending
              << " collisionSurfaceRadius=" << surfaceRadius
              << " voxelMeshTopRadius=" << contactMeshTopRadius
              << " collisionMinusMeshRadius=" << surfaceRadius - contactMeshTopRadius
              << " radialSurfaceAgreement=" << radialSurfaceAgreement << '\n';
    for (int tick = 0; tick < 120; ++tick) {
        const glm::vec3 positionBefore = idleProbe.position;
        const glm::vec3 velocityBefore = idleProbe.velocity;
        glm::vec3 upBefore;
        if (!fw::PlanetaryCameraFrame::Normalize(positionBefore, upBefore)) {
            idleContactStable = false;
            break;
        }
        const bool groundedBefore = idleProbe.isGrounded;
        idleProbePhysics.StepSimulation(idleProbe, fixedDt, world);
        const glm::vec3 integratedVelocity = velocityBefore +
            (idleProbe.dbg_gravityAccel - (idleProbe.drag / idleProbe.mass) * velocityBefore) * fixedDt;
        const float collisionRadialCorrection = glm::dot(
            idleProbe.velocity - integratedVelocity, upBefore);
        maxPositivePostCollisionRadialVelocity = std::max(maxPositivePostCollisionRadialVelocity,
            glm::dot(idleProbe.velocity, upBefore));
        if (collisionRadialCorrection > maxCollisionRadialDelta) {
            maxCollisionRadialDelta = collisionRadialCorrection;
            maxCollisionRadialDeltaTick = tick;
        }

        glm::vec3 directionNow;
        float resolvedSurfaceRadius = 0.0f;
        const bool hasSurface = fw::PlanetaryCameraFrame::Normalize(idleProbe.position, directionNow) &&
            world.TryResolvePlanetarySpawn(directionNow, 0.11f, idleProbe.radius,
                surfaceSpawn, &resolvedSurfaceRadius) == fw::PlanetarySpawnResolveStatus::Ready;
        const float signedClearance = hasSurface ? glm::length(idleProbe.position) - resolvedSurfaceRadius
            : std::numeric_limits<float>::quiet_NaN();
        if (hasSurface) maxIdlePenetration = std::max(maxIdlePenetration, -signedClearance);

        if (groundedBefore && !idleProbe.isGrounded && firstIdleGroundedLossTick < 0) {
            firstIdleGroundedLossTick = tick;
            const auto centerQuery = world.QueryBlockCartesian(idleProbe.position);
            std::cout << "[D1.2.19d.4][IDLE_CONTACT_LOSS] tick=" << tick
                      << " jumpInput=" << deviceManager.GetInput().isJumping
                      << " posBefore=" << positionBefore.x << ',' << positionBefore.y << ',' << positionBefore.z
                      << " posAfter=" << idleProbe.position.x << ',' << idleProbe.position.y << ',' << idleProbe.position.z
                      << " cameraAfter=" << idleProbe.position.x + directionNow.x * idleProbe.eyeOffset << ','
                      << idleProbe.position.y + directionNow.y * idleProbe.eyeOffset << ','
                      << idleProbe.position.z + directionNow.z * idleProbe.eyeOffset
                      << " radialVelocityBefore=" << glm::dot(velocityBefore, upBefore)
                      << " radialVelocityIntegrated=" << glm::dot(integratedVelocity, upBefore)
                      << " radialVelocityAfter=" << glm::dot(idleProbe.velocity, upBefore)
                      << " collisionRadialCorrection=" << collisionRadialCorrection
                      << " gravity=" << idleProbe.dbg_gravityAccel.x << ',' << idleProbe.dbg_gravityAccel.y << ','
                      << idleProbe.dbg_gravityAccel.z << " radialNormal=" << upBefore.x << ',' << upBefore.y << ','
                      << upBefore.z << " clearance=" << signedClearance
                      << " penetration=" << std::max(0.0f, -signedClearance)
                      << " lastVoxel=" << idleProbe.dbg_lastLookupCoord.x << ','
                      << idleProbe.dbg_lastLookupCoord.y << ',' << idleProbe.dbg_lastLookupCoord.z
                      << " lastSolid=" << idleProbe.dbg_lastLookupSolid
                      << " centerStatus=" << static_cast<unsigned>(centerQuery.status)
                      << " solidContacts=" << idleProbe.dbg_voxelContacts
                      << " terrainPending=" << idleProbe.dbg_terrainPending
                      << " pendingSamples=" << idleProbe.dbg_terrainPendingSamples << '\n';
        }
        if (collisionRadialCorrection > 0.02f && !collisionRadialDeltaLogged) {
            std::cout << "[D1.2.19d.4][COLLISION_RADIAL_DELTA] tick=" << tick
                      << " deltaRadial=" << collisionRadialCorrection
                      << " radialVelocityIntegrated=" << glm::dot(integratedVelocity, upBefore)
                      << " radialVelocityAfter=" << glm::dot(idleProbe.velocity, upBefore)
                      << " groundedBefore=" << groundedBefore << " groundedAfter=" << idleProbe.isGrounded
                      << " positionBefore=" << positionBefore.x << ',' << positionBefore.y << ',' << positionBefore.z
                      << " positionAfter=" << idleProbe.position.x << ',' << idleProbe.position.y << ','
                      << idleProbe.position.z << " lastVoxel=" << idleProbe.dbg_lastLookupCoord.x << ','
                      << idleProbe.dbg_lastLookupCoord.y << ',' << idleProbe.dbg_lastLookupCoord.z
                      << " lastSolid=" << idleProbe.dbg_lastLookupSolid
                      << " contacts=" << idleProbe.dbg_voxelContacts
                      << " terrainPending=" << idleProbe.dbg_terrainPending << '\n';
            collisionRadialDeltaLogged = true;
        }
        if (!idleProbe.isGrounded) idleContactStable = false;
    }
    std::cout << "[D1.2.19d.4][IDLE_CONTACT_TRACE] ticks=120 firstGroundedLossTick="
              << firstIdleGroundedLossTick << " contactStable=" << idleContactStable
              << " maxCollisionRadialDelta=" << maxCollisionRadialDelta
              << " maxCollisionRadialDeltaTick=" << maxCollisionRadialDeltaTick
              << " maxPositivePostCollisionRadialVelocity=" << maxPositivePostCollisionRadialVelocity
              << " maxPenetration=" << maxIdlePenetration << '\n';
    bool fallAndAnchorPass = fallbackPolicyPass && radialSurfaceAgreement && gravityTowardCenterEveryTick &&
        transformMatchesBodyEveryTick && groundedObserved && body.isGrounded &&
        settledClearance >= -0.005f && settledClearance < 0.5f &&
        glm::length(body.position) < radiusBeforeFall;
    fallAndAnchorPass = fallAndAnchorPass && idleContactStable;

    glm::vec3 jumpUp;
    fw::PlanetaryCameraFrame::Normalize(body.position, jumpUp);
    const glm::vec3 jumpPositionBeforeMovement = body.position;
    const float jumpRadialVelocityBeforeMovement = glm::dot(body.velocity, jumpUp);
    deviceManager.GetInput().isJumping = true;
    movement.Update(registry, &context, fixedDt);
    const float jumpRadialVelocityAfterMovement = glm::dot(body.velocity, jumpUp);
    const bool jumpInputConsumed = !deviceManager.GetInput().isJumping &&
        jumpRadialVelocityAfterMovement > 0.0f;
    std::cout << "[D1.2.19d.4][SPACE_INPUT] tick=0 position=" << jumpPositionBeforeMovement.x << ','
              << jumpPositionBeforeMovement.y << ',' << jumpPositionBeforeMovement.z
              << " groundedBefore=" << body.isGrounded << " inputConsumed=" << jumpInputConsumed
              << " radialVelocityBefore=" << jumpRadialVelocityBeforeMovement
              << " radialVelocityAfterMovement=" << jumpRadialVelocityAfterMovement << '\n';

    float maximumJumpHeight = 0.0f;
    const float jumpStartRadius = glm::length(body.position);
    bool landedAgain = false;
    bool jumpGroundedLossLogged = false;
    // Tiny uses inverse-square gravity referenced to its nominal Cube-Sphere
    // radius. A low-altitude jump therefore needs longer than 10 seconds to
    // return; keep simulating until the physical collision reports landing.
    for (int tick = 0; tick < 2400; ++tick) {
        const bool groundedBeforePhysics = body.isGrounded;
        const glm::vec3 positionBeforePhysics = body.position;
        const glm::vec3 velocityBeforePhysics = body.velocity;
        glm::vec3 upBeforePhysics;
        fw::PlanetaryCameraFrame::Normalize(positionBeforePhysics, upBeforePhysics);
        cameraSync.Update(registry, &context, fixedDt);
        movement.Update(registry, &context, fixedDt);
        const glm::vec3 velocityAfterMovement = body.velocity;
        physics.Update(registry, &context, fixedDt);
        cameraSystem.Update(registry, &context, fixedDt);
        maximumJumpHeight = std::max(maximumJumpHeight, glm::length(body.position) - jumpStartRadius);

        if (groundedBeforePhysics && !body.isGrounded && !jumpGroundedLossLogged) {
            const glm::vec3 integratedVelocity = velocityAfterMovement +
                (body.dbg_gravityAccel - (body.drag / body.mass) * velocityAfterMovement) * fixedDt;
            const glm::vec3 currentUp = glm::normalize(body.position);
            const auto centerQuery = world.QueryBlockCartesian(body.position);
            std::cout << "[D1.2.19d.4][JUMP_CONTACT_LOSS] tick=" << tick
                      << " inputConsumedEarlier=" << jumpInputConsumed
                      << " posBefore=" << positionBeforePhysics.x << ',' << positionBeforePhysics.y << ','
                      << positionBeforePhysics.z << " posAfter=" << body.position.x << ',' << body.position.y
                      << ',' << body.position.z << " cameraAfter=" << transform.x << ',' << transform.y << ','
                      << transform.z << " radialVelocityBeforeMovement="
                      << glm::dot(velocityBeforePhysics, upBeforePhysics)
                      << " radialVelocityAfterMovement=" << glm::dot(velocityAfterMovement, upBeforePhysics)
                      << " radialVelocityIntegrated=" << glm::dot(integratedVelocity, upBeforePhysics)
                      << " radialVelocityAfterCollision=" << glm::dot(body.velocity, upBeforePhysics)
                      << " collisionRadialCorrection=" << glm::dot(body.velocity - integratedVelocity, upBeforePhysics)
                      << " groundBefore=" << groundedBeforePhysics << " groundAfter=" << body.isGrounded
                      << " lastVoxel=" << body.dbg_lastLookupCoord.x << ',' << body.dbg_lastLookupCoord.y << ','
                      << body.dbg_lastLookupCoord.z << " lastSolid=" << body.dbg_lastLookupSolid
                      << " contacts=" << body.dbg_voxelContacts << " terrainPending=" << body.dbg_terrainPending
                      << " pendingSamples=" << body.dbg_terrainPendingSamples
                      << " centerStatus=" << static_cast<unsigned>(centerQuery.status)
                      << " radialNormal=" << currentUp.x << ',' << currentUp.y << ',' << currentUp.z << '\n';
            jumpGroundedLossLogged = true;
        }

        glm::vec3 currentUp;
        if (!fw::PlanetaryCameraFrame::Normalize(body.position, currentUp)) {
            transformMatchesBodyEveryTick = false;
        } else {
            const glm::vec3 expectedCameraPosition = body.position + currentUp * body.eyeOffset;
            const glm::vec3 actualCameraPosition(transform.x, transform.y, transform.z);
            if (glm::length(expectedCameraPosition - actualCameraPosition) > 1.0e-4f) {
                transformMatchesBodyEveryTick = false;
            }
        }
        if (tick > 0 && body.isGrounded) {
            landedAgain = true;
            break;
        }
    }

    // Separate deterministic walk trace for the collision step-up path. This
    // observes whether collision position correction becomes outward velocity.
    entt::registry walkRegistry;
    const entt::entity walkEntity = walkRegistry.create();
    auto& walkPlayer = walkRegistry.emplace<PlayerControllerComponent>(walkEntity);
    auto& walkTransform = walkRegistry.emplace<TransformComponent>(walkEntity);
    auto& walkCamera = walkRegistry.emplace<CameraComponent>(walkEntity);
    auto& walkBody = walkRegistry.emplace<RigidBodyComponent>(walkEntity).body;
    walkBody = body;
    walkPlayer.planetarySpawnPending = false;
    walkCamera.planetaryFrameUp = camera.planetaryFrameUp;
    walkCamera.planetaryFrameForward = camera.planetaryFrameForward;
    walkCamera.planetaryFrameInitialized = camera.planetaryFrameInitialized;
    walkCamera.yaw = camera.yaw;
    walkCamera.pitch = camera.pitch;
    walkTransform.x = walkBody.position.x;
    walkTransform.y = walkBody.position.y + walkBody.eyeOffset;
    walkTransform.z = walkBody.position.z;
    deviceManager.GetInput().isJumping = false;
    deviceManager.GetInput().moveForward = 1.0f;
    deviceManager.GetInput().moveRight = 0.0f;
    float maxWalkRadialVelocityAfterCollision = -std::numeric_limits<float>::infinity();
    int firstWalkOutwardVelocityTick = -1;
    bool walkGroundedObserved = walkBody.isGrounded;
    int firstWalkGroundedLossTick = walkBody.isGrounded ? -1 : 0;
    int firstWalkRegroundedTick = -1;
    bool walkGroundedPreviousTick = walkBody.isGrounded;
    size_t walkDenseSolidSamples = 0;
    size_t walkDensePendingSamples = 0;
    size_t walkDenseOutOfBoundsSamples = 0;
    float walkMaximumDensePenetration = 0.0f;
    glm::ivec3 walkDeepestVoxel{-1};
    std::cout << "[D1.2.19d.5][WALK_BEGIN] grounded=" << walkBody.isGrounded
              << " radialVelocity=" << glm::dot(walkBody.velocity, glm::normalize(walkBody.position)) << '\n';
    for (int tick = 0; tick < 600; ++tick) {
        glm::vec3 upBefore;
        if (!fw::PlanetaryCameraFrame::Normalize(walkBody.position, upBefore)) break;
        const glm::vec3 positionBefore = walkBody.position;
        cameraSync.Update(walkRegistry, &context, fixedDt);
        movement.Update(walkRegistry, &context, fixedDt);
        const glm::vec3 velocityAfterMovement = walkBody.velocity;
        physics.Update(walkRegistry, &context, fixedDt);
        cameraSystem.Update(walkRegistry, &context, fixedDt);
        const float radialVelocityAfterCollision = glm::dot(walkBody.velocity, upBefore);
        const ColliderVolumeAudit volumeAudit = AuditDensePlanetaryCollider(
            world, walkBody, fw::PlanetSize::Tiny);
        walkDenseSolidSamples += volumeAudit.solidSamples;
        walkDensePendingSamples += volumeAudit.pendingSamples;
        walkDenseOutOfBoundsSamples += volumeAudit.outOfBoundsSamples;
        if (volumeAudit.maximumRadialPenetration > walkMaximumDensePenetration) {
            walkMaximumDensePenetration = volumeAudit.maximumRadialPenetration;
            walkDeepestVoxel = volumeAudit.deepestVoxel;
        }
        if (!walkBody.isGrounded && firstWalkGroundedLossTick < 0) {
            firstWalkGroundedLossTick = tick;
            glm::vec3 directionAfter;
            float localSurfaceRadius = 0.0f;
            const bool hasLocalSurface = fw::PlanetaryCameraFrame::Normalize(walkBody.position, directionAfter) &&
                world.TryResolvePlanetarySpawn(directionAfter, 0.11f, walkBody.radius,
                    surfaceSpawn, &localSurfaceRadius) == fw::PlanetarySpawnResolveStatus::Ready;
            std::cout << "[D1.2.19d.5][WALK_GROUNDED_LOSS] tick=" << tick
                      << " radialBefore=" << glm::dot(velocityAfterMovement, upBefore)
                      << " radialAfter=" << radialVelocityAfterCollision
                      << " positionBefore=" << positionBefore.x << ',' << positionBefore.y << ',' << positionBefore.z
                      << " positionAfter=" << walkBody.position.x << ',' << walkBody.position.y << ','
                      << walkBody.position.z << " bodyRadius=" << glm::length(walkBody.position)
                      << " footSurfaceClearance=" << (hasLocalSurface
                          ? glm::length(walkBody.position) - localSurfaceRadius
                          : std::numeric_limits<float>::quiet_NaN())
                      << " lastVoxel=" << walkBody.dbg_lastLookupCoord.x << ','
                      << walkBody.dbg_lastLookupCoord.y << ',' << walkBody.dbg_lastLookupCoord.z
                      << " lastSolid=" << walkBody.dbg_lastLookupSolid
                      << " contacts=" << walkBody.dbg_voxelContacts
                      << " air=" << walkBody.dbg_airSamples
                      << " pending=" << walkBody.dbg_terrainPendingSamples
                      << " radialContact=" << walkBody.dbg_radialGroundContact
                      << " tangentialSolidBlock=" << walkBody.dbg_tangentialSolidBlock
                      << " againstWall=" << walkBody.isAgainstWall
                      << " groundVoxel=" << walkBody.dbg_groundVoxelCoord.x << ','
                      << walkBody.dbg_groundVoxelCoord.y << ',' << walkBody.dbg_groundVoxelCoord.z
                      << " groundNormal=" << walkBody.dbg_groundContactNormal.x << ','
                      << walkBody.dbg_groundContactNormal.y << ',' << walkBody.dbg_groundContactNormal.z
                      << " denseSolid=" << volumeAudit.solidSamples
                      << " denseMaxPenetration=" << volumeAudit.maximumRadialPenetration
                      << " denseVoxel=" << volumeAudit.deepestVoxel.x << ','
                      << volumeAudit.deepestVoxel.y << ',' << volumeAudit.deepestVoxel.z
                      << " terrainPending=" << walkBody.dbg_terrainPending << '\n';
        }
        if (!walkGroundedPreviousTick && walkBody.isGrounded && firstWalkRegroundedTick < 0) {
            firstWalkRegroundedTick = tick;
            std::cout << "[D1.2.19d.6][WALK_REGROUNDED] tick=" << tick
                      << " radialSpeed=" << glm::dot(walkBody.velocity, upBefore)
                      << " solidContacts=" << walkBody.dbg_voxelContacts
                      << " lastVoxel=" << walkBody.dbg_lastLookupCoord.x << ','
                      << walkBody.dbg_lastLookupCoord.y << ',' << walkBody.dbg_lastLookupCoord.z << '\n';
        }
        walkGroundedPreviousTick = walkBody.isGrounded;
        walkGroundedObserved = walkGroundedObserved || walkBody.isGrounded;
        maxWalkRadialVelocityAfterCollision = std::max(
            maxWalkRadialVelocityAfterCollision, radialVelocityAfterCollision);
        if (radialVelocityAfterCollision > 0.05f && firstWalkOutwardVelocityTick < 0) {
            firstWalkOutwardVelocityTick = tick;
            const glm::vec3 integratedVelocity = velocityAfterMovement +
                (walkBody.dbg_gravityAccel - (walkBody.drag / walkBody.mass) * velocityAfterMovement) * fixedDt;
            std::cout << "[D1.2.19d.4][WALK_OUTWARD_VELOCITY] tick=" << tick
                      << " radialBefore=" << glm::dot(velocityAfterMovement, upBefore)
                      << " radialIntegrated=" << glm::dot(integratedVelocity, upBefore)
                      << " radialAfterCollision=" << radialVelocityAfterCollision
                      << " collisionRadialDelta=" << glm::dot(walkBody.velocity - integratedVelocity, upBefore)
                      << " positionBefore=" << positionBefore.x << ',' << positionBefore.y << ',' << positionBefore.z
                      << " positionAfter=" << walkBody.position.x << ',' << walkBody.position.y << ','
                      << walkBody.position.z << " grounded=" << walkBody.isGrounded
                      << " lastVoxel=" << walkBody.dbg_lastLookupCoord.x << ',' << walkBody.dbg_lastLookupCoord.y
                      << ',' << walkBody.dbg_lastLookupCoord.z << " lastSolid=" << walkBody.dbg_lastLookupSolid
                      << " contacts=" << walkBody.dbg_voxelContacts
                      << " terrainPending=" << walkBody.dbg_terrainPending
                      << " pendingSamples=" << walkBody.dbg_terrainPendingSamples << '\n';
        }
        if (walkBody.dbg_terrainPending) break;
    }
    std::cout << "[D1.2.19d.4][WALK_TRACE] ticks<=600 firstOutwardVelocityTick="
              << firstWalkOutwardVelocityTick
              << " maxPositiveRadialVelocity=" << std::max(0.0f, maxWalkRadialVelocityAfterCollision)
              << " lastGrounded=" << walkBody.isGrounded
              << " terrainPending=" << walkBody.dbg_terrainPending
              << " firstGroundedLossTick=" << firstWalkGroundedLossTick
              << " firstRegroundedTick=" << firstWalkRegroundedTick
              << " denseSolidSamples=" << walkDenseSolidSamples
              << " densePendingSamples=" << walkDensePendingSamples
              << " denseOutOfBoundsSamples=" << walkDenseOutOfBoundsSamples
              << " denseMaxPenetration=" << walkMaximumDensePenetration
              << " deepestVoxel=" << walkDeepestVoxel.x << ',' << walkDeepestVoxel.y << ','
              << walkDeepestVoxel.z << '\n';
    const bool walkContactPass = walkGroundedObserved && walkBody.isGrounded &&
        firstWalkOutwardVelocityTick < 0 && !walkBody.dbg_terrainPending;
    std::cout << (walkContactPass ? "[PASS]" : "[FAIL]")
              << " walking contact recovers after terrain variation without collision-generated outward velocity\n";
    deviceManager.GetInput().moveForward = 0.0f;
    const float finalClearance = glm::length(body.position) - surfaceRadius;
    const bool jumpAndReturnPass = jumpInputConsumed && maximumJumpHeight > 0.25f &&
        maximumJumpHeight < 3.0f && landedAgain &&
        body.isGrounded && transformMatchesBodyEveryTick && finalClearance >= -0.005f &&
        finalClearance < 0.5f;

    std::cout << "[OBSERVED] radial fall ticks=" << fallTicks << " gravityInward="
              << gravityTowardCenterEveryTick << " grounded=" << body.isGrounded
              << " settledClearance=" << settledClearance << " jumpApex=" << maximumJumpHeight
              << " jumpConsumed=" << jumpInputConsumed << " relanded=" << landedAgain
              << " finalClearance=" << finalClearance << " bodyTransformSync="
              << transformMatchesBodyEveryTick << " fallbackPending=" << fallback.pending << '\n';
    const bool locomotionScenariosPass = TestTinyLocomotionContinuity(world);
    const bool overallPass = fallAndAnchorPass && jumpAndReturnPass && walkContactPass &&
        locomotionScenariosPass;
    std::cout << (overallPass ? "[PASS]" : "[FAIL]")
              << " radial gravity, physical landing, Space jump, relanding and body/Transform camera anchoring\n";
    return overallPass;
}

bool TestPendingPlayerIsFrozen(fw::GameWorld& world) {
    entt::registry registry;
    const entt::entity entity = registry.create();
    auto& player = registry.emplace<PlayerControllerComponent>(entity);
    player.planetarySpawnPending = true;
    auto& transform = registry.emplace<TransformComponent>(entity, 12.0f, 13.0f, 14.0f);
    auto& camera = registry.emplace<CameraComponent>(entity);
    camera.yaw = 27.0f;
    camera.pitch = -8.0f;
    auto& body = registry.emplace<RigidBodyComponent>(entity).body;
    body.position = {2.0f, 3.0f, 4.0f};
    body.velocity = {5.0f, 6.0f, 7.0f};

    const glm::vec3 positionBefore = body.position;
    const glm::vec3 velocityBefore = body.velocity;
    const glm::vec3 transformBefore(transform.x, transform.y, transform.z);
    const float yawBefore = camera.yaw;
    const float pitchBefore = camera.pitch;
    SharedContext context{};
    context.forgeWorld = &world;
    fw::PlayerMovementSystem movement;
    movement.Update(registry, &context, 1.0f / 60.0f);
    fw::PhysicsSystem physics;
    physics.Update(registry, &context, 1.0f / 60.0f);
    return glm::length(body.position - positionBefore) == 0.0f &&
        glm::length(body.velocity - velocityBefore) == 0.0f &&
        glm::length(glm::vec3(transform.x, transform.y, transform.z) - transformBefore) == 0.0f &&
        camera.yaw == yawBefore && camera.pitch == pitchBefore && player.planetarySpawnPending;
}

bool TestPlanetaryCollisionQueryStates(fw::GameWorld& world) {
    const float radius = fw::PlanetMath::GetPlanetRadius(fw::PlanetSize::Tiny);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const fw::WorldVoxelQueryResult pendingQuery = world.QueryBlockCartesian(up * (radius + 10.0f));
    const fw::WorldVoxelQueryResult upperAirQuery = world.QueryBlockCartesian(up * (radius + 110.0f));

    RigidBody pending;
    pending.position = up * (radius + 10.0f);
    pending.velocity = -up;
    const glm::vec3 pendingStart = pending.position;
    PhysicsEngine physics;
    physics.StepSimulation(pending, PhysicsEngine::FIXED_DT, world);
    const bool pendingSafe = pendingQuery.status == fw::WorldVoxelQueryStatus::TerrainPending &&
        pending.dbg_terrainPending && pending.dbg_terrainPendingSamples > 0 &&
        pending.dbg_voxelContacts == 0 && !pending.isGrounded &&
        glm::length(pending.position - pendingStart) < 1.0e-6f &&
        glm::length(pending.velocity) < 1.0e-5f;

    // Tiny's centered radial cell domain reaches the origin, so temporarily use
    // Medium to construct a genuine local cell coordinate below zero. Restore
    // the Tiny fixture geometry before subsequent generation/solver checks.
    auto& registry = world.GetRegistry();
    const entt::entity planetEntity = world.GetPlanetEntity();
    if (!registry.valid(planetEntity) || !registry.all_of<fw::PlanetGeometryComponent>(planetEntity)) return false;
    auto& planetGeometry = registry.get<fw::PlanetGeometryComponent>(planetEntity);
    const fw::PlanetSize savedPlanetSize = planetGeometry.planetSize;
    planetGeometry.planetSize = fw::PlanetSize::Medium;
    const float outOfBoundsPlanetRadius = fw::PlanetMath::GetPlanetRadius(fw::PlanetSize::Medium);
    const float outOfBoundsRadius = std::max(0.0f,
        outOfBoundsPlanetRadius - static_cast<float>(fw::PlanetRadialMapping::ReferenceVoxelOffset) - 1.0f);
    const glm::vec3 outOfBoundsPosition = up * outOfBoundsRadius;
    const fw::WorldVoxelQueryResult outOfBoundsQuery = world.QueryBlockCartesian(outOfBoundsPosition);
    RigidBody outOfBounds;
    outOfBounds.position = outOfBoundsPosition;
    outOfBounds.velocity = -up;
    const glm::vec3 outOfBoundsStart = outOfBounds.position;
    physics.StepSimulation(outOfBounds, PhysicsEngine::FIXED_DT, world);
    planetGeometry.planetSize = savedPlanetSize;
    const bool outOfBoundsSafe = outOfBoundsQuery.status == fw::WorldVoxelQueryStatus::OutOfBounds &&
        outOfBounds.dbg_outOfBoundsSamples > 0 && outOfBounds.dbg_voxelContacts == 0 &&
        !outOfBounds.isGrounded && glm::length(outOfBounds.position - outOfBoundsStart) < 1.0e-6f;

    std::cout << "[OBSERVED] pending query=" << static_cast<unsigned>(pendingQuery.status)
              << " pendingSamples=" << pending.dbg_terrainPendingSamples
              << " grounded=" << pending.isGrounded
              << " OOB query=" << static_cast<unsigned>(outOfBoundsQuery.status)
              << " OOB samples=" << outOfBounds.dbg_outOfBoundsSamples
              << " grounded=" << outOfBounds.isGrounded << '\n';
    const bool upperAirPass = upperAirQuery.status == fw::WorldVoxelQueryStatus::Air &&
        upperAirQuery.block == fw::BlockType::Air;
    std::cout << ((pendingSafe && outOfBoundsSafe && upperAirPass) ? "[PASS]" : "[FAIL]")
              << " TerrainPending and OutOfBounds block safely without false Grounded/solid contact\n";
    return pendingSafe && outOfBoundsSafe && upperAirPass;
}

bool TestPlanetarySolidVoxelContact(fw::GameWorld& world, float& contactBoundaryRadius) {
    const float planetRadius = fw::PlanetMath::GetPlanetRadius(fw::PlanetSize::Tiny);
    const glm::vec3 direction = fw::CubeSphereMapping::FaceUVToDirection(
        fw::CubeFace::PositiveZ, glm::vec2(0.5f, 0.5f));
    int outermostSolidRow = -1;
    for (int y = fw::ChunkDimensions::VoxelsY - 1; y >= 0; --y) {
        const float sampleRadius = fw::PlanetRadialMapping::GetCellCenterRadius(0, y, planetRadius);
        const fw::WorldVoxelQueryResult sample = world.QueryBlockCartesian(direction * sampleRadius);
        if (sample.status == fw::WorldVoxelQueryStatus::SolidVoxel) {
            outermostSolidRow = sample.voxelCoord.y;
            break;
        }
        if (sample.status == fw::WorldVoxelQueryStatus::TerrainPending ||
            sample.status == fw::WorldVoxelQueryStatus::OutOfBounds ||
            sample.status == fw::WorldVoxelQueryStatus::InvalidInput) return false;
    }
    if (outermostSolidRow < 0) return false;

    contactBoundaryRadius = fw::PlanetRadialMapping::GetCellOuterRadius(
        0, outermostSolidRow, planetRadius);
    const auto solidProbe = world.QueryBlockCartesian(direction * (contactBoundaryRadius - 0.05f));
    const auto airProbe = world.QueryBlockCartesian(direction * (contactBoundaryRadius + 0.05f));
    if (solidProbe.status != fw::WorldVoxelQueryStatus::SolidVoxel ||
        airProbe.status != fw::WorldVoxelQueryStatus::Air) return false;

    RigidBody body;
    body.position = direction * contactBoundaryRadius;
    body.velocity = -direction;
    PhysicsEngine physics;
    physics.StepSimulation(body, PhysicsEngine::FIXED_DT, world);
    const bool pass = body.isGrounded && body.dbg_voxelContacts > 0 &&
        !body.dbg_terrainPending && body.dbg_lastLookupSolid;
    std::cout << "[OBSERVED] contact boundary=" << contactBoundaryRadius
              << " solidContacts=" << body.dbg_voxelContacts
              << " solidProbe=" << static_cast<unsigned>(solidProbe.status)
              << " airProbe=" << static_cast<unsigned>(airProbe.status)
              << " grounded=" << body.isGrounded << '\n';
    std::cout << (pass ? "[PASS]" : "[FAIL]")
              << " solid voxel sample increments contact count and permits Grounded\n";
    return pass;
}

bool TestPostPhysicsCameraSync(fw::GameWorld& world, const glm::vec3& direction) {
    glm::vec3 bodyPosition(0.0f);
    if (world.TryResolvePlanetarySpawn(direction, 0.11f, 0.3f, bodyPosition) !=
        fw::PlanetarySpawnResolveStatus::Ready) return false;

    entt::registry registry;
    const entt::entity entity = registry.create();
    registry.emplace<PlayerControllerComponent>(entity);
    auto& transform = registry.emplace<TransformComponent>(entity, -10.0f, -20.0f, -30.0f);
    auto& body = registry.emplace<RigidBodyComponent>(entity).body;
    body.position = bodyPosition;
    body.velocity = glm::normalize(direction) * -0.1f;

    SharedContext context{};
    context.forgeWorld = &world;
    fw::PhysicsSystem physics;
    physics.Update(registry, &context, 1.0f / 60.0f);

    glm::vec3 up;
    if (!fw::PlanetaryCameraFrame::Normalize(body.position, up)) return false;
    const glm::vec3 expectedCameraPosition = body.position + up * body.eyeOffset;
    const glm::vec3 actualCameraPosition(transform.x, transform.y, transform.z);
    return glm::length(actualCameraPosition - expectedCameraPosition) < 1.0e-5f &&
        glm::length(body.dbg_gravityAccel - up * glm::dot(body.dbg_gravityAccel, up)) < 1.0e-4f;
}

struct PlanetaryNeighborhoodStats {
    size_t resolvedCoordinates = 0;
    size_t crossFaceRelations = 0;
    size_t reciprocalRelations = 0;
    size_t seamVoxelSamples = 0;
    size_t seamFacesCulled = 0;
    size_t seamCullMisses = 0;
    size_t failedResolutions = 0;
    std::array<size_t, 9> seamCullBySlot{};
    std::array<size_t, 9> seamSamplesBySlot{};
    uint64_t lookupNanoseconds = 0;
    uint64_t orientationNanoseconds = 0;
    uint64_t buildNanoseconds = 0;
};

bool TestPlanetaryNeighborhoodContracts(
    const fw::MaterialRegistry& materials, PlanetaryNeighborhoodStats& stats) {
    constexpr std::array<fw::PlanetSize, 6> sizes{
        fw::PlanetSize::Tiny, fw::PlanetSize::Micro, fw::PlanetSize::Small,
        fw::PlanetSize::Medium, fw::PlanetSize::Large, fw::PlanetSize::Huge};
    constexpr std::array<std::pair<int, int>, 4> localDirections{{
        {-1,0}, {1,0}, {0,-1}, {0,1}}};
    constexpr fw::PlanetID planet{7};
    bool pass = true;

    for (const fw::PlanetSize size : sizes) {
        const int n = static_cast<int>(fw::PlanetMath::GetFaceResolution(size));
        for (int faceIndex = 0; faceIndex < 6; ++faceIndex) {
            for (int row = 0; row < n; ++row) {
                for (int col = 0; col < n; ++col) {
                    const fw::PlanetChunkCoord source{
                        planet, static_cast<fw::CubeFace>(faceIndex), col, row, 2};
                    for (const auto [dx, dz] : localDirections) {
                        fw::planetary_neighborhood::Address address;
                        const auto lookupStart = std::chrono::steady_clock::now();
                        const bool resolved = fw::planetary_neighborhood::TryResolveAddress(
                            source, dx, dz, size, address);
                        stats.lookupNanoseconds += static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - lookupStart).count());
                        if (!resolved) {
                            ++stats.failedResolutions;
                            pass = false;
                            continue;
                        }
                        ++stats.resolvedCoordinates;
                        const fw::PlanetChunkCoord expected = fw::PlanetMath::ResolveNeighbor(
                            source, dx, -dz, size);
                        int keyX = -1;
                        int keyZ = -1;
                        fw::PlanetChunkCoord decoded{};
                        const bool storageRoundTrip = address.coordinate == expected &&
                            address.coordinate.planet == source.planet &&
                            address.coordinate.layer == source.layer &&
                            fw::planet_grid_compat::TryLegacyNetToPlanetChunkCoord(
                                address.storageCx, address.storageCz, n, source.planet,
                                source.layer, decoded) && decoded == expected &&
                            fw::planet_grid_compat::TryPlanetChunkCoordToLegacyNet(
                                expected, n, keyX, keyZ) &&
                            keyX == address.storageCx && keyZ == address.storageCz;
                        pass = pass && storageRoundTrip;

                        if (expected.face == source.face) continue;
                        ++stats.crossFaceRelations;

                        int backCol = 0;
                        int backRow = 0;
                        bool reverseFound = false;
                        for (const auto [backDx, backDr] : std::array<std::pair<int, int>, 4>{{
                                 {-1,0}, {1,0}, {0,-1}, {0,1}}}) {
                            if (fw::PlanetMath::ResolveNeighbor(
                                    expected, backDx, backDr, size) == source) {
                                backCol = backDx;
                                backRow = backDr;
                                reverseFound = true;
                                break;
                            }
                        }
                        if (!reverseFound) {
                            ++stats.failedResolutions;
                            pass = false;
                            continue;
                        }
                        fw::planetary_neighborhood::Address backAddress;
                        const bool reciprocal = fw::planetary_neighborhood::TryResolveAddress(
                            expected, backCol, -backRow, size, backAddress) &&
                            backAddress.coordinate == source;
                        pass = pass && reciprocal;
                        if (reciprocal) ++stats.reciprocalRelations;

                        // Independently derive target boundary samples by matching
                        // the two Cube-Sphere edge directions, then verify that the
                        // prepared snapshot presents those voxels in source-local order.
                        fw::VoxelChunkComponent sourceData{};
                        fw::VoxelChunkComponent targetData{};
                        fw::VoxelChunkComponent oriented{};
                        sourceData.planetCoord = source;
                        targetData.planetCoord = expected;
                        sourceData.isGenerated = targetData.isGenerated = true;
                        std::array<int, CHUNK_SIZE> targetIndices{};
                        targetIndices.fill(-1);
                        for (int tangent = 0; tangent < CHUNK_SIZE; ++tangent) {
                            glm::vec2 sourceUv;
                            if (dx != 0) {
                                sourceUv.x = (static_cast<float>(col) + (dx > 0 ? 1.0f : 0.0f)) /
                                    static_cast<float>(n);
                                sourceUv.y = (static_cast<float>(row + 1) -
                                    (static_cast<float>(tangent) + 0.5f) / CHUNK_SIZE) /
                                    static_cast<float>(n);
                            } else {
                                sourceUv.x = (static_cast<float>(col) +
                                    (static_cast<float>(tangent) + 0.5f) / CHUNK_SIZE) /
                                    static_cast<float>(n);
                                sourceUv.y = (static_cast<float>(row + 1) - (dz > 0 ? 1.0f : 0.0f)) /
                                    static_cast<float>(n);
                            }
                            const glm::vec3 sourceEdgeDirection = glm::normalize(
                                fw::CubeSphereMapping::FaceUVToCubeDir(source.face, sourceUv));

                            int matchedTargetTangent = -1;
                            for (int targetTangent = 0; targetTangent < CHUNK_SIZE; ++targetTangent) {
                                glm::vec2 targetUv;
                                if (backCol != 0) {
                                    targetUv.x = (static_cast<float>(expected.col) +
                                        (backCol < 0 ? 0.0f : 1.0f)) / static_cast<float>(n);
                                    targetUv.y = (static_cast<float>(expected.row + 1) -
                                        (static_cast<float>(targetTangent) + 0.5f) / CHUNK_SIZE) /
                                        static_cast<float>(n);
                                } else {
                                    targetUv.x = (static_cast<float>(expected.col) +
                                        (static_cast<float>(targetTangent) + 0.5f) / CHUNK_SIZE) /
                                        static_cast<float>(n);
                                    targetUv.y = (static_cast<float>(expected.row + 1) -
                                        (backRow < 0 ? 1.0f : 0.0f)) / static_cast<float>(n);
                                }
                                const glm::vec3 targetEdgeDirection = glm::normalize(
                                    fw::CubeSphereMapping::FaceUVToCubeDir(expected.face, targetUv));
                                if (glm::length(sourceEdgeDirection - targetEdgeDirection) < 2.0e-5f) {
                                    matchedTargetTangent = targetTangent;
                                    break;
                                }
                            }
                            if (matchedTargetTangent < 0) {
                                ++stats.failedResolutions;
                                pass = false;
                                continue;
                            }
                            targetIndices[tangent] = matchedTargetTangent;
                            const int targetX = backCol != 0
                                ? (backCol < 0 ? 0 : CHUNK_SIZE - 1) : matchedTargetTangent;
                            const int targetZ = backCol != 0
                                ? matchedTargetTangent : (backRow < 0 ? CHUNK_SIZE - 1 : 0);
                            targetData.blocks[targetX][0][targetZ] =
                                static_cast<uint8_t>(tangent + 1);
                        }

                        const auto orientStart = std::chrono::steady_clock::now();
                        const bool orientationBuilt = fw::planetary_neighborhood::TryOrientCrossFaceSnapshot(
                            source, dx, dz, size, targetData, oriented);
                        stats.orientationNanoseconds += static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - orientStart).count());
                        pass = pass && orientationBuilt;
                        if (!orientationBuilt) continue;

                        for (int tangent = 0; tangent < CHUNK_SIZE; ++tangent) {
                            const int expectedTangent = targetIndices[tangent];
                            if (expectedTangent < 0) continue;
                            const int targetX = backCol != 0
                                ? (backCol < 0 ? 0 : CHUNK_SIZE - 1) : expectedTangent;
                            const int targetZ = backCol != 0
                                ? expectedTangent : (backRow < 0 ? CHUNK_SIZE - 1 : 0);
                            const int virtualX = dx != 0
                                ? (dx > 0 ? 0 : CHUNK_SIZE - 1) : tangent;
                            const int virtualZ = dx != 0
                                ? tangent : (dz > 0 ? 0 : CHUNK_SIZE - 1);
                            const bool sampleMatches = oriented.blocks[virtualX][0][virtualZ] ==
                                targetData.blocks[targetX][0][targetZ];
                            pass = pass && sampleMatches;
                            ++stats.seamVoxelSamples;
                        }

                        // One solid pair per directed seam must suppress exactly
                        // the shared face while leaving the other five faces visible.
                        constexpr int testTangent = CHUNK_SIZE / 2;
                        const int targetTangent = targetIndices[testTangent];
                        if (targetTangent < 0) {
                            pass = false;
                            continue;
                        }
                        const int sourceX = dx != 0 ? (dx > 0 ? CHUNK_SIZE - 1 : 0) : testTangent;
                        const int sourceZ = dx != 0 ? testTangent : (dz > 0 ? CHUNK_SIZE - 1 : 0);
                        sourceData.blocks[sourceX][8][sourceZ] = 3;
                        const int targetX = backCol != 0
                            ? (backCol < 0 ? 0 : CHUNK_SIZE - 1) : targetTangent;
                        const int targetZ = backCol != 0
                            ? targetTangent : (backRow < 0 ? CHUNK_SIZE - 1 : 0);
                        targetData.blocks[targetX][8][targetZ] = 3;
                        const auto orientAgainStart = std::chrono::steady_clock::now();
                        const bool orientationAgain = fw::planetary_neighborhood::TryOrientCrossFaceSnapshot(
                            source, dx, dz, size, targetData, oriented);
                        stats.orientationNanoseconds += static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - orientAgainStart).count());
                        if (!orientationAgain) {
                            pass = false;
                            continue;
                        }
                        std::array<const fw::VoxelChunkComponent*, 9> mesherNeighbors{};
                        mesherNeighbors[static_cast<size_t>((dx + 1) * 3 + dz + 1)] = &oriented;
                        const auto buildStart = std::chrono::steady_clock::now();
                        const auto seamMesh = fw::VoxelChunkMesher::Build(
                            sourceData, mesherNeighbors, materials, true, size);
                        stats.buildNanoseconds += static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - buildStart).count());
                        const bool culled = seamMesh.vertices.size() == 30;
                        pass = pass && culled;
                        const size_t slot = static_cast<size_t>((dx + 1) * 3 + dz + 1);
                        ++stats.seamSamplesBySlot[slot];
                        if (culled) {
                            ++stats.seamFacesCulled;
                            ++stats.seamCullBySlot[slot];
                        } else if (stats.seamCullMisses < 4) {
                            ++stats.seamCullMisses;
                            std::cout << "[P02C][DIAG] first seam cull miss face=" << faceIndex
                                      << " cell=" << col << ',' << row << " dx/dz=" << dx << ',' << dz
                                      << " back=" << backCol << ',' << backRow
                                      << " tangent=" << testTangent << " targetTangent=" << targetTangent
                                      << " targetBlock=" << static_cast<unsigned>(targetData.blocks[targetX][8][targetZ])
                                      << " virtualBlock=" << static_cast<unsigned>(oriented.blocks[
                                             dx != 0 ? (dx > 0 ? 0 : CHUNK_SIZE - 1) : testTangent][8][
                                             dx != 0 ? testTangent : (dz > 0 ? 0 : CHUNK_SIZE - 1)])
                                      << " vertices=" << seamMesh.vertices.size() << '\n';
                        }
                    }
                }
            }
        }
    }

    pass = pass && stats.crossFaceRelations == 2112 &&
        stats.reciprocalRelations == stats.crossFaceRelations &&
        stats.seamVoxelSamples == stats.crossFaceRelations * CHUNK_SIZE &&
        stats.seamFacesCulled == stats.crossFaceRelations &&
        stats.failedResolutions == 0 &&
        fw::planetary_neighborhood::Classify(false, false, false, false, false) ==
            fw::planetary_neighborhood::Availability::Missing &&
        fw::planetary_neighborhood::Classify(true, true, true, true, false) ==
            fw::planetary_neighborhood::Availability::Pending &&
        fw::planetary_neighborhood::Classify(true, true, true, true, true) ==
            fw::planetary_neighborhood::Availability::Available &&
        fw::planetary_neighborhood::Classify(true, false, false, false, false) ==
            fw::planetary_neighborhood::Availability::Invalid;
    return pass;
}

struct P02aCoordinateStats {
    size_t faceRoundTrips = 0;
    size_t sizeInvariants = 0;
    size_t legacyNetRoundTrips = 0;
    size_t generatorCanonicalSamples = 0;
    size_t gridBoundaryCases = 0;
    size_t gridBoundaryPreviousCell = 0;
    size_t edgePositions = 0;
    size_t reciprocalEdges = 0;
    size_t uniqueLogicalChunkKeys = 0;
    float maxDirectionError = 0.0f;
    float maxGeneratorPositionError = 0.0f;
};

bool TestP02aCoordinateAndTopology(P02aCoordinateStats& stats) {
    constexpr std::array<fw::PlanetSize, 6> sizes{
        fw::PlanetSize::Tiny, fw::PlanetSize::Micro, fw::PlanetSize::Small,
        fw::PlanetSize::Medium, fw::PlanetSize::Large, fw::PlanetSize::Huge};
    constexpr std::array<fw::CubeFace, 6> faces{
        fw::CubeFace::PositiveZ, fw::CubeFace::NegativeZ, fw::CubeFace::PositiveX,
        fw::CubeFace::NegativeX, fw::CubeFace::PositiveY, fw::CubeFace::NegativeY};
    constexpr std::array<std::pair<int, int>, 4> offsets{{{-1,0}, {1,0}, {0,-1}, {0,1}}};
    bool valid = true;

    for (const auto size : sizes) {
        const int n = static_cast<int>(fw::PlanetMath::GetFaceResolution(size));
        const int extent = fw::PlanetMath::GetEditorCanvasExtents(size);
        ++stats.sizeInvariants;
        valid = valid && 2 * extent + 1 == n;
        std::set<std::tuple<int, int, int>> uniqueKeys;
        for (const auto face : faces) {
            for (int row = 0; row < n; ++row) {
                for (int col = 0; col < n; ++col) {
                    uniqueKeys.emplace(static_cast<int>(face), col, row);
                    const int legacyRow = n - 1 - row;
                    valid = valid && legacyRow >= 0 && legacyRow < n && (n - 1 - legacyRow) == row;
                    const fw::PlanetChunkCoord canonical{{7}, face, col, row, 2};
                    int globalX = -1, globalZ = -1;
                    fw::PlanetChunkCoord decoded{};
                    const bool netRoundTrip = fw::planet_grid_compat::TryPlanetChunkCoordToLegacyNet(
                            canonical, n, globalX, globalZ) &&
                        fw::planet_grid_compat::TryLegacyNetToPlanetChunkCoord(
                            globalX, globalZ, n, canonical.planet, canonical.layer, decoded) &&
                        decoded == canonical;
                    if (!netRoundTrip && stats.legacyNetRoundTrips == 1) {
                        std::cout << "[P02B][DIAG] net roundtrip size=" << n << " face=" << static_cast<int>(face)
                                  << " col=" << col << " row=" << row << " global=" << globalX << ',' << globalZ << '\n';
                    }
                    valid = valid && netRoundTrip;
                    ++stats.legacyNetRoundTrips;

                    fw::CubeFace adapterFace{};
                    glm::vec2 adapterUv(0.0f);
                    const bool uvAdapted = fw::planet_grid_compat::TryLegacyNetCellToFaceUV(
                        globalX, globalZ, 0.5f, 0.5f, n, adapterFace, adapterUv);
                    const glm::vec2 centerUv = fw::CubeSphereMapping::GridToUV(col, row, n);
                    const bool uvMatch = uvAdapted && adapterFace == face && glm::length(adapterUv - centerUv) < 1.0e-6f;
                    if (!uvMatch && stats.legacyNetRoundTrips == 1) {
                        std::cout << "[P02B][DIAG] UV adapter size=" << n << " face=" << static_cast<int>(face)
                                  << " col=" << col << " row=" << row << " global=" << globalX << ',' << globalZ
                                  << " uv=" << adapterUv.x << ',' << adapterUv.y << " expected="
                                  << centerUv.x << ',' << centerUv.y << '\n';
                    }
                    valid = valid && uvMatch;

                    const glm::vec2 centerChart = fw::CubeSphereMapping::FaceUVToChartCoordinates(centerUv);
                    float continuousX = 0.0f, continuousZ = 0.0f;
                    const bool chartMapped = fw::planet_grid_compat::TryFaceChartToLegacyNetContinuous(
                        face, centerChart.x, centerChart.y, n, continuousX, continuousZ);
                    const bool chartMatch = chartMapped &&
                        std::abs(continuousX - static_cast<float>(globalX)) < 1.0e-5f &&
                        std::abs(continuousZ - static_cast<float>(globalZ)) < 1.0e-5f;
                    if (!chartMatch && stats.legacyNetRoundTrips == 1) {
                        std::cout << "[P02B][DIAG] chart net size=" << n << " face=" << static_cast<int>(face)
                                  << " col=" << col << " row=" << row << " continuous=" << continuousX << ','
                                  << continuousZ << " expected=" << globalX << ',' << globalZ << '\n';
                    }
                    valid = valid && chartMatch;

                    glm::vec3 chunkPosition(0.0f);
                    glm::quat chunkRotation(1.0f, 0.0f, 0.0f, 0.0f);
                    const float radius = fw::PlanetMath::GetPlanetRadius(size);
                    const bool transformOk = fw::MapWorldGenerator::GetSphericalChunkTransform(
                        size, globalX, globalZ, chunkPosition, chunkRotation);
                    const float transformError = glm::length(
                        chunkPosition - fw::CubeSphereMapping::FaceUVToDirection(face, centerUv) * radius);
                    stats.maxGeneratorPositionError = std::max(stats.maxGeneratorPositionError, transformError);
                    ++stats.generatorCanonicalSamples;
                    if ((!transformOk || transformError >= 2.0e-4f) && stats.generatorCanonicalSamples == 1) {
                        std::cout << "[P02B][DIAG] chunk transform size=" << n << " face=" << static_cast<int>(face)
                                  << " col=" << col << " row=" << row << " ok=" << transformOk
                                  << " error=" << transformError << '\n';
                    }
                    valid = valid && transformOk && transformError < 2.0e-4f;

                    glm::vec3 truePosition(0.0f);
                    const bool truePositionOk = fw::MapWorldGenerator::GetTrueSphericalPosition(
                        size, false, globalX, globalZ, 8.0f, 25.0f, 8.0f, truePosition);
                    const float truePositionError = glm::length(truePosition - chunkPosition);
                    if ((!truePositionOk || truePositionError >= 2.0e-4f) && stats.generatorCanonicalSamples == 1) {
                        std::cout << "[P02B][DIAG] true position size=" << n << " face=" << static_cast<int>(face)
                                  << " col=" << col << " row=" << row << " ok=" << truePositionOk
                                  << " error=" << truePositionError << '\n';
                    }
                    valid = valid && truePositionOk && truePositionError < 2.0e-4f;

                    glm::vec3 legacyRadialSample(0.0f);
                    const bool legacyRadialSampleOk = fw::MapWorldGenerator::GetTrueSphericalPosition(
                        size, false, globalX, globalZ, 8.0f, 0.0f, 8.0f, legacyRadialSample);
                    const glm::vec3 expectedLegacyRadialSample =
                        glm::normalize(chunkPosition) * (radius - 25.0f);
                    const float legacyRadialError = glm::length(
                        legacyRadialSample - expectedLegacyRadialSample);
                    if ((!legacyRadialSampleOk || legacyRadialError >= 2.0e-4f) &&
                        stats.generatorCanonicalSamples == 1) {
                        std::cout << "[P02B][DIAG] legacy radial compatibility size=" << n
                                  << " face=" << static_cast<int>(face) << " col=" << col
                                  << " row=" << row << " ok=" << legacyRadialSampleOk
                                  << " error=" << legacyRadialError << '\n';
                    }
                    valid = valid && legacyRadialSampleOk && legacyRadialError < 2.0e-4f;

                    const glm::vec3 worldSample = fw::CubeSphereMapping::FaceUVToDirection(face, centerUv) * (radius + 5.0f);
                    float flatX = 0.0f, localY = 0.0f, flatZ = 0.0f;
                    fw::MapWorldGenerator::WorldToVoxelCoord(size, false, worldSample, flatX, localY, flatZ);
                    const float gridX = (flatX - fw::ChunkDimensions::HorizontalWorldExtent * 0.5f) /
                        fw::ChunkDimensions::HorizontalWorldExtent;
                    const float gridZ = (flatZ - fw::ChunkDimensions::HorizontalWorldExtent * 0.5f) /
                        fw::ChunkDimensions::HorizontalWorldExtent;
                    const bool worldGridMatch = std::abs(gridX - static_cast<float>(globalX)) < 2.0e-4f &&
                        std::abs(gridZ - static_cast<float>(globalZ)) < 2.0e-4f;
                    if (!worldGridMatch && stats.generatorCanonicalSamples == 1) {
                        std::cout << "[P02B][DIAG] WorldToVoxelCoord size=" << n << " face=" << static_cast<int>(face)
                                  << " col=" << col << " row=" << row << " flat=" << gridX << ',' << gridZ
                                  << " expected=" << globalX << ',' << globalZ << '\n';
                    }
                    valid = valid && worldGridMatch;

                    const glm::vec2 uv = fw::CubeSphereMapping::GridToUV(col, row, n);
                    const glm::vec3 direction = fw::CubeSphereMapping::FaceUVToDirection(face, uv);
                    int foundFace = -1;
                    glm::vec2 foundUv(0.0f);
                    fw::CubeSphereMapping::DirectionToFaceUV(direction, foundFace, foundUv);
                    const glm::vec3 restored = fw::CubeSphereMapping::FaceUVToDirection(foundFace, foundUv);
                    const float error = glm::length(direction - restored);
                    stats.maxDirectionError = std::max(stats.maxDirectionError, error);
                    ++stats.faceRoundTrips;
                    valid = valid && foundFace == static_cast<int>(face) && error <= 2.0e-5f;

                    const fw::PlanetChunkCoord source{{1}, face, col, row, 0};
                    for (const auto [dc, dr] : offsets) {
                        if ((dc < 0 && col != 0) || (dc > 0 && col != n - 1) ||
                            (dr < 0 && row != 0) || (dr > 0 && row != n - 1)) continue;
                        const auto neighbor = fw::PlanetMath::ResolveNeighbor(source, dc, dr, size);
                        ++stats.edgePositions;
                        bool reciprocal = false;
                        for (const auto [rdc, rdr] : offsets) {
                            const auto back = fw::PlanetMath::ResolveNeighbor(neighbor, rdc, rdr, size);
                            if (back == source) reciprocal = true;
                        }
                        if (reciprocal) ++stats.reciprocalEdges;
                        else valid = false;
                    }
                }
            }
        }
        stats.uniqueLogicalChunkKeys += uniqueKeys.size();

        for (int k = 0; k <= n; ++k) {
            const float edge = static_cast<float>(k) / static_cast<float>(n);
            const std::array<float, 3> samples{
                edge,
                std::nextafter(edge, -std::numeric_limits<float>::infinity()),
                std::nextafter(edge, std::numeric_limits<float>::infinity())};
            for (float u : samples) {
                int colA = -1, rowA = -1, colB = -1, rowB = -1;
                fw::CubeSphereMapping::UVToGrid({u, 0.5f}, n, colA, rowA);
                fw::CubeSphereMapping::UVToGrid({u, 0.5f}, n, colB, rowB);
                ++stats.gridBoundaryCases;
                valid = valid && colA == colB && rowA == rowB && colA >= 0 && colA < n;
                if (k > 0 && k < n && u == edge && colA < k) ++stats.gridBoundaryPreviousCell;
            }
            for (float v : samples) {
                int colA = -1, rowA = -1, colB = -1, rowB = -1;
                fw::CubeSphereMapping::UVToGrid({0.5f, v}, n, colA, rowA);
                fw::CubeSphereMapping::UVToGrid({0.5f, v}, n, colB, rowB);
                ++stats.gridBoundaryCases;
                valid = valid && colA == colB && rowA == rowB && rowA >= 0 && rowA < n;
                if (k > 0 && k < n && v == edge && rowA < k) ++stats.gridBoundaryPreviousCell;
            }
        }
    }
    return valid;
}

struct P02aRayStats {
    bool axialHit = false;
    float axialDistance = -1.0f;
    bool outsideOriginHit = false;
    float outsideOriginDistance = -1.0f;
    bool parallelBoundaryHit = false;
    float parallelBoundaryDistance = -1.0f;
    bool voxelBoundaryHit = false;
    bool insideOriginHit = false;
    float insideOriginDistance = -1.0f;
    bool nanDirectionHit = false;
    bool infiniteDirectionHit = false;
    bool nonFiniteOriginRejected = false;
    bool invalidDistanceRejected = false;
    bool translatedParentHit = false;
    bool scaledHit = false;
    float scaledDistance = -1.0f;
    float uniformlyScaledDistance = -1.0f;
    bool distanceContractPass = false;
    bool invalidInputRejected = false;
    bool maxDistanceClipped = false;
    uint64_t queryNanoseconds = 0;
    uint64_t ddaNanoseconds = 0;
};

bool TestP02aRaycastCharacterization(P02aRayStats& stats) {
    SharedContext isolatedContext{};
    fw::GameWorld isolatedWorld;
    isolatedWorld.Initialize(&isolatedContext);
    auto& registry = isolatedWorld.GetRegistry();
    if (registry.valid(isolatedWorld.GetPlanetEntity()) &&
        registry.all_of<fw::PlanetGeometryComponent>(isolatedWorld.GetPlanetEntity())) {
        registry.get<fw::PlanetGeometryComponent>(isolatedWorld.GetPlanetEntity()).isLogicalSphere = false;
    }
    isolatedContext.forgeWorld = &isolatedWorld;
    constexpr uint8_t solidId = 3;
    const auto chunkEntity = isolatedWorld.CreateChunkEntity("P02a_Raycast", {0.0f, 0.0f, 0.0f}, 0, 0);
    if (chunkEntity == entt::null || !registry.valid(chunkEntity)) return false;
    auto& chunk = registry.get<fw::VoxelChunkComponent>(chunkEntity);
    auto& transform = registry.get<fw::TransformComponent>(chunkEntity);

    fw::RaycastQuery query{};
    query.mode = fw::RaycastMode::Voxel;
    query.maxDistance = 100.0f;
    query.ray = {{0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, -1.0f}};
    chunk.blocks[0][0][0] = solidId;
    const auto axial = fw::RaycastSystem::Cast(&isolatedContext, query);
    stats.axialHit = axial.hit;
    stats.axialDistance = axial.distance;

    // A solid voxel at the far AABB boundary reveals whether DDA distance is
    // measured from the original ray origin or from a clamped entry cell.
    chunk.blocks[0][0][0] = 0;
    chunk.blocks[0][0][15] = solidId;
    query.ray = {{0.0f, 0.0f, 30.0f}, {0.0f, 0.0f, -1.0f}};
    const auto outside = fw::RaycastSystem::Cast(&isolatedContext, query);
    stats.outsideOriginHit = outside.hit;
    stats.outsideOriginDistance = outside.distance;

    chunk.blocks[0][0][15] = 0;
    chunk.blocks[0][0][0] = solidId;
    query.ray = {{-0.5f, 0.0f, 5.0f}, {0.0f, 0.0f, -1.0f}};
    const auto boundary = fw::RaycastSystem::Cast(&isolatedContext, query);
    stats.parallelBoundaryHit = boundary.hit;
    stats.parallelBoundaryDistance = boundary.distance;
    query.ray = {{0.5f, 0.0f, 5.0f}, {0.0f, 0.0f, -1.0f}};
    stats.voxelBoundaryHit = fw::RaycastSystem::Cast(&isolatedContext, query).hit;
    query.ray = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}};
    const auto inside = fw::RaycastSystem::Cast(&isolatedContext, query);
    stats.insideOriginHit = inside.hit;
    stats.insideOriginDistance = inside.distance;
    query.ray.direction = {std::numeric_limits<float>::quiet_NaN(), 0.0f, -1.0f};
    const auto nanDirection = fw::RaycastSystem::Cast(&isolatedContext, query);
    stats.nanDirectionHit = nanDirection.hit;
    query.ray.direction = {std::numeric_limits<float>::infinity(), 0.0f, -1.0f};
    const auto infiniteDirection = fw::RaycastSystem::Cast(&isolatedContext, query);
    stats.infiniteDirectionHit = infiniteDirection.hit;
    query.ray = {{std::numeric_limits<float>::quiet_NaN(), 0.0f, 5.0f}, {0.0f, 0.0f, -1.0f}};
    const auto nanOrigin = fw::RaycastSystem::Cast(&isolatedContext, query);
    query.ray.origin.x = std::numeric_limits<float>::infinity();
    const auto infiniteOrigin = fw::RaycastSystem::Cast(&isolatedContext, query);
    stats.nonFiniteOriginRejected = !nanOrigin.hit && !infiniteOrigin.hit &&
        nanOrigin.status == fw::RaycastResultStatus::InvalidInput &&
        infiniteOrigin.status == fw::RaycastResultStatus::InvalidInput;
    query.ray = {{0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, -1.0f}};
    query.maxDistance = std::numeric_limits<float>::quiet_NaN();
    const auto nanDistance = fw::RaycastSystem::Cast(&isolatedContext, query);
    query.maxDistance = std::numeric_limits<float>::infinity();
    const auto infiniteDistance = fw::RaycastSystem::Cast(&isolatedContext, query);
    query.maxDistance = -1.0f;
    const auto negativeDistance = fw::RaycastSystem::Cast(&isolatedContext, query);
    stats.invalidDistanceRejected = !nanDistance.hit && !infiniteDistance.hit && !negativeDistance.hit &&
        nanDistance.status == fw::RaycastResultStatus::InvalidInput &&
        infiniteDistance.status == fw::RaycastResultStatus::InvalidInput &&
        negativeDistance.status == fw::RaycastResultStatus::InvalidInput;
    stats.invalidInputRejected = !stats.nanDirectionHit && !stats.infiniteDirectionHit &&
        nanDirection.status == fw::RaycastResultStatus::InvalidInput &&
        infiniteDirection.status == fw::RaycastResultStatus::InvalidInput &&
        stats.nonFiniteOriginRejected && stats.invalidDistanceRejected;

    query.ray = {{0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, -1.0f}};
    query.maxDistance = 4.49f;
    const auto clipped = fw::RaycastSystem::Cast(&isolatedContext, query);
    query.maxDistance = 4.5f;
    const auto atLimit = fw::RaycastSystem::Cast(&isolatedContext, query);
    stats.maxDistanceClipped = !clipped.hit && atLimit.hit && std::abs(atLimit.distance - 4.5f) < 1.0e-5f;

    // Parent translation is observable because the current broadphase checks
    // TransformComponent.location before computing the global matrix.
    const auto parent = registry.create();
    auto& parentTransform = registry.emplace<fw::TransformComponent>(parent);
    parentTransform.location = {1000.0f, 0.0f, 0.0f};
    transform.parent = parent;
    query.ray = {{1000.0f, 0.0f, 5.0f}, {0.0f, 0.0f, -1.0f}};
    query.maxDistance = 20.0f;
    stats.translatedParentHit = fw::RaycastSystem::Cast(&isolatedContext, query).hit;
    transform.parent = entt::null;

    transform.scale = {1.0f, 1.0f, 2.0f};
    query.maxDistance = 100.0f;
    query.ray = {{0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, -1.0f}};
    const auto scaled = fw::RaycastSystem::Cast(&isolatedContext, query);
    stats.scaledHit = scaled.hit;
    stats.scaledDistance = scaled.distance;
    transform.scale = {2.0f, 2.0f, 2.0f};
    query.ray = {{0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, -1.0f}};
    stats.uniformlyScaledDistance = fw::RaycastSystem::Cast(&isolatedContext, query).distance;
    stats.distanceContractPass = std::abs(stats.axialDistance - 4.5f) < 1.0e-5f &&
        std::abs(stats.outsideOriginDistance - 14.5f) < 1.0e-5f &&
        std::abs(stats.parallelBoundaryDistance - 4.5f) < 1.0e-5f &&
        stats.voxelBoundaryHit == false && stats.insideOriginDistance == 0.0f &&
        stats.translatedParentHit && stats.scaledHit &&
        std::abs(stats.scaledDistance - 9.0f) < 1.0e-5f &&
        std::abs(stats.uniformlyScaledDistance - 9.0f) < 1.0e-5f;

    // Microbenchmarks are informational and intentionally use fixed inputs.
    constexpr int iterations = 4000;
    const auto ddaStart = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        query.ray.origin.z = 5.0f + static_cast<float>(i & 1);
        (void)fw::RaycastSystem::Cast(&isolatedContext, query);
    }
    stats.ddaNanoseconds = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - ddaStart).count());
    return stats.axialHit && stats.outsideOriginHit && stats.parallelBoundaryHit &&
        stats.insideOriginHit && stats.distanceContractPass && stats.invalidInputRejected &&
        stats.maxDistanceClipped;
}

struct PlanetaryRayProbeStats {
    size_t probes = 0;
    size_t hits = 0;
    size_t correctFaces = 0;
    size_t correctChunks = 0;
    size_t correctDistances = 0;
    size_t correctNormals = 0;
    size_t correctFirstCells = 0;
    bool pendingClassified = false;
    bool missingClassified = false;
    bool tangentialCrossingPass = false;
    float tangentialDistance = -1.0f;
    bool outOfBoundsClassified = false;
    uint64_t castNanoseconds = 0;
};

bool TestPlanetaryRayTraversal(PlanetaryRayProbeStats& stats) {
    const std::array<fw::PlanetSize, 6> sizes = {
        fw::PlanetSize::Tiny, fw::PlanetSize::Micro, fw::PlanetSize::Small,
        fw::PlanetSize::Medium, fw::PlanetSize::Large, fw::PlanetSize::Huge};
    const std::array<glm::vec2, 9> charts = {
        glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(-1.0f, 0.0f),
        glm::vec2(0.0f, 1.0f), glm::vec2(0.0f, -1.0f), glm::vec2(1.0f, 1.0f),
        glm::vec2(-1.0f, 1.0f), glm::vec2(1.0f, -1.0f), glm::vec2(-1.0f, -1.0f)};

    for (const auto size : sizes) {
        SharedContext context{};
        fw::GameWorld world;
        world.Initialize(&context);
        context.forgeWorld = &world;
        auto& registry = world.GetRegistry();
        const auto planetEntity = world.GetPlanetEntity();
        auto& geometry = registry.get<fw::PlanetGeometryComponent>(planetEntity);
        geometry.isLogicalSphere = true;
        geometry.planetSize = size;
        const int resolution = static_cast<int>(fw::PlanetMath::GetFaceResolution(size));
        const float planetRadius = fw::PlanetMath::GetPlanetRadius(size);
        const fw::PlanetID planetId{0x50403244u};

        struct Probe {
            glm::vec3 direction{0.0f};
            int expectedFace = -1;
            glm::ivec3 expectedVoxel{0};
            entt::entity expectedEntity{entt::null};
        };
        std::vector<Probe> probes;
        std::map<std::pair<int, int>, entt::entity> entitiesByKey;
        for (int face = 0; face < 6; ++face) {
            for (const glm::vec2& chart : charts) {
                const glm::vec3 direction = fw::CubeSphereMapping::FaceChartToDirection(face, chart);
                fw::CubeFace canonicalFace{};
                glm::vec2 uv(0.0f);
                fw::CubeSphereMapping::DirectionToFaceUV(direction, canonicalFace, uv);
                int col = 0, row = 0;
                fw::CubeSphereMapping::UVToGrid(uv, resolution, col, row);
                const fw::PlanetChunkCoord coordinate{planetId, canonicalFace, col, row, 0};
                int chunkX = 0, chunkZ = 0;
                if (!fw::planet_grid_compat::TryPlanetChunkCoordToLegacyNet(
                        coordinate, resolution, chunkX, chunkZ)) return false;
                const int withinCol = std::clamp(static_cast<int>(std::floor(
                    (uv.x * resolution - col) * CHUNK_SIZE)), 0, CHUNK_SIZE - 1);
                const int withinRow = std::clamp(static_cast<int>(std::floor(
                    (1.0f - (uv.y * resolution - row)) * CHUNK_SIZE)), 0, CHUNK_SIZE - 1);
                const int voxelY = static_cast<int>(std::floor(
                    fw::PlanetRadialMapping::GetVoxelCellCoordinateFromDistance(planetRadius, planetRadius)));
                const int voxelX = chunkX * CHUNK_SIZE + withinCol;
                const int voxelZ = chunkZ * CHUNK_SIZE + withinRow;
                if (voxelX < 0 || voxelZ < 0 || voxelY < 0 || voxelY >= CHUNK_HEIGHT) {
                    std::cerr << "[P02D][DIAG] mapped voxel out of bounds size=" << resolution
                              << " face=" << face << " canonicalFace=" << static_cast<int>(canonicalFace)
                              << " chart=" << chart.x << ',' << chart.y
                              << " voxel=" << voxelX << ',' << voxelY << ',' << voxelZ
                              << " key=" << chunkX << ',' << chunkZ << "\n";
                    return false;
                }
                const int localX = withinCol;
                const int localZ = withinRow;
                auto key = std::make_pair(chunkX, chunkZ);
                auto found = entitiesByKey.find(key);
                entt::entity entity = entt::null;
                if (found == entitiesByKey.end()) {
                    entity = world.CreateChunkEntity("P02D_Synthetic_Ray", {0.0f, 0.0f, 0.0f}, chunkX, chunkZ);
                    if (entity == entt::null || !registry.valid(entity)) {
                        std::cerr << "[P02D][DIAG] chunk creation failed key=" << chunkX << ',' << chunkZ << '\n';
                        return false;
                    }
                    auto& chunk = registry.get<fw::VoxelChunkComponent>(entity);
                    chunk.planetCoord = coordinate;
                    chunk.isGenerated = true;
                    entitiesByKey.emplace(key, entity);
                } else {
                    entity = found->second;
                }
                auto& chunk = registry.get<fw::VoxelChunkComponent>(entity);
                chunk.blocks[localX][voxelY][localZ] = 3;
                int actualFace = -1;
                glm::vec2 actualUv(0.0f);
                fw::CubeSphereMapping::DirectionToFaceUV(direction, actualFace, actualUv);
                probes.push_back({direction, actualFace, {voxelX, voxelY, voxelZ}, entity});
            }
        }

        const auto planetaryCastStart = std::chrono::steady_clock::now();
        for (const Probe& probe : probes) {
            ++stats.probes;
            fw::RaycastQuery query{};
            query.mode = fw::RaycastMode::Voxel;
            query.maxDistance = 8.0f;
            query.ray = {probe.direction * (planetRadius + 6.0f), -probe.direction};
            const fw::RaycastHit hit = fw::RaycastSystem::Cast(&context, query);
            if (!hit.hit || hit.status != fw::RaycastResultStatus::Hit) continue;
            ++stats.hits;
            if (hit.faceIndex == probe.expectedFace) ++stats.correctFaces;
            if (hit.targetEntity == probe.expectedEntity) ++stats.correctChunks;
            if (hit.voxelPosition == probe.expectedVoxel) ++stats.correctFirstCells;
            if (std::abs(hit.distance - 5.5f) <= 2.0e-4f) ++stats.correctDistances;
            if (IsFinite(glm::vec2(hit.faceNormal.x, hit.faceNormal.y)) &&
                std::isfinite(hit.faceNormal.z) && glm::length(hit.faceNormal) > 0.999f &&
                glm::dot(hit.faceNormal, probe.direction) > 0.999f) ++stats.correctNormals;
        }
        stats.castNanoseconds += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - planetaryCastStart).count());
    }

    // Missing chunk storage and an existing-but-unready chunk have distinct raycast statuses.
    SharedContext pendingContext{};
    fw::GameWorld pendingWorld;
    pendingWorld.Initialize(&pendingContext);
    pendingContext.forgeWorld = &pendingWorld;
    auto& pendingRegistry = pendingWorld.GetRegistry();
    auto& pendingGeometry = pendingRegistry.get<fw::PlanetGeometryComponent>(pendingWorld.GetPlanetEntity());
    pendingGeometry.isLogicalSphere = true;
    pendingGeometry.planetSize = fw::PlanetSize::Tiny;
    const glm::vec3 direction(0.0f, 0.0f, 1.0f);
    fw::RaycastQuery pendingQuery{};
    pendingQuery.mode = fw::RaycastMode::Voxel;
    pendingQuery.maxDistance = 100.0f;
    pendingQuery.ray = {direction * (fw::PlanetMath::GetPlanetRadius(fw::PlanetSize::Tiny) + 20.0f), -direction};
    const auto pending = fw::RaycastSystem::Cast(&pendingContext, pendingQuery);
    stats.missingClassified = !pending.hit && pending.status == fw::RaycastResultStatus::Missing;
    const fw::PlanetID pendingPlanetId{0x50403247u};
    const int pendingResolution = static_cast<int>(fw::PlanetMath::GetFaceResolution(fw::PlanetSize::Tiny));
    fw::CubeFace pendingFace{};
    glm::vec2 pendingUv(0.0f);
    fw::CubeSphereMapping::DirectionToFaceUV(direction, pendingFace, pendingUv);
    int pendingCol = 0, pendingRow = 0;
    fw::CubeSphereMapping::UVToGrid(pendingUv, pendingResolution, pendingCol, pendingRow);
    const fw::PlanetChunkCoord pendingCoord{pendingPlanetId, pendingFace, pendingCol, pendingRow, 0};
    int pendingStorageX = 0, pendingStorageZ = 0;
    if (fw::planet_grid_compat::TryPlanetChunkCoordToLegacyNet(
            pendingCoord, pendingResolution, pendingStorageX, pendingStorageZ)) {
        const auto unreadyEntity = pendingWorld.CreateChunkEntity(
            "P02D_Unready", {0.0f, 0.0f, 0.0f}, pendingStorageX, pendingStorageZ);
        if (unreadyEntity != entt::null && pendingRegistry.valid(unreadyEntity)) {
            pendingRegistry.get<fw::VoxelChunkComponent>(unreadyEntity).planetCoord = pendingCoord;
            const auto unready = fw::RaycastSystem::Cast(&pendingContext, pendingQuery);
            stats.pendingClassified = !unready.hit && unready.status == fw::RaycastResultStatus::Pending;
        }
    }

    // A tangent ray crosses a Cube-Sphere face and voxel boundary before the solid cell.
    SharedContext tangentContext{};
    fw::GameWorld tangentWorld;
    tangentWorld.Initialize(&tangentContext);
    tangentContext.forgeWorld = &tangentWorld;
    auto& tangentRegistry = tangentWorld.GetRegistry();
    auto& tangentGeometry = tangentRegistry.get<fw::PlanetGeometryComponent>(tangentWorld.GetPlanetEntity());
    tangentGeometry.isLogicalSphere = true;
    tangentGeometry.planetSize = fw::PlanetSize::Tiny;
    const float tinyRadius = fw::PlanetMath::GetPlanetRadius(fw::PlanetSize::Tiny);
    const int tinyResolution = static_cast<int>(fw::PlanetMath::GetFaceResolution(fw::PlanetSize::Tiny));
    const fw::PlanetID tangentPlanetId{0x50403245u};
    std::map<std::pair<int, int>, entt::entity> tangentEntities;
    glm::ivec3 tangentTargetVoxel(0);
    fw::CubeFace tangentTargetFace = fw::CubeFace::PositiveZ;
    int tangentTargetCol = 0;
    int tangentTargetRow = 0;
    int tangentTargetLocalX = 0;
    int tangentTargetLocalY = 0;
    int tangentTargetLocalZ = 0;
    auto ensureTangentChunkAndVoxel = [&](const glm::vec3& point, bool solid) {
        const float pointRadius = glm::length(point);
        if (!(pointRadius > 0.0f)) return false;
        fw::CubeFace face{};
        glm::vec2 uv(0.0f);
        fw::CubeSphereMapping::DirectionToFaceUV(point / pointRadius, face, uv);
        int col = 0, row = 0;
        fw::CubeSphereMapping::UVToGrid(uv, tinyResolution, col, row);
        const fw::PlanetChunkCoord coord{tangentPlanetId, face, col, row, 0};
        int storageX = 0, storageZ = 0;
        if (!fw::planet_grid_compat::TryPlanetChunkCoordToLegacyNet(
                coord, tinyResolution, storageX, storageZ)) return false;
        const int localX = std::clamp(static_cast<int>(std::floor(
            (uv.x * tinyResolution - col) * CHUNK_SIZE)), 0, CHUNK_SIZE - 1);
        const int localZ = std::clamp(static_cast<int>(std::floor(
            (1.0f - (uv.y * tinyResolution - row)) * CHUNK_SIZE)), 0, CHUNK_SIZE - 1);
        const int localY = static_cast<int>(std::floor(
            fw::PlanetRadialMapping::GetVoxelCellCoordinateFromDistance(pointRadius, tinyRadius)));
        if (localY < 0 || localY >= CHUNK_HEIGHT) return false;
        const auto key = std::make_pair(storageX, storageZ);
        entt::entity entity = entt::null;
        auto found = tangentEntities.find(key);
        if (found == tangentEntities.end()) {
            entity = tangentWorld.CreateChunkEntity("P02D_Tangent", {0.0f, 0.0f, 0.0f}, storageX, storageZ);
            if (entity == entt::null || !tangentRegistry.valid(entity)) return false;
            auto& chunk = tangentRegistry.get<fw::VoxelChunkComponent>(entity);
            chunk.planetCoord = coord;
            chunk.isGenerated = true;
            tangentEntities.emplace(key, entity);
        } else {
            entity = found->second;
        }
        auto& chunk = tangentRegistry.get<fw::VoxelChunkComponent>(entity);
        if (chunk.planetCoord != coord) return false;
        chunk.blocks[localX][localY][localZ] = solid ? 3 : 0;
        if (solid) {
            tangentTargetVoxel = {storageX * CHUNK_SIZE + localX, localY,
                                  storageZ * CHUNK_SIZE + localZ};
            tangentTargetFace = face;
            tangentTargetCol = col;
            tangentTargetRow = row;
            tangentTargetLocalX = localX;
            tangentTargetLocalY = localY;
            tangentTargetLocalZ = localZ;
        }
        return true;
    };
    const glm::vec3 tangentOrigin(0.0f, 0.0f, tinyRadius);
    const glm::vec3 tangentDirection(1.0f, 0.0f, 0.0f);
    const glm::vec3 tangentTarget = tangentOrigin + tangentDirection * (tinyRadius + 8.0f);
    bool tangentFixtureReady = ensureTangentChunkAndVoxel(tangentOrigin, false) &&
        ensureTangentChunkAndVoxel(tangentTarget, true);
    if (tangentFixtureReady) {
        // Provide generated Air for every logical cell so this specifically exercises
        // traversal across the face seam rather than reporting an unrelated missing chunk.
        for (int faceIndex = 0; faceIndex < 6 && tangentFixtureReady; ++faceIndex) {
            for (int row = 0; row < tinyResolution && tangentFixtureReady; ++row) {
                for (int col = 0; col < tinyResolution; ++col) {
                    const fw::PlanetChunkCoord coord{tangentPlanetId,
                        static_cast<fw::CubeFace>(faceIndex), col, row, 0};
                    int storageX = 0, storageZ = 0;
                    if (!fw::planet_grid_compat::TryPlanetChunkCoordToLegacyNet(
                            coord, tinyResolution, storageX, storageZ)) {
                        tangentFixtureReady = false;
                        break;
                    }
                    const auto key = std::make_pair(storageX, storageZ);
                    entt::entity entity = entt::null;
                    const auto found = tangentEntities.find(key);
                    if (found != tangentEntities.end()) {
                        entity = found->second;
                    } else {
                        entity = tangentWorld.CreateChunkEntity(
                            "P02D_Tangent_Air", {0.0f, 0.0f, 0.0f}, storageX, storageZ);
                        if (entity != entt::null && tangentRegistry.valid(entity)) {
                            tangentRegistry.get<fw::VoxelChunkComponent>(entity).planetCoord = coord;
                            tangentEntities.emplace(key, entity);
                        }
                    }
                    if (entity == entt::null || !tangentRegistry.valid(entity) ||
                        tangentRegistry.get<fw::VoxelChunkComponent>(entity).planetCoord != coord) {
                        tangentFixtureReady = false;
                        break;
                    }
                    tangentRegistry.get<fw::VoxelChunkComponent>(entity).isGenerated = true;
                }
            }
        }
        if (tangentFixtureReady) {
            int storageX = 0, storageZ = 0;
            const fw::PlanetChunkCoord targetCoord{
                tangentPlanetId, tangentTargetFace, tangentTargetCol, tangentTargetRow, 0};
            tangentFixtureReady = fw::planet_grid_compat::TryPlanetChunkCoordToLegacyNet(
                targetCoord, tinyResolution, storageX, storageZ);
            const auto targetFound = tangentEntities.find({storageX, storageZ});
            if (tangentFixtureReady && targetFound != tangentEntities.end()) {
                tangentRegistry.get<fw::VoxelChunkComponent>(targetFound->second)
                    .blocks[tangentTargetLocalX][tangentTargetLocalY][tangentTargetLocalZ] = 3;
            } else {
                tangentFixtureReady = false;
            }
        }
    }
    if (tangentFixtureReady) {
        fw::RaycastQuery tangentQuery{};
        tangentQuery.mode = fw::RaycastMode::Voxel;
        tangentQuery.maxDistance = 60.0f;
        tangentQuery.ray = {tangentOrigin, tangentDirection};
        const auto tangentHit = fw::RaycastSystem::Cast(&tangentContext, tangentQuery);
        const float radialBoundary = tinyRadius +
            (static_cast<float>(tangentTargetVoxel.y) -
             static_cast<float>(fw::PlanetRadialMapping::ReferenceVoxelOffset) - 0.5f) *
                fw::ChunkDimensions::VoxelWorldSize;
        glm::dvec3 tangentFaceNormal(fw::CubeSphereMapping::FaceUVToCubeDir(
            tangentTargetFace, {0.5f, 0.5f}));
        const glm::dvec3 tangentChartU = glm::dvec3(
            fw::CubeSphereMapping::FaceUVToCubeDir(tangentTargetFace, {0.75f, 0.5f}) -
            fw::CubeSphereMapping::FaceUVToCubeDir(tangentTargetFace, {0.5f, 0.5f})) * 2.0;
        const double targetChartX = 2.0 *
            (static_cast<double>(tangentTargetCol * CHUNK_SIZE + tangentTargetLocalX) /
             (static_cast<double>(tinyResolution) * CHUNK_SIZE)) - 1.0;
        const glm::dvec3 tangentVoxelPlane = tangentChartU - targetChartX * tangentFaceNormal;
        const double tangentPlaneDenominator = glm::dot(glm::dvec3(tangentDirection), tangentVoxelPlane);
        const float angularBoundary = static_cast<float>(
            -glm::dot(glm::dvec3(tangentOrigin), tangentVoxelPlane) / tangentPlaneDenominator);
        const float radialEntry = std::sqrt(radialBoundary * radialBoundary - tinyRadius * tinyRadius);
        const float expectedTangentHit = std::max(radialEntry, angularBoundary);
        stats.tangentialDistance = tangentHit.distance;
        tangentQuery.maxDistance = expectedTangentHit - 0.02f;
        const auto beforeTangentBoundary = fw::RaycastSystem::Cast(&tangentContext, tangentQuery);
        tangentQuery.maxDistance = expectedTangentHit + 0.02f;
        const auto afterTangentBoundary = fw::RaycastSystem::Cast(&tangentContext, tangentQuery);
        const glm::vec3 expectedTangentNormal = angularBoundary >= radialEntry
            ? -glm::normalize(glm::vec3(tangentVoxelPlane))
            : -glm::normalize(tangentHit.worldPosition);
        stats.tangentialCrossingPass = tangentHit.hit &&
            tangentHit.status == fw::RaycastResultStatus::Hit &&
            tangentHit.targetEntity != entt::null && tangentHit.voxelPosition == tangentTargetVoxel &&
            tangentTargetFace != fw::CubeFace::PositiveZ && expectedTangentHit > tinyRadius &&
            std::abs(tangentHit.distance - expectedTangentHit) < 2.0e-3f &&
            !beforeTangentBoundary.hit && afterTangentBoundary.hit &&
            glm::dot(tangentHit.faceNormal, expectedTangentNormal) > 0.999f;
        if (!stats.tangentialCrossingPass) {
            std::cerr << "[P02D][DIAG] tangent hit=" << tangentHit.hit
                      << " status=" << static_cast<int>(tangentHit.status)
                      << " targetEntity=" << static_cast<uint32_t>(tangentHit.targetEntity)
                      << " expectedVoxel=" << tangentTargetVoxel.x << ',' << tangentTargetVoxel.y << ',' << tangentTargetVoxel.z
                      << " actualVoxel=" << tangentHit.voxelPosition.x << ',' << tangentHit.voxelPosition.y << ',' << tangentHit.voxelPosition.z
                      << " hitDistance=" << tangentHit.distance << " expectedDistance=" << expectedTangentHit
                      << " beforeHit=" << beforeTangentBoundary.hit << " beforeStatus=" << static_cast<int>(beforeTangentBoundary.status)
                      << " afterHit=" << afterTangentBoundary.hit << " afterStatus=" << static_cast<int>(afterTangentBoundary.status)
                      << " afterDistance=" << afterTangentBoundary.distance
                      << " postSampleStatus=" << static_cast<int>(tangentWorld.QueryBlockCartesian(
                            tangentOrigin + tangentDirection * (expectedTangentHit + 0.01f)).status)
                      << " normalDot=" << glm::dot(tangentHit.faceNormal, expectedTangentNormal) << '\n';
        }
    }

    // This medium planet has a positive inner OOB radius; all preceding ray samples stay in one loaded chunk.
    SharedContext oobContext{};
    fw::GameWorld oobWorld;
    oobWorld.Initialize(&oobContext);
    oobContext.forgeWorld = &oobWorld;
    auto& oobRegistry = oobWorld.GetRegistry();
    auto& oobGeometry = oobRegistry.get<fw::PlanetGeometryComponent>(oobWorld.GetPlanetEntity());
    oobGeometry.isLogicalSphere = true;
    oobGeometry.planetSize = fw::PlanetSize::Medium;
    const float mediumRadius = fw::PlanetMath::GetPlanetRadius(fw::PlanetSize::Medium);
    const int mediumResolution = static_cast<int>(fw::PlanetMath::GetFaceResolution(fw::PlanetSize::Medium));
    const float innerBoundary = mediumRadius -
        (static_cast<float>(fw::PlanetRadialMapping::ReferenceVoxelOffset) + 0.5f);
    const glm::vec3 oobOrigin(0.0f, 0.0f, innerBoundary + 10.0f);
    fw::CubeFace oobFace{};
    glm::vec2 oobUv(0.0f);
    fw::CubeSphereMapping::DirectionToFaceUV(glm::normalize(oobOrigin), oobFace, oobUv);
    int oobCol = 0, oobRow = 0;
    fw::CubeSphereMapping::UVToGrid(oobUv, mediumResolution, oobCol, oobRow);
    const fw::PlanetChunkCoord oobCoord{{0x50403246u}, oobFace, oobCol, oobRow, 0};
    int oobStorageX = 0, oobStorageZ = 0;
    const bool oobAddressOk = fw::planet_grid_compat::TryPlanetChunkCoordToLegacyNet(
        oobCoord, mediumResolution, oobStorageX, oobStorageZ);
    if (oobAddressOk) {
        const auto oobEntity = oobWorld.CreateChunkEntity(
            "P02D_OutOfBounds", {0.0f, 0.0f, 0.0f}, oobStorageX, oobStorageZ);
        if (oobEntity != entt::null && oobRegistry.valid(oobEntity)) {
            auto& chunk = oobRegistry.get<fw::VoxelChunkComponent>(oobEntity);
            chunk.planetCoord = oobCoord;
            chunk.isGenerated = true;
            fw::RaycastQuery oobQuery{};
            oobQuery.mode = fw::RaycastMode::Voxel;
            oobQuery.maxDistance = 20.0f;
            oobQuery.ray = {oobOrigin, {0.0f, 0.0f, -1.0f}};
            const auto oobCast = fw::RaycastSystem::Cast(&oobContext, oobQuery);
            const auto directOob = oobWorld.QueryBlockCartesian(
                {0.0f, 0.0f, innerBoundary - 10.0f});
            stats.outOfBoundsClassified = !oobCast.hit &&
                oobCast.status == fw::RaycastResultStatus::OutOfBounds &&
                directOob.status == fw::WorldVoxelQueryStatus::OutOfBounds;
        }
    }

    const size_t expectedProbes = sizes.size() * 6 * charts.size();
    return stats.probes == expectedProbes && stats.hits == expectedProbes &&
        stats.correctFaces == expectedProbes && stats.correctChunks == expectedProbes &&
        stats.correctFirstCells == expectedProbes && stats.correctDistances == expectedProbes &&
        stats.correctNormals == expectedProbes && stats.pendingClassified && stats.missingClassified &&
        stats.tangentialCrossingPass && stats.outOfBoundsClassified;
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path fixturePath = "../fixtures/TinySphere/world_map.json";
    const bool p03aCharacterizationOnly = argc == 2 && std::string(argv[1]) == "--p03a-characterize";
    if (argc == 3 && std::string(argv[1]) == "--fixture") {
        fixturePath = argv[2];
    } else if (argc != 1 && !p03aCharacterizationOnly) {
        std::cerr << "Uso: FairworldPlanetaryVoxelE2ETests [--fixture <path> | --p03a-characterize]\n";
        return 1;
    }

    if (p03aCharacterizationOnly) {
        SharedContext context{};
        fw::GameWorld world;
        if (!InitializeP03aSyntheticPlanet(context, world, true, 0x50334199u)) return 1;
        const bool probeMatrix = RunP03CollisionProbeMatrix(world);
        const bool overlap = RunP03DeepOverlapDiagnostic(world);
        const bool contracts = TestP03aCollisionContracts();
        const bool p03d4Contacts = TestP03d4GroundedContactScenarios();
        const bool p03d5RadialContacts = TestP03d5RadialUpperOnlyContactSearch();
        std::cout << "[P03A][SUMMARY] probeMatrix=" << probeMatrix << " overlap=" << overlap
                  << " contracts=" << contracts << " p03d4Contacts=" << p03d4Contacts
                  << " p03d5RadialContacts=" << p03d5RadialContacts << '\n';
        return probeMatrix && overlap && contracts && p03d4Contacts && p03d5RadialContacts ? 0 : 1;
    }

    const bool p03d1FwbParityPass = fw::TerrainValidation::RunP03D1FwbSemanticParityTest();
    if (!p03d1FwbParityPass) return 1;

    std::error_code pathError;
    const auto absoluteFixture = std::filesystem::absolute(fixturePath, pathError);
    if (pathError || !std::filesystem::is_regular_file(absoluteFixture)) {
        std::cerr << "[FAIL] Fixture non accessibile: " << fixturePath.string() << '\n';
        return 1;
    }

    fw::MapDocument document;
    if (!document.LoadJSON(absoluteFixture.string())) {
        std::cerr << "[FAIL] MapDocument::LoadJSON non ha caricato " << absoluteFixture.string() << '\n';
        return 1;
    }

    std::string fixtureError;
    if (!ValidateFixture(document, fixtureError)) {
        std::cerr << "[FAIL] Fixture: " << fixtureError << '\n';
        return 1;
    }
    const auto& planet = document.planets.front();
    std::cout << "[PASS] Fixture: " << absoluteFixture.string() << '\n'
              << "[PASS] PlanetSize=Tiny resolution="
              << fw::PlanetMath::GetFaceResolution(fw::PlanetSize::Tiny)
              << " isFlat=false template=default_terrain activeInstances=1"
              << " face=0 gridX=1 gridY=1 spawn=" << planet.spawnPoints.front().name << '\n';

    size_t cameraOrientationSteps = 0;
    float cameraMaxStepAngle = 0.0f;
    float cameraClosedLoopError = 0.0f;
    float cameraCubeEdgeMaxJump = 0.0f;
    const bool cameraFramePass = TestPlanetaryCameraFrame(
        cameraOrientationSteps, cameraMaxStepAngle, cameraClosedLoopError, cameraCubeEdgeMaxJump);
    std::cout << "[OBSERVED] planetary camera orientation steps=" << cameraOrientationSteps
              << " max step angle=" << cameraMaxStepAngle
              << " closed-loop forward error=" << cameraClosedLoopError
              << " Cube-Sphere edge max jump=" << cameraCubeEdgeMaxJump << '\n';
    std::cout << (cameraFramePass ? "[PASS]" : "[FAIL]")
              << " planetary camera yaw/pitch, pole transport, Cube-Sphere edges and closed-loop stability\n";
    if (!cameraFramePass) return 1;

    const bool movementMathPass = TestPlanetaryMovementMath();
    std::cout << (movementMathPass ? "[PASS]" : "[FAIL]")
              << " planar WASD normalization, radial/tangent split and radial terminal clamp\n";
    if (!movementMathPass) return 1;

    size_t radialCellsChecked = 0;
    float radialRoundTripError = 0.0f;
    const bool radialMappingPass = TestUnifiedPlanetRadialMapping(radialCellsChecked, radialRoundTripError);
    std::cout << "[OBSERVED] unified radial mapping cells=" << radialCellsChecked
              << " max round-trip error=" << radialRoundTripError << '\n';
    std::cout << (radialMappingPass ? "[PASS]" : "[FAIL]")
              << " radial voxel centers, faces, cell bounds, nextafter tie-break and inverse mapping\n";
    if (!radialMappingPass) return 1;

    fw::BlockRegistry blockRegistry;
    blockRegistry.Initialize();
    fw::MaterialRegistry materialRegistry;
    materialRegistry.Initialize();

    PlanetaryNeighborhoodStats neighborhoodStats{};
    const bool neighborhoodPass = TestPlanetaryNeighborhoodContracts(
        materialRegistry, neighborhoodStats);
    std::cout << "[P02C][OBSERVED] resolved coordinates=" << neighborhoodStats.resolvedCoordinates
              << " cross-face relations=" << neighborhoodStats.crossFaceRelations
              << " reciprocal=" << neighborhoodStats.reciprocalRelations
              << " seam voxel samples=" << neighborhoodStats.seamVoxelSamples
              << " culled seam faces=" << neighborhoodStats.seamFacesCulled
              << " slot culls=";
    for (size_t slot = 0; slot < neighborhoodStats.seamCullBySlot.size(); ++slot)
        std::cout << (slot == 0 ? "" : ",") << neighborhoodStats.seamCullBySlot[slot]
                  << '/' << neighborhoodStats.seamSamplesBySlot[slot];
    std::cout << " lookup ns=" << neighborhoodStats.lookupNanoseconds
              << " snapshot orientation ns=" << neighborhoodStats.orientationNanoseconds
              << " seam mesher ns=" << neighborhoodStats.buildNanoseconds
              << " failures=" << neighborhoodStats.failedResolutions << '\n';
    std::cout << (neighborhoodPass ? "[P02C][PASS]" : "[P02C][FAIL]")
              << " topological storage mapping, oriented cross-face samples and seam culling across all PlanetSize values\n";
    if (!neighborhoodPass) return 1;

    SharedContext context{};
    context.blockRegistry = &blockRegistry;
    context.materialRegistry = &materialRegistry;
    context.isMapBuilderMode = true;
    context.isForgeMode = false;

    const bool legacyApiPass = ValidateLegacyChunkCreation(context);
    std::cout << (legacyApiPass ? "[PASS]" : "[FAIL]")
              << " legacy Cartesian CreateChunkEntity coordinates and manager registration\n";
    if (!legacyApiPass) return 1;

    size_t legacyOpposedFaces = 0;
    size_t correctedCubeTriangles = 0;
    const bool sixFaceWindingPass = TestLegacySixFaceWinding(
        materialRegistry, legacyOpposedFaces, correctedCubeTriangles);
    std::cout << "[OBSERVED] isolated cube: legacy opposite-normal faces=" << legacyOpposedFaces
              << "/6; corrected triangles=" << correctedCubeTriangles << "/12\n";
    std::cout << (sixFaceWindingPass ? "[PASS]" : "[FAIL]")
              << " six voxel face orientations, UV/material preservation and corrected winding\n";

    size_t warpedWindingMismatches = 0;
    size_t warpedCorrectionSwaps = 0;
    size_t warpedDegenerateTriangles = 0;
    const bool warpedWindingPass = TestWindingAcrossPlanetaryDeformation(
        warpedWindingMismatches, warpedCorrectionSwaps, warpedDegenerateTriangles);
    std::cout << "[OBSERVED] six Cube-Sphere face deformations: pre-correction winding mismatches="
              << warpedWindingMismatches << "/72; swaps=" << warpedCorrectionSwaps
              << "/72; degenerates=" << warpedDegenerateTriangles << '\n';
    std::cout << (warpedWindingPass ? "[PASS]" : "[FAIL]")
              << " winding classification preserved across Cube-Sphere deformation and corrected stream\n";
    if (!warpedWindingPass) return 1;

    size_t cubeSphereEdgeSamples = 0;
    size_t cubeSphereCornerSamples = 0;
    float cubeSphereMaxDirectionError = 0.0f;
    const bool cubeSphereBoundaryPass = TestCubeSphereEdgesAndCorners(
        cubeSphereEdgeSamples, cubeSphereCornerSamples, cubeSphereMaxDirectionError);
    std::cout << "[OBSERVED] Cube-Sphere edge samples=" << cubeSphereEdgeSamples
              << " corner samples=" << cubeSphereCornerSamples
              << " max direction round-trip error=" << cubeSphereMaxDirectionError << '\n';
    std::cout << (cubeSphereBoundaryPass ? "[PASS]" : "[FAIL]")
              << " six-face Cube-Sphere boundary/corner direction continuity and tie-break determinism\n";
    if (!sixFaceWindingPass || !cubeSphereBoundaryPass) return 1;

    uint64_t p03d3InputDigest = 0;
    uint64_t p03d3DirectionDigest = 0;
    size_t p03d3MappingSamples = 0;
    const bool p03d3MappingPass = TestP03D3DeterministicFaceMapping(
        p03d3InputDigest, p03d3DirectionDigest, p03d3MappingSamples);
    std::cout << "[P03D3][MAPPING] sixSizes=6 faces=6 samples=" << p03d3MappingSamples
              << " uvDigest=0x" << std::hex << p03d3InputDigest
              << " directionDigest=0x" << p03d3DirectionDigest << std::dec << '\n';
    std::cout << (p03d3MappingPass ? "[PASS]" : "[FAIL]")
              << " deterministic FaceUVToDirection centers, endpoints and adjacent boundaries\n";
    if (!p03d3MappingPass) return 1;

    P02aCoordinateStats p02aCoordinateStats{};
    const bool p02aCoordinatePass = TestP02aCoordinateAndTopology(p02aCoordinateStats);
    std::cout << "[P02A][OBSERVED] face/chart round trips=" << p02aCoordinateStats.faceRoundTrips
              << " max direction error=" << p02aCoordinateStats.maxDirectionError
              << " PlanetSize extent invariants=" << p02aCoordinateStats.sizeInvariants << "/6"
              << " legacy net round trips=" << p02aCoordinateStats.legacyNetRoundTrips
              << " generator canonical samples=" << p02aCoordinateStats.generatorCanonicalSamples
              << " max generator position error=" << p02aCoordinateStats.maxGeneratorPositionError
              << " unique logical chunk keys=" << p02aCoordinateStats.uniqueLogicalChunkKeys
              << " deterministic grid boundary samples=" << p02aCoordinateStats.gridBoundaryCases
              << " exact represented internal boundaries assigned previous cell="
              << p02aCoordinateStats.gridBoundaryPreviousCell
              << " oriented edge positions=" << p02aCoordinateStats.edgePositions
              << " reciprocal=" << p02aCoordinateStats.reciprocalEdges << '\n'
              << (p02aCoordinatePass ? "[P02A][PASS]" : "[P02A][FAIL]")
              << " six PlanetSize coordinate round trips and cross-face reciprocal topology\n";
    if (!p02aCoordinatePass) return 1;

    P02aRayStats p02aRayStats{};
    const bool p02aRayTestConstructed = TestP02aRaycastCharacterization(p02aRayStats);
    std::cout << "[P02A][OBSERVED] axial hit=" << p02aRayStats.axialHit
              << " distance=" << p02aRayStats.axialDistance
              << " outside-origin hit=" << p02aRayStats.outsideOriginHit
              << " distance=" << p02aRayStats.outsideOriginDistance
              << " parallel-boundary hit=" << p02aRayStats.parallelBoundaryHit
              << " voxel-boundary hit=" << p02aRayStats.voxelBoundaryHit
              << " inside-origin hit=" << p02aRayStats.insideOriginHit
              << " NaN-direction result-hit=" << p02aRayStats.nanDirectionHit
              << " infinite-direction result-hit=" << p02aRayStats.infiniteDirectionHit
              << " nonfinite-origin rejected=" << p02aRayStats.nonFiniteOriginRejected
              << " invalid-distance rejected=" << p02aRayStats.invalidDistanceRejected
              << " invalid-input rejected=" << p02aRayStats.invalidInputRejected
              << " translated-parent hit=" << p02aRayStats.translatedParentHit
              << " scaled hit=" << p02aRayStats.scaledHit
              << " distance=" << p02aRayStats.scaledDistance
              << " uniform-scale distance=" << p02aRayStats.uniformlyScaledDistance
              << " distance contract=" << p02aRayStats.distanceContractPass
              << " maxDistance clipping=" << p02aRayStats.maxDistanceClipped
              << " 4000 voxel casts ns=" << p02aRayStats.ddaNanoseconds << '\n'
              << (p02aRayTestConstructed ? "[P02D][PASS]" : "[P02D][FAIL]")
              << " Cartesian slab/DDA contract: parallel axes, entry distance, transforms, limits and invalid rays\n";
    if (!p02aRayTestConstructed) return 1;

    PlanetaryRayProbeStats planetaryRayStats{};
    const bool planetaryRayPass = TestPlanetaryRayTraversal(planetaryRayStats);
    std::cout << "[P02D][OBSERVED] planetary probes=" << planetaryRayStats.probes
              << " hits=" << planetaryRayStats.hits
              << " face/chunk/first-cell/distance/normal=" << planetaryRayStats.correctFaces << '/'
              << planetaryRayStats.correctChunks << '/' << planetaryRayStats.correctFirstCells << '/'
              << planetaryRayStats.correctDistances << '/' << planetaryRayStats.correctNormals
              << " pending=" << planetaryRayStats.pendingClassified
              << " missing=" << planetaryRayStats.missingClassified
              << " tangential=" << planetaryRayStats.tangentialCrossingPass
              << " tangentialDistance=" << planetaryRayStats.tangentialDistance
              << " OOB=" << planetaryRayStats.outOfBoundsClassified
              << " castNs=" << planetaryRayStats.castNanoseconds
              << " castNs/ray=" << (planetaryRayStats.probes ?
                    planetaryRayStats.castNanoseconds / planetaryRayStats.probes : 0) << '\n'
              << (planetaryRayPass ? "[P02D][PASS]" : "[P02D][FAIL]")
              << " exact planetary boundary traversal over six sizes, faces, edges and corners\n";
    if (!planetaryRayPass) return 1;

    fw::GameWorld world;
    world.Initialize(&context);
    auto& registry = world.GetRegistry();
    const entt::entity planetEntity = world.GetPlanetEntity();
    if (!registry.valid(planetEntity) || !registry.all_of<fw::PlanetGeometryComponent>(planetEntity)) {
        std::cerr << "[FAIL] GameWorld planet singleton non inizializzato\n";
        return 1;
    }
    auto& geometry = registry.get<fw::PlanetGeometryComponent>(planetEntity);
    geometry.planetSize = fw::PlanetSize::Tiny;
    geometry.isLogicalSphere = true;
    const int resolution = static_cast<int>(fw::PlanetMath::GetFaceResolution(fw::PlanetSize::Tiny));

    std::cout << "[RUN] MapWorldGenerator::Generate (world sphere, all 6 faces)\n";
    size_t resolvedActiveTemplates = 0;
    for (const auto& instance : planet.chunkInstances) {
        if (!instance.isActive) continue;
        for (const auto& terrain : document.terrainLibrary) {
            if (terrain.id == instance.templateId) {
                ++resolvedActiveTemplates;
                break;
            }
        }
    }
    size_t validTransforms = 0;
    std::set<std::pair<int, int>> uniqueGlobalCandidates;
    std::set<std::string> uniqueChunkIds;
    for (int face = 0; face < 6; ++face) {
        for (int row = 0; row < resolution; ++row) {
            for (int col = 0; col < resolution; ++col) {
                const int globalX = (face % 3) * resolution + col;
                const int globalZ = (face / 3) * resolution + row;
                uniqueGlobalCandidates.emplace(globalX, globalZ);
                uniqueChunkIds.emplace("WorldChunk_" + std::to_string(globalX) + "_" + std::to_string(globalZ));
                glm::vec3 position;
                glm::quat rotation;
                if (fw::MapWorldGenerator::GetSphericalChunkTransform(
                        fw::PlanetSize::Tiny, globalX, globalZ, position, rotation)) {
                    ++validTransforms;
                }
            }
        }
    }
    std::cout << "[OBSERVED] valid spherical transforms=" << validTransforms
              << " expected=" << (6 * resolution * resolution)
              << " uniqueGlobalCandidates=" << uniqueGlobalCandidates.size()
              << " uniqueChunkIds=" << uniqueChunkIds.size() << '\n';
    const size_t expectedChunks = static_cast<size_t>(6 * resolution * resolution);
    const bool candidateSetPass = validTransforms == expectedChunks &&
        uniqueGlobalCandidates.size() == expectedChunks && uniqueChunkIds.size() == expectedChunks;
    std::cout << (candidateSetPass ? "[PASS]" : "[FAIL]") << " 54 unique spherical candidate keys and IDs\n";
    fw::MapWorldGenerator::Generate(document, 0, world, nullptr, 0.0f, glm::vec3(0.0f));

    const bool pendingCollisionPass = TestPlanetaryCollisionQueryStates(world);
    if (!pendingCollisionPass) return 1;

    std::vector<entt::entity> chunkEntities;
    std::set<std::tuple<int, int, int>> uniquePlanetCoordinates;
    size_t validPlanetCoords = 0;
    size_t taggedBefore = 0;
    bool coordinatesValid = true;
    auto generatedView = registry.view<fw::VoxelChunkComponent, fw::BiomeDataComponent, fw::TerrainGenTag>();
    for (entt::entity entity : generatedView) {
        chunkEntities.push_back(entity);
        ++taggedBefore;
        const auto& chunk = generatedView.get<fw::VoxelChunkComponent>(entity);
        if (!chunk.planetCoord.planet.IsValid()) {
            coordinatesValid = false;
            continue;
        }
        const int face = static_cast<int>(chunk.planetCoord.face);
        const int col = chunk.planetCoord.col;
        const int row = chunk.planetCoord.row;
        if (face < 0 || face >= 6 || col < 0 || col >= resolution || row < 0 || row >= resolution ||
            !uniquePlanetCoordinates.emplace(face, col, row).second) {
            coordinatesValid = false;
        } else {
            ++validPlanetCoords;
            const int legacyRow = resolution - 1 - row;
            const int expectedGlobalX = (face % 3) * resolution + col;
            const int expectedGlobalZ = (face / 3) * resolution + legacyRow;
            if (chunk.cx != expectedGlobalX || chunk.cz != expectedGlobalZ ||
                world.GetChunkManager().GetChunkEntity(expectedGlobalX, expectedGlobalZ) != entity) {
                coordinatesValid = false;
            }
        }
    }

    std::cout << "[OBSERVED] active instances=" << planet.chunkInstances.size()
              << " resolved active templates=" << resolvedActiveTemplates
              << " ECS chunks=" << chunkEntities.size()
              << " valid PlanetChunkCoord=" << validPlanetCoords
              << " TerrainGenTag before solver=" << taggedBefore << '\n';
    for (int face = 0; face < 6; ++face) {
        for (int col = 0; col < resolution; ++col) {
            for (int row = 0; row < resolution; ++row) {
                if (uniquePlanetCoordinates.find({face, col, row}) == uniquePlanetCoordinates.end()) {
                    const int globalX = (face % 3) * resolution + col;
                    const int legacyRow = resolution - 1 - row;
                    const int globalZ = (face / 3) * resolution + legacyRow;
                    const entt::entity indexed = world.GetChunkManager().GetChunkEntity(globalX, globalZ);
                    std::cout << "[MISSING] PlanetChunkCoord face=" << face << " col=" << col << " row=" << row
                              << " global=" << globalX << ',' << globalZ;
                    if (indexed != entt::null && registry.valid(indexed) && registry.all_of<fw::VoxelChunkComponent>(indexed)) {
                        const auto& alias = registry.get<fw::VoxelChunkComponent>(indexed);
                        std::cout << " managerLookup=entity " << static_cast<uint32_t>(indexed)
                                  << " contains face=" << static_cast<int>(alias.planetCoord.face)
                                  << " col=" << alias.planetCoord.col << " row=" << alias.planetCoord.row;
                    } else {
                        std::cout << " managerLookup=missing";
                    }
                    std::cout << '\n';
                }
            }
        }
    }
    std::map<std::pair<int, int>, entt::entity> firstGenerationLookup;
    std::set<uint32_t> firstGenerationEntities;
    bool managerRegistrationPass = true;
    size_t managerLookups = 0;
    for (int face = 0; face < 6; ++face) {
        for (int col = 0; col < resolution; ++col) {
            for (int row = 0; row < resolution; ++row) {
                const int globalX = (face % 3) * resolution + col;
                const int globalZ = (face / 3) * resolution + (resolution - 1 - row);
                const entt::entity entity = world.GetChunkManager().GetChunkEntity(globalX, globalZ);
                if (entity == entt::null || !registry.valid(entity) ||
                    !registry.all_of<fw::VoxelChunkComponent>(entity)) {
                    managerRegistrationPass = false;
                    continue;
                }
                const auto& chunk = registry.get<fw::VoxelChunkComponent>(entity);
                if (chunk.cx != globalX || chunk.cz != globalZ ||
                    static_cast<int>(chunk.planetCoord.face) != face ||
                    chunk.planetCoord.col != col || chunk.planetCoord.row != row ||
                    !firstGenerationLookup.emplace(std::make_pair(globalX, globalZ), entity).second ||
                    !firstGenerationEntities.insert(static_cast<uint32_t>(entity)).second) {
                    managerRegistrationPass = false;
                } else {
                    ++managerLookups;
                }
            }
        }
    }
    const size_t managerCountAfterFirst = world.GetChunkManager().GetLoadedChunkCount();
    std::cout << "[OBSERVED] logical manager lookups=" << managerLookups
              << " unique entities=" << firstGenerationEntities.size()
              << " registered keys=" << managerCountAfterFirst << '\n';
    std::cout << (managerRegistrationPass && managerLookups == expectedChunks &&
                  firstGenerationEntities.size() == expectedChunks && managerCountAfterFirst == expectedChunks
                      ? "[PASS]" : "[FAIL]")
              << " ECS/ChunkManager logical identity invariant\n";

    std::cout << "[RUN] MapWorldGenerator::Generate repeat for idempotency\n";
    fw::MapWorldGenerator::Generate(document, 0, world, nullptr, 0.0f, glm::vec3(0.0f));
    bool repeatStable = world.GetChunkManager().GetLoadedChunkCount() == managerCountAfterFirst;
    size_t repeatLookups = 0;
    for (const auto& [key, firstEntity] : firstGenerationLookup) {
        const entt::entity secondEntity = world.GetChunkManager().GetChunkEntity(key.first, key.second);
        if (secondEntity != firstEntity || !registry.valid(secondEntity) ||
            !registry.all_of<fw::VoxelChunkComponent>(secondEntity)) {
            repeatStable = false;
            continue;
        }
        const auto& chunk = registry.get<fw::VoxelChunkComponent>(secondEntity);
        if (chunk.cx != key.first || chunk.cz != key.second) repeatStable = false;
        ++repeatLookups;
    }
    const size_t chunksAfterRepeat = registry.view<fw::VoxelChunkComponent, fw::BiomeDataComponent,
                                                   fw::TerrainGenTag>().size_hint();
    std::cout << "[OBSERVED] repeat logical lookups=" << repeatLookups
              << " registered keys=" << world.GetChunkManager().GetLoadedChunkCount()
              << " ECS chunks=" << chunksAfterRepeat << '\n';
    repeatStable = repeatStable && repeatLookups == expectedChunks && chunksAfterRepeat == expectedChunks;
    std::cout << (repeatStable ? "[PASS]" : "[FAIL]") << " MapWorldGenerator idempotent entity reuse\n";

    const bool generationPass = candidateSetPass && chunkEntities.size() == expectedChunks &&
        validPlanetCoords == expectedChunks && coordinatesValid && taggedBefore == expectedChunks &&
        managerRegistrationPass && repeatStable;
    std::cout << (generationPass ? "[PASS]" : "[FAIL]")
              << " MapWorldGenerator expectedChunks=" << expectedChunks << '\n';

    glm::vec3 pendingSpawnPosition(0.0f);
    const auto pendingSpawnStatus = world.TryResolvePlanetarySpawn(
        fw::CubeSphereMapping::FaceUVToDirection(fw::CubeFace::PositiveZ, {0.5f, 0.5f}),
        10.0f, 0.3f, pendingSpawnPosition);
    const bool pendingSpawnPass = pendingSpawnStatus == fw::PlanetarySpawnResolveStatus::TerrainPending;
    std::cout << (pendingSpawnPass ? "[PASS]" : "[FAIL]")
              << " spawn remains pending until TerrainSolver marks the target chunk generated\n";
    if (!pendingSpawnPass) return 1;

    std::vector<entt::entity> solverQueue = chunkEntities;
    size_t meshRequests = 0;
    const int solved = fw::TerrainSolverSystem::Update(
        registry, solverQueue, static_cast<int>(chunkEntities.size()),
        [&world, &meshRequests](entt::entity entity) {
            ++meshRequests;
            world.MarkMeshDirty(entity);
        }, &blockRegistry);

    uint64_t nonAirVoxels = 0;
    std::map<uint8_t, uint64_t> blockDistribution;
    std::unordered_set<uint8_t> sourceBlockIds;
    size_t solvedChunks = 0;
    size_t taggedAfter = 0;
    bool chunkGeneratedFlags = true;
    for (entt::entity entity : chunkEntities) {
        if (!registry.valid(entity) || !registry.all_of<fw::VoxelChunkComponent>(entity)) continue;
        const auto& chunk = registry.get<fw::VoxelChunkComponent>(entity);
        if (chunk.isGenerated) ++solvedChunks;
        else chunkGeneratedFlags = false;
        if (registry.all_of<fw::TerrainGenTag>(entity)) ++taggedAfter;
        for (int x = 0; x < CHUNK_SIZE; ++x) {
            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                for (int z = 0; z < CHUNK_SIZE; ++z) {
                    const uint8_t block = chunk.blocks[x][y][z];
                    if (block != 0) {
                        ++nonAirVoxels;
                        ++blockDistribution[block];
                        sourceBlockIds.insert(block);
                    }
                }
            }
        }
    }

    std::cout << "[OBSERVED] TerrainSolver processed=" << solved
              << " queueRemaining=" << solverQueue.size()
              << " generatedChunks=" << solvedChunks
              << " TerrainGenTag after solver=" << taggedAfter
              << " meshRequests=" << meshRequests
              << " nonAirVoxels=" << nonAirVoxels << '\n';
    std::cout << "[OBSERVED] block ID distribution:";
    for (const auto& [blockId, count] : blockDistribution) {
        std::cout << ' ' << static_cast<unsigned int>(blockId) << '=' << count;
    }
    std::cout << '\n';

    if (const char* dumpPath = std::getenv("FAIRWORLD_P03D2_VOXEL_DUMP")) {
        struct ChunkSnapshot {
            fw::PlanetChunkCoord coord{};
            const fw::VoxelChunkComponent* chunk = nullptr;
            uint64_t hash = 1469598103934665603ull;
            uint64_t nonAir = 0;
        };
        std::vector<ChunkSnapshot> snapshots;
        snapshots.reserve(chunkEntities.size());
        for (const entt::entity entity : chunkEntities) {
            if (!registry.valid(entity) || !registry.all_of<fw::VoxelChunkComponent>(entity)) continue;
            const auto& chunk = registry.get<fw::VoxelChunkComponent>(entity);
            ChunkSnapshot snapshot{chunk.planetCoord, &chunk};
            for (int x = 0; x < CHUNK_SIZE; ++x) {
                for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                    for (int z = 0; z < CHUNK_SIZE; ++z) {
                        const uint8_t block = chunk.blocks[x][y][z];
                        snapshot.hash ^= block;
                        snapshot.hash *= 1099511628211ull;
                        if (block != 0) ++snapshot.nonAir;
                    }
                }
            }
            snapshots.push_back(snapshot);
        }
        std::sort(snapshots.begin(), snapshots.end(), [](const auto& a, const auto& b) {
            return std::tie(a.coord.planet.value, a.coord.face, a.coord.col, a.coord.row, a.coord.layer) <
                   std::tie(b.coord.planet.value, b.coord.face, b.coord.col, b.coord.row, b.coord.layer);
        });
        if (std::getenv("FAIRWORLD_P03D2_TRACE_VOXEL_CHUNKS")) {
            for (const auto& snapshot : snapshots) {
                std::cout << "[P03D2][VOXEL_CHUNK] planet=" << snapshot.coord.planet.value
                          << " face=" << static_cast<int>(snapshot.coord.face)
                          << " col=" << snapshot.coord.col << " row=" << snapshot.coord.row
                          << " layer=" << snapshot.coord.layer << " hash=0x" << std::hex << snapshot.hash
                          << std::dec << " nonAir=" << snapshot.nonAir << '\n';
            }
        }
        const std::filesystem::path outputPath(dumpPath);
        std::error_code directoryError;
        if (outputPath.has_parent_path()) {
            std::filesystem::create_directories(outputPath.parent_path(), directoryError);
        }
        std::ofstream dump(outputPath, std::ios::binary | std::ios::trunc);
        const uint32_t version = 1;
        const uint32_t count = static_cast<uint32_t>(snapshots.size());
        dump.write(reinterpret_cast<const char*>(&version), sizeof(version));
        dump.write(reinterpret_cast<const char*>(&count), sizeof(count));
        for (const auto& snapshot : snapshots) {
            const int32_t face = static_cast<int32_t>(snapshot.coord.face);
            dump.write(reinterpret_cast<const char*>(&snapshot.coord.planet.value), sizeof(snapshot.coord.planet.value));
            dump.write(reinterpret_cast<const char*>(&face), sizeof(face));
            dump.write(reinterpret_cast<const char*>(&snapshot.coord.col), sizeof(snapshot.coord.col));
            dump.write(reinterpret_cast<const char*>(&snapshot.coord.row), sizeof(snapshot.coord.row));
            dump.write(reinterpret_cast<const char*>(&snapshot.coord.layer), sizeof(snapshot.coord.layer));
            dump.write(reinterpret_cast<const char*>(snapshot.chunk->blocks), sizeof(snapshot.chunk->blocks));
        }
        if (!dump) {
            std::cerr << "[P03D2][FAIL] unable to write voxel snapshot: " << outputPath.string() << '\n';
            return 1;
        }
        std::cout << "[P03D2][VOXEL_DUMP] chunks=" << count << " path=" << outputPath.string() << '\n';
    }

    if (std::getenv("FAIRWORLD_P03D2_TRACE_TERRAIN_DECISIONS")) {
        const std::array<std::tuple<int, int, int, int>, 2> targets{{
            {3, 0, 2, 13 * CHUNK_SIZE + 3},
            {5, 2, 1, 12 * CHUNK_SIZE + 7}
        }};
        for (const auto& [targetFace, targetCol, targetRow, packedXZ] : targets) {
            const int localX = packedXZ / CHUNK_SIZE;
            const int localZ = packedXZ % CHUNK_SIZE;
            for (entt::entity entity : chunkEntities) {
                if (!registry.valid(entity) || !registry.all_of<fw::VoxelChunkComponent, fw::BiomeDataComponent>(entity)) continue;
                const auto& source = registry.get<fw::VoxelChunkComponent>(entity);
                const auto& biome = registry.get<fw::BiomeDataComponent>(entity);
                if (static_cast<int>(source.planetCoord.face) != targetFace || source.planetCoord.col != targetCol ||
                    source.planetCoord.row != targetRow || source.planetCoord.layer != 0) continue;
                fw::TerrainGenerationContext ctx{};
                ctx.planetSeed = biome.baseTerrain.seed;
                ctx.planetaryReliefScale = biome.planetaryReliefScale;
                ctx.chunkCoord = {source.planetCoord.col, source.planetCoord.row};
                ctx.layer = source.planetCoord.layer;
                ctx.chunkCenterSphere = glm::normalize(biome.chunkCenterWorld);
                if (glm::length(biome.chunkCenterWorld) < 0.1f) ctx.chunkCenterSphere = glm::vec3(0.0f, 1.0f, 0.0f);
                ctx.faceIndex = targetFace;
                ctx.voxelResolutionX = CHUNK_SIZE;
                ctx.voxelResolutionY = CHUNK_HEIGHT;
                ctx.voxelResolutionZ = CHUNK_SIZE;
                ctx.isFlat = biome.isFlat;
                ctx.planetRadius = fw::PlanetMath::GetPlanetRadius(biome.planetSize);
                ctx.faceGridResolution = fw::PlanetMath::GetFaceResolution(biome.planetSize) * CHUNK_SIZE;
                for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                    if (fw::PlanetRadialMapping::ClassifyCellRadialDomain(ctx.layer, y, ctx.planetRadius) ==
                        fw::PlanetRadialMapping::RadialDomain::Valid) { ctx.firstValidRadialY = y; break; }
                    if (y == CHUNK_HEIGHT - 1) ctx.firstValidRadialY = CHUNK_HEIGHT;
                }
                const fw::TerrainRuleOverrides emptyOverrides;
                auto baseRules = fw::ResolveTerrainRules(biome.baseTerrain.baseRules, emptyOverrides, 1.0f, &blockRegistry);
                std::vector<std::pair<fw::MapRegion, fw::ResolvedTerrainRules>> regions;
                regions.reserve(biome.overlappingRegions.size());
                for (const auto& region : biome.overlappingRegions) {
                    regions.push_back({region, fw::ResolveTerrainRules(baseRules.rules, region.overrides, 1.0f, &blockRegistry)});
                }
                fw::VoxelChunkComponent replayChunk{};
                fw::TerrainWorkspace workspace;
                fw::TerrainSolver solver;
                solver.GenerateChunk(ctx, baseRules, regions, workspace, replayChunk);
                const float surface = workspace.surfaceHeights[localZ * CHUNK_SIZE + localX];
                const float voxelGlobal = fw::PlanetRadialMapping::GetGlobalRadialOffset(ctx.layer, 71.0f);
                const float depth = surface - voxelGlobal;
                const auto& heightRules = baseRules.rules.height;
                const auto* plainsRules = std::get_if<fw::PlainsRules>(&heightRules.specialized);
                fw::Noise plainsNoise(ctx.planetSeed);
                plainsNoise.SetNoiseType(fw::NoiseType::OpenSimplex2);
                plainsNoise.SetFractalType(fw::FractalType::FBm);
                plainsNoise.SetFractalOctaves(plainsRules ? plainsRules->octaves : 4);
                plainsNoise.SetFrequency(heightRules.common.frequency);
                const glm::vec3 samplePosition = solver.GetVoxelSpherePos(ctx, localX, 0, localZ) * heightRules.common.macroScale;
                const float noiseValue = plainsNoise.GetNoise(samplePosition.x, samplePosition.y, samplePosition.z);
                const float normalizedNoise = glm::clamp((noiseValue + 1.0f) * 0.5f, 0.0f, 1.0f);
                const float plainsResponse = normalizedNoise * normalizedNoise * (3.0f - 2.0f * normalizedNoise);
                const float reconstructedSurface = (heightRules.common.baseHeight +
                    heightRules.common.amplitude * plainsResponse - heightRules.common.baseHeight) * ctx.planetaryReliefScale;
                const int globalX = ctx.chunkCoord.x * ctx.voxelResolutionX + localX;
                const int globalZ = ctx.chunkCoord.z * ctx.voxelResolutionZ + localZ;
                const float mapU = static_cast<float>(globalX) / static_cast<float>(ctx.faceGridResolution);
                const float mapV = static_cast<float>(globalZ) / static_cast<float>(ctx.faceGridResolution);
                const glm::vec3 cubeDirection = fw::CubeSphereMapping::FaceUVToCubeDir(
                    static_cast<fw::CubeFace>(targetFace), glm::vec2(mapU, mapV));
                const glm::vec3 mappedDirection = fw::CubeSphereMapping::FaceUVToDirection(
                    static_cast<fw::CubeFace>(targetFace), glm::vec2(mapU, mapV));
                const glm::vec3 normalizedDirection = glm::normalize(mappedDirection);
                const float squaredLengthFloat = glm::dot(cubeDirection, cubeDirection);
                const float sqrtLengthFloat = std::sqrt(squaredLengthFloat);
                const float inverseSqrtFloat = glm::inversesqrt(squaredLengthFloat);
                const glm::vec3 normalizedViaInverseSqrt = cubeDirection * inverseSqrtFloat;
                const glm::vec3 normalizedViaSqrt = cubeDirection / sqrtLengthFloat;
                const double dx = static_cast<double>(cubeDirection.x);
                const double dy = static_cast<double>(cubeDirection.y);
                const double dz = static_cast<double>(cubeDirection.z);
                const double squaredLengthDouble = dx * dx + dy * dy + dz * dz;
                const double inverseSqrtDouble = 1.0 / std::sqrt(squaredLengthDouble);
                const glm::vec3 normalizedViaDouble(
                    static_cast<float>(dx * inverseSqrtDouble),
                    static_cast<float>(dy * inverseSqrtDouble),
                    static_cast<float>(dz * inverseSqrtDouble));
                std::cout << std::setprecision(10) << std::hexfloat
                          << "[P03D2][TERRAIN_DECISION] face=" << targetFace << " col=" << targetCol
                          << " row=" << targetRow << " local=" << localX << ',' << localZ
                          << " layer=" << ctx.layer << " planetSeed=" << ctx.planetSeed
                          << " planetRadius=" << ctx.planetRadius << " reliefScale=" << ctx.planetaryReliefScale
                          << " surface=" << surface << " voxelGlobalY=" << voxelGlobal
                          << " depth=" << depth << " sample=" << samplePosition.x << ',' << samplePosition.y << ',' << samplePosition.z
                          << " uv=" << mapU << ',' << mapV << " cube=" << cubeDirection.x << ',' << cubeDirection.y
                          << ',' << cubeDirection.z << " mapped=" << mappedDirection.x << ','
                          << mappedDirection.y << ',' << mappedDirection.z << " normalized="
                          << normalizedDirection.x << ',' << normalizedDirection.y << ',' << normalizedDirection.z
                          << " qf=" << squaredLengthFloat << " sqrtf=" << sqrtLengthFloat
                          << " invsqrtf=" << inverseSqrtFloat
                          << " glmInv=" << normalizedViaInverseSqrt.x << ',' << normalizedViaInverseSqrt.y << ',' << normalizedViaInverseSqrt.z
                          << " glmSqrt=" << normalizedViaSqrt.x << ',' << normalizedViaSqrt.y << ',' << normalizedViaSqrt.z
                          << " qd=" << squaredLengthDouble << " invsqrtD=" << inverseSqrtDouble
                          << " doubleNorm=" << normalizedViaDouble.x << ',' << normalizedViaDouble.y << ',' << normalizedViaDouble.z
                          << " noise=" << noiseValue << " normalized=" << normalizedNoise
                          << " response=" << plainsResponse << " reconstructedSurface=" << reconstructedSurface << std::defaultfloat
                          << " depthDecimal=" << std::setprecision(10) << depth
                          << " materialized=" << static_cast<unsigned int>(replayChunk.blocks[localX][71][localZ])
                          << " original=" << static_cast<unsigned int>(source.blocks[localX][71][localZ]) << '\n';
                break;
            }
        }
    }

    size_t p02aCoordinateAgreement = 0;
    size_t p02aCoordinateDisagreement = 0;
    size_t p02aCrossQuerySolid = 0;
    size_t p02aCrossQueryHit = 0;
    uint64_t p02aLegacyChunkCoordHash = 1469598103934665603ull;
    const float p02aPlanetRadius = fw::PlanetMath::GetPlanetRadius(fw::PlanetSize::Tiny);
    for (int face = 0; face < 6; ++face) {
        for (int row = 0; row < resolution; ++row) {
            for (int col = 0; col < resolution; ++col) {
                const glm::vec3 direction = fw::CubeSphereMapping::CellToDirection(face, col, row, resolution);
                const glm::vec3 sample = direction * (p02aPlanetRadius + 18.0f);
                float flatX = 0.0f, localY = 0.0f, flatZ = 0.0f;
                fw::MapWorldGenerator::WorldToVoxelCoord(
                    fw::PlanetSize::Tiny, false, sample, flatX, localY, flatZ);
                int chunkX = 0, chunkZ = 0;
                fw::MapWorldGenerator::GetChunkCoordFromPosition(
                    fw::PlanetSize::Tiny, false, sample, chunkX, chunkZ);
                fw::CubeFace traceFace{};
                glm::vec2 traceUv(0.0f);
                fw::CubeSphereMapping::DirectionToFaceUV(glm::normalize(sample), traceFace, traceUv);
                const glm::vec2 traceChart = fw::CubeSphereMapping::FaceUVToChartCoordinates(traceUv);
                const float traceFactor = p02aPlanetRadius /
                    fw::ChunkDimensions::HorizontalWorldExtent;
                const float traceLocalCx = traceChart.x * traceFactor;
                const float traceLocalCy = traceChart.y * traceFactor;
                const int traceFaceIndex = static_cast<int>(traceFace);
                int traceRawCx = 0, traceRawCy = 0;
                if (traceFaceIndex == 0) { traceRawCx = (int)std::floor(traceLocalCx); traceRawCy = (int)std::ceil(traceLocalCy); }
                else if (traceFaceIndex == 1 || traceFaceIndex == 2) { traceRawCx = (int)std::ceil(traceLocalCx); traceRawCy = (int)std::floor(traceLocalCy); }
                else if (traceFaceIndex == 3 || traceFaceIndex == 4 || traceFaceIndex == 5) { traceRawCx = (int)std::floor(traceLocalCx); traceRawCy = (int)std::ceil(traceLocalCy); }
                int traceCanvasN = fw::PlanetMath::GetEditorCanvasExtents(fw::PlanetSize::Tiny);
                const int traceClampedCx = std::clamp(traceRawCx, -traceCanvasN, traceCanvasN);
                const int traceClampedCy = std::clamp(traceRawCy, -traceCanvasN, traceCanvasN);
                const int traceStride = fw::PlanetMath::GetFaceResolution(fw::PlanetSize::Tiny);
                const int traceExpectedKeyX = (traceClampedCx + traceCanvasN) + (traceFaceIndex % 3) * traceStride;
                const int traceExpectedKeyZ = (traceClampedCy + traceCanvasN) + (traceFaceIndex / 3) * traceStride;
                if (std::getenv("FAIRWORLD_P03B_TRACE") != nullptr) {
                std::cout << std::setprecision(9)
                          << "[P03B][TRACE] docPlanet=\"" << planet.name << "\" PlanetID=Invalid"
                          << " face=" << face << " col=" << col << " row=" << row
                          << " legacyKey=" << chunkX << ',' << chunkZ
                          << " replayKey=" << traceExpectedKeyX << ',' << traceExpectedKeyZ
                          << " world=" << std::hexfloat << sample.x << ',' << sample.y << ',' << sample.z
                          << " dir=" << direction.x << ',' << direction.y << ',' << direction.z
                          << " uv=" << traceUv.x << ',' << traceUv.y
                          << " chart=" << traceChart.x << ',' << traceChart.y
                          << " local=" << traceLocalCx << ',' << traceLocalCy
                          << std::defaultfloat << " raw=" << traceRawCx << ',' << traceRawCy
                          << " clampN=" << traceCanvasN << " final=" << chunkX << ',' << chunkZ
                          << " worldToVoxel=" << flatX << ',' << localY << ',' << flatZ << '\n';
                }
                for (const int value : {face, col, row, chunkX, chunkZ}) {
                    p02aLegacyChunkCoordHash ^= static_cast<uint32_t>(value);
                    p02aLegacyChunkCoordHash *= 1099511628211ull;
                }
                const int worldToVoxelChunkX = static_cast<int>(std::floor(flatX / CHUNK_SIZE));
                const int worldToVoxelChunkZ = static_cast<int>(std::floor(flatZ / CHUNK_SIZE));
                if (chunkX == worldToVoxelChunkX && chunkZ == worldToVoxelChunkZ) ++p02aCoordinateAgreement;
                else ++p02aCoordinateDisagreement;
            }
        }
    }
    context.forgeWorld = &world;
    for (int face = 0; face < 6; ++face) {
        const glm::vec3 direction = fw::CubeSphereMapping::FaceChartToDirection(
            face, {0.0f, 0.0f});
        const auto point = world.QueryBlockCartesian(direction * (p02aPlanetRadius - 0.5f));
        if (point.status == fw::WorldVoxelQueryStatus::SolidVoxel) ++p02aCrossQuerySolid;
        fw::RaycastQuery query{};
        query.mode = fw::RaycastMode::Voxel;
        query.maxDistance = 100.0f;
        query.ray = {direction * (p02aPlanetRadius + 50.0f), -direction};
        const auto cast = fw::RaycastSystem::Cast(&context, query);
        if (cast.hit) ++p02aCrossQueryHit;
        std::cout << "[P02A][FACE_QUERY] face=" << face
                  << " expectedRayDirection=" << direction.x << ',' << direction.y << ',' << direction.z
                  << " pointStatus=" << static_cast<int>(point.status)
                  << " pointVoxel=" << point.voxelCoord.x << ',' << point.voxelCoord.y << ',' << point.voxelCoord.z
                  << " CastVoxelHit=" << cast.hit << " distance=" << cast.distance
                  << " voxel=" << cast.voxelPosition.x << ',' << cast.voxelPosition.y << ',' << cast.voxelPosition.z
                  << '\n';
    }
    std::cout << "[P02A][OBSERVED] WorldToVoxelCoord vs GetChunkCoordFromPosition at cell centers: agreement="
              << p02aCoordinateAgreement << " disagreement=" << p02aCoordinateDisagreement
              << " legacyGetChunkCoordDigest=0x" << std::hex << p02aLegacyChunkCoordHash << std::dec
              << " / " << (6 * resolution * resolution)
              << "; six center probes QueryBlockCartesian solid=" << p02aCrossQuerySolid
              << "/6 CastVoxel hit=" << p02aCrossQueryHit << "/6\n";
    if (p02aCrossQuerySolid != 6 || p02aCrossQueryHit != 6) {
        std::cerr << "[P02D][FAIL] authoritative Tiny solid probes and planetary CastVoxel disagree\n";
        return 1;
    }
    std::cout << "[P02D][PASS] six Tiny authoritative solid probes return six planetary ray hits\n";
    constexpr uint64_t p02aLegacyChunkCoordDigest = 0x8429a4e135752c13ull;
    const bool legacyChunkCoordCompatibilityPass =
        p02aLegacyChunkCoordHash == p02aLegacyChunkCoordDigest;
    std::cout << (legacyChunkCoordCompatibilityPass ? "[PASS]" : "[FAIL]")
              << " GetChunkCoordFromPosition legacy output digest preserved\n";
    if (!legacyChunkCoordCompatibilityPass) return 1;

    // Characterize distinct CPU paths on the generated Tiny fixture. These are
    // timings for the current build/machine, not acceptance thresholds.
    constexpr int p02aIterations = 1000;
    fw::RaycastQuery p02aBroadphaseQuery{};
    p02aBroadphaseQuery.mode = fw::RaycastMode::Voxel;
    p02aBroadphaseQuery.maxDistance = 1.0f;
    p02aBroadphaseQuery.ray = {{100000.0f, 100000.0f, 100000.0f}, {0.0f, 0.0f, -1.0f}};
    context.forgeWorld = &world;
    const auto broadphaseStart = std::chrono::steady_clock::now();
    for (int i = 0; i < p02aIterations; ++i) (void)fw::RaycastSystem::Cast(&context, p02aBroadphaseQuery);
    const auto broadphaseNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - broadphaseStart).count());

    const auto pointQueryStart = std::chrono::steady_clock::now();
    size_t pointSolidResults = 0;
    for (int i = 0; i < p02aIterations; ++i) {
        const auto sample = world.QueryBlockCartesian(glm::vec3(0.0f, 0.0f, 42.0f + (i & 1) * 0.1f));
        if (sample.status == fw::WorldVoxelQueryStatus::SolidVoxel) ++pointSolidResults;
    }
    const auto pointQueryNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - pointQueryStart).count());

    PhysicsEngine p02aPhysics;
    RigidBody p02aBody{};
    p02aBody.position = {0.0f, 0.0f, 42.5f};
    p02aBody.isFlying = true;
    const auto probeStart = std::chrono::steady_clock::now();
    for (int i = 0; i < p02aIterations; ++i) p02aPhysics.StepSimulation(p02aBody, 0.0f, world);
    const auto probeNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - probeStart).count());
    std::cout << "[P02A][BENCH] iterations=" << p02aIterations
              << " CastVoxel broadphase all-chunk reject total_ns=" << broadphaseNs
              << " ns_per_query=" << (broadphaseNs / p02aIterations)
              << " QueryBlockCartesian total_ns=" << pointQueryNs
              << " ns_per_query=" << (pointQueryNs / p02aIterations)
              << " solid_results=" << pointSolidResults
              << " PhysicsEngine::StepSimulation+d6 collision path total_ns=" << probeNs
              << " ns_per_step=" << (probeNs / p02aIterations)
              << " last_solid_contacts=" << p02aBody.dbg_voxelContacts << '\n';

    size_t cpuMeshesBuilt = 0;
    size_t cpuMeshVertices = 0;
    size_t cpuMeshIndices = 0;
    size_t cpuMeshTriangles = 0;
    size_t invalidMeshPositions = 0;
    size_t invalidMeshUVs = 0;
    size_t invalidMeshNormals = 0;
    size_t invalidMeshWindings = 0;
    size_t invalidMeshIndices = 0;
    size_t sourceMeshMismatches = 0;
    size_t zeroMaterialVertices = 0;
    size_t meshResultsAssociatedToSource = 0;
    size_t degenerateTriangles = 0;
    size_t outwardRadialTriangles = 0;
    size_t inwardRadialTriangles = 0;
    size_t indeterminateRadialTriangles = 0;
    std::array<size_t, 6> trianglesByPlanetFace{};
    std::array<size_t, 6> degenerateByPlanetFace{};
    std::array<size_t, 6> windingMismatchByPlanetFace{};
    std::array<size_t, 6> outwardByPlanetFace{};
    std::array<size_t, 6> inwardByPlanetFace{};
    std::array<size_t, 6> indeterminateByPlanetFace{};
    uint64_t stableMeshHash = 1469598103934665603ull;
    uint64_t builderNanoseconds = 0;
    std::vector<fw::Vertex> windingCostSample;
    bool cpuMeshContractPass = true;
    size_t p02aCrossFaceDirectedEdges = 0;
    size_t p02aCrossFaceNeighborsDelivered = 0;
    size_t p02aCrossFaceNeighborMismatches = 0;
    size_t p02cCrossFaceTrianglesRemoved = 0;
    size_t p02cSameFaceOnlyTriangles = 0;
    size_t p02cLegacyFlatTriangles = 0;
    uint64_t p02cLegacyFlatMeshHash = 1469598103934665603ull;
    std::ofstream p03d3MeshDump;
    if (const char* meshDumpPath = std::getenv("FAIRWORLD_P03D3_MESH_DUMP")) {
        const std::filesystem::path outputPath(meshDumpPath);
        std::error_code directoryError;
        if (outputPath.has_parent_path()) {
            std::filesystem::create_directories(outputPath.parent_path(), directoryError);
        }
        p03d3MeshDump.open(outputPath, std::ios::binary | std::ios::trunc);
        const uint32_t version = 1;
        const uint32_t expectedChunks = static_cast<uint32_t>(chunkEntities.size());
        p03d3MeshDump.write(reinterpret_cast<const char*>(&version), sizeof(version));
        p03d3MeshDump.write(reinterpret_cast<const char*>(&expectedChunks), sizeof(expectedChunks));
    }
    for (entt::entity entity : chunkEntities) {
        if (!registry.valid(entity) || !registry.all_of<fw::VoxelChunkComponent>(entity)) {
            cpuMeshContractPass = false;
            ++sourceMeshMismatches;
            continue;
        }
        const auto& chunk = registry.get<fw::VoxelChunkComponent>(entity);
        std::array<const fw::VoxelChunkComponent*, 9> neighbors{};
        std::array<std::shared_ptr<fw::VoxelChunkComponent>, 9> neighborSnapshots{};
        bool neighborPending = false;
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                if (dx == 0 && dz == 0) continue;
                const size_t slot = static_cast<size_t>((dx + 1) * 3 + (dz + 1));
                if (!chunk.planetCoord.planet.IsValid()) {
                    const entt::entity neighborEntity = world.GetChunkManager().GetChunkEntity(
                        chunk.cx + dx, chunk.cz + dz);
                    if (neighborEntity != entt::null && registry.valid(neighborEntity) &&
                        registry.all_of<fw::VoxelChunkComponent>(neighborEntity)) {
                        neighborSnapshots[slot] = std::make_shared<fw::VoxelChunkComponent>(
                            registry.get<fw::VoxelChunkComponent>(neighborEntity));
                        neighbors[slot] = neighborSnapshots[slot].get();
                    }
                    continue;
                }

                const int n = resolution;
                const bool cardinal = (dx == -1 && dz == 0) || (dx == 1 && dz == 0) ||
                    (dx == 0 && dz == -1) || (dx == 0 && dz == 1);
                fw::planetary_neighborhood::Address address;
                bool addressValid = false;
                if (cardinal) {
                    addressValid = fw::planetary_neighborhood::TryResolveAddress(
                        chunk.planetCoord, dx, dz, fw::PlanetSize::Tiny, address);
                } else {
                    address.coordinate = chunk.planetCoord;
                    address.coordinate.col += dx;
                    address.coordinate.row -= dz;
                    addressValid = fw::planetary_neighborhood::IsValidCoordinate(
                        address.coordinate, n) &&
                        fw::planet_grid_compat::TryPlanetChunkCoordToLegacyNet(
                            address.coordinate, n, address.storageCx, address.storageCz);
                }
                if (!addressValid) continue;

                const entt::entity neighborEntity = world.GetChunkManager().GetChunkEntity(
                    address.storageCx, address.storageCz);
                const bool found = neighborEntity != entt::null;
                const bool entityValid = found && registry.valid(neighborEntity);
                const bool hasChunk = entityValid && registry.all_of<fw::VoxelChunkComponent>(neighborEntity);
                const fw::VoxelChunkComponent* target = hasChunk
                    ? &registry.get<fw::VoxelChunkComponent>(neighborEntity) : nullptr;
                const bool identityMatches = target && target->planetCoord == address.coordinate;
                const auto availability = fw::planetary_neighborhood::Classify(
                    found, entityValid, hasChunk, identityMatches, target && target->isGenerated);
                if (availability == fw::planetary_neighborhood::Availability::Pending) {
                    neighborPending = true;
                    continue;
                }
                if (availability != fw::planetary_neighborhood::Availability::Available) continue;

                neighborSnapshots[slot] = std::make_shared<fw::VoxelChunkComponent>(*target);
                if (cardinal && address.coordinate.face != chunk.planetCoord.face) {
                    fw::VoxelChunkComponent oriented;
                    if (!fw::planetary_neighborhood::TryOrientCrossFaceSnapshot(
                            chunk.planetCoord, dx, dz, fw::PlanetSize::Tiny, *target, oriented)) {
                        neighborSnapshots[slot].reset();
                        cpuMeshContractPass = false;
                        continue;
                    }
                    *neighborSnapshots[slot] = std::move(oriented);
                }
                neighbors[slot] = neighborSnapshots[slot].get();
            }
        }

        if (neighborPending) {
            cpuMeshContractPass = false;
            continue;
        }

        const auto& logical = chunk.planetCoord;
        for (const auto [dc, dr] : std::array<std::pair<int, int>, 4>{{{-1,0}, {1,0}, {0,-1}, {0,1}}}) {
            const int n = resolution;
            if ((dc < 0 && logical.col != 0) || (dc > 0 && logical.col != n - 1) ||
                (dr < 0 && logical.row != 0) || (dr > 0 && logical.row != n - 1)) continue;
            const auto expectedCoord = fw::PlanetMath::ResolveNeighbor(logical, dc, dr, fw::PlanetSize::Tiny);
            const entt::entity expectedEntity = world.GetChunkManager().GetChunkEntity(
                (static_cast<int>(expectedCoord.face) % 3) * n + expectedCoord.col,
                (static_cast<int>(expectedCoord.face) / 3) * n + (n - 1 - expectedCoord.row));
            ++p02aCrossFaceDirectedEdges;
            bool delivered = false;
            if (expectedEntity != entt::null && registry.valid(expectedEntity) &&
                registry.all_of<fw::VoxelChunkComponent>(expectedEntity)) {
                const auto* expectedChunk = &registry.get<fw::VoxelChunkComponent>(expectedEntity);
                const size_t expectedSlot = static_cast<size_t>((dc + 1) * 3 + (-dr + 1));
                const auto* supplied = neighbors[expectedSlot];
                delivered = supplied && supplied->planetCoord == expectedChunk->planetCoord &&
                    supplied->planetCoord == expectedCoord;
            }
            if (delivered) ++p02aCrossFaceNeighborsDelivered;
            else ++p02aCrossFaceNeighborMismatches;
        }

        const auto buildStart = std::chrono::steady_clock::now();
        const fw::VoxelChunkCpuMesh cpuMesh = fw::VoxelChunkMesher::Build(
            chunk, neighbors, materialRegistry, true, fw::PlanetSize::Tiny);
        if (p03d3MeshDump) {
            const int32_t face = static_cast<int32_t>(chunk.planetCoord.face);
            const int32_t col = chunk.planetCoord.col;
            const int32_t row = chunk.planetCoord.row;
            const uint32_t vertexCount = static_cast<uint32_t>(cpuMesh.vertices.size());
            const uint32_t indexCount = static_cast<uint32_t>(cpuMesh.indices.size());
            p03d3MeshDump.write(reinterpret_cast<const char*>(&face), sizeof(face));
            p03d3MeshDump.write(reinterpret_cast<const char*>(&col), sizeof(col));
            p03d3MeshDump.write(reinterpret_cast<const char*>(&row), sizeof(row));
            p03d3MeshDump.write(reinterpret_cast<const char*>(&vertexCount), sizeof(vertexCount));
            p03d3MeshDump.write(reinterpret_cast<const char*>(&indexCount), sizeof(indexCount));
            for (const auto& vertex : cpuMesh.vertices) {
                const float fields[] = {
                    vertex.position.x, vertex.position.y, vertex.position.z,
                    vertex.roughMetal.x, vertex.roughMetal.y,
                    vertex.normal.x, vertex.normal.y, vertex.normal.z
                };
                p03d3MeshDump.write(reinterpret_cast<const char*>(fields), sizeof(fields));
                p03d3MeshDump.write(reinterpret_cast<const char*>(&vertex.materialID), sizeof(vertex.materialID));
            }
        }
        builderNanoseconds += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - buildStart).count());
        auto withoutCrossFaceNeighbors = neighbors;
        for (size_t slot = 0; slot < withoutCrossFaceNeighbors.size(); ++slot) {
            const auto* neighbor = withoutCrossFaceNeighbors[slot];
            if (neighbor && neighbor->planetCoord.planet.IsValid() &&
                neighbor->planetCoord.face != chunk.planetCoord.face) {
                withoutCrossFaceNeighbors[slot] = nullptr;
            }
        }
        const auto meshWithoutCrossFaceNeighbors = fw::VoxelChunkMesher::Build(
            chunk, withoutCrossFaceNeighbors, materialRegistry, true, fw::PlanetSize::Tiny);
        p02cSameFaceOnlyTriangles += meshWithoutCrossFaceNeighbors.vertices.size() / 3;
        if (meshWithoutCrossFaceNeighbors.vertices.size() < cpuMesh.vertices.size() ||
            (meshWithoutCrossFaceNeighbors.vertices.size() - cpuMesh.vertices.size()) % 3 != 0) {
            cpuMeshContractPass = false;
        } else {
            p02cCrossFaceTrianglesRemoved +=
                (meshWithoutCrossFaceNeighbors.vertices.size() - cpuMesh.vertices.size()) / 3;
        }

        std::array<const fw::VoxelChunkComponent*, 9> legacyFlatNeighbors{};
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                if (dx == 0 && dz == 0) continue;
                const entt::entity legacyEntity = world.GetChunkManager().GetChunkEntity(
                    chunk.cx + dx, chunk.cz + dz);
                if (legacyEntity != entt::null && registry.valid(legacyEntity) &&
                    registry.all_of<fw::VoxelChunkComponent>(legacyEntity)) {
                    legacyFlatNeighbors[static_cast<size_t>((dx + 1) * 3 + dz + 1)] =
                        &registry.get<fw::VoxelChunkComponent>(legacyEntity);
                }
            }
        }
        const auto legacyFlatMesh = fw::VoxelChunkMesher::Build(
            chunk, legacyFlatNeighbors, materialRegistry, true, fw::PlanetSize::Tiny);
        p02cLegacyFlatTriangles += legacyFlatMesh.vertices.size() / 3;
        for (const auto& vertex : legacyFlatMesh.vertices)
            p02cLegacyFlatMeshHash = HashVertex(p02cLegacyFlatMeshHash, vertex);
        if (cpuMesh.vertices.empty()) continue;
        ++cpuMeshesBuilt;
        cpuMeshVertices += cpuMesh.vertices.size();
        cpuMeshIndices += cpuMesh.indices.size();
        if (cpuMesh.vertices.size() % 3 != 0 || cpuMesh.indices.size() != cpuMesh.vertices.size()) {
            ++invalidMeshIndices;
            cpuMeshContractPass = false;
        }
        if (cpuMesh.sourceChunkX != chunk.cx || cpuMesh.sourceChunkZ != chunk.cz ||
            cpuMesh.sourcePlanetCoord.planet.value != chunk.planetCoord.planet.value ||
            cpuMesh.sourcePlanetCoord.face != chunk.planetCoord.face ||
            cpuMesh.sourcePlanetCoord.col != chunk.planetCoord.col ||
            cpuMesh.sourcePlanetCoord.row != chunk.planetCoord.row ||
            cpuMesh.sourcePlanetCoord.layer != chunk.planetCoord.layer) {
            ++sourceMeshMismatches;
            cpuMeshContractPass = false;
        } else {
            ++meshResultsAssociatedToSource;
        }
        windingCostSample.insert(windingCostSample.end(), cpuMesh.vertices.begin(), cpuMesh.vertices.end());
        for (const auto& vertex : cpuMesh.vertices) stableMeshHash = HashVertex(stableMeshHash, vertex);

        std::set<uint8_t> chunkBlockIds;
        for (int x = 0; x < CHUNK_SIZE; ++x) {
            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                for (int z = 0; z < CHUNK_SIZE; ++z) {
                    const uint8_t block = chunk.blocks[x][y][z];
                    if (block != 0) chunkBlockIds.insert(block);
                }
            }
        }
        for (size_t i = 0; i < cpuMesh.vertices.size(); ++i) {
            const auto& vertex = cpuMesh.vertices[i];
            const glm::vec3 pos(vertex.position.x, vertex.position.y, vertex.position.z);
            const glm::vec3 normal(vertex.normal.x, vertex.normal.y, vertex.normal.z);
            const glm::vec2 uv(vertex.roughMetal.x, vertex.roughMetal.y);
            if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) {
                ++invalidMeshPositions;
                cpuMeshContractPass = false;
            }
            if (!IsFinite(uv) || uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) {
                ++invalidMeshUVs;
                cpuMeshContractPass = false;
            }
            if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z) ||
                std::abs(glm::length(normal) - 1.0f) > 1.0e-3f) {
                ++invalidMeshNormals;
                cpuMeshContractPass = false;
            }
            if (vertex.materialID == 0) {
                ++zeroMaterialVertices;
                cpuMeshContractPass = false;
            } else if (chunkBlockIds.find(static_cast<uint8_t>(vertex.materialID)) == chunkBlockIds.end()) {
                ++sourceMeshMismatches;
                cpuMeshContractPass = false;
            }
            if (i < cpuMesh.indices.size() && cpuMesh.indices[i] != i) {
                ++invalidMeshIndices;
                cpuMeshContractPass = false;
            }
        }
        for (size_t i = 0; i + 2 < cpuMesh.indices.size(); i += 3) {
            const auto& a = cpuMesh.vertices[cpuMesh.indices[i]];
            const auto& b = cpuMesh.vertices[cpuMesh.indices[i + 1]];
            const auto& c = cpuMesh.vertices[cpuMesh.indices[i + 2]];
            const glm::vec3 pa(a.position.x, a.position.y, a.position.z);
            const glm::vec3 pb(b.position.x, b.position.y, b.position.z);
            const glm::vec3 pc(c.position.x, c.position.y, c.position.z);
            const glm::vec3 na(a.normal.x, a.normal.y, a.normal.z);
            const glm::vec3 nb(b.normal.x, b.normal.y, b.normal.z);
            const glm::vec3 nc(c.normal.x, c.normal.y, c.normal.z);
            const glm::vec3 geometricNormal = glm::cross(pb - pa, pc - pa);
            const size_t planetFace = static_cast<size_t>(chunk.planetCoord.face);
            if (planetFace < trianglesByPlanetFace.size()) ++trianglesByPlanetFace[planetFace];
            const float area2 = glm::length(geometricNormal);
            if (!std::isfinite(area2) || area2 <= 1.0e-7f) {
                ++degenerateTriangles;
                if (planetFace < degenerateByPlanetFace.size()) ++degenerateByPlanetFace[planetFace];
            }
            const float normalAlignment = glm::dot(geometricNormal, na + nb + nc);
            if (area2 > 1.0e-7f && normalAlignment <= 0.0f) {
                ++invalidMeshWindings;
                if (planetFace < windingMismatchByPlanetFace.size()) ++windingMismatchByPlanetFace[planetFace];
                cpuMeshContractPass = false;
            }
            const glm::vec3 centroid = (pa + pb + pc) / 3.0f;
            const glm::vec3 averageNormal = na + nb + nc;
            const float radialAlignment = glm::dot(centroid, averageNormal);
            if (!std::isfinite(radialAlignment) || std::abs(radialAlignment) <= 1.0e-7f) {
                ++indeterminateRadialTriangles;
                if (planetFace < indeterminateByPlanetFace.size()) ++indeterminateByPlanetFace[planetFace];
            } else if (radialAlignment > 0.0f) {
                ++outwardRadialTriangles;
                if (planetFace < outwardByPlanetFace.size()) ++outwardByPlanetFace[planetFace];
            } else {
                ++inwardRadialTriangles;
                if (planetFace < inwardByPlanetFace.size()) ++inwardByPlanetFace[planetFace];
            }
            ++cpuMeshTriangles;
        }
    }
    cpuMeshContractPass = cpuMeshContractPass && cpuMeshesBuilt > 0 && cpuMeshVertices > 0 &&
        cpuMeshIndices == cpuMeshVertices && invalidMeshIndices == 0 && invalidMeshPositions == 0 &&
        invalidMeshUVs == 0 && invalidMeshNormals == 0 && invalidMeshWindings == 0 &&
        degenerateTriangles == 0 && sourceMeshMismatches == 0 && zeroMaterialVertices == 0 &&
        meshResultsAssociatedToSource == cpuMeshesBuilt &&
        p02aCrossFaceDirectedEdges == 72 && p02aCrossFaceNeighborsDelivered == 72 &&
        p02aCrossFaceNeighborMismatches == 0 &&
        p02cLegacyFlatTriangles == 135820 && p02cLegacyFlatMeshHash == 0x20c0d276200e0e20ull;
    std::cout << "[OBSERVED] shared CPU mesher results=" << cpuMeshesBuilt
              << " vertices=" << cpuMeshVertices << " indices=" << cpuMeshIndices
              << " triangles=" << cpuMeshTriangles
              << " invalidIndices=" << invalidMeshIndices
              << " invalidPositions=" << invalidMeshPositions
              << " invalidUV=" << invalidMeshUVs
              << " invalidNormals=" << invalidMeshNormals
              << " windingMismatches=" << invalidMeshWindings
              << " sourceMaterial/association mismatches=" << sourceMeshMismatches
              << " materialID0 vertices=" << zeroMaterialVertices
              << " source-associated results=" << meshResultsAssociatedToSource << '\n';
    std::cout << "[P02C][OBSERVED] triangles removed by resolved cross-face neighbors="
              << p02cCrossFaceTrianglesRemoved << " (per-chunk comparison against the same-face-only input)\n"
              << "[P02C][BASELINE] same-face-only triangles=" << p02cSameFaceOnlyTriangles << '\n'
              << "[P02C][BASELINE] legacy flat lookup triangles=" << p02cLegacyFlatTriangles
              << " stableMeshHash=0x" << std::hex << p02cLegacyFlatMeshHash << std::dec << '\n';
    std::cout << "[P02A][OBSERVED] cross-face directed edges=" << p02aCrossFaceDirectedEdges
              << " expected topological neighbors delivered to mesher=" << p02aCrossFaceNeighborsDelivered
              << " absent/mismatched=" << p02aCrossFaceNeighborMismatches << '\n';
    std::cout << "[OBSERVED] geometry degenerates(area2<=1e-7 or nonfinite)=" << degenerateTriangles
              << " radial-facing outward=" << outwardRadialTriangles
              << " inward=" << inwardRadialTriangles
              << " indeterminate=" << indeterminateRadialTriangles
              << " stableMeshHash=0x" << std::hex << stableMeshHash << std::dec << '\n';
    for (size_t face = 0; face < trianglesByPlanetFace.size(); ++face) {
        std::cout << "[FACE] CubeSphere face=" << face
                  << " triangles=" << trianglesByPlanetFace[face]
                  << " degenerate=" << degenerateByPlanetFace[face]
                  << " windingMismatch=" << windingMismatchByPlanetFace[face]
                  << " outward=" << outwardByPlanetFace[face]
                  << " inward=" << inwardByPlanetFace[face]
                  << " indeterminate=" << indeterminateByPlanetFace[face] << '\n';
    }

    if (!windingCostSample.empty() && windingCostSample.size() % 3 == 0) {
        auto alignedCopy = windingCostSample;
        const auto alignedStart = std::chrono::steady_clock::now();
        const size_t alignedSwaps = ApplyWindingPassForBenchmark(alignedCopy);
        const auto alignedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - alignedStart).count();

        auto reversedCopy = windingCostSample;
        for (size_t i = 0; i + 2 < reversedCopy.size(); i += 3) std::swap(reversedCopy[i + 1], reversedCopy[i + 2]);
        const auto reversedStart = std::chrono::steady_clock::now();
        const size_t reversedSwaps = ApplyWindingPassForBenchmark(reversedCopy);
        const auto reversedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - reversedStart).count();
        const double triangleCount = static_cast<double>(windingCostSample.size() / 3);
        std::cout << "[BENCH] production Build total_ms=" << (static_cast<double>(builderNanoseconds) / 1.0e6)
                  << " triangles=" << cpuMeshTriangles
                  << " aligned replay swaps=" << alignedSwaps
                  << " elapsed_ns_per_triangle=" << (static_cast<double>(alignedNs) / triangleCount)
                  << " reversed replay swaps=" << reversedSwaps
                  << " reversed_elapsed_ns_per_triangle=" << (static_cast<double>(reversedNs) / triangleCount)
                  << " (runner-only predicate replay; not an alternate mesher)\n";
    }
    std::cout << (cpuMeshContractPass ? "[PASS]" : "[FAIL]")
              << " shared production CPU voxel mesh contract\n";

    const bool solverPass = solved == static_cast<int>(chunkEntities.size()) && solverQueue.empty() &&
        solvedChunks == chunkEntities.size() && chunkGeneratedFlags && taggedAfter == taggedBefore &&
        meshRequests == chunkEntities.size() && nonAirVoxels > 0 && !blockDistribution.empty();
    std::cout << (solverPass ? "[PASS]" : "[FAIL]") << " TerrainSolver completion" << '\n';
    if (!solverPass) {
        std::cerr << "[FAIL] TerrainSolver non ha completato il contratto voxel atteso\n";
    }

    float contactBoundaryRadius = 0.0f;
    const bool solidContactPass = solverPass && TestPlanetarySolidVoxelContact(world, contactBoundaryRadius);
    if (!solidContactPass) return 1;

    size_t spawnSamples = 0;
    size_t spawnColliderChecks = 0;
    float minimumSpawnClearance = 0.0f;
    const bool spawnPass = solverPass && TestPlanetarySpawnResolution(
        world, spawnSamples, spawnColliderChecks, minimumSpawnClearance);
    std::cout << "[OBSERVED] planetary spawn samples=" << spawnSamples
              << " collider voxel probes=" << spawnColliderChecks
              << " minimum surface clearance=" << minimumSpawnClearance << '\n';
    std::cout << (spawnPass ? "[PASS]" : "[FAIL]")
              << " Tiny spawn surface, radial normal, deterministic six-face/edge/corner and collider clearance\n";
    float settledClearance = 0.0f;
    float maximumJumpHeight = 0.0f;
    float maxCollisionFrameStep = 0.0f;
    size_t physicsFaceTransitions = 0;
    size_t physicsCellTransitions = 0;
    const bool pendingPlayerPass = TestPendingPlayerIsFrozen(world);
    std::cout << (pendingPlayerPass ? "[PASS]" : "[FAIL]")
              << " planetarySpawnPending blocks movement and physics without changing body/camera state\n";
    const bool postPhysicsCameraSyncPass = TestPostPhysicsCameraSync(
        world, fw::CubeSphereMapping::FaceUVToDirection(fw::CubeFace::PositiveZ, {0.5f, 0.5f}));
    std::cout << (postPhysicsCameraSyncPass ? "[PASS]" : "[FAIL]")
              << " camera transform follows the post-collision body position plus radial eye offset\n";
    const bool planetaryPhysicsPass = spawnPass && TestPlanetaryPhysics(
        world, fw::CubeSphereMapping::FaceUVToDirection(fw::CubeFace::PositiveZ, {0.5f, 0.5f}),
        settledClearance, maximumJumpHeight, maxCollisionFrameStep,
        physicsFaceTransitions, physicsCellTransitions);
    std::cout << "[OBSERVED] planetary physics settled clearance=" << settledClearance
              << " maximum jump height=" << maximumJumpHeight
              << " collision frame max step=" << maxCollisionFrameStep
              << " physics face transitions=" << physicsFaceTransitions
              << " grid transitions=" << physicsCellTransitions << '\n';
    std::cout << (planetaryPhysicsPass ? "[PASS]" : "[FAIL]")
              << " Tiny radial gravity, terminal speed, stable spherical collision frame, rest and jump/landing\n";
    const bool p03ProbeMatrixPass = RunP03CollisionProbeMatrix(world);
    std::cout << (p03ProbeMatrixPass ? "[P03][PASS]" : "[P03][FAIL]")
              << " 14 deterministic planetary collision characterization scenarios\n";
    const auto falseGroundWitness = FindP03FalseGroundWitness(world);
    std::cout << "[P03][OBSERVED] false-ground search candidates=" << falseGroundWitness.candidates
              << " found=" << falseGroundWitness.found
              << " start=" << falseGroundWitness.startPosition.x << ',' << falseGroundWitness.startPosition.y
              << ',' << falseGroundWitness.startPosition.z
              << " velocity=" << falseGroundWitness.initialVelocity.x << ','
              << falseGroundWitness.initialVelocity.y << ',' << falseGroundWitness.initialVelocity.z
              << " final=" << falseGroundWitness.finalPosition.x << ',' << falseGroundWitness.finalPosition.y
              << ',' << falseGroundWitness.finalPosition.z
              << " contactVoxel=" << falseGroundWitness.contactVoxel.x << ','
              << falseGroundWitness.contactVoxel.y << ',' << falseGroundWitness.contactVoxel.z
              << " probes=" << falseGroundWitness.probes
              << " bottomSupportProbes=" << falseGroundWitness.bottomSupportProbes << '\n';
    const bool p03DeepOverlapPass = RunP03DeepOverlapDiagnostic(world);
    std::cout << (p03DeepOverlapPass ? "[P03][PASS]" : "[P03][FAIL]")
              << " one-meter initial voxel overlap recovered without an artificial outward impulse\n";
    const bool p03aCollisionPass = TestP03aCollisionContracts();
    std::cout << (p03aCollisionPass ? "[P03A][PASS]" : "[P03A][FAIL]")
              << " grounded support, terrain availability, and logical-sphere dispatch contracts\n";
    const bool p03d4ContactPass = TestP03d4GroundedContactScenarios();
    std::cout << (p03d4ContactPass ? "[P03D4][PASS]" : "[P03D4][FAIL]")
              << " grounded upper-body, side, ceiling, falling, support-loss scenarios\n";
    const bool p03d5RadialContactPass = TestP03d5RadialUpperOnlyContactSearch();
    const bool gravityAnchoringPass = planetaryPhysicsPass && TestPlanetaryGravityAnchoring(
        world, fw::CubeSphereMapping::FaceUVToDirection(fw::CubeFace::PositiveZ, {0.5f, 0.5f}));
    if (!pendingPlayerPass || !postPhysicsCameraSyncPass || !planetaryPhysicsPass ||
        !gravityAnchoringPass || !p03ProbeMatrixPass || !p03DeepOverlapPass || !p03aCollisionPass ||
        !p03d4ContactPass || !p03d5RadialContactPass) return 1;
    const auto& savedSpawn = planet.spawnPoints.front();
    const float spawnPlanetRadius = fw::PlanetMath::GetPlanetRadius(planet.planetSize);
    const glm::vec3 savedSpawnDirection = fw::CubeSphereMapping::FaceChartToDirection(
        savedSpawn.faceIndex, glm::vec2(savedSpawn.localX / spawnPlanetRadius, savedSpawn.localZ / spawnPlanetRadius));
    glm::vec3 resolvedFixtureSpawn(0.0f);
    float fixtureSurfaceRadius = 0.0f;
    const auto fixtureResolveStatus = world.TryResolvePlanetarySpawn(
        savedSpawnDirection, savedSpawn.heightOffset, 0.3f, resolvedFixtureSpawn, &fixtureSurfaceRadius);
    const float oldFormulaRadius = spawnPlanetRadius + savedSpawn.heightOffset;
    const float oldFormulaPenetration = fixtureSurfaceRadius - oldFormulaRadius;
    const bool oldFormulaPenetrates = fixtureResolveStatus == fw::PlanetarySpawnResolveStatus::Ready &&
        oldFormulaPenetration > 0.0f;
    std::cout << "[OBSERVED] fixture spawn surfaceRadius=" << fixtureSurfaceRadius
              << " savedOffset=" << savedSpawn.heightOffset << " oldBaseRadiusFormula=" << oldFormulaRadius
              << " oldFormulaSurfacePenetration=" << oldFormulaPenetration << '\n';
    std::cout << (oldFormulaPenetrates ? "[PASS]" : "[FAIL]")
              << " regression witness: legacy reference-radius placement is below the solved voxel surface\n";

    // Execute the production update once with the deliberately headless context.
    // With no JobSystem it can process ECS/terrain state but cannot dispatch the
    // production async voxel mesh worker.
    world.Update(0.0f);
    size_t pendingMeshRequests = 0;
    size_t pendingMeshJobs = 0;
    auto dirtyView = registry.view<fw::VoxelChunkComponent, fw::ChunkDirtyComponent>();
    for (entt::entity entity : dirtyView) {
        ++pendingMeshRequests;
        if (dirtyView.get<fw::ChunkDirtyComponent>(entity).pendingJob) ++pendingMeshJobs;
    }

    size_t meshComponents = 0;
    size_t associatedNonEmptyMeshes = 0;
    size_t vertices = 0;
    size_t triangles = 0;
    size_t nonZeroMaterialVertices = 0;
    size_t invalidVertexAttributes = 0;
    size_t meshSourceMismatch = 0;
    size_t inconsistentWindingNormals = 0;
    auto meshView = registry.view<fw::VoxelChunkComponent, fw::MeshComponent>();
    for (entt::entity entity : meshView) {
        ++meshComponents;
        const auto& mesh = meshView.get<fw::MeshComponent>(entity);
        if (mesh.vertices.empty()) continue;
        ++associatedNonEmptyMeshes;
        vertices += mesh.vertices.size();
        triangles += mesh.vertices.size() / 3;
        for (const auto& vertex : mesh.vertices) {
            if (vertex.materialID != 0) ++nonZeroMaterialVertices;
            const glm::vec3 normal(vertex.normal.x, vertex.normal.y, vertex.normal.z);
            const glm::vec2 uv(vertex.roughMetal.x, vertex.roughMetal.y);
            if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z) ||
                !IsFinite(uv) || uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f ||
                std::abs(glm::length(normal) - 1.0f) > 1.0e-3f) {
                ++invalidVertexAttributes;
            }
            if (vertex.materialID == 0 || sourceBlockIds.find(vertex.materialID) == sourceBlockIds.end()) {
                ++meshSourceMismatch;
            }
        }
        for (size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
            const auto& a = mesh.vertices[i];
            const auto& b = mesh.vertices[i + 1];
            const auto& c = mesh.vertices[i + 2];
            const glm::vec3 pa(a.position.x, a.position.y, a.position.z);
            const glm::vec3 pb(b.position.x, b.position.y, b.position.z);
            const glm::vec3 pc(c.position.x, c.position.y, c.position.z);
            const glm::vec3 na(a.normal.x, a.normal.y, a.normal.z);
            const glm::vec3 nb(b.normal.x, b.normal.y, b.normal.z);
            const glm::vec3 nc(c.normal.x, c.normal.y, c.normal.z);
            const glm::vec3 geometricNormal = glm::cross(pb - pa, pc - pa);
            const glm::vec3 averageNormal = na + nb + nc;
            if (glm::length(geometricNormal) > 1.0e-7f && glm::dot(geometricNormal, averageNormal) <= 0.0f) {
                ++inconsistentWindingNormals;
            }
        }
    }

    std::cout << "[OBSERVED] ECS-associated MeshComponent=" << meshComponents
              << " non-empty meshes=" << associatedNonEmptyMeshes
              << " vertices=" << vertices << " triangles=" << triangles
              << " nonzero material vertices=" << nonZeroMaterialVertices
              << " invalid UV/normal attributes=" << invalidVertexAttributes
              << " winding/normal mismatches=" << inconsistentWindingNormals
              << " pendingMeshRequests=" << pendingMeshRequests
              << " pendingJobs=" << pendingMeshJobs
              << " jobSystemAvailable=" << (context.jobSystem ? "yes" : "no") << '\n';

    std::cout << "[NOT VERIFIED] MeshComponent published by the production async worker / VRAM-DMA GPU upload "
                 "is a separate renderer contract; this runner validates the shared CPU builder result associated "
                 "with its source ECS chunk and does not simulate publication or upload.\n";

    if (!generationPass || !solverPass || !spawnPass || !oldFormulaPenetrates || !cpuMeshContractPass) return 1;

    std::cout << "[PASS] Planetary voxel CPU end-to-end contract verified; GPU publication NOT VERIFIED.\n";
    return 0;
}
