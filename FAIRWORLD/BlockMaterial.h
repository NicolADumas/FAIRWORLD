#pragma once
#include "World.h" // Per BlockType

// =====================================================================
// BlockMaterial — Proprietà fisiche ingegneristiche di ogni materiale
// =====================================================================
// Ogni blocco in FAIRWORLD occupa 1 m³ di volume esterno.
// Le proprietà qui definite governano:
//   - Dinamica rotazionale e traslazionale (massa, momento d'inerzia)
//   - Fluidodinamica (spinta di Archimede: F_A = ρ_fluido * g * V)
//   - Termodinamica (capacità termica: Q = m * c * ΔT)
//   - Meccanica dello scavo (sforzo di taglio: τ = F / A)
// =====================================================================

struct BlockMaterial {
    float density;          // ρ [kg/m³] — massa volumica del materiale
    float mass;             // m [kg] — massa di un blocco 1m³ (= density * 1.0 m³)
    float heatCapacitySp;   // c [J/(kg·K)] — capacità termica specifica
    float yieldStrength;    // τ_yield [MPa] — sforzo di taglio al limite di snervamento
    float miningTime;       // t [s] — tempo base per rompere il blocco a mani nude
    bool  isFlammable;      // Può bruciare (legno, foglie...)
    bool  floatsInWater;    // Galleggia? (density < ρ_acqua = 1000 kg/m³)
    const char* name;       // Nome leggibile per debug/UI
};

// Lookup table statica — zero overhead, indicizzata per (uint8_t)BlockType
// I valori sono basati su dati ingegneristici reali dove possibile.
inline const BlockMaterial& GetBlockMaterial(BlockType type) {
    //                              ρ        m       c       τ_yield  t_mine  flame  float   name
    static const BlockMaterial MATERIALS[] = {
        /* Air            0 */ {   0.0f,    0.0f,    0.0f,   0.0f,   0.0f,  false, false, "Air"           },
        /* Grass          1 */ { 1200.0f, 1200.0f, 1800.0f,  0.05f,  0.8f,  false, false, "Grass"         },
        /* Dirt           2 */ { 1500.0f, 1500.0f,  900.0f,  0.10f,  0.6f,  false, false, "Dirt"          },
        /* Stone          3 */ { 2700.0f, 2700.0f,  790.0f, 50.00f,  4.5f,  false, false, "Stone"         },
        /* Wood           4 */ {  600.0f,  600.0f, 1700.0f,  5.00f,  2.0f,   true,  true, "Wood"          },
        /* Sand           5 */ { 1600.0f, 1600.0f,  830.0f,  0.02f,  0.4f,  false, false, "Sand"          },
        /* Water          6 */ { 1000.0f, 1000.0f, 4186.0f,  0.00f,  0.0f,  false, false, "Water"         },
        /* Lava           7 */ { 2600.0f, 2600.0f, 1600.0f,  0.00f,  0.0f,  false, false, "Lava"          },
        /* Leaves         8 */ {  200.0f,  200.0f, 1400.0f,  0.01f,  0.3f,   true,  true, "Leaves"        },
        /* MobSpawner     9 */ { 3000.0f, 3000.0f,  500.0f,100.00f, 10.0f,  false, false, "MobSpawner"    },
        /* LightSource   10 */ {  800.0f,  800.0f, 1000.0f,  0.50f,  0.5f,  false,  true, "LightSource"   },
        /* Mushroom      11 */ {  300.0f,  300.0f, 1500.0f,  0.01f,  0.2f,  false,  true, "Mushroom"      },
        /* Ore           12 */ { 5000.0f, 5000.0f,  450.0f, 80.00f,  6.0f,  false, false, "Ore"           },
        /* Ice           13 */ {  917.0f,  917.0f, 2090.0f,  1.00f,  1.5f,  false,  true, "Ice"           },
        /* StargateFrame 14 */ { 8000.0f, 8000.0f,  200.0f,999.00f, -1.0f,  false, false, "StargateFrame" },
        /* StargatePortal15 */ {    0.0f,    0.0f,    0.0f,  0.00f, -1.0f,  false, false, "StargatePortal"},
    };

    uint8_t idx = (uint8_t)type;
    if (idx >= sizeof(MATERIALS) / sizeof(MATERIALS[0])) idx = 0;
    return MATERIALS[idx];
}

// Costanti fisiche universali usate dal motore
namespace PhysicsConstants {
    constexpr float RHO_WATER   = 1000.0f;  // ρ acqua [kg/m³]
    constexpr float RHO_AIR     = 1.225f;   // ρ aria [kg/m³]
    constexpr float BLOCK_VOLUME = 1.0f;    // Volume di un blocco [m³]
}
