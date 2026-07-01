#pragma once
#include "System.h"

namespace fw {
    class JoltPhysicsSystem : public System {
    public:
        static void InitializeGlobals() {}
        static void ShutdownGlobals() {}
        void Update(entt::registry& registry, SharedContext* context, float dt) override {}
    };
}
