#pragma once
#include <entt/entt.hpp>
#include "ForgeComponents.h"

namespace fw {

class PortalSystem {
public:
    // Aggiorna le matrici M_teleport di tutti i portali
    static void UpdatePortals(entt::registry& registry);

    // Controlla se la posizione è passata attraverso il piano del portale
    // Restituisce true e trasforma la posizione e la velocità se il passaggio è avvenuto
    static bool CheckAndTeleport(Vec3& position, Vec3& velocity, const Vec3& lastPosition, const PortalComponent& portal, const TransformComponent& portalTransform);
};

} // namespace fw
