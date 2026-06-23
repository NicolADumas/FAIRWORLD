#pragma once
#include <string>
#include <vector>
#include <windows.h> // Per FILETIME (hot-reload)

// ─────────────────────────────────────────────────────────────────
//  BlockDef — invariato
// ─────────────────────────────────────────────────────────────────
struct BlockDef {
    int         id;
    std::string name;
    std::string tex_top, tex_side, tex_bottom;
    float       hardness;
    bool        transparent;
    float       alpha;
    
    // Physics and Liquids (Phase 2)
    bool        isSolid = true;
    bool        isLiquid = false;
    int         damagePerSecond = 0;
};

// ─────────────────────────────────────────────────────────────────
//  MobTemplate — struttura gerarchica completa
// ─────────────────────────────────────────────────────────────────

// 1. Statistiche RPG — Sei Attributi Primari
//    I valori configurati nel JSON inizializzano CharacterStats a runtime.
//    Le statistiche derivate (HP max, danno, ecc.) si calcolano da questi.
struct MobStats {
    int level = 1;

    // Attributi Primari (specchiati in CharacterStats)
    int vit  = 10;   // Vitalità    → HP, Stamina, Poise
    int str  = 10;   // Forza       → Danno Fisico
    int dex  = 10;   // Destrezza   → Precisione, Schivata, Vel. Attacco
    int intl = 10;   // Intelligenza → Danno Magico, MP
    int res  = 10;   // Resistenza  → Difesa Magica, resist. status
    int luk  = 10;   // Fortuna     → Critico, Drop Rate, resist. debuff

    // Ricompense alla morte
    int         expYield    = 20;
    std::string dropTableID = "";     // Es. "loot_goblin_tier1"
};

// 2. Parametri AI e sensi
struct MobAIParameters {
    float walkSpeed       = 2.0f;   // Velocità in stato PATROL
    float runSpeed        = 5.0f;   // Velocità in stato CHASE
    float turnSpeed       = 8.0f;   // Velocità di rotazione (gradi/sec, usata per slerp)
    float detectionRadius = 10.0f;  // Raggio di rilevamento
    float fieldOfView     = 120.0f; // Angolo di visuale in gradi
    float loseSightRadius = 15.0f;  // Distanza limite per rinunciare all'inseguimento
    float attackRange     = 1.5f;   // Distanza in cui scatta lo stato ATTACK
    float attackCooldown  = 1.5f;   // Secondi tra un attacco e l'altro
    std::string behavior  = "idle"; // "idle", "patrol", "chase_player", "flee_player"
};

// 3. Fisica e collider (capsule)
struct MobPhysics {
    float colliderRadius    = 0.4f;   // Raggio della capsula di collisione
    float colliderHeight    = 1.8f;   // Altezza della capsula
    float colliderOffsetY   = 0.0f;   // Offset verticale dal pivot del modello
    float mass              = 70.0f;  // Massa in kg (per knockback)
    float knockbackResistance = 0.5f; // 0.0 = vola via, 1.0 = immobile
};

// 4. Risorse grafiche e audio
struct MobResources {
    std::string modelPath            = "";
    std::string texturePath          = "";
    std::string animatorControllerID = "humanoid_default"; // Set di animazioni
    std::string onHitSound           = "";
    std::string onDeathSound         = "";
};

// 5. Metadati di identificazione
struct MobTemplate {
    // Core
    std::string id          = "";           // ID univoco (es. "goblin_warrior_01")
    std::string displayName = "New Mob";    // Nome mostrato in UI
    std::string faction     = "Monster";    // "Monster", "Undead", "NPC", "Friendly"

    // Sub-componenti
    MobStats         stats;
    MobAIParameters  ai;
    MobPhysics       physics;
    MobResources     resources;
};

// Alias di compatibilità: il vecchio nome MobDef ora punta a MobTemplate
using MobDef = MobTemplate;

// ─────────────────────────────────────────────────────────────────
//  AssetManager
// ─────────────────────────────────────────────────────────────────
class AssetManager {
public:
    bool LoadAll(const std::string& dir);
    void SaveBlocksJson();
    void SaveMobsJson();

    // Hot-reload: controlla se mobs.json è stato modificato dall'esterno.
    // Ritorna true se c'è stato un reload. Cheap (solo stat del file).
    bool CheckAndReloadMobs();

    std::vector<BlockDef>&    GetBlocks() { return m_blocks; }
    std::vector<MobTemplate>& GetMobs()   { return m_mobs;   }

    BlockDef*    GetBlock(int id);
    MobTemplate* GetMob(int id);
    MobTemplate* GetMobByID(const std::string& id) {
        for (auto& mob : m_mobs) {
            if (mob.id == id) return &mob;
        }
        return nullptr;
    }

    bool SaveTexturePNG(const std::string& filename, int width, int height, const void* pixels);

private:
    std::string              m_baseDir;
    std::vector<BlockDef>    m_blocks;
    std::vector<MobTemplate> m_mobs;
    FILETIME                 m_mobsFileTime = {}; // Timestamp ultimo caricamento (hot-reload)

    bool LoadBlocksJson();
    bool LoadMobsJson();
};
