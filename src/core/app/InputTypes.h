#pragma once

#include "entt/entt.hpp"
#include <unordered_map>
#include <vector>

namespace fw {
    // L'alfabeto universale per ogni periferica fisica del Sistema Operativo
    enum class InputID {
        NONE, // Usato anche per indicare "Nessun Modificatore Richiesto"
        
        // --- MOUSE ---
        MOUSE_LEFT,
        MOUSE_RIGHT,
        MOUSE_MIDDLE,
        
        // --- TASTIERA ---
        KEY_W, KEY_A, KEY_S, KEY_D, 
        KEY_E, KEY_Q, KEY_F, KEY_C, KEY_H, KEY_J, KEY_I,
        KEY_SPACE, KEY_ESC, KEY_ENTER,
        KEY_SHIFT, KEY_CTRL, KEY_ALT, // Modificatori

        // --- FRECCE ---
        KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,

        // --- GAMEPAD (XInput layout) ---
        PAD_FACE_DOWN,   // A / Croce
        PAD_FACE_RIGHT,  // B / Cerchio
        PAD_FACE_LEFT,   // X / Quadrato
        PAD_FACE_UP,     // Y / Triangolo
        PAD_TRIGGER_L,   // L2
        PAD_TRIGGER_R,   // R2
        PAD_BUMPER_L,    // L1
        PAD_BUMPER_R,    // R1
        PAD_DPAD_UP,
        PAD_DPAD_DOWN,
        PAD_DPAD_LEFT,
        PAD_DPAD_RIGHT,
        PAD_THUMB_L,
        PAD_THUMB_R,
        PAD_START,
        PAD_SELECT
    };

    // Associazione tra un'azione logica e i suoi tasti fisici (es: W + Shift)
    struct ActionBinding {
        InputID primaryKey;
        InputID modifierKey = InputID::NONE; // NONE = nessun modificatore richiesto
    };

    // Il registro di tutte le associazioni azione -> tasti
    struct ActionMap {
        std::unordered_map<entt::id_type, std::vector<ActionBinding>> bindings;

        // Stato per la UI "Premi un tasto" nell'HubState
        bool isListening = false;
        entt::id_type actionBeingMapped = 0;
    };
}

// Struttura dati pura dell'input (Astratta e Indipendente dall'Hardware)
struct InputState {
    float moveForward = 0.0f; // -1.0 a 1.0 (W/S o Levetta Y sinistra)
    float moveRight   = 0.0f; // -1.0 a 1.0 (A/D o Levetta X sinistra)
    float lookYaw     = 0.0f; // Mouse X Delta o Levetta X destra
    float lookPitch   = 0.0f; // Mouse Y Delta o Levetta Y destra
    
    bool isJumping    = false; // true solo nel frame in cui premi
    bool isRunning    = false; // true finché W+Shift sono tenuti premuti
    bool isMining     = false; // true finché tenuto premuto
    bool isPlacing    = false; // true solo nel frame in cui premi (per piazzare blocchi/attaccare)
    
    // Funzioni per consumare gli input discreti (es. durante sub-stepping fisico)
    void ConsumeLook() {
        lookYaw = 0.0f;
        lookPitch = 0.0f;
    }
    
    void ConsumeJump() {
        isJumping = false;
    }
};
