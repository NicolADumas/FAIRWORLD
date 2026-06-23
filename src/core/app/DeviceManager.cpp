#include "pch.h"
#include "DeviceManager.h"
#include "SharedContext.h"
#include "entt/entt.hpp"

// Lean and Mean riduce drasticamente gli header inutili di Windows
#define WIN32_LEAN_AND_MEAN 
#include <windows.h>
#include <Xinput.h>
#include <cmath>
#include <algorithm>

// Auto-link della libreria XInput per MSVC
#pragma comment(lib, "XInput.lib")

static float NormalizeStick(SHORT value, SHORT deadzone) {
    if (std::abs(value) < deadzone) {
        return 0.0f;
    }
    float magnitude = static_cast<float>(std::abs(value)) - deadzone;
    float normalized = magnitude / (32767.0f - deadzone);
    float finalValue = (value > 0) ? normalized : -normalized;
    return std::clamp(finalValue, -1.0f, 1.0f);
}

void DeviceManager::Update(SharedContext* context) {
    if (!context) return;
    using namespace entt::literals;

    // 1. --- POLLING GAMEPAD (XINPUT) ---
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));
    DWORD dwResult = XInputGetState(0, &state);

    if (dwResult == ERROR_SUCCESS) {
        context->isGamepadConnected = true;
        context->gamepadIndex = 0;

        context->gamepadInput.leftThumbX  = NormalizeStick(state.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        context->gamepadInput.leftThumbY  = NormalizeStick(state.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        context->gamepadInput.rightThumbX = NormalizeStick(state.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        context->gamepadInput.rightThumbY = NormalizeStick(state.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);

        context->gamepadInput.leftTrigger  = state.Gamepad.bLeftTrigger  / 255.0f;
        context->gamepadInput.rightTrigger = state.Gamepad.bRightTrigger / 255.0f;

        const WORD btns = state.Gamepad.wButtons;
        context->gamepadInput.buttons = btns;
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
        context->isGamepadConnected       = false;
        context->gamepadIndex             = -1;
        context->gamepadInput             = {};  
    }

    // 2. --- POLLING ACTION MAP & TASTIERA ---
    // Azzeriamo lo stato dell'input per questo frame
    InputState& in = context->currentInput;
    in.moveForward = 0.0f;
    in.moveRight = 0.0f;
    in.lookYaw = 0.0f;
    in.lookPitch = 0.0f;
    in.isJumping = false;
    in.isRunning = false;
    in.isMining = false;
    in.isPlacing = false;

    // Combina input digitale (Tastiera/D-PAD via ActionMap) e analogico
    if (fw::IsActionActive("MOVE_FORWARD"_hs, context)) in.moveForward += 1.0f;
    if (fw::IsActionActive("MOVE_BACKWARD"_hs, context)) in.moveForward -= 1.0f;
    if (fw::IsActionActive("MOVE_LEFT"_hs, context)) in.moveRight -= 1.0f;
    if (fw::IsActionActive("MOVE_RIGHT"_hs, context)) in.moveRight += 1.0f;
    if (fw::IsActionActive("RUN_FORWARD"_hs, context)) in.isRunning = true;

    if (context->isGamepadConnected) {
        in.moveForward += context->gamepadInput.leftThumbY;
        in.moveRight += context->gamepadInput.leftThumbX;
    }

    // Clamp per evitare che andare in diagonale + levetta superi 1.0
    in.moveForward = std::clamp(in.moveForward, -1.0f, 1.0f);
    in.moveRight = std::clamp(in.moveRight, -1.0f, 1.0f);

    // Gestione "Just Pressed" per il salto
    bool jumpActive = fw::IsActionActive("JUMP"_hs, context);
    if (jumpActive && !m_jumpWasDown) {
        in.isJumping = true;
    }
    m_jumpWasDown = jumpActive;

    // 3. --- GESTIONE MOUSE E LOCK DEL CURSORE ---
    HWND hwnd = (HWND)context->window;
    
    // Unlock forzato se l'engine lo richiede (es. UI, Inventario aperti) o se si preme ESC/PAUSE
    bool escDown = fw::IsActionActive("PAUSE"_hs, context) || (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (escDown && !m_escWasDown) {
        m_cursorLocked = false;
    }
    m_escWasDown = escDown;

    if (context->requireFreeCursor) {
        m_cursorLocked = false;
    }

    bool lDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    bool rDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    
    // Se clicchiamo e non siamo in un menu, blocchiamo il cursore (o se si usa il pad)
    if (!m_cursorLocked && !context->requireFreeCursor) {
        if (lDown && !m_lButtonWasDown) {
            m_cursorLocked = true;
            m_firstMouse = true;
        }
        if (context->isGamepadConnected) {
            m_cursorLocked = true;
            m_firstMouse = true;
        }
    }

    // Visibilità Cursore OS
    if (m_cursorLocked && m_cursorVisible) {
        ShowCursor(FALSE);
        m_cursorVisible = false;
    } else if (!m_cursorLocked && !m_cursorVisible) {
        ShowCursor(TRUE);
        m_cursorVisible = true;
    }

    // Calcolo Delta Mouse
    if (m_cursorLocked && hwnd) {
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        POINT center = {
            (clientRect.right  - clientRect.left) / 2,
            (clientRect.bottom - clientRect.top)  / 2
        };
        ClientToScreen(hwnd, &center);

        POINT cursorPos;
        GetCursorPos(&cursorPos);
        float xoffset = (float)(cursorPos.x - center.x);
        float yoffset = (float)(center.y    - cursorPos.y); // invertito per convenzione FPS

        SetCursorPos(center.x, center.y);

        if (!m_firstMouse) {
            // Sensibilità base del mouse (potrebbe essere letta da config in futuro)
            float mouseSens = 0.1f;
            in.lookYaw = xoffset * mouseSens;
            in.lookPitch = yoffset * mouseSens;
        }
        m_firstMouse = false;
    } else {
        m_firstMouse = true;
    }

    // Aggiungiamo il contributo dell'analogico destro (look)
    if (context->isGamepadConnected) {
        // Scala gamepad (es. 150 gradi/sec, assumendo venga moltiplicato per dt nel sistema ricevente)
        // Siccome il deltaTime non ce l'abbiamo qui, passiamo il valore raw e il sistema lo moltiplicherà per dt.
        // Wait, mouse offset è già delta. Il gamepad è rate (deg/sec).
        // Per uniformare, passeremo un "Delta Equivalente".
        // Più semplice: passiamo il rate e chi legge decide.
        // Ma per coerenza, se lookYaw è un delta angolare puro:
        // Lasciamo che la levetta passi i valori raw e chi usa lookYaw (CameraSystem) applica il deltaTime/Sens.
        // Per ora passiamo i valori scalati per simulare un delta standard (es 1.5).
        if (std::abs(context->gamepadInput.rightThumbX) > 0.1f) in.lookYaw += context->gamepadInput.rightThumbX * 1.5f;
        if (std::abs(context->gamepadInput.rightThumbY) > 0.1f) in.lookPitch += context->gamepadInput.rightThumbY * 1.5f;
    }

    // Freccette tastiera (legacy look)
    if (GetAsyncKeyState(VK_UP)    & 0x8000) in.lookPitch += 1.5f;
    if (GetAsyncKeyState(VK_DOWN)  & 0x8000) in.lookPitch -= 1.5f;
    if (GetAsyncKeyState(VK_LEFT)  & 0x8000) in.lookYaw -= 1.5f;
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000) in.lookYaw += 1.5f;

    // Override Azioni da Gamepad
    if (fw::IsActionActive("DESTROY_BLOCK"_hs, context)) rDown = true;
    if (fw::IsActionActive("PLACE_BLOCK"_hs, context)) lDown = true;

    // Azioni Mouse/Pad attive solo se il cursore è bloccato (in game)
    if (m_cursorLocked) {
        in.isPlacing = (!m_lButtonWasDown && lDown); // Just pressed
        in.isMining = rDown; // Held
    }

    // Aggiorna stato old
    m_lButtonWasDown = lDown;
    m_rButtonWasDown = rDown;
}
