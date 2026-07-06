#pragma once
#include "System.h"
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

namespace fw {

namespace Layers {
    static constexpr uint16_t NON_MOVING = 0;
    static constexpr uint16_t MOVING = 1;
    static constexpr uint16_t NUM_LAYERS = 2;
}

class JoltPhysicsSystem : public System {
public:
    static void InitializeGlobals();
    static void ShutdownGlobals();

    JoltPhysicsSystem();
    ~JoltPhysicsSystem();

    void Update(entt::registry& registry, SharedContext* context, float dt) override;

    JPH::PhysicsSystem* GetSystem() { return m_physicsSystem; }
    JPH::TempAllocatorImpl* GetTempAllocator() { return m_tempAllocator; }
    JPH::JobSystemThreadPool* GetJobSystem() { return m_jobSystem; }

private:
    JPH::PhysicsSystem* m_physicsSystem = nullptr;
    JPH::TempAllocatorImpl* m_tempAllocator = nullptr;
    JPH::JobSystemThreadPool* m_jobSystem = nullptr;
};

} // namespace fw
