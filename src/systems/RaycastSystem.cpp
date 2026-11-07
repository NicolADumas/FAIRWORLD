#include "pch.h"
#include "RaycastSystem.h"
#include "SharedContext.h"
#include "GameWorld.h"
#include "ForgeComponents.h"
#include "FAIRWORLD.h"
#include "CubeSphereMapping.h"
#include "WorldProjectManager.h"
#include "PlanetComponents.h"
#include "PlanetRadialMapping.h"
#include "PlanetGridCompatibility.h"
#include <algorithm>
#include <iostream>
#include <vector>
#include <array>
#include <limits>
#include <cmath>

namespace fw {

namespace {

bool IsFinite(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

double DotDouble(const glm::dvec3& a, const glm::dvec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

struct RayBoundaryEvent {
    double t = 0.0;
    glm::dvec3 normal{0.0};
};

void AddEvent(std::vector<RayBoundaryEvent>& events, double t, double start, double end,
    const glm::dvec3& normal = glm::dvec3(0.0)) {
    if (std::isfinite(t) && t >= start && t <= end) events.push_back({t, normal});
}

void AddSphereEvents(std::vector<RayBoundaryEvent>& events,
    const glm::dvec3& origin, const glm::dvec3& direction, double radius,
    double start, double end) {
    if (!(radius > 0.0) || !std::isfinite(radius)) return;
    const double a = DotDouble(direction, direction);
    const double b = 2.0 * DotDouble(origin, direction);
    const double c = DotDouble(origin, origin) - radius * radius;
    const double discriminant = b * b - 4.0 * a * c;
    if (!(discriminant > 0.0) || !(a > 0.0)) return;
    const double root = std::sqrt(discriminant);
    const double roots[2] = {(-b - root) / (2.0 * a), (-b + root) / (2.0 * a)};
    for (double t : roots) {
        const glm::dvec3 point = origin + direction * t;
        const double length = std::sqrt(DotDouble(point, point));
        AddEvent(events, t, start, end, length > 0.0 ? point / length : glm::dvec3(0.0));
    }
}

bool GetFaceChartAxes(CubeFace face, glm::dvec3& normal, glm::dvec3& chartU, glm::dvec3& chartV) {
    const glm::vec3 center = CubeSphereMapping::FaceUVToCubeDir(face, {0.5f, 0.5f});
    const glm::vec3 x = CubeSphereMapping::FaceUVToCubeDir(face, {0.75f, 0.5f});
    const glm::vec3 y = CubeSphereMapping::FaceUVToCubeDir(face, {0.5f, 0.25f});
    normal = glm::dvec3(center);
    chartU = glm::dvec3((x - center) * 2.0f);
    chartV = glm::dvec3((y - center) * 2.0f);
    return DotDouble(normal, normal) == 1.0 && DotDouble(chartU, chartU) == 1.0 &&
        DotDouble(chartV, chartV) == 1.0;
}

RaycastHit CastPlanetaryVoxel(SharedContext* context, const RaycastQuery& query,
    PlanetSize planetSize) {
    RaycastHit result;
    result.distance = query.maxDistance;
    result.type = RaycastHitType::Voxel;
    auto* world = context ? context->forgeWorld : nullptr;
    if (!world) {
        result.status = RaycastResultStatus::Pending;
        result.distance = 0.0f;
        return result;
    }

    const double radius = static_cast<double>(PlanetMath::GetPlanetRadius(planetSize));
    const double voxelSize = static_cast<double>(ChunkDimensions::VoxelWorldSize);
    const int resolution = static_cast<int>(PlanetMath::GetFaceResolution(planetSize));
    if (!(radius > 0.0) || !(voxelSize > 0.0) || resolution <= 0) {
        result.status = RaycastResultStatus::InvalidInput;
        return result;
    }
    const double outerRadius = radius +
        (static_cast<double>(ChunkDimensions::VoxelsY) -
         static_cast<double>(PlanetRadialMapping::ReferenceVoxelOffset) - 0.5) * voxelSize;
    if (!(outerRadius > 0.0)) {
        result.status = RaycastResultStatus::InvalidInput;
        return result;
    }

    const glm::dvec3 origin(query.ray.origin);
    const glm::dvec3 direction(query.ray.direction);
    const double a = DotDouble(direction, direction);
    const double b = 2.0 * DotDouble(origin, direction);
    const double c = DotDouble(origin, origin) - outerRadius * outerRadius;
    const double discriminant = b * b - 4.0 * a * c;
    if (DotDouble(origin, origin) > outerRadius * outerRadius && !(discriminant > 0.0)) return result;

    double start = 0.0;
    double end = static_cast<double>(query.maxDistance);
    if (DotDouble(origin, origin) > outerRadius * outerRadius) {
        const double root = std::sqrt(discriminant);
        start = std::max(0.0, (-b - root) / (2.0 * a));
        end = std::min(end, (-b + root) / (2.0 * a));
    } else {
        const double root = std::sqrt(std::max(0.0, discriminant));
        end = std::min(end, (-b + root) / (2.0 * a));
    }
    if (!(end >= start) || end < 0.0) return result;

    std::vector<RayBoundaryEvent> events;
    const size_t angularBoundaryCount = static_cast<size_t>(resolution) *
        static_cast<size_t>(ChunkDimensions::VoxelsX + ChunkDimensions::VoxelsZ);
    events.reserve(static_cast<size_t>(6) * (angularBoundaryCount + 2) +
        static_cast<size_t>(2 * (ChunkDimensions::VoxelsY + 1) + 2));
    AddEvent(events, start, start, end);
    AddEvent(events, end, start, end);

    // Angular voxel boundaries are exact planes through the planet center.
    // Each face cell is subdivided by the voxel grid, so a solid voxel cannot
    // be reported at the start of a larger chunk interval before the ray
    // actually enters that voxel.
    for (int faceIndex = 0; faceIndex < 6; ++faceIndex) {
        glm::dvec3 faceNormal, chartU, chartV;
        if (!GetFaceChartAxes(static_cast<CubeFace>(faceIndex), faceNormal, chartU, chartV)) {
            result.status = RaycastResultStatus::InvalidInput;
            return result;
        }
        const int faceColumns = resolution * ChunkDimensions::VoxelsX;
        const int faceRows = resolution * ChunkDimensions::VoxelsZ;
        auto addFaceGridPlane = [&](const glm::dvec3& planeNormal) {
            const double denominator = DotDouble(planeNormal, direction);
            if (denominator == 0.0) return;
            const double t = -DotDouble(planeNormal, origin) / denominator;
            if (!std::isfinite(t) || t < start || t > end) return;
            const glm::dvec3 point = origin + direction * t;
            const double depth = DotDouble(point, faceNormal);
            if (!(depth > 0.0)) return;
            const double otherU = DotDouble(point, chartU) / depth;
            const double otherV = DotDouble(point, chartV) / depth;
            constexpr double faceEdgeTolerance = 1.0e-10;
            if (otherU < -1.0 - faceEdgeTolerance || otherU > 1.0 + faceEdgeTolerance ||
                otherV < -1.0 - faceEdgeTolerance || otherV > 1.0 + faceEdgeTolerance) return;
            AddEvent(events, t, start, end, planeNormal);
        };
        for (int boundary = 0; boundary <= faceColumns; ++boundary) {
            const double chartX = 2.0 * static_cast<double>(boundary) / faceColumns - 1.0;
            addFaceGridPlane(chartU - chartX * faceNormal);
        }
        for (int boundary = 0; boundary <= faceRows; ++boundary) {
            const double chartY = 1.0 - 2.0 * static_cast<double>(boundary) / faceRows;
            addFaceGridPlane(chartV - chartY * faceNormal);
        }
    }

    // Radial cell boundaries are concentric spheres. The half-cell term follows
    // WorldToVoxelCoord's floor(localY), not a fixed-step approximation.
    for (int boundary = 0; boundary <= ChunkDimensions::VoxelsY; ++boundary) {
        const double boundaryRadius = radius +
            (static_cast<double>(boundary) -
             static_cast<double>(PlanetRadialMapping::ReferenceVoxelOffset) - 0.5) * voxelSize;
        AddSphereEvents(events, origin, direction, boundaryRadius, start, end);
    }

    std::sort(events.begin(), events.end(), [](const auto& lhs, const auto& rhs) { return lhs.t < rhs.t; });
    const auto& registry = world->GetRegistry();
    PlanetID activePlanetId = PlanetID::Invalid();
    struct PlanetPointSample {
        WorldVoxelQueryResult voxel{};
        entt::entity entity{entt::null};
        CubeFace face = CubeFace::PositiveZ;
        glm::vec2 uv{0.0f};
        bool missing = false;
    };
    auto samplePlanetVoxel = [&](const glm::vec3& point) {
        PlanetPointSample sample;
        if (!IsFinite(point)) {
            sample.voxel.status = WorldVoxelQueryStatus::InvalidInput;
            return sample;
        }
        const float radialDistance = glm::length(point);
        if (!std::isfinite(radialDistance)) {
            sample.voxel.status = WorldVoxelQueryStatus::InvalidInput;
            return sample;
        }
        const float localY = PlanetRadialMapping::GetVoxelCellCoordinateFromDistance(
            radialDistance, static_cast<float>(radius));
        if (!std::isfinite(localY)) {
            sample.voxel.status = WorldVoxelQueryStatus::InvalidInput;
            return sample;
        }
        if (localY < 0.0f) {
            sample.voxel.status = WorldVoxelQueryStatus::OutOfBounds;
            return sample;
        }
        if (localY >= static_cast<float>(ChunkDimensions::VoxelsY)) {
            sample.voxel.status = WorldVoxelQueryStatus::Air;
            sample.voxel.block = BlockType::Air;
            return sample;
        }
        if (radialDistance <= 1.0e-8f) {
            sample.voxel.status = WorldVoxelQueryStatus::OutOfBounds;
            return sample;
        }

        CubeSphereMapping::DirectionToFaceUV(point / radialDistance, sample.face, sample.uv);
        int col = 0, row = 0;
        CubeSphereMapping::UVToGrid(sample.uv, resolution, col, row);
        const PlanetChunkCoord storageAddress{PlanetID::Invalid(), sample.face, col, row, 0};
        int storageX = 0, storageZ = 0;
        if (!planet_grid_compat::TryPlanetChunkCoordToLegacyNet(
                storageAddress, resolution, storageX, storageZ)) {
            sample.voxel.status = WorldVoxelQueryStatus::InvalidInput;
            return sample;
        }

        const int voxelY = static_cast<int>(std::floor(localY));
        const float withinU = sample.uv.x * static_cast<float>(resolution) - static_cast<float>(col);
        const float withinV = sample.uv.y * static_cast<float>(resolution) - static_cast<float>(row);
        const int localX = std::clamp(static_cast<int>(std::floor(withinU * ChunkDimensions::VoxelsX)),
            0, ChunkDimensions::VoxelsX - 1);
        // The legacy net's z axis runs opposite canonical face row.
        const int localZ = std::clamp(static_cast<int>(std::floor(
            (1.0f - withinV) * ChunkDimensions::VoxelsZ)), 0, ChunkDimensions::VoxelsZ - 1);
        sample.voxel.voxelCoord = {
            storageX * ChunkDimensions::VoxelsX + localX,
            voxelY,
            storageZ * ChunkDimensions::VoxelsZ + localZ};
        sample.entity = world->GetChunkManager().GetChunkEntity(storageX, storageZ);
        if (sample.entity == entt::null || !registry.valid(sample.entity) ||
            !registry.all_of<VoxelChunkComponent>(sample.entity)) {
            sample.missing = true;
            return sample;
        }
        const auto& chunk = registry.get<VoxelChunkComponent>(sample.entity);
        if (!chunk.planetCoord.planet.IsValid() || chunk.planetCoord.face != sample.face ||
            chunk.planetCoord.col != col || chunk.planetCoord.row != row || chunk.planetCoord.layer != 0) {
            sample.voxel.status = WorldVoxelQueryStatus::InvalidInput;
            return sample;
        }
        if (!activePlanetId.IsValid()) activePlanetId = chunk.planetCoord.planet;
        const PlanetChunkCoord coordinate{activePlanetId, sample.face, col, row, 0};
        if (chunk.planetCoord != coordinate || !chunk.planetCoord.planet.IsValid()) {
            sample.voxel.status = WorldVoxelQueryStatus::InvalidInput;
            return sample;
        }
        if (!chunk.isGenerated) {
            sample.voxel.status = WorldVoxelQueryStatus::TerrainPending;
            return sample;
        }
        sample.voxel.block = static_cast<BlockType>(chunk.blocks[localX][voxelY][localZ]);
        if (sample.voxel.block == BlockType::OutOfBounds) {
            sample.voxel.status = WorldVoxelQueryStatus::OutOfBounds;
        } else if (sample.voxel.block == BlockType::Air) {
            sample.voxel.status = WorldVoxelQueryStatus::Air;
        } else if (sample.voxel.block == BlockType::Water || sample.voxel.block == BlockType::Lava ||
                   sample.voxel.block == BlockType::StargatePortal) {
            sample.voxel.status = WorldVoxelQueryStatus::PassableVoxel;
        } else {
            sample.voxel.status = WorldVoxelQueryStatus::SolidVoxel;
        }
        return sample;
    };
    auto makeHit = [&](double distance, const WorldVoxelQueryResult& voxel, entt::entity entity,
        CubeFace face, const glm::vec2& uv, glm::dvec3 normal) {
        result.hit = true;
        result.status = RaycastResultStatus::Hit;
        result.distance = static_cast<float>(distance);
        result.worldPosition = query.ray.origin + query.ray.direction * result.distance;
        result.voxelPosition = voxel.voxelCoord;
        result.targetEntity = entity;
        result.type = RaycastHitType::Voxel;
        if (glm::dot(glm::vec3(normal), query.ray.direction) > 0.0f) normal = -normal;
        const float normalLength = glm::length(glm::vec3(normal));
        result.faceNormal = normalLength > 0.0f ? glm::vec3(normal) / normalLength : -query.ray.direction;
        result.faceIndex = static_cast<int>(face);
        result.uv = uv;
    };

    // A ray may begin inside a solid voxel; that is a valid zero-distance hit.
    bool sawOutOfBounds = false;
    const auto initial = samplePlanetVoxel(query.ray.origin);
    if (initial.missing) {
        result.status = RaycastResultStatus::Missing;
        result.distance = 0.0f;
        return result;
    } else if (initial.voxel.status == WorldVoxelQueryStatus::SolidVoxel && initial.entity != query.ignoreEntity) {
        makeHit(0.0, initial.voxel, initial.entity, initial.face, initial.uv, -glm::dvec3(query.ray.direction));
        return result;
    } else if (initial.voxel.status == WorldVoxelQueryStatus::TerrainPending) {
        result.status = RaycastResultStatus::Pending;
        result.distance = 0.0f;
        return result;
    } else if (initial.voxel.status == WorldVoxelQueryStatus::InvalidInput) {
        result.status = RaycastResultStatus::InvalidInput;
        result.distance = 0.0f;
        return result;
    }
    sawOutOfBounds = initial.voxel.status == WorldVoxelQueryStatus::OutOfBounds;

    for (size_t i = 0; i + 1 < events.size(); ++i) {
        const double t0 = events[i].t;
        const double t1 = events[i + 1].t;
        if (!(t1 > t0)) continue;
        const double middle = t0 + (t1 - t0) * 0.5;
        const glm::dvec3 sampleD = origin + direction * middle;
        const glm::vec3 sample(static_cast<float>(sampleD.x), static_cast<float>(sampleD.y), static_cast<float>(sampleD.z));
        const auto pointSample = samplePlanetVoxel(sample);
        const auto& voxel = pointSample.voxel;
        if (pointSample.missing) {
            result.status = RaycastResultStatus::Missing;
            result.distance = static_cast<float>(t0);
            return result;
        }
        if (voxel.status == WorldVoxelQueryStatus::TerrainPending) {
            result.status = RaycastResultStatus::Pending;
            result.distance = static_cast<float>(t0);
            return result;
        }
        if (voxel.status == WorldVoxelQueryStatus::InvalidInput) {
            result.status = RaycastResultStatus::InvalidInput;
            return result;
        }
        if (voxel.status == WorldVoxelQueryStatus::OutOfBounds) sawOutOfBounds = true;
        if (voxel.status != WorldVoxelQueryStatus::SolidVoxel) continue;

        const entt::entity entity = pointSample.entity;
        if (entity == query.ignoreEntity) continue;
        if (!registry.valid(entity) || !registry.all_of<VoxelChunkComponent>(entity)) {
            result.status = RaycastResultStatus::Pending;
            result.distance = static_cast<float>(t0);
            return result;
        }
        glm::dvec3 hitNormal = -direction;
        double strongestCrossing = 0.0;
        for (size_t j = i; j > 0 && std::abs(events[j - 1].t - t0) <= 1.0e-10 * std::max(1.0, std::abs(t0)); --j) {
            const auto& candidate = events[j - 1];
            const double crossing = std::abs(DotDouble(direction, candidate.normal));
            if (crossing > strongestCrossing) {
                strongestCrossing = crossing;
                hitNormal = DotDouble(direction, candidate.normal) > 0.0 ? -candidate.normal : candidate.normal;
            }
        }
        for (size_t j = i; j < events.size() && std::abs(events[j].t - t0) <= 1.0e-10 * std::max(1.0, std::abs(t0)); ++j) {
            const auto& candidate = events[j];
            const double crossing = std::abs(DotDouble(direction, candidate.normal));
            if (crossing > strongestCrossing) {
                strongestCrossing = crossing;
                hitNormal = DotDouble(direction, candidate.normal) > 0.0 ? -candidate.normal : candidate.normal;
            }
        }
        makeHit(t0, voxel, entity, pointSample.face, pointSample.uv, hitNormal);
        return result;
    }
    if (sawOutOfBounds) result.status = RaycastResultStatus::OutOfBounds;
    return result;
}

} // namespace

bool RaycastSystem::IntersectAABB(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& aabbMin, const glm::vec3& aabbMax, float& tMin, float& tMax) {
    if (!IsFinite(rayOrigin) || !IsFinite(rayDir) || !IsFinite(aabbMin) || !IsFinite(aabbMax) ||
        glm::any(glm::greaterThan(aabbMin, aabbMax))) return false;

    double nearT = -std::numeric_limits<double>::infinity();
    double farT = std::numeric_limits<double>::infinity();
    for (int axis = 0; axis < 3; ++axis) {
        const double origin = rayOrigin[axis];
        const double direction = rayDir[axis];
        const double minimum = aabbMin[axis];
        const double maximum = aabbMax[axis];
        if (direction == 0.0) {
            if (origin < minimum || origin > maximum) return false;
            continue;
        }
        double axisNear = (minimum - origin) / direction;
        double axisFar = (maximum - origin) / direction;
        if (axisNear > axisFar) std::swap(axisNear, axisFar);
        nearT = std::max(nearT, axisNear);
        farT = std::min(farT, axisFar);
        if (farT < nearT) return false;
    }
    if (farT < 0.0) return false;
    tMin = static_cast<float>(nearT);
    tMax = static_cast<float>(farT);
    return std::isfinite(tMin) || tMin < 0.0f;
}

RaycastHit RaycastSystem::Cast(SharedContext* context, const RaycastQuery& query) {
    RaycastHit invalid;
    invalid.status = RaycastResultStatus::InvalidInput;
    if (!context || !IsFinite(query.ray.origin) || !IsFinite(query.ray.direction) ||
        !std::isfinite(query.maxDistance) || query.maxDistance < 0.0f) return invalid;
    const float directionLength = glm::length(query.ray.direction);
    if (!std::isfinite(directionLength) || directionLength <= 1.0e-8f) return invalid;
    RaycastQuery normalizedQuery = query;
    normalizedQuery.ray.direction /= directionLength;

    RaycastMode mode = query.mode;
    if (mode == RaycastMode::Auto) {
        if (context->engine) {
            GameMode engineMode = context->engine->GetGameMode();
            if (engineMode == GameMode::Play || engineMode == GameMode::ChunkEditor) {
                mode = RaycastMode::Voxel;
            } else if (engineMode == GameMode::PlanetMapper) {
                mode = RaycastMode::Sphere;
            } else {
                mode = RaycastMode::Physics;
            }
        } else {
            mode = RaycastMode::Voxel; // Fallback
        }
    }

    switch (mode) {
        case RaycastMode::Voxel:   return CastVoxel(context, normalizedQuery);
        case RaycastMode::Sphere:  return CastSphere(context, normalizedQuery);
        case RaycastMode::Physics: return CastPhysics(context, normalizedQuery);
        default:                   return invalid;
    }
}

RaycastHit RaycastSystem::CastVoxel(SharedContext* context, const RaycastQuery& query) {
    RaycastHit bestHit;
    bestHit.distance = query.maxDistance;
    bestHit.type = RaycastHitType::Voxel;

    if (!context->forgeWorld) return bestHit;

    auto& registry = context->forgeWorld->GetRegistry();
    auto planetView = registry.view<PlanetGeometryComponent>();
    if (!planetView.empty()) {
        auto planetIt = planetView.begin();
        const auto planetEntity = *planetIt++;
        if (planetIt != planetView.end()) {
            RaycastHit invalid;
            invalid.type = RaycastHitType::Voxel;
            invalid.status = RaycastResultStatus::InvalidInput;
            return invalid;
        }
        const auto& geometry = planetView.get<PlanetGeometryComponent>(planetEntity);
        if (geometry.isLogicalSphere) {
            return CastPlanetaryVoxel(context, query, geometry.planetSize);
        }
    }

    auto chunkView = registry.view<VoxelChunkComponent, TransformComponent>();

    for (auto chunkEnt : chunkView) {
        if (chunkEnt == query.ignoreEntity) continue;
        
        auto& transform = chunkView.get<TransformComponent>(chunkEnt);
        auto& chunkData = chunkView.get<VoxelChunkComponent>(chunkEnt);

        fw::Mat4 cGlobal = transform.computeGlobalMatrix(registry);
        glm::mat4 chunkGlobalMatrix = glm::transpose(*reinterpret_cast<glm::mat4*>(&cGlobal));
        glm::mat4 invTransform = glm::inverse(chunkGlobalMatrix);

        glm::vec3 localOrigin = glm::vec3(invTransform * glm::vec4(query.ray.origin, 1.0f));
        glm::vec3 localDir = glm::vec3(invTransform * glm::vec4(query.ray.direction, 0.0f));
        if (!IsFinite(localOrigin) || !IsFinite(localDir) || glm::dot(localDir, localDir) <= 1.0e-16f) continue;

        glm::vec3 aabbMin(-0.5f, -0.5f, -0.5f);
        glm::vec3 aabbMax(CHUNK_SIZE - 0.5f, CHUNK_HEIGHT - 0.5f, CHUNK_SIZE - 0.5f);

        float tMin = 0.0f, tMax = 0.0f;
        if (!IntersectAABB(localOrigin, localDir, aabbMin, aabbMax, tMin, tMax)) {
            continue;
        }

        const float entryDistance = std::max(0.0f, tMin);
        const float exitDistance = std::min(query.maxDistance, tMax);
        if (entryDistance > exitDistance || entryDistance > bestHit.distance) {
            continue;
        }

        const glm::vec3 ddaOrigin = localOrigin + glm::vec3(0.5f);
        const glm::vec3 entryPoint = ddaOrigin + localDir * entryDistance;
        glm::ivec3 currentPos;
        for (int axis = 0; axis < 3; ++axis) {
            const float value = entryPoint[axis];
            int cell = static_cast<int>(std::floor(value));
            const float nearest = std::round(value);
            if (localDir[axis] < 0.0f && std::abs(value - nearest) <=
                4.0f * std::numeric_limits<float>::epsilon() * std::max(1.0f, std::abs(value))) {
                cell = static_cast<int>(nearest) - 1;
            }
            currentPos[axis] = cell;
        }

        glm::ivec3 step(
            (localDir.x > 0) ? 1 : ((localDir.x < 0) ? -1 : 0),
            (localDir.y > 0) ? 1 : ((localDir.y < 0) ? -1 : 0),
            (localDir.z > 0) ? 1 : ((localDir.z < 0) ? -1 : 0)
        );

        glm::vec3 tDelta(
            (localDir.x != 0) ? std::abs(1.0f / localDir.x) : std::numeric_limits<float>::infinity(),
            (localDir.y != 0) ? std::abs(1.0f / localDir.y) : std::numeric_limits<float>::infinity(),
            (localDir.z != 0) ? std::abs(1.0f / localDir.z) : std::numeric_limits<float>::infinity()
        );

        auto nextCrossing = [&](int axis) {
            if (step[axis] == 0) return std::numeric_limits<float>::infinity();
            const float boundary = static_cast<float>(currentPos[axis] + (step[axis] > 0 ? 1 : 0));
            return (boundary - ddaOrigin[axis]) / localDir[axis];
        };
        glm::vec3 tMaxAxis(nextCrossing(0), nextCrossing(1), nextCrossing(2));
        for (int axis = 0; axis < 3; ++axis) {
            while (tMaxAxis[axis] < entryDistance) tMaxAxis[axis] += tDelta[axis];
        }

        float currentDist = entryDistance;
        glm::ivec3 enteredNormal(0);
        bool found = false;

        while (currentDist <= exitDistance && currentDist <= bestHit.distance) {
            if (currentPos.x < 0 || currentPos.x >= CHUNK_SIZE ||
                currentPos.y < 0 || currentPos.y >= CHUNK_HEIGHT ||
                currentPos.z < 0 || currentPos.z >= CHUNK_SIZE) {
                break;
            }

            if (currentPos.x >= 0 && currentPos.x < CHUNK_SIZE &&
                currentPos.y >= 0 && currentPos.y < CHUNK_HEIGHT &&
                currentPos.z >= 0 && currentPos.z < CHUNK_SIZE) {
                
                BlockType bType = static_cast<BlockType>(chunkData.blocks[currentPos.x][currentPos.y][currentPos.z]);
                if (bType != BlockType::Air && bType != BlockType::Water && bType != BlockType::OutOfBounds) {
                    found = true;
                    break;
                }
            }

            const float nextT = std::min({tMaxAxis.x, tMaxAxis.y, tMaxAxis.z});
            if (!std::isfinite(nextT) || nextT > exitDistance) break;
            const float tieTolerance = 8.0f * std::numeric_limits<float>::epsilon() *
                std::max(1.0f, std::abs(nextT));
            enteredNormal = glm::ivec3(0);
            int selectedAxis = -1;
            for (int axis = 0; axis < 3; ++axis) {
                if (std::abs(tMaxAxis[axis] - nextT) <= tieTolerance) {
                    currentPos[axis] += step[axis];
                    if (selectedAxis < 0 || std::abs(localDir[axis]) > std::abs(localDir[selectedAxis])) {
                        selectedAxis = axis;
                    }
                    tMaxAxis[axis] += tDelta[axis];
                }
            }
            if (selectedAxis >= 0) enteredNormal[selectedAxis] = -step[selectedAxis];
            currentDist = nextT;
        }

        if (found && currentDist <= bestHit.distance) {
            bestHit.hit = true;
            bestHit.status = RaycastResultStatus::Hit;
            bestHit.distance = currentDist;
            bestHit.targetEntity = chunkEnt;
            bestHit.voxelPosition = glm::ivec3(chunkData.cx * CHUNK_SIZE + currentPos.x, currentPos.y, chunkData.cz * CHUNK_SIZE + currentPos.z);
            bestHit.worldPosition = query.ray.origin + query.ray.direction * currentDist;
            glm::vec3 localNormal = glm::vec3(enteredNormal);
            if (glm::dot(localNormal, localNormal) == 0.0f) localNormal = -glm::sign(localDir);
            const glm::mat3 normalMatrix = glm::transpose(glm::mat3(invTransform));
            const glm::vec3 worldNormal = normalMatrix * localNormal;
            const float normalLength = glm::length(worldNormal);
            bestHit.faceNormal = normalLength > 0.0f ? worldNormal / normalLength : -query.ray.direction;
        }
    }

    return bestHit;
}

RaycastHit RaycastSystem::CastSphere(SharedContext* context, const RaycastQuery& query) {
    RaycastHit result;
    result.type = RaycastHitType::Sphere;
    
    // Fallback if not injected properly, though usually it should be set via PlanetMapper context
    glm::vec3 sphereCenter{0.0f};
    float sphereRadius = 50.0f; // Default R
    if (context && context->projectManager) {
        const auto& doc = context->projectManager->GetDocument();
        // Just grab the first planet for now
        if (!doc.planets.empty()) {
            sphereRadius = fw::PlanetMath::GetPlanetRadius(doc.planets[0].planetSize);
        }
    }
    
    glm::vec3 oc = query.ray.origin - sphereCenter;
    float b = glm::dot(oc, query.ray.direction);
    float c = glm::dot(oc, oc) - sphereRadius * sphereRadius;
    float h = b * b - c;
    if (!(h >= 0.0f) || !std::isfinite(h) || !(sphereRadius > 0.0f)) return result;

    h = glm::sqrt(h);
    const float t = -b - h;
    if (t < 0.0f || t > query.maxDistance) return result;
    
    result.hit = true;
    result.status = RaycastResultStatus::Hit;
    result.distance = t;
    result.worldPosition = query.ray.origin + query.ray.direction * t;
    result.faceNormal = glm::normalize(result.worldPosition - sphereCenter);
    
    CubeSphereMapping::DirectionToFaceUV(result.faceNormal, result.faceIndex, result.uv);
    
    return result;
}

RaycastHit RaycastSystem::CastPhysics(SharedContext* context, const RaycastQuery& query) {
    RaycastHit result;
    result.type = RaycastHitType::Physics;
    // Jolt physics raycast integration placeholder
    return result;
}

} // namespace fw
