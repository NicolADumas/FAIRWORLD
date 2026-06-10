#include "pch.h"
#include "DeviceManager.h"
#include "SharedContext.h"

// Lean and Mean riduce drasticamente gli header inutili di Windows
#define WIN32_LEAN_AND_MEAN 
#include <windows.h>
#include <Xinput.h>
#include <cmath>
#include <algorithm>

// Auto-link della libreria XInput per MSVC
#pragma comment(lib, "XInput.lib")

// Funzione helper locale per applicare la deadzone e normalizzare tra -1.0 e 1.0
static float NormalizeStick(SHORT value, SHORT deadzone) {
    if (std::abs(value) < deadzone) {
        return 0.0f;
    }
    
    // Sottraiamo la deadzone per avere uno scale lineare a partire da zero
    float magnitude = static_cast<float>(std::abs(value)) - deadzone;
    float normalized = magnitude / (32767.0f - deadzone);
    
    // Ripristiniamo il segno e clampiamo per sicurezza
    float finalValue = (value > 0) ? normalized : -normalized;
    return std::clamp(finalValue, -1.0f, 1.0f);
}

void DeviceManager::Update(SharedContext* context) {
    if (!context) return;

    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));

    // Interroghiamo il Controller 0 (il primo connesso)
    DWORD dwResult = XInputGetState(0, &state);

    if (dwResult == ERROR_SUCCESS) {
        context->isGamepadConnected = true;
        context->gamepadIndex = 0;

        // Assi analogici con deadzone ufficiale Microsoft
        context->gamepadInput.leftThumbX  = NormalizeStick(state.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        context->gamepadInput.leftThumbY  = NormalizeStick(state.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        context->gamepadInput.rightThumbX = NormalizeStick(state.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        context->gamepadInput.rightThumbY = NormalizeStick(state.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);

        // Trigger analogici normalizzati 0.0 -> 1.0
        context->gamepadInput.leftTrigger  = state.Gamepad.bLeftTrigger  / 255.0f;
        context->gamepadInput.rightTrigger = state.Gamepad.bRightTrigger / 255.0f;

        // Bitmask grezzo (retrocompatibile con il vecchio codice)
        const WORD btns = state.Gamepad.wButtons;
        context->gamepadInput.buttons = btns;

        // Decodifica booleana per l'Action Mapping (IsActionActive)
        context->gamepadInput.buttonA      = (btns & XINPUT_GAMEPAD_A) != 0;
        context->gamepadInput.buttonB      = (btns & XINPUT_GAMEPAD_B) != 0;
        context->gamepadInput.buttonX      = (btns & XINPUT_GAMEPAD_X) != 0;
        context->gamepadInput.buttonY      = (btns & XINPUT_GAMEPAD_Y) != 0;
        context->gamepadInput.bumperLeft   = (btns & XINPUT_GAMEPAD_LEFT_SHOULDER)  != 0;
        context->gamepadInput.bumperRight  = (btns & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
        context->gamepadInput.thumbLeft    = (btns & XINPUT_GAMEPAD_LEFT_THUMB)  != 0;
        context->gamepadInput.thumbRight   = (btns & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
        context->gamepadInput.buttonStart  = (btns & XINPUT_GAMEPAD_START)  != 0;
        context->gamepadInput.buttonSelect = (btns & XINPUT_GAMEPAD_BACK)   != 0;
        context->gamepadInput.dpadUp       = (btns & XINPUT_GAMEPAD_DPAD_UP)    != 0;
        context->gamepadInput.dpadDown     = (btns & XINPUT_GAMEPAD_DPAD_DOWN)  != 0;
        context->gamepadInput.dpadLeft     = (btns & XINPUT_GAMEPAD_DPAD_LEFT)  != 0;
        context->gamepadInput.dpadRight    = (btns & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;

    } else {
        // Dispositivo disconnesso: azzeramento totale per evitare input fantasma
        context->isGamepadConnected       = false;
        context->gamepadIndex             = -1;
        context->gamepadInput             = {};  // zero-init di tutta la struct
    }

    // TODO Futuro: qui aggiungeremo l'aggiornamento per la porta seriale (DESKARM) e la socket (BCI)
}
