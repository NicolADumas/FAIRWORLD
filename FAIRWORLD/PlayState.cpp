#include "pch.h"
#include "PlayState.h"
#include "SharedContext.h"
#include "FAIRWORLD.h"
#include "Components.h"
#include <iostream>
#include <fstream>
#include "json.hpp"

using json = nlohmann::json;

PlayState::PlayState(SharedContext* context) : m_context(context) {
    std::cout << "[PlayState] Costruito.\n";
}

PlayState::~PlayState() {
    std::cout << "[PlayState] Distrutto.\n";
}

std::expected<void, std::string> PlayState::Init() {
    if (m_context->targetGameJsonPath.empty()) {
        return std::unexpected("Percorso JSON non specificato dal contesto globale!");
    }

    std::cout << "[PlayState] Avvio parsing configurazione Data-Driven da: " << m_context->targetGameJsonPath << "\n";
    
    std::ifstream file(m_context->targetGameJsonPath);
    if (!file.is_open()) {
        return std::unexpected("Impossibile aprire il file di configurazione: " + m_context->targetGameJsonPath);
    }

    // Parsing "no-throw": chiediamo a nlohmann di non lanciare eccezioni ma di restituire is_discarded
    json data = json::parse(file, nullptr, false);
    if (data.is_discarded()) {
        return std::unexpected("Errore di sintassi nel JSON del livello.");
    }

    std::cout << "[PlayState] Creazione entities EnTT dal JSON in corso...\n";
    
    int entityCount = 0;
    if (data.contains("entities") && data["entities"].is_array()) {
        for (const auto& entityJson : data["entities"]) {
            auto entity = m_registry.create();
            entityCount++;
            
            if (entityJson.contains("name")) {
                m_registry.emplace<NameComponent>(entity, entityJson["name"].get<std::string>());
            }
            
            if (entityJson.contains("transform")) {
                auto& tf = entityJson["transform"];
                m_registry.emplace<TransformComponent>(entity, 
                    tf.value("x", 0.0f), 
                    tf.value("y", 0.0f), 
                    tf.value("z", 0.0f)
                );
            }
        }
    }
    
    std::cout << "[PlayState] Create " << entityCount << " entità EnTT base!\n";
    std::cout << "[PlayState] Inizializzato con successo.\n";
    
    return {};
}

void PlayState::Update(float dt) {
    m_context->engine->Update(dt);
}

void PlayState::Render() {
    m_context->engine->Render();
}
