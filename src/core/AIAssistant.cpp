#include "pch.h"
#include "AIAssistant.h"
#include "World.h"
#include "MobManager.h"
#include "Player.h"
#include <algorithm>
#include <cctype>
#include "httplib.h"
#include "json.hpp"
#include <thread>

using json = nlohmann::json;

AIAssistant::AIAssistant() {}

void AIAssistant::QueryAIServer(std::string prompt, glm::vec3 playerPos) {
    httplib::Client cli("http://localhost:5000");
    cli.set_connection_timeout(5);

    json reqData;
    reqData["prompt"] = prompt;
    reqData["x"] = playerPos.x;
    reqData["y"] = playerPos.y;
    reqData["z"] = playerPos.z;

    auto res = cli.Post("/generate", reqData.dump(), "application/json");

    std::lock_guard<std::mutex> lock(m_mutex);
    if (res && res->status == 200) {
        m_asyncResponse = res->body;
    } else {
        m_asyncResponse = "ERROR";
    }
    m_hasAsyncResponse = true;
    m_isThinking = false;
}

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

void AIAssistant::AddGhostBlock(int x, int y, int z, BlockType type) {
    m_previewBlocks.push_back({ glm::ivec3(x, y, z), type });
    m_previewDirty = true;
}

void AIAssistant::ConfirmHologram(World& world) {
    for (const auto& gb : m_previewBlocks) {
        if (world.IsInBounds(gb.pos.x, gb.pos.y, gb.pos.z)) {
            world.SetBlock(gb.pos.x, gb.pos.y, gb.pos.z, gb.type);
        }
    }
    m_previewBlocks.clear();
    m_state = AIState::Idle;
}

void AIAssistant::CancelHologram() {
    m_previewBlocks.clear();
    m_state = AIState::Idle;
    m_previewDirty = true;
}

std::string AIAssistant::ProcessPlayerMessage(const std::string& message, World& world, MobManager& mobManager, Player& player, const glm::vec3& playerPos, const glm::vec3& playerFront) {
    std::string lowerMsg = ToLower(message);
    
    // Gestione risposte di approvazione
    if (m_state == AIState::WaitingForApproval) {
        if (lowerMsg == "si" || lowerMsg == "sì" || lowerMsg == "ok" || lowerMsg == "conferma") {
            ConfirmHologram(world);
            return "[Sistema]: Perfetto! I blocchi fantasma sono diventati reali.";
        } else if (lowerMsg == "no" || lowerMsg == "annulla") {
            CancelHologram();
            return "[Sistema]: Costruzione annullata. Rimuovo l'ologramma.";
        } else {
            return "[Sistema]: Sto aspettando una tua conferma. Scrivi 'si' per costruire l'ologramma che vedi, o 'no' per annullare.";
        }
    }

    // Calcoliamo una posizione davanti al player
    glm::vec3 targetPos = player.stats.IsAlive() ? playerPos + playerFront * 5.0f : glm::vec3(0.0f, 30.0f, 0.0f);
    targetPos = glm::floor(targetPos);

    // Gestione risposte asincrone del server
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_hasAsyncResponse) {
            m_hasAsyncResponse = false;
            if (m_asyncResponse == "ERROR") {
                return "[Sistema]: Si è verificato un errore contattando il server AI.";
            }

            try {
                json data = json::parse(m_asyncResponse);
                m_previewBlocks.clear();
                for (auto& blockInfo : data) {
                    AddGhostBlock(blockInfo["x"], blockInfo["y"], blockInfo["z"], (BlockType)blockInfo["id"]);
                }
                m_state = AIState::WaitingForApproval;
                return "[Sistema]: Il Server AI ha risposto! Ologramma caricato. Scrivi 'si' per confermare.";
            } catch (...) {
                return "[Sistema]: Errore nel parse della risposta del server AI.";
            }
        }
    }

    if (m_isThinking) {
        return "[Sistema]: Sto già elaborando un'altra richiesta, attendi...";
    }

    // Se non abbiamo l'approvazione e non stiamo pensando, inviamo la query al server
    m_isThinking = true;
    m_aiFuture = std::async(std::launch::async, &AIAssistant::QueryAIServer, this, message, targetPos);
    
    return "[Sistema]: Inoltro la richiesta al cervello AI. Attendi la creazione dell'ologramma...";
}

void AIAssistant::BuildHouse(glm::vec3 pos) {
    m_previewBlocks.clear();
    int px = (int)pos.x;
    int py = (int)pos.y;
    int pz = (int)pos.z;

    if (py < 1) py = 1;

    for (int x = px - 2; x <= px + 2; x++) {
        for (int y = py; y <= py + 3; y++) {
            for (int z = pz - 2; z <= pz + 2; z++) {
                if (x == px - 2 || x == px + 2 || z == pz - 2 || z == pz + 2 || y == py + 3) {
                    AddGhostBlock(x, y, z, BlockType::Wood);
                } else {
                    AddGhostBlock(x, y, z, BlockType::Air);
                }
            }
        }
    }

    // Porta
    AddGhostBlock(px, py, pz - 2, BlockType::Air);
    AddGhostBlock(px, py + 1, pz - 2, BlockType::Air);
    
    // Luce
    AddGhostBlock(px, py + 2, pz, BlockType::LightSource);
}

void AIAssistant::BuildWall(glm::vec3 pos) {
    m_previewBlocks.clear();
    int px = (int)pos.x;
    int py = (int)pos.y;
    int pz = (int)pos.z;

    for (int x = px - 3; x <= px + 3; x++) {
        for (int y = py; y <= py + 2; y++) {
            AddGhostBlock(x, y, pz, BlockType::Stone);
        }
    }
}

void AIAssistant::BuildTower(glm::vec3 pos) {
    m_previewBlocks.clear();
    int px = (int)pos.x;
    int py = (int)pos.y;
    int pz = (int)pos.z;

    for (int y = py; y <= py + 8; y++) {
        for (int x = px - 2; x <= px + 2; x++) {
            for (int z = pz - 2; z <= pz + 2; z++) {
                if (x == px - 2 || x == px + 2 || z == pz - 2 || z == pz + 2) {
                    if (y == py + 8) {
                        if ((x + z) % 2 == 0) AddGhostBlock(x, y, z, BlockType::Stone);
                    } else {
                        AddGhostBlock(x, y, z, BlockType::Stone);
                    }
                } else if (y == py + 7) {
                    AddGhostBlock(x, y, z, BlockType::Wood);
                }
            }
        }
    }
    
    AddGhostBlock(px, py, pz - 2, BlockType::Air);
    AddGhostBlock(px, py + 1, pz - 2, BlockType::Air);
}

void AIAssistant::SpawnEntity(MobManager& mobManager, const std::string& entityType, glm::vec3 pos) {
    MobTemplate tmpl;
    tmpl.id = entityType;
    tmpl.displayName = "Mostro Creato dall'AI";
    tmpl.faction = "enemy";
    tmpl.ai.behavior = "chase_player";
    tmpl.ai.detectionRadius = 15.0f;
    tmpl.ai.attackRange = 1.5f;
    tmpl.ai.runSpeed = 3.0f;
    tmpl.stats.level = 1;
    tmpl.stats.str = 5;
    tmpl.stats.vit = 10;
    
    mobManager.Spawn(tmpl, pos);
}
