#include "pch.h"
#define GLM_ENABLE_EXPERIMENTAL
#include "MobManager.h"
#include "Player.h"
#include "World.h"
#include <glm/gtx/norm.hpp>
#include <iostream>
#include <algorithm>

void MobManager::Spawn(const MobTemplate& tmpl, glm::vec3 pos) {
    instances.push_back(MobInstance::FromTemplate(tmpl, pos));
    std::cout << "[MOB] Spawno " << tmpl.displayName << " in (" << pos.x << ", " << pos.y << ", " << pos.z << ")" << std::endl;
}

void MobManager::DespawnAll() {
    instances.clear();
    std::cout << "[MOB] Despawnati tutti i mob." << std::endl;
}

int MobManager::Raycast(glm::vec3 rayOrigin, glm::vec3 rayDir, float maxDist) {
    int closestIndex = -1;
    float closestDist = maxDist;

    for (size_t i = 0; i < instances.size(); i++) {
        if (!instances[i].isAlive) continue;

        // Hitbox sferica attorno alla testa/busto del mob (per semplicità)
        glm::vec3 targetCenter = instances[i].position + glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 toTarget = targetCenter - rayOrigin;
        
        float projection = glm::dot(toTarget, rayDir);
        if (projection < 0.0f) continue; // Dietro il raggio
        
        glm::vec3 closestPoint = rayOrigin + rayDir * projection;
        float d2 = glm::distance2(targetCenter, closestPoint);
        
        // Raggio hitbox di circa 0.6 metri
        if (d2 <= 0.6f * 0.6f) {
            float dist = glm::distance(rayOrigin, targetCenter);
            if (dist < closestDist) {
                closestDist = dist;
                closestIndex = (int)i;
            }
        }
    }
    return closestIndex;
}

void MobManager::UpdateMobs(float deltaTime, glm::vec3 playerPos, Player& player, AssetManager& assets, World& world) {
    for (auto& mob : instances) {
        if (!mob.isAlive) continue;

        // Recupera i parametri dell'AI dal template originale
        MobTemplate* tmpl = assets.GetMob(0); // Fallback iniziale
        for (auto& t : assets.GetMobs()) {
            if (t.id == mob.templateID) {
                tmpl = &t;
                break;
            }
        }
        if (!tmpl) continue;

        // Gestione Cooldown Attacchi
        if (mob.attackCooldownTimer > 0.0f) {
            mob.attackCooldownTimer -= deltaTime;
        }

        glm::vec3 dir = playerPos - mob.position;
        // Calcolo della distanza in 3D
        float distance = glm::length(dir);

        // AI Behavior: chase_player o patrol
        // Se il comportamento nel json è "chase_player" o se rileva il player entro il raggio d'azione
        bool isChasing = (mob.behavior == "chase_player") || (distance < tmpl->ai.detectionRadius);

        if (isChasing && player.stats.IsAlive()) {
            // Se fuori dal range di attacco, si muove verso il player
            if (distance > tmpl->ai.attackRange) {
                // Calcola direzione orizzontale
                glm::vec3 moveDir = glm::normalize(glm::vec3(dir.x, 0.0f, dir.z));
                // Velocità basata sul runSpeed del template
                mob.position.x += moveDir.x * tmpl->ai.runSpeed * deltaTime;
                mob.position.z += moveDir.z * tmpl->ai.runSpeed * deltaTime;
            }
            // Se a portata di attacco ed è pronto, colpisce il player!
            else if (mob.attackCooldownTimer <= 0.0f) {
                int incomingDmg = mob.stats.GetPhysicalAtk();
                int finalDmg = player.stats.TakePhysicalDamage(incomingDmg);
                mob.attackCooldownTimer = tmpl->ai.attackCooldown;
                std::cout << "[COMBAT] " << mob.displayName << " infligge " << finalDmg 
                          << " danni fisici al Player! (HP Player: " << player.stats.currentHP << "/" << player.stats.GetMaxHP() << ")" << std::endl;
            }
        }
        
        // --- GRAVITA E COLLISIONE COL TERRENO ---
        mob.velocity.y -= 20.0f * deltaTime;
        mob.position.y += mob.velocity.y * deltaTime;

        int bx = (int)floor(mob.position.x);
        int by = (int)floor(mob.position.y);
        int bz = (int)floor(mob.position.z);

        if (world.GetBlock(bx, by, bz) != BlockType::Air) {
            mob.position.y = (float)(by + 1);
            mob.velocity.y = 0.0f;
        }

        // Kill-plane: if mob falls off the world
        if (mob.position.y < -100.0f) {
            mob.isAlive = false;
            std::cout << "[MOB] " << mob.displayName << " e' caduto nel vuoto." << std::endl;
        }
    }
}
