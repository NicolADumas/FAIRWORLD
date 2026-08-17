#include "JoltPhysicsSystem.h"
#include <iostream>
#include <cstdarg>

using namespace JPH;

namespace fw {

class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
    }

    virtual uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
    virtual BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer inLayer) const override { return mObjectToBroadPhase[inLayer]; }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    virtual const char* GetBroadPhaseLayerName(BroadPhaseLayer inLayer) const override {
        switch ((BroadPhaseLayer::Type)inLayer) {
            case (BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
            case (BroadPhaseLayer::Type)BroadPhaseLayers::MOVING: return "MOVING";
            default: return "INVALID";
        }
    }
#endif

private:
    BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl : public ObjectVsBroadPhaseLayerFilter {
public:
    virtual bool ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const override {
        switch (inLayer1) {
            case Layers::NON_MOVING: return inLayer2 == BroadPhaseLayers::MOVING;
            case Layers::MOVING: return true;
            default: return false;
        }
    }
};

class ObjectLayerPairFilterImpl : public ObjectLayerPairFilter {
public:
    virtual bool ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const override {
        switch (inObject1) {
            case Layers::NON_MOVING: return inObject2 == Layers::MOVING;
            case Layers::MOVING: return true;
            default: return false;
        }
    }
};

static void TraceImpl(const char* inFMT, ...) {
    va_list list;
    va_start(list, inFMT);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), inFMT, list);
    va_end(list);
    std::cout << buffer << std::endl;
}

static BPLayerInterfaceImpl sBroadPhaseLayerInterface;
static ObjectVsBroadPhaseLayerFilterImpl sObjectVsBroadPhaseLayerFilter;
static ObjectLayerPairFilterImpl sObjectLayerPairFilter;

void JoltPhysicsSystem::InitializeGlobals() {
    JPH::RegisterDefaultAllocator();
    JPH::Trace = TraceImpl;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = [](const char *inExpression, const char *inMessage, const char *inFile, uint inLine) {
        std::cerr << inFile << ":" << inLine << ": (" << inExpression << ") " << (inMessage != nullptr? inMessage : "") << std::endl;
        return true;
    };)
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
}

void JoltPhysicsSystem::ShutdownGlobals() {
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

JoltPhysicsSystem::JoltPhysicsSystem() {
    m_tempAllocator = new TempAllocatorImpl(10 * 1024 * 1024);
    m_jobSystem = new JobSystemThreadPool(cMaxPhysicsJobs, cMaxPhysicsBarriers, thread::hardware_concurrency() - 1);
    
    m_physicsSystem = new PhysicsSystem();
    m_physicsSystem->Init(1024, 0, 1024, 1024, sBroadPhaseLayerInterface, sObjectVsBroadPhaseLayerFilter, sObjectLayerPairFilter);
}

JoltPhysicsSystem::~JoltPhysicsSystem() {
    delete m_physicsSystem;
    delete m_jobSystem;
    delete m_tempAllocator;
}

void JoltPhysicsSystem::Update(entt::registry& registry, SharedContext* context, float dt) {
    // Only stepping manual updates if needed for main game state. 
    // Lab uses its own stepping mechanism for fine control.
}

} // namespace fw
