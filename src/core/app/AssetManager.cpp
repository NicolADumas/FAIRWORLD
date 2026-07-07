#include "pch.h"
#include "AssetManager.h"
#include <fstream>
#include <iostream>
#include "json.hpp" // nlohmann/json locale

#pragma warning(disable: 4996)
#define _CRT_SECURE_NO_WARNINGS
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────
//  Helper: legge il timestamp di scrittura di un file
// ─────────────────────────────────────────────────────────────────
static FILETIME GetFileWriteTime(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data))
        return data.ftLastWriteTime;
    FILETIME zero = {};
    return zero;
}

// ─────────────────────────────────────────────────────────────────
//  LoadAll
// ─────────────────────────────────────────────────────────────────
bool AssetManager::LoadAll(const std::string& directory) {
    m_baseDir = directory;
    m_blocks.clear();
    m_mobs.clear();

    // Caricamento Blocchi
    std::string blocksPath = directory + "definitions/blocks.json";
    std::ifstream blocksFile(blocksPath);
    if (blocksFile.is_open()) {
        try {
            json j;
            blocksFile >> j;
            for (const auto& item : j["blocks"]) {
                BlockDef def;
                def.id          = item["id"];
                def.name        = item["name"];
                def.tex_top     = item["tex_top"];
                def.tex_side    = item["tex_side"];
                def.tex_bottom  = item["tex_bottom"];
                def.hardness    = item["hardness"];
                def.transparent = item["transparent"];
                def.alpha       = item["alpha"];
                
                if (item.contains("isSolid")) def.isSolid = item["isSolid"];
                if (item.contains("isLiquid")) def.isLiquid = item["isLiquid"];
                if (item.contains("damagePerSecond")) def.damagePerSecond = item["damagePerSecond"];
                
                m_blocks.push_back(def);
            }
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Errore di parsing in blocks.json: " << e.what() << std::endl;
        }
        blocksFile.close();
    } else {
        std::cerr << "[ERROR] Impossibile caricare: " << blocksPath << std::endl;
        return false;
    }

    // Caricamento Mob
    if (!LoadMobsJson()) {
        std::cerr << "[ASSETS] Nessun mobs.json trovato, lista mob vuota.\n";
    }

    // Caricamento Biomi
    if (!LoadBiomesJson()) {
        std::cerr << "[ASSETS] Nessun biomes.json trovato, uso biomi di default.\n";
        BiomeDef def;
        m_biomes.push_back(def);
    }

    std::cout << "[ASSETS] Caricati " << m_blocks.size() << " blocchi, "
              << m_mobs.size() << " mob e " << m_biomes.size() << " biomi.\n";
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  LoadMobsJson  (interno — usato anche dall'hot-reload)
// ─────────────────────────────────────────────────────────────────
bool AssetManager::LoadMobsJson() {
    std::string mobsPath = m_baseDir + "definitions/mobs.json";
    std::ifstream mobsFile(mobsPath);
    if (!mobsFile.is_open()) return false;

    json j;
    try {
        mobsFile >> j;
        mobsFile.close();

        m_mobs.clear();
        for (const auto& item : j["mobs"]) {
            MobTemplate mob;

            // ── Core ─────────────────────────────────────────────────
            if (item.contains("id")) {
                if (item["id"].is_string()) mob.id = item["id"].get<std::string>();
                else if (item["id"].is_number()) mob.id = std::to_string(item["id"].get<int>());
            }
            mob.displayName = item.value("displayName", item.value("name", "Unknown"));
            mob.faction     = item.value("faction",     "Monster");

            // Fallback piatto per risorse
            mob.resources.modelPath = item.value("modelPath", "");
            mob.resources.texturePath = item.value("texturePath", "");

            // ── Stats ─────────────────────────────────────────────────
            if (item.contains("stats")) {
                auto& s = item["stats"];
                mob.stats.level    = s.value("level",    1);
                mob.stats.vit      = s.value("vit",      10);
                mob.stats.str      = s.value("str",      10);
                mob.stats.dex      = s.value("dex",      10);
                mob.stats.intl     = s.value("intl",     10);
                mob.stats.res      = s.value("res",      10);
                mob.stats.luk      = s.value("luk",      10);
                mob.stats.expYield    = s.value("expYield",    20);
                mob.stats.dropTableID = s.value("dropTableID", "");
            }

            // ── AI ────────────────────────────────────────────────────
            if (item.contains("ai")) {
                auto& a = item["ai"];
                mob.ai.walkSpeed        = a.value("walkSpeed",        2.0f);
                mob.ai.runSpeed         = a.value("runSpeed",         5.0f);
                mob.ai.turnSpeed        = a.value("turnSpeed",        8.0f);
                mob.ai.detectionRadius  = a.value("detectionRadius",  10.0f);
                mob.ai.fieldOfView      = a.value("fieldOfView",      120.0f);
                mob.ai.loseSightRadius  = a.value("loseSightRadius",  15.0f);
                mob.ai.attackRange      = a.value("attackRange",      1.5f);
                mob.ai.attackCooldown   = a.value("attackCooldown",   1.5f);
                mob.ai.behavior         = a.value("behavior",         "idle");
            } else {
                mob.ai.behavior = item.value("behavior", "idle");
            }

            // ── Physics ───────────────────────────────────────────────
            if (item.contains("physics")) {
                auto& p = item["physics"];
                mob.physics.colliderRadius      = p.value("colliderRadius",      0.4f);
                mob.physics.colliderHeight      = p.value("colliderHeight",      1.8f);
                mob.physics.colliderOffsetY     = p.value("colliderOffsetY",     0.0f);
                mob.physics.mass                = p.value("mass",                70.0f);
                mob.physics.knockbackResistance = p.value("knockbackResistance", 0.5f);
            }

            // ── Resources ─────────────────────────────────────────────
            if (item.contains("resources")) {
                auto& r = item["resources"];
                mob.resources.modelPath            = r.value("modelPath",            mob.resources.modelPath);
                mob.resources.texturePath          = r.value("texturePath",          mob.resources.texturePath);
                mob.resources.animatorControllerID = r.value("animatorControllerID", "humanoid_default");
                mob.resources.onHitSound           = r.value("onHitSound",           "");
                mob.resources.onDeathSound         = r.value("onDeathSound",         "");
            }

            m_mobs.push_back(mob);
        }
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Errore di parsing in mobs.json: " << e.what() << std::endl;
        mobsFile.close();
        return false;
    }

    // Aggiorna il timestamp per il prossimo controllo hot-reload
    m_mobsFileTime = GetFileWriteTime(mobsPath);
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  CheckAndReloadMobs  — hot-reload via file timestamp
// ─────────────────────────────────────────────────────────────────
bool AssetManager::CheckAndReloadMobs() {
    std::string mobsPath = m_baseDir + "definitions/mobs.json";
    FILETIME currentTime = GetFileWriteTime(mobsPath);

    // CompareFileTime: 0 = uguale, -1 = primo è precedente, 1 = primo è più recente
    if (CompareFileTime(&currentTime, &m_mobsFileTime) != 0) {
        std::cout << "[HOT-RELOAD] mobs.json modificato — ricaricamento...\n";
        if (LoadMobsJson()) {
            std::cout << "[HOT-RELOAD] Ricaricati " << m_mobs.size() << " mob.\n";
            return true;
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────
//  SaveBlocksJson
// ─────────────────────────────────────────────────────────────────
void AssetManager::SaveBlocksJson() {
    std::string filepath = m_baseDir + "definitions/blocks.json";
    json j;
    j["blocks"] = json::array();
    for (const auto& block : m_blocks) {
        j["blocks"].push_back({
            {"id",          block.id},
            {"name",        block.name},
            {"tex_top",     block.tex_top},
            {"tex_side",    block.tex_side},
            {"tex_bottom",  block.tex_bottom},
            {"hardness",    block.hardness},
            {"transparent", block.transparent},
            {"alpha",       block.alpha},
            {"isSolid",     block.isSolid},
            {"isLiquid",    block.isLiquid},
            {"damagePerSecond", block.damagePerSecond}
        });
    }

    std::ofstream file(filepath);
    if (file.is_open()) {
        file << j.dump(4);
        file.close();
        std::cout << "[ASSETS] blocks.json salvato in " << filepath << "\n";
    } else {
        std::cerr << "[ASSETS] Errore nel salvare " << filepath << "\n";
    }
}

// ─────────────────────────────────────────────────────────────────
//  LoadBiomesJson
// ─────────────────────────────────────────────────────────────────
bool AssetManager::LoadBiomesJson() {
    std::string path = m_baseDir + "definitions/biomes.json";
    std::ifstream file(path);
    if (!file.is_open()) return false;

    json j;
    try {
        file >> j;
        file.close();

        m_biomes.clear();
        for (const auto& item : j["biomes"]) {
            BiomeDef def;
            def.id = item.value("id", "biome_default");
            def.name = item.value("name", "Nuovo Bioma");
            def.minTemperature = item.value("minTemperature", 0.0f);
            def.maxTemperature = item.value("maxTemperature", 1.0f);
            def.minHumidity = item.value("minHumidity", 0.0f);
            def.maxHumidity = item.value("maxHumidity", 1.0f);
            def.minHeight = item.value("minHeight", 0.0f);
            def.maxHeight = item.value("maxHeight", 1.0f);
            def.surfaceBlockId = item.value("surfaceBlockId", 1);
            def.subsurfaceBlockId = item.value("subsurfaceBlockId", 2);
            def.perlinFrequency = item.value("perlinFrequency", 0.02f);
            m_biomes.push_back(def);
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Errore di parsing in biomes.json: " << e.what() << std::endl;
        return false;
    }
}

// ─────────────────────────────────────────────────────────────────
//  SaveBiomesJson
// ─────────────────────────────────────────────────────────────────
void AssetManager::SaveBiomesJson() {
    json j;
    j["biomes"] = json::array();

    for (const auto& b : m_biomes) {
        json item;
        item["id"] = b.id;
        item["name"] = b.name;
        item["minTemperature"] = b.minTemperature;
        item["maxTemperature"] = b.maxTemperature;
        item["minHumidity"] = b.minHumidity;
        item["maxHumidity"] = b.maxHumidity;
        item["minHeight"] = b.minHeight;
        item["maxHeight"] = b.maxHeight;
        item["surfaceBlockId"] = b.surfaceBlockId;
        item["subsurfaceBlockId"] = b.subsurfaceBlockId;
        item["perlinFrequency"] = b.perlinFrequency;
        j["biomes"].push_back(item);
    }

    std::string path = m_baseDir + "definitions/biomes.json";
    std::ofstream file(path);
    if (file.is_open()) {
        file << j.dump(4);
        file.close();
        std::cout << "[ASSETS] Biomi salvati in: " << path << std::endl;
    } else {
        std::cerr << "[ERROR] Impossibile salvare " << path << std::endl;
    }
}

// ─────────────────────────────────────────────────────────────────
//  SaveMobsJson
// ─────────────────────────────────────────────────────────────────
void AssetManager::SaveMobsJson() {
    std::string filepath = m_baseDir + "definitions/mobs.json";
    json j;
    j["mobs"] = json::array();

    for (const auto& mob : m_mobs) {
        j["mobs"].push_back({
            {"id",          mob.id},
            {"displayName", mob.displayName},
            {"faction",     mob.faction},
            {"stats", {
                {"level",       mob.stats.level},
                {"vit",         mob.stats.vit},
                {"str",         mob.stats.str},
                {"dex",         mob.stats.dex},
                {"intl",        mob.stats.intl},
                {"res",         mob.stats.res},
                {"luk",         mob.stats.luk},
                {"expYield",    mob.stats.expYield},
                {"dropTableID", mob.stats.dropTableID}
            }},
            {"ai", {
                {"walkSpeed",       mob.ai.walkSpeed},
                {"runSpeed",        mob.ai.runSpeed},
                {"turnSpeed",       mob.ai.turnSpeed},
                {"detectionRadius", mob.ai.detectionRadius},
                {"fieldOfView",     mob.ai.fieldOfView},
                {"loseSightRadius", mob.ai.loseSightRadius},
                {"attackRange",     mob.ai.attackRange},
                {"attackCooldown",  mob.ai.attackCooldown},
                {"behavior",        mob.ai.behavior}
            }},
            {"physics", {
                {"colliderRadius",      mob.physics.colliderRadius},
                {"colliderHeight",      mob.physics.colliderHeight},
                {"colliderOffsetY",     mob.physics.colliderOffsetY},
                {"mass",                mob.physics.mass},
                {"knockbackResistance", mob.physics.knockbackResistance}
            }},
            {"resources", {
                {"modelPath",            mob.resources.modelPath},
                {"texturePath",          mob.resources.texturePath},
                {"animatorControllerID", mob.resources.animatorControllerID},
                {"onHitSound",           mob.resources.onHitSound},
                {"onDeathSound",         mob.resources.onDeathSound}
            }}
        });
    }

    std::ofstream file(filepath);
    if (file.is_open()) {
        file << j.dump(4);
        file.close();
        std::cout << "[ASSETS] mobs.json salvato: " << m_mobs.size() << " mob in " << filepath << "\n";
        // Aggiorna il timestamp per evitare un false-positive del hot-reload
        m_mobsFileTime = GetFileWriteTime(filepath);
    } else {
        std::cerr << "[ASSETS] Errore nel salvare " << filepath << "\n";
    }
}

// ─────────────────────────────────────────────────────────────────
//  Lookup helpers
// ─────────────────────────────────────────────────────────────────
BlockDef* AssetManager::GetBlock(int id) {
    for (auto& b : m_blocks)
        if (b.id == id) return &b;
    return nullptr;
}

MobTemplate* AssetManager::GetMob(int id) {
    // Cerca per indice numerico (posizione nell'array)
    for (auto& m : m_mobs)
        if (&m - &m_mobs[0] == id) return &m;
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────
//  SaveTexturePNG
// ─────────────────────────────────────────────────────────────────
bool AssetManager::SaveTexturePNG(const std::string& filename, int width, int height, const void* pixels) {
    std::string path = m_baseDir + "textures/" + filename;
    CreateDirectoryA((m_baseDir + "textures").c_str(), NULL);

    int result = stbi_write_png(path.c_str(), width, height, 4, pixels, width * 4);
    if (result == 0) {
        std::cerr << "[ASSETS] Errore nel salvataggio della texture " << path << "\n";
        return false;
    }
    std::cout << "[ASSETS] Texture salvata: " << path << "\n";
    return true;
}
