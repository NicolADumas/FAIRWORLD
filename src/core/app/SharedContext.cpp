#include "pch.h"
#include "SharedContext.h"

// Isoliamo Windows qui: nessun altro file dovrà includere questi header
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace fw {

    // Helper locale, non visibile fuori da questo .cpp
    static bool IsPhysicalKeyPressed(InputID key, SharedContext* ctx) {
        // NONE è il codice per "nessun modificatore": ritorna sempre vero
        if (key == InputID::NONE) return true;

        switch (key) {
            // --- MOUSE ---
            case InputID::MOUSE_LEFT:   return (ctx->keyboardState[VK_LBUTTON] & 0x80) != 0;
            case InputID::MOUSE_RIGHT:  return (ctx->keyboardState[VK_RBUTTON] & 0x80) != 0;
            case InputID::MOUSE_MIDDLE: return (ctx->keyboardState[VK_MBUTTON] & 0x80) != 0;

            // --- TASTIERA: Movimento ---
            case InputID::KEY_W:     return (ctx->keyboardState['W'] & 0x80) != 0;
            case InputID::KEY_S:     return (ctx->keyboardState['S'] & 0x80) != 0;
            case InputID::KEY_A:     return (ctx->keyboardState['A'] & 0x80) != 0;
            case InputID::KEY_D:     return (ctx->keyboardState['D'] & 0x80) != 0;

            // --- TASTIERA: Azioni ---
            case InputID::KEY_SPACE: return (ctx->keyboardState[VK_SPACE]   & 0x80) != 0;
            case InputID::KEY_ESC:   return (ctx->keyboardState[VK_ESCAPE]  & 0x80) != 0;
            case InputID::KEY_ENTER: return (ctx->keyboardState[VK_RETURN]  & 0x80) != 0;

            // --- TASTIERA: Modificatori ---
            case InputID::KEY_SHIFT: return (ctx->keyboardState[VK_SHIFT]   & 0x80) != 0;
            case InputID::KEY_CTRL:  return (ctx->keyboardState[VK_CONTROL] & 0x80) != 0;
            case InputID::KEY_ALT:   return (ctx->keyboardState[VK_MENU]    & 0x80) != 0;

            // --- TASTIERA: Frecce ---
            case InputID::KEY_UP:    return (ctx->keyboardState[VK_UP]    & 0x80) != 0;
            case InputID::KEY_DOWN:  return (ctx->keyboardState[VK_DOWN]  & 0x80) != 0;
            case InputID::KEY_LEFT:  return (ctx->keyboardState[VK_LEFT]  & 0x80) != 0;
            case InputID::KEY_RIGHT: return (ctx->keyboardState[VK_RIGHT] & 0x80) != 0;

            // --- GAMEPAD (dati già pre-decodificati da DeviceManager) ---
            case InputID::PAD_FACE_DOWN:  return ctx && ctx->gamepadInput.buttonA;
            case InputID::PAD_FACE_RIGHT: return ctx && ctx->gamepadInput.buttonB;
            case InputID::PAD_FACE_LEFT:  return ctx && ctx->gamepadInput.buttonX;
            case InputID::PAD_FACE_UP:    return ctx && ctx->gamepadInput.buttonY;
            case InputID::PAD_TRIGGER_L:  return ctx && ctx->gamepadInput.leftTrigger  > 0.5f;
            case InputID::PAD_TRIGGER_R:  return ctx && ctx->gamepadInput.rightTrigger > 0.5f;
            case InputID::PAD_BUMPER_L:   return ctx && ctx->gamepadInput.bumperLeft;
            case InputID::PAD_BUMPER_R:   return ctx && ctx->gamepadInput.bumperRight;
            case InputID::PAD_THUMB_L:    return ctx && ctx->gamepadInput.thumbLeft;
            case InputID::PAD_THUMB_R:    return ctx && ctx->gamepadInput.thumbRight;
            case InputID::PAD_DPAD_UP:    return ctx && ctx->gamepadInput.dpadUp;
            case InputID::PAD_DPAD_DOWN:  return ctx && ctx->gamepadInput.dpadDown;
            case InputID::PAD_DPAD_LEFT:  return ctx && ctx->gamepadInput.dpadLeft;
            case InputID::PAD_DPAD_RIGHT: return ctx && ctx->gamepadInput.dpadRight;
            case InputID::PAD_START:      return ctx && ctx->gamepadInput.buttonStart;
            case InputID::PAD_SELECT:     return ctx && ctx->gamepadInput.buttonSelect;

            default: return false;
        }
    }

    // Funzione pubblica del bus: verifica se un'azione è attiva
    bool IsActionActive(entt::id_type actionHash, SharedContext* ctx) {
        if (!ctx) return false;

        auto it = ctx->actionMap.bindings.find(actionHash);
        if (it == ctx->actionMap.bindings.end()) return false;

        // Itera su tutti i binding registrati per questa azione (es: tasto + gamepad)
        for (const auto& binding : it->second) {
            bool primaryActive  = IsPhysicalKeyPressed(binding.primaryKey,  ctx);
            bool modifierActive = IsPhysicalKeyPressed(binding.modifierKey, ctx);

            // Se la combo (o il tasto singolo) è premuta: l'azione scatta
            if (primaryActive && modifierActive) {
                return true;
            }
        }
        return false;
    }

    const char* InputIDToString(InputID key) {
        switch (key) {
            case InputID::NONE: return "NESSUNO";
            case InputID::MOUSE_LEFT: return "Click Sinistro";
            case InputID::MOUSE_RIGHT: return "Click Destro";
            case InputID::MOUSE_MIDDLE: return "Click Centrale";
            case InputID::KEY_W: return "W";
            case InputID::KEY_A: return "A";
            case InputID::KEY_S: return "S";
            case InputID::KEY_D: return "D";
            case InputID::KEY_SPACE: return "Spazio";
            case InputID::KEY_ESC: return "Esc";
            case InputID::KEY_ENTER: return "Invio";
            case InputID::KEY_SHIFT: return "Shift";
            case InputID::KEY_CTRL: return "Ctrl";
            case InputID::KEY_ALT: return "Alt";
            case InputID::KEY_UP: return "Freccia Su";
            case InputID::KEY_DOWN: return "Freccia Giu";
            case InputID::KEY_LEFT: return "Freccia Sx";
            case InputID::KEY_RIGHT: return "Freccia Dx";
            
            case InputID::PAD_FACE_DOWN: return "A / Croce";
            case InputID::PAD_FACE_RIGHT: return "B / Cerchio";
            case InputID::PAD_FACE_LEFT: return "X / Quadrato";
            case InputID::PAD_FACE_UP: return "Y / Triangolo";
            case InputID::PAD_TRIGGER_L: return "Grilletto Sinistro";
            case InputID::PAD_TRIGGER_R: return "Grilletto Destro";
            case InputID::PAD_BUMPER_L: return "Dorsale Sinistro";
            case InputID::PAD_BUMPER_R: return "Dorsale Destro";
            case InputID::PAD_DPAD_UP: return "D-PAD Su";
            case InputID::PAD_DPAD_DOWN: return "D-PAD Giu";
            case InputID::PAD_DPAD_LEFT: return "D-PAD Sx";
            case InputID::PAD_DPAD_RIGHT: return "D-PAD Dx";
            case InputID::PAD_THUMB_L: return "Analogico L3";
            case InputID::PAD_THUMB_R: return "Analogico R3";
            case InputID::PAD_START: return "Start";
            case InputID::PAD_SELECT: return "Select";
            default: return "?";
        }
    }

    InputID GetFirstPressedKey(SharedContext* ctx, bool checkForGamepad) {
        // Range di chiavi da controllare in base alla modalità
        int start = checkForGamepad ? (int)InputID::PAD_FACE_DOWN : (int)InputID::MOUSE_LEFT;
        int end   = checkForGamepad ? (int)InputID::PAD_SELECT + 1 : (int)InputID::KEY_RIGHT + 1;

        for (int i = start; i < end; ++i) {
            InputID key = (InputID)i;
            if (IsPhysicalKeyPressed(key, ctx)) {
                return key;
            }
        }
        return InputID::NONE;
    }

    void InitDefaultBindings(SharedContext* ctx) {
        if (!ctx) return;
        if (!ctx->actionMap.bindings.empty()) return; // Già inizializzato

        // MOVE_FORWARD
        ctx->actionMap.bindings[entt::hashed_string("MOVE_FORWARD")].push_back({InputID::KEY_W, InputID::NONE});
        ctx->actionMap.bindings[entt::hashed_string("MOVE_FORWARD")].push_back({InputID::PAD_DPAD_UP, InputID::NONE});

        // MOVE_BACKWARD
        ctx->actionMap.bindings[entt::hashed_string("MOVE_BACKWARD")].push_back({InputID::KEY_S, InputID::NONE});
        ctx->actionMap.bindings[entt::hashed_string("MOVE_BACKWARD")].push_back({InputID::PAD_DPAD_DOWN, InputID::NONE});

        // MOVE_LEFT
        ctx->actionMap.bindings[entt::hashed_string("MOVE_LEFT")].push_back({InputID::KEY_A, InputID::NONE});
        ctx->actionMap.bindings[entt::hashed_string("MOVE_LEFT")].push_back({InputID::PAD_DPAD_LEFT, InputID::NONE});

        // MOVE_RIGHT
        ctx->actionMap.bindings[entt::hashed_string("MOVE_RIGHT")].push_back({InputID::KEY_D, InputID::NONE});
        ctx->actionMap.bindings[entt::hashed_string("MOVE_RIGHT")].push_back({InputID::PAD_DPAD_RIGHT, InputID::NONE});

        // JUMP
        ctx->actionMap.bindings[entt::hashed_string("JUMP")].push_back({InputID::KEY_SPACE, InputID::NONE});
        ctx->actionMap.bindings[entt::hashed_string("JUMP")].push_back({InputID::PAD_FACE_DOWN, InputID::NONE});

        // DESTROY_BLOCK
        ctx->actionMap.bindings[entt::hashed_string("DESTROY_BLOCK")].push_back({InputID::MOUSE_RIGHT, InputID::NONE});
        ctx->actionMap.bindings[entt::hashed_string("DESTROY_BLOCK")].push_back({InputID::PAD_TRIGGER_R, InputID::NONE});

        // PLACE_BLOCK
        ctx->actionMap.bindings[entt::hashed_string("PLACE_BLOCK")].push_back({InputID::MOUSE_LEFT, InputID::NONE});
        ctx->actionMap.bindings[entt::hashed_string("PLACE_BLOCK")].push_back({InputID::PAD_TRIGGER_L, InputID::NONE});

        // PAUSE
        ctx->actionMap.bindings[entt::hashed_string("PAUSE")].push_back({InputID::KEY_ESC, InputID::NONE});
        ctx->actionMap.bindings[entt::hashed_string("PAUSE")].push_back({InputID::PAD_START, InputID::NONE});
    }
}
