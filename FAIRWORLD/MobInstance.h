#pragma once
#include "AssetManager.h"
#include "CharacterStats.h"
#include <glm/glm.hpp>

struct MobInstance {
    std::string templateID;
    std::string displayName;
    std::string faction;
    CharacterStats stats;
    glm::vec3 position;
    glm::vec3 velocity = glm::vec3(0.0f);
    bool isAlive = true;
    float attackCooldownTimer = 0.0f;
    std::string behavior; // Copiato da ai.behavior

    static MobInstance FromTemplate(const MobTemplate& tmpl, glm::vec3 pos) {
        MobInstance inst;
        inst.templateID = tmpl.id;
        inst.displayName = tmpl.displayName;
        inst.faction = tmpl.faction;
        
        // Inizializza statistiche RPG basate sul template
        inst.stats = CharacterStats::FromAttributes(
            tmpl.stats.level,
            tmpl.stats.vit,
            tmpl.stats.str,
            tmpl.stats.dex,
            tmpl.stats.intl,
            tmpl.stats.res,
            tmpl.stats.luk
        );
        inst.stats.nextLevelExp = 999999; // I mob non livellano da soli in questo modo
        inst.stats.currentHP = inst.stats.GetMaxHP();

        inst.position = pos;
        inst.isAlive = true;
        inst.attackCooldownTimer = 0.0f;
        inst.behavior = tmpl.ai.behavior;
        return inst;
    }
};
