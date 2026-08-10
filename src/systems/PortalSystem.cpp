#include "pch.h"
#include "PortalSystem.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace fw {

// Helper: converte da Mat4 a glm::mat4
static glm::mat4 ToGlm(const Mat4& m) {
    glm::mat4 r;
    for(int i = 0; i < 4; i++) {
        for(int j = 0; j < 4; j++) {
            r[j][i] = m.m[i][j]; // Trasposta per il memory layout di GLM (col-major)
        }
    }
    return r;
}

// Helper: converte da glm::mat4 a Mat4
static Mat4 ToMat4(const glm::mat4& g) {
    Mat4 r;
    for(int i = 0; i < 4; i++) {
        for(int j = 0; j < 4; j++) {
            r.m[i][j] = g[j][i];
        }
    }
    return r;
}

void PortalSystem::UpdatePortals(entt::registry& registry) {
    auto view = registry.view<PortalComponent, TransformComponent>();
    for (auto entity : view) {
        auto& portal = view.get<PortalComponent>(entity);
        if (!portal.isActive || portal.targetPortal == entt::null) continue;

        // Se il target non esiste o non ha un transform, salta
        if (!registry.valid(portal.targetPortal) || !registry.all_of<TransformComponent>(portal.targetPortal)) {
            continue;
        }

        const auto& transIn = view.get<TransformComponent>(entity);
        const auto& transOut = registry.get<TransformComponent>(portal.targetPortal);

        // Calcola M_teleport = matrixOut * inverse(matrixIn)
        glm::mat4 invMatrixIn = glm::inverse(ToGlm(transIn.worldMatrix()));
        glm::mat4 matrixOut   = ToGlm(transOut.worldMatrix());
        
        // Offset di rotazione di 180 gradi sull'asse Y per "uscire" nella direzione giusta
        // altrimenti il giocatore entra e continua a guardare verso il portale di uscita
        glm::mat4 rotation180 = glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        
        glm::mat4 mTeleport = matrixOut * rotation180 * invMatrixIn;
        portal.mTeleport = ToMat4(mTeleport);
    }
}

bool PortalSystem::CheckAndTeleport(Vec3& position, Vec3& velocity, const Vec3& lastPosition, const PortalComponent& portal, const TransformComponent& portalTransform) {
    if (!portal.isActive) return false;

    // Controllo semplificato: distanza dal centro
    // In un sistema reale, si fa il raycast dal lastPosition al position contro il piano del portale
    Vec3 diff = position - portalTransform.location;
    float distSq = diff.dot(diff);
    
    // Se siamo vicinissimi al centro del portale (meno di 1 metro)
    if (distSq < 1.0f) {
        // Applica M_teleport
        glm::mat4 mTel = ToGlm(portal.mTeleport);
        glm::vec4 newPos = mTel * glm::vec4(position.x, position.y, position.z, 1.0f);
        
        // Aggiorna posizione
        position.x = newPos.x;
        position.y = newPos.y;
        position.z = newPos.z;
        
        // Trasforma anche la velocità ruotandola
        glm::mat3 rotTel = glm::mat3(mTel);
        glm::vec3 newVel = rotTel * glm::vec3(velocity.x, velocity.y, velocity.z);
        
        velocity.x = newVel.x;
        velocity.y = newVel.y;
        velocity.z = newVel.z;
        
        return true;
    }
    
    return false;
}

} // namespace fw
