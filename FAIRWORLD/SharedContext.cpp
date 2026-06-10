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
            case InputID::MOUSE_LEFT:   return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            case InputID::MOUSE_RIGHT:  return (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
            case InputID::MOUSE_MIDDLE: return (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;

            // --- TASTIERA: Movimento ---
            case InputID::KEY_W:     return (GetAsyncKeyState('W') & 0x8000) != 0;
            case InputID::KEY_S:     return (GetAsyncKeyState('S') & 0x8000) != 0;
            case InputID::KEY_A:     return (GetAsyncKeyState('A') & 0x8000) != 0;
            case InputID::KEY_D:     return (GetAsyncKeyState('D') & 0x8000) != 0;

            // --- TASTIERA: Azioni ---
            case InputID::KEY_SPACE: return (GetAsyncKeyState(VK_SPACE)   & 0x8000) != 0;
            case InputID::KEY_ESC:   return (GetAsyncKeyState(VK_ESCAPE)  & 0x8000) != 0;
            case InputID::KEY_ENTER: return (GetAsyncKeyState(VK_RETURN)  & 0x8000) != 0;

            // --- TASTIERA: Modificatori ---
            case InputID::KEY_SHIFT: return (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
            case InputID::KEY_CTRL:  return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            case InputID::KEY_ALT:   return (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;

            // --- TASTIERA: Frecce ---
            case InputID::KEY_UP:    return (GetAsyncKeyState(VK_UP)    & 0x8000) != 0;
            case InputID::KEY_DOWN:  return (GetAsyncKeyState(VK_DOWN)  & 0x8000) != 0;
            case InputID::KEY_LEFT:  return (GetAsyncKeyState(VK_LEFT)  & 0x8000) != 0;
            case InputID::KEY_RIGHT: return (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;

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
}
