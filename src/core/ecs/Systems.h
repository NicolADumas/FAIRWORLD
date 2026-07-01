#pragma once
#include "System.h"

namespace fw {
    class CameraSystem : public System {
    public:
        void Update(entt::registry& registry, SharedContext* context, float dt) override;
    };

    class PlayerMovementSystem : public System {
    public:
        void Update(entt::registry& registry, SharedContext* context, float dt) override;
    };

    class PhysicsSystem : public System {
    public:
        void Update(entt::registry& registry, SharedContext* context, float dt) override;
    };

    class CameraSyncSystem : public System {
    public:
        void Update(entt::registry& registry, SharedContext* context, float dt) override;
    };
}
