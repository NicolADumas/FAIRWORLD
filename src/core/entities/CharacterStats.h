#pragma once
#include <cmath>
#include <algorithm>

// ═══════════════════════════════════════════════════════════════════════
//  CharacterStats — Componente universale per Player e Mob
//
//  Architettura: Dirty Flag Cache
//  ┌─────────────────────────────────────────────────────────────────┐
//  │  Attributi Primari (privati)  →  ogni setter imposta isDirty   │
//  │  Statistiche Derivate (mutable cache)  →  ricalcolate solo     │
//  │  quando necessario (lazy evaluation). Getter sempre O(1).      │
//  │  Vuoi aggiungere bonus da equipaggiamento o buff temporanei?   │
//  │  Aggiungili dentro RecalculateDerivedStats(), gli attributi    │
//  │  base rimangono intatti e separati.                            │
//  └─────────────────────────────────────────────────────────────────┘
//
//  Sei Attributi Fondamentali:
//    VIT  Vitalità    → HP, Stamina, Poise (resistenza stagger)
//    STR  Forza       → Danno fisico (asce, spade), capacità di carico
//    DEX  Destrezza   → Precisione (Hit), Schivata (Evasion), danno leggero
//    INT  Intelligenza → Danno magico, MP massimi
//    RES  Resistenza  → Difesa magica, resist. status ailment, Stamina
//    LUK  Fortuna     → Critico, Drop Rate, resist. debuff (RNG puro)
// ═══════════════════════════════════════════════════════════════════════

class CharacterStats {

    // ── Attributi Primari — accesso esclusivo via Setter/Getter ────────
    int m_vit  = 10;
    int m_str  = 10;
    int m_dex  = 10;
    int m_int  = 10;
    int m_res  = 10;
    int m_luk  = 10;

    // ── Cache delle Statistiche Derivate (mutable: modificabili in const) ─
    mutable int   m_maxHP          = 0;    // VIT × 15 + 100
    mutable int   m_maxMP          = 0;    // INT × 10 + 50
    mutable int   m_maxStamina     = 0;    // VIT × 4 + RES × 3 + 60

    mutable int   m_physicalAtk    = 0;    // STR × 2  (+arma al momento del colpo)
    mutable int   m_magicalAtk     = 0;    // INT × 2  (+staff al momento del colpo)
    mutable int   m_physicalDef    = 0;    // VIT × 1  (base minimo — armatura aggiunge il grosso)
    mutable int   m_magicalDef     = 0;    // RES × 3  (principale difesa magica)

    mutable float m_hitAccuracy    = 0.0f; // 80% base + DEX × 0.5% (cap 99%)
    mutable float m_evasionRate    = 0.0f; // 2%  base + DEX × 0.3% + LUK × 0.1% (cap 60%)
    mutable float m_critChance     = 0.0f; // 1%  base + DEX × 0.2% + LUK × 0.5% (cap 75%)
    mutable float m_atkSpeed       = 0.0f; // × (1.0 + DEX × 0.01)
    mutable float m_moveSpeed      = 0.0f; // 4.0 + DEX × 0.06  u/s
    mutable float m_dropMultiplier = 0.0f; // 1.0 + LUK / 100  (es. LUK 50 → ×1.50)
    mutable int   m_poise          = 0;    // VIT × 2  (+armatura pesante a runtime)

    mutable bool  m_isDirty = true;

    // ── Ricalcolo interno — O(1) per getter se !isDirty ─────────────────
    void RecalculateDerivedStats() const {
        // Risorse
        m_maxHP      = 100 + (m_vit * 15);
        m_maxMP      = 50  + (m_int * 10);
        m_maxStamina = 60  + (m_vit * 4) + (m_res * 3);

        // Offensiva
        m_physicalAtk = m_str * 2;
        m_magicalAtk  = m_int * 2;

        // Difensiva — fisica leggera da VIT (armatura è separata), magica pesante da RES
        m_physicalDef = m_vit * 1;
        m_magicalDef  = m_res * 3;

        // Azione & Riflessi (con cap per game balance)
        m_hitAccuracy  = (std::min)(0.99f, 0.80f + (m_dex * 0.005f));
        m_evasionRate  = (std::min)(0.60f, 0.02f + (m_dex * 0.003f) + (m_luk * 0.001f));
        m_critChance   = (std::min)(0.75f, 0.01f + (m_dex * 0.002f) + (m_luk * 0.005f));
        m_atkSpeed     = 1.0f + (m_dex * 0.01f);
        m_moveSpeed    = 4.0f + (m_dex * 0.06f);

        // LUK Mechanics
        // dropFinale = dropBase × m_dropMultiplier
        m_dropMultiplier = 1.0f + (static_cast<float>(m_luk) / 100.0f);

        // Poise: quanti danni assorbi prima che l'animazione venga interrotta (stagger)
        m_poise = m_vit * 2;

        m_isDirty = false;
    }

public:
    // ── Progressione (pubblico — necessario per save/load) ───────────────
    int level        = 1;
    int currentExp   = 0;
    int nextLevelExp = 100;

    // ── Stato Corrente (runtime — non serializzato) ──────────────────────
    int currentHP      = 0;
    int currentMP      = 0;
    int currentStamina = 0;

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    //  SETTER — ogni modifica invalida la cache (O(1) lazy)
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    void SetVIT(int v) { m_vit  = (std::max)(1, v); m_isDirty = true; }
    void SetSTR(int v) { m_str  = (std::max)(1, v); m_isDirty = true; }
    void SetDEX(int v) { m_dex  = (std::max)(1, v); m_isDirty = true; }
    void SetINT(int v) { m_int  = (std::max)(1, v); m_isDirty = true; }
    void SetRES(int v) { m_res  = (std::max)(1, v); m_isDirty = true; }
    void SetLUK(int v) { m_luk  = (std::max)(1, v); m_isDirty = true; }

    void AddVIT(int a) { SetVIT(m_vit + a); }
    void AddSTR(int a) { SetSTR(m_str + a); }
    void AddDEX(int a) { SetDEX(m_dex + a); }
    void AddINT(int a) { SetINT(m_int + a); }
    void AddRES(int a) { SetRES(m_res + a); }
    void AddLUK(int a) { SetLUK(m_luk + a); }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    //  GETTER — Attributi Primari (senza ricalcolo)
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    int GetVIT() const { return m_vit; }
    int GetSTR() const { return m_str; }
    int GetDEX() const { return m_dex; }
    int GetINT() const { return m_int; }
    int GetRES() const { return m_res; }
    int GetLUK() const { return m_luk; }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    //  GETTER — Statistiche Derivate (lazy: ricalcola solo se dirty)
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    int   GetMaxHP()          const { if (m_isDirty) RecalculateDerivedStats(); return m_maxHP;          }
    int   GetMaxMP()          const { if (m_isDirty) RecalculateDerivedStats(); return m_maxMP;          }
    int   GetMaxStamina()     const { if (m_isDirty) RecalculateDerivedStats(); return m_maxStamina;     }
    int   GetPhysicalAtk()    const { if (m_isDirty) RecalculateDerivedStats(); return m_physicalAtk;    }
    int   GetMagicalAtk()     const { if (m_isDirty) RecalculateDerivedStats(); return m_magicalAtk;     }
    int   GetPhysicalDef()    const { if (m_isDirty) RecalculateDerivedStats(); return m_physicalDef;    }
    int   GetMagicalDef()     const { if (m_isDirty) RecalculateDerivedStats(); return m_magicalDef;     }
    float GetHitAccuracy()    const { if (m_isDirty) RecalculateDerivedStats(); return m_hitAccuracy;    }
    float GetEvasionRate()    const { if (m_isDirty) RecalculateDerivedStats(); return m_evasionRate;    }
    float GetCritChance()     const { if (m_isDirty) RecalculateDerivedStats(); return m_critChance;     }
    float GetAtkSpeed()       const { if (m_isDirty) RecalculateDerivedStats(); return m_atkSpeed;       }
    float GetMoveSpeed()      const { if (m_isDirty) RecalculateDerivedStats(); return m_moveSpeed;      }
    float GetDropMultiplier() const { if (m_isDirty) RecalculateDerivedStats(); return m_dropMultiplier; }
    int   GetPoise()          const { if (m_isDirty) RecalculateDerivedStats(); return m_poise;          }

    // Danno totale con arma/spell equipaggiata (calcolato al momento del colpo)
    int GetTotalPhysicalDamage(int weaponDamage = 0) const { return GetPhysicalAtk() + weaponDamage; }
    int GetTotalMagicalDamage (int spellPower   = 0) const { return GetMagicalAtk()  + spellPower;   }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    //  MECCANICHE LUK — non lineari, influenzano l'RNG
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    // Probabilità finale drop: dropBase × dropMultiplier
    // Esempio: LUK 50, item con drop 1% → 1.50%
    float CalculateFinalDropRate(float baseDropRate) const {
        return (std::min)(1.0f, baseDropRate * GetDropMultiplier());
    }

    // Mitiga la probabilità di un debuff in arrivo grazie alla fortuna
    // Esempio: baseChance=0.50f, LUK=60 → 0.50 - 0.18 = 32% di essere avvelenato
    float CalculateDebuffResistance(float baseChance) const {
        float lukResist = static_cast<float>(m_luk) * 0.003f; // 0.3% per punto LUK
        return (std::max)(0.0f, baseChance - lukResist);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    //  INIZIALIZZAZIONE
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void Initialize() {
        CalculateNextLevelExp();
        RecalculateDerivedStats();
        currentHP      = m_maxHP;
        currentMP      = m_maxMP;
        currentStamina = m_maxStamina;
    }

    // Factory: crea da 6 attributi primari — usato per istanziare mob a runtime
    // e per la preview live nell'editor ImGui.
    static CharacterStats FromAttributes(
        int lv,
        int vit, int str, int dex,
        int intl, int res, int luk)
    {
        CharacterStats cs;
        cs.level = lv;
        cs.SetVIT(vit);  cs.SetSTR(str);  cs.SetDEX(dex);
        cs.SetINT(intl); cs.SetRES(res);  cs.SetLUK(luk);
        cs.Initialize();
        return cs;
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    //  EXP & LEVEL UP
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    // Ritorna true se è avvenuto almeno un level-up.
    // Il while gestisce l'EXP droppata che porta a più livelli in un colpo.
    bool AddExperience(int amount) {
        currentExp += amount;
        bool leveledUp = false;
        while (currentExp >= nextLevelExp) { LevelUp(); leveledUp = true; }
        return leveledUp;
    }

    void LevelUp() {
        currentExp -= nextLevelExp;
        level++;
        // Crescita base automatica — in futuro il Player avrà punti da distribuire
        AddVIT(2); AddSTR(1); AddDEX(1); AddINT(1); AddRES(1); AddLUK(1);
        CalculateNextLevelExp();
        // Cura completa al level-up (tipico dei JRPG)
        currentHP      = GetMaxHP();
        currentMP      = GetMaxMP();
        currentStamina = GetMaxStamina();
    }

    // Curva esponenziale: EXP = 100 × level^1.5
    //   Lv  2 →   282 EXP  |  Lv  5 →  1118 EXP
    //   Lv 10 →  3162 EXP  |  Lv 50 → 35355 EXP
    void CalculateNextLevelExp() {
        nextLevelExp = static_cast<int>(100.0f * std::pow(static_cast<float>(level), 1.5f));
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    //  COMBATTIMENTO
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    // Danno fisico in entrata: ridotto da PhysicalDef. Minimo garantito: 1.
    int TakePhysicalDamage(int incomingDamage) {
        int effective = (std::max)(1, incomingDamage - GetPhysicalDef());
        currentHP = (std::max)(0, currentHP - effective);
        return effective;
    }

    // Danno magico in entrata: ridotto da MagicalDef.
    int TakeMagicalDamage(int incomingDamage) {
        int effective = (std::max)(1, incomingDamage - GetMagicalDef());
        currentHP = (std::max)(0, currentHP - effective);
        return effective;
    }

    void HealHP    (int amount) { currentHP      = (std::min)(GetMaxHP(),      currentHP      + amount); }
    void RestoreMP (int amount) { currentMP      = (std::min)(GetMaxMP(),      currentMP      + amount); }
    void UseStamina(int amount) { currentStamina = (std::max)(0,               currentStamina - amount); }
    void RegainStamina(int a)   { currentStamina = (std::min)(GetMaxStamina(), currentStamina + a);      }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    //  UTILITY
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    bool IsAlive()      const { return currentHP > 0; }
    float GetHPPercent() const {
        int mx = GetMaxHP();
        return mx > 0 ? static_cast<float>(currentHP) / mx : 0.0f;
    }
    float GetMPPercent() const {
        int mx = GetMaxMP();
        return mx > 0 ? static_cast<float>(currentMP) / mx : 0.0f;
    }
    float GetExpPercent() const {
        return nextLevelExp > 0 ? static_cast<float>(currentExp) / nextLevelExp : 0.0f;
    }
};
