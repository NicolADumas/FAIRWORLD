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
    // === REGISTRAZIONE DEL KERNEL BUS INPUT (Action Mapping) ===
    // Le stringhe vengono hashate a compile-time da EnTT: costo zero a runtime
    using namespace entt::literals;

    auto& bindings = m_context->actionMap.bindings;

    // Movimento
    bindings["MOVE_FORWARD"_hs].push_back({fw::InputID::KEY_W});
    bindings["MOVE_BACKWARD"_hs].push_back({fw::InputID::KEY_S});
    bindings["MOVE_LEFT"_hs].push_back({fw::InputID::KEY_A});
    bindings["MOVE_RIGHT"_hs].push_back({fw::InputID::KEY_D});

    // Corsa: combo W + Shift
    bindings["RUN_FORWARD"_hs].push_back({fw::InputID::KEY_W, fw::InputID::KEY_SHIFT});

    // Salto: tasto + grilletto gamepad
    bindings["JUMP"_hs].push_back({fw::InputID::KEY_SPACE});
    bindings["JUMP"_hs].push_back({fw::InputID::PAD_FACE_DOWN});

    // Distruzione blocco: mouse sinistro O grilletto destro gamepad
    bindings["DESTROY_BLOCK"_hs].push_back({fw::InputID::MOUSE_LEFT});
    bindings["DESTROY_BLOCK"_hs].push_back({fw::InputID::PAD_TRIGGER_R});

    // Posizionamento blocco: mouse destro O grilletto sinistro gamepad
    bindings["PLACE_BLOCK"_hs].push_back({fw::InputID::MOUSE_RIGHT});
    bindings["PLACE_BLOCK"_hs].push_back({fw::InputID::PAD_TRIGGER_L});

    // Menu / Pausa
    bindings["PAUSE"_hs].push_back({fw::InputID::KEY_ESC});
    bindings["PAUSE"_hs].push_back({fw::InputID::PAD_START});

    std::cout << "[PlayState] Action Map registrata: "
              << bindings.size() << " azioni logiche nel Kernel Bus.\n";

    // === CARICAMENTO CONFIGURAZIONE DATA-DRIVEN (JSON) ===
    if (m_context->targetGameJsonPath.empty()) {
        return std::unexpected("Percorso JSON non specificato dal contesto globale!");
    }

    std::cout << "[PlayState] Avvio parsing configurazione Data-Driven da: "
              << m_context->targetGameJsonPath << "\n";
    
    std::ifstream file(m_context->targetGameJsonPath);
    if (!file.is_open()) {
        return std::unexpected("Impossibile aprire il file di configurazione: " + m_context->targetGameJsonPath);
    }

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
