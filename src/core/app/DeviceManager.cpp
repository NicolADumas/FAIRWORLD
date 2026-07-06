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
#include <imgui.h>

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

bool DeviceManager::IsPhysicalKeyPressed(fw::InputID key) {
    if (m_ignoreGameInput) return false; // Silenzia il gioco se ImGui ha il focus
    if (key == fw::InputID::NONE) return true;

    switch (key) {
        // --- MOUSE ---
        case fw::InputID::MOUSE_LEFT:   return (m_currentKeyboardState[VK_LBUTTON] & 0x80) != 0;
        case fw::InputID::MOUSE_RIGHT:  return (m_currentKeyboardState[VK_RBUTTON] & 0x80) != 0;
        case fw::InputID::MOUSE_MIDDLE: return (m_currentKeyboardState[VK_MBUTTON] & 0x80) != 0;

        // --- TASTIERA: Movimento ---
        case fw::InputID::KEY_W:     return (m_currentKeyboardState['W'] & 0x80) != 0;
        case fw::InputID::KEY_S:     return (m_currentKeyboardState['S'] & 0x80) != 0;
        case fw::InputID::KEY_A:     return (m_currentKeyboardState['A'] & 0x80) != 0;
        case fw::InputID::KEY_D:     return (m_currentKeyboardState['D'] & 0x80) != 0;

        // --- TASTIERA: Azioni ---
        case fw::InputID::KEY_SPACE: return (m_currentKeyboardState[VK_SPACE]   & 0x80) != 0;
        case fw::InputID::KEY_ESC:   return (m_currentKeyboardState[VK_ESCAPE]  & 0x80) != 0;
        case fw::InputID::KEY_ENTER: return (m_currentKeyboardState[VK_RETURN]  & 0x80) != 0;

        // --- TASTIERA: Modificatori ---
        case fw::InputID::KEY_SHIFT: return (m_currentKeyboardState[VK_SHIFT]   & 0x80) != 0;
        case fw::InputID::KEY_CTRL:  return (m_currentKeyboardState[VK_CONTROL] & 0x80) != 0;
        case fw::InputID::KEY_ALT:   return (m_currentKeyboardState[VK_MENU]    & 0x80) != 0;

        // --- TASTIERA: Frecce ---
        case fw::InputID::KEY_UP:    return (m_currentKeyboardState[VK_UP]    & 0x80) != 0;
        case fw::InputID::KEY_DOWN:  return (m_currentKeyboardState[VK_DOWN]  & 0x80) != 0;
        case fw::InputID::KEY_LEFT:  return (m_currentKeyboardState[VK_LEFT]  & 0x80) != 0;
        case fw::InputID::KEY_RIGHT: return (m_currentKeyboardState[VK_RIGHT] & 0x80) != 0;

        // --- GAMEPAD ---
        case fw::InputID::PAD_FACE_DOWN:  return m_gamepadInput.buttonA;
        case fw::InputID::PAD_FACE_RIGHT: return m_gamepadInput.buttonB;
        case fw::InputID::PAD_FACE_LEFT:  return m_gamepadInput.buttonX;
        case fw::InputID::PAD_FACE_UP:    return m_gamepadInput.buttonY;
        case fw::InputID::PAD_TRIGGER_L:  return m_gamepadInput.leftTrigger  > 0.5f;
        case fw::InputID::PAD_TRIGGER_R:  return m_gamepadInput.rightTrigger > 0.5f;
        case fw::InputID::PAD_BUMPER_L:   return m_gamepadInput.bumperLeft;
        case fw::InputID::PAD_BUMPER_R:   return m_gamepadInput.bumperRight;
        case fw::InputID::PAD_THUMB_L:    return m_gamepadInput.thumbLeft;
        case fw::InputID::PAD_THUMB_R:    return m_gamepadInput.thumbRight;
        case fw::InputID::PAD_DPAD_UP:    return m_gamepadInput.dpadUp;
        case fw::InputID::PAD_DPAD_DOWN:  return m_gamepadInput.dpadDown;
        case fw::InputID::PAD_DPAD_LEFT:  return m_gamepadInput.dpadLeft;
        case fw::InputID::PAD_DPAD_RIGHT: return m_gamepadInput.dpadRight;
        case fw::InputID::PAD_START:      return m_gamepadInput.buttonStart;
        case fw::InputID::PAD_SELECT:     return m_gamepadInput.buttonSelect;

        default: return false;
    }
}

bool DeviceManager::IsKeyJustPressed(fw::InputID key) {
    if (m_ignoreGameInput) return false;
    if (key == fw::InputID::NONE) return false;

    switch (key) {
        // MOUSE
        case fw::InputID::MOUSE_LEFT:   return (m_currentKeyboardState[VK_LBUTTON] & 0x80) != 0 && (m_previousKeyboardState[VK_LBUTTON] & 0x80) == 0;
        case fw::InputID::MOUSE_RIGHT:  return (m_currentKeyboardState[VK_RBUTTON] & 0x80) != 0 && (m_previousKeyboardState[VK_RBUTTON] & 0x80) == 0;
        case fw::InputID::MOUSE_MIDDLE: return (m_currentKeyboardState[VK_MBUTTON] & 0x80) != 0 && (m_previousKeyboardState[VK_MBUTTON] & 0x80) == 0;

        // TASTIERA
        case fw::InputID::KEY_W:     return (m_currentKeyboardState['W'] & 0x80) != 0 && (m_previousKeyboardState['W'] & 0x80) == 0;
        case fw::InputID::KEY_S:     return (m_currentKeyboardState['S'] & 0x80) != 0 && (m_previousKeyboardState['S'] & 0x80) == 0;
        case fw::InputID::KEY_A:     return (m_currentKeyboardState['A'] & 0x80) != 0 && (m_previousKeyboardState['A'] & 0x80) == 0;
        case fw::InputID::KEY_D:     return (m_currentKeyboardState['D'] & 0x80) != 0 && (m_previousKeyboardState['D'] & 0x80) == 0;

        case fw::InputID::KEY_SPACE: return (m_currentKeyboardState[VK_SPACE]   & 0x80) != 0 && (m_previousKeyboardState[VK_SPACE]   & 0x80) == 0;
        case fw::InputID::KEY_ESC:   return (m_currentKeyboardState[VK_ESCAPE]  & 0x80) != 0 && (m_previousKeyboardState[VK_ESCAPE]  & 0x80) == 0;
        case fw::InputID::KEY_ENTER: return (m_currentKeyboardState[VK_RETURN]  & 0x80) != 0 && (m_previousKeyboardState[VK_RETURN]  & 0x80) == 0;

        case fw::InputID::KEY_SHIFT: return (m_currentKeyboardState[VK_SHIFT]   & 0x80) != 0 && (m_previousKeyboardState[VK_SHIFT]   & 0x80) == 0;
        case fw::InputID::KEY_CTRL:  return (m_currentKeyboardState[VK_CONTROL] & 0x80) != 0 && (m_previousKeyboardState[VK_CONTROL] & 0x80) == 0;
        case fw::InputID::KEY_ALT:   return (m_currentKeyboardState[VK_MENU]    & 0x80) != 0 && (m_previousKeyboardState[VK_MENU]    & 0x80) == 0;

        case fw::InputID::KEY_UP:    return (m_currentKeyboardState[VK_UP]    & 0x80) != 0 && (m_previousKeyboardState[VK_UP]    & 0x80) == 0;
        case fw::InputID::KEY_DOWN:  return (m_currentKeyboardState[VK_DOWN]  & 0x80) != 0 && (m_previousKeyboardState[VK_DOWN]  & 0x80) == 0;
        case fw::InputID::KEY_LEFT:  return (m_currentKeyboardState[VK_LEFT]  & 0x80) != 0 && (m_previousKeyboardState[VK_LEFT]  & 0x80) == 0;
        case fw::InputID::KEY_RIGHT: return (m_currentKeyboardState[VK_RIGHT] & 0x80) != 0 && (m_previousKeyboardState[VK_RIGHT] & 0x80) == 0;

        default: return false; // Per il gamepad andrebbe gestito a parte
    }
}

bool DeviceManager::IsKeyReleased(fw::InputID key) {
    if (m_ignoreGameInput) return false;
    if (key == fw::InputID::NONE) return false;

    switch (key) {
        // MOUSE
        case fw::InputID::MOUSE_LEFT:   return (m_currentKeyboardState[VK_LBUTTON] & 0x80) == 0 && (m_previousKeyboardState[VK_LBUTTON] & 0x80) != 0;
        case fw::InputID::MOUSE_RIGHT:  return (m_currentKeyboardState[VK_RBUTTON] & 0x80) == 0 && (m_previousKeyboardState[VK_RBUTTON] & 0x80) != 0;
        case fw::InputID::MOUSE_MIDDLE: return (m_currentKeyboardState[VK_MBUTTON] & 0x80) == 0 && (m_previousKeyboardState[VK_MBUTTON] & 0x80) != 0;

        // TASTIERA
        case fw::InputID::KEY_W:     return (m_currentKeyboardState['W'] & 0x80) == 0 && (m_previousKeyboardState['W'] & 0x80) != 0;
        case fw::InputID::KEY_S:     return (m_currentKeyboardState['S'] & 0x80) == 0 && (m_previousKeyboardState['S'] & 0x80) != 0;
        case fw::InputID::KEY_A:     return (m_currentKeyboardState['A'] & 0x80) == 0 && (m_previousKeyboardState['A'] & 0x80) != 0;
        case fw::InputID::KEY_D:     return (m_currentKeyboardState['D'] & 0x80) == 0 && (m_previousKeyboardState['D'] & 0x80) != 0;

        case fw::InputID::KEY_SPACE: return (m_currentKeyboardState[VK_SPACE]   & 0x80) == 0 && (m_previousKeyboardState[VK_SPACE]   & 0x80) != 0;
        case fw::InputID::KEY_ESC:   return (m_currentKeyboardState[VK_ESCAPE]  & 0x80) == 0 && (m_previousKeyboardState[VK_ESCAPE]  & 0x80) != 0;
        case fw::InputID::KEY_ENTER: return (m_currentKeyboardState[VK_RETURN]  & 0x80) == 0 && (m_previousKeyboardState[VK_RETURN]  & 0x80) != 0;

        case fw::InputID::KEY_SHIFT: return (m_currentKeyboardState[VK_SHIFT]   & 0x80) == 0 && (m_previousKeyboardState[VK_SHIFT]   & 0x80) != 0;
        case fw::InputID::KEY_CTRL:  return (m_currentKeyboardState[VK_CONTROL] & 0x80) == 0 && (m_previousKeyboardState[VK_CONTROL] & 0x80) != 0;
        case fw::InputID::KEY_ALT:   return (m_currentKeyboardState[VK_MENU]    & 0x80) == 0 && (m_previousKeyboardState[VK_MENU]    & 0x80) != 0;

        case fw::InputID::KEY_UP:    return (m_currentKeyboardState[VK_UP]    & 0x80) == 0 && (m_previousKeyboardState[VK_UP]    & 0x80) != 0;
        case fw::InputID::KEY_DOWN:  return (m_currentKeyboardState[VK_DOWN]  & 0x80) == 0 && (m_previousKeyboardState[VK_DOWN]  & 0x80) != 0;
        case fw::InputID::KEY_LEFT:  return (m_currentKeyboardState[VK_LEFT]  & 0x80) == 0 && (m_previousKeyboardState[VK_LEFT]  & 0x80) != 0;
        case fw::InputID::KEY_RIGHT: return (m_currentKeyboardState[VK_RIGHT] & 0x80) == 0 && (m_previousKeyboardState[VK_RIGHT] & 0x80) != 0;

        default: return false; // Gamepad
    }
}

bool DeviceManager::IsActionActive(entt::id_type actionHash) {
    auto it = m_actionMap.bindings.find(actionHash);
    if (it == m_actionMap.bindings.end()) return false;

    for (const auto& binding : it->second) {
        bool primaryActive  = IsPhysicalKeyPressed(binding.primaryKey);
        bool modifierActive = IsPhysicalKeyPressed(binding.modifierKey);

        if (primaryActive && modifierActive) {
            return true;
        }
    }
    return false;
}

fw::InputID DeviceManager::GetFirstPressedKey(bool checkForGamepad) {
    int start = checkForGamepad ? (int)fw::InputID::PAD_FACE_DOWN : (int)fw::InputID::MOUSE_LEFT;
    int end   = checkForGamepad ? (int)fw::InputID::PAD_SELECT + 1 : (int)fw::InputID::KEY_RIGHT + 1;

    for (int i = start; i < end; ++i) {
        fw::InputID key = (fw::InputID)i;
        if (IsPhysicalKeyPressed(key)) {
            return key;
        }
    }
    return fw::InputID::NONE;
}

namespace fw {
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
    
    // Funzione globale delegata
    bool IsActionActive(entt::id_type actionHash, DeviceManager* dm) {
        if (!dm) return false;
        return dm->IsActionActive(actionHash);
    }
}

void DeviceManager::Update(SharedContext* context) {
    if (!context) return;
    using namespace entt::literals;

    // 0. --- CACHE STATO TASTIERA (Zero Latenza per IsActionActive) ---
    std::memcpy(m_previousKeyboardState, m_currentKeyboardState, 256);
    GetKeyboardState(m_currentKeyboardState);

    ImGuiIO& io = ImGui::GetIO();
    // Blocchiamo l'input del gioco (WASD, Spazio, Click) SOLO se ImGui vuole catturare l'input 
    // E noi gli abbiamo esplicitamente dato il cursore (menu aperti).
    m_ignoreGameInput = (io.WantCaptureKeyboard || io.WantCaptureMouse) && requireFreeCursor;

    // 1. --- POLLING GAMEPAD (XINPUT) ---
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));
    DWORD dwResult = XInputGetState(0, &state);

    if (dwResult == ERROR_SUCCESS) {
        m_gamepadInput.isConnected = true;
        m_gamepadIndex = 0;

        m_gamepadInput.leftThumbX  = NormalizeStick(state.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        m_gamepadInput.leftThumbY  = NormalizeStick(state.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        m_gamepadInput.rightThumbX = NormalizeStick(state.Gamepad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
        m_gamepadInput.rightThumbY = NormalizeStick(state.Gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);

        m_gamepadInput.leftTrigger  = state.Gamepad.bLeftTrigger  / 255.0f;
        m_gamepadInput.rightTrigger = state.Gamepad.bRightTrigger / 255.0f;

        const WORD btns = state.Gamepad.wButtons;
        m_gamepadInput.buttons = btns;
        m_gamepadInput.buttonA      = (btns & XINPUT_GAMEPAD_A) != 0;
        m_gamepadInput.buttonB      = (btns & XINPUT_GAMEPAD_B) != 0;
        m_gamepadInput.buttonX      = (btns & XINPUT_GAMEPAD_X) != 0;
        m_gamepadInput.buttonY      = (btns & XINPUT_GAMEPAD_Y) != 0;
        m_gamepadInput.bumperLeft   = (btns & XINPUT_GAMEPAD_LEFT_SHOULDER)  != 0;
        m_gamepadInput.bumperRight  = (btns & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
        m_gamepadInput.thumbLeft    = (btns & XINPUT_GAMEPAD_LEFT_THUMB)  != 0;
        m_gamepadInput.thumbRight   = (btns & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
        m_gamepadInput.buttonStart  = (btns & XINPUT_GAMEPAD_START)  != 0;
        m_gamepadInput.buttonSelect = (btns & XINPUT_GAMEPAD_BACK)   != 0;
        m_gamepadInput.dpadUp       = (btns & XINPUT_GAMEPAD_DPAD_UP)    != 0;
        m_gamepadInput.dpadDown     = (btns & XINPUT_GAMEPAD_DPAD_DOWN)  != 0;
        m_gamepadInput.dpadLeft     = (btns & XINPUT_GAMEPAD_DPAD_LEFT)  != 0;
        m_gamepadInput.dpadRight    = (btns & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
    } else {
        m_gamepadInput.isConnected        = false;
        m_gamepadIndex             = -1;
        m_gamepadInput             = {};  
    }

    // 2. --- POLLING ACTION MAP & TASTIERA ---
    // Azzeriamo lo stato dell'input per questo frame
    InputState& in = m_currentInput;
    in.moveForward = 0.0f;
    in.moveRight = 0.0f;
    in.lookYaw = 0.0f;
    in.lookPitch = 0.0f;
    in.isRunning = false;
    in.isJumping = false;
    in.isMining = false;
    in.isPlacing = false;

    // Combina input digitale (Tastiera/D-PAD via ActionMap) e analogico
    if (fw::IsActionActive("MOVE_FORWARD"_hs, this)) in.moveForward += 1.0f;
    if (fw::IsActionActive("MOVE_BACKWARD"_hs, this)) in.moveForward -= 1.0f;
    if (fw::IsActionActive("MOVE_LEFT"_hs, this)) in.moveRight -= 1.0f;
    if (fw::IsActionActive("MOVE_RIGHT"_hs, this)) in.moveRight += 1.0f;
    if (fw::IsActionActive("RUN_FORWARD"_hs, this)) in.isRunning = true;

    if (m_gamepadInput.isConnected) {
        in.moveForward += m_gamepadInput.leftThumbY;
        in.moveRight += m_gamepadInput.leftThumbX;
    }

    // Clamp per evitare che andare in diagonale + levetta superi 1.0
    in.moveForward = std::clamp(in.moveForward, -1.0f, 1.0f);
    in.moveRight = std::clamp(in.moveRight, -1.0f, 1.0f);

    // Gestione "Just Pressed" per il salto
    bool jumpActive = fw::IsActionActive("JUMP"_hs, this);
    if (jumpActive && !m_jumpWasDown) {
        in.isJumping = true;
    }
    m_jumpWasDown = jumpActive;

    // 3. --- GESTIONE MOUSE E LOCK DEL CURSORE ---
    HWND hwnd = (HWND)context->window;
    
    // Unlock forzato se l'engine lo richiede (es. UI, Inventario aperti) o se si preme ESC/PAUSE
    bool escDown = fw::IsActionActive("PAUSE"_hs, this) || (m_currentKeyboardState[VK_ESCAPE] & 0x80) != 0;
    if (escDown && !m_escWasDown) {
        m_cursorLocked = false;
    }
    m_escWasDown = escDown;

    if (requireFreeCursor) {
        m_cursorLocked = false;
    }

    bool rawLeftClick = (m_currentKeyboardState[VK_LBUTTON] & 0x80) != 0;
    
    // Se clicchiamo e non siamo in un menu, blocchiamo il cursore (o se si usa il pad)
    if (!m_cursorLocked && !requireFreeCursor) {
        // Usa rawLeftClick per il lock del cursore, non l'azione logica
        if (rawLeftClick && !m_lButtonWasDown) {
            m_cursorLocked = true;
            m_firstMouse = true;
        }
        if (m_gamepadInput.isConnected) {
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
            // Sensibilità applicata solo in PlayerMovementSystem
            float mouseSens = 1.0f;
            in.lookYaw += xoffset * mouseSens;
            in.lookPitch += yoffset * mouseSens;
        }
        m_firstMouse = false;
    } else {
        m_firstMouse = true;
    }

    // Aggiungiamo il contributo dell'analogico destro (look)
    if (m_gamepadInput.isConnected) {
        // Aumentata la deadzone a 0.25f per evitare stick drift ("guarda in alto da solo")
        const float deadzone = 0.25f;
        if (std::abs(m_gamepadInput.rightThumbX) > deadzone) {
            in.lookYaw += m_gamepadInput.rightThumbX * 35.0f;
        }
        if (std::abs(m_gamepadInput.rightThumbY) > deadzone) {
            in.lookPitch += m_gamepadInput.rightThumbY * 35.0f;
        }
    }

    // Freccette tastiera (legacy look)
    // Moltiplicatori aumentati a 25.0f perché lookYaw/lookPitch non si accumulano più nel DeviceManager
    if (m_currentKeyboardState[VK_UP]    & 0x80) in.lookPitch += 25.0f;
    if (m_currentKeyboardState[VK_DOWN]  & 0x80) in.lookPitch -= 25.0f;
    if (m_currentKeyboardState[VK_LEFT]  & 0x80) in.lookYaw -= 25.0f;
    if (m_currentKeyboardState[VK_RIGHT] & 0x80) in.lookYaw += 25.0f;

    // Azioni da ActionMap (Kernel Bus) - Astrazione Hardware
    bool actionMining = fw::IsActionActive("DESTROY_BLOCK"_hs, this);
    bool actionPlacing = fw::IsActionActive("PLACE_BLOCK"_hs, this);

    // Azioni Mouse/Pad attive solo se il cursore è bloccato (in game)
    if (m_cursorLocked) {
        in.isPlacing = (!m_rButtonWasDown && actionPlacing); // Just pressed (usiamo rButtonWasDown come memoria di placing)
        in.isMining = actionMining; // Held
    }

    // Aggiorna stato old per il frame successivo
    m_lButtonWasDown = rawLeftClick; // Per il lock del cursore
    m_rButtonWasDown = actionPlacing; // Per il just_pressed di place block
}

void DeviceManager::InitDefaultBindings() {
    if (!m_actionMap.bindings.empty()) return; // Già inizializzato

    using namespace fw;
    // MOVE_FORWARD
    m_actionMap.bindings[entt::hashed_string("MOVE_FORWARD")].push_back({InputID::KEY_W, InputID::NONE});
    m_actionMap.bindings[entt::hashed_string("MOVE_FORWARD")].push_back({InputID::PAD_DPAD_UP, InputID::NONE});

    // MOVE_BACKWARD
    m_actionMap.bindings[entt::hashed_string("MOVE_BACKWARD")].push_back({InputID::KEY_S, InputID::NONE});
    m_actionMap.bindings[entt::hashed_string("MOVE_BACKWARD")].push_back({InputID::PAD_DPAD_DOWN, InputID::NONE});

    // MOVE_LEFT
    m_actionMap.bindings[entt::hashed_string("MOVE_LEFT")].push_back({InputID::KEY_A, InputID::NONE});
    m_actionMap.bindings[entt::hashed_string("MOVE_LEFT")].push_back({InputID::PAD_DPAD_LEFT, InputID::NONE});

    // MOVE_RIGHT
    m_actionMap.bindings[entt::hashed_string("MOVE_RIGHT")].push_back({InputID::KEY_D, InputID::NONE});
    m_actionMap.bindings[entt::hashed_string("MOVE_RIGHT")].push_back({InputID::PAD_DPAD_RIGHT, InputID::NONE});

    // JUMP
    m_actionMap.bindings[entt::hashed_string("JUMP")].push_back({InputID::KEY_SPACE, InputID::NONE});
    m_actionMap.bindings[entt::hashed_string("JUMP")].push_back({InputID::PAD_FACE_DOWN, InputID::NONE});

    // FLY_BOOST
    m_actionMap.bindings[entt::hashed_string("FLY_BOOST")].push_back({InputID::KEY_SHIFT, InputID::NONE});
    m_actionMap.bindings[entt::hashed_string("FLY_BOOST")].push_back({InputID::PAD_TRIGGER_R, InputID::NONE});

    // FLY_DOWN
    m_actionMap.bindings[entt::hashed_string("FLY_DOWN")].push_back({InputID::KEY_CTRL, InputID::NONE});
    m_actionMap.bindings[entt::hashed_string("FLY_DOWN")].push_back({InputID::PAD_TRIGGER_L, InputID::NONE});

    // DESTROY_BLOCK
    m_actionMap.bindings[entt::hashed_string("DESTROY_BLOCK")].push_back({InputID::MOUSE_LEFT, InputID::NONE});
    m_actionMap.bindings[entt::hashed_string("DESTROY_BLOCK")].push_back({InputID::PAD_TRIGGER_R, InputID::NONE});

    // PLACE_BLOCK
    m_actionMap.bindings[entt::hashed_string("PLACE_BLOCK")].push_back({InputID::MOUSE_RIGHT, InputID::NONE});
    m_actionMap.bindings[entt::hashed_string("PLACE_BLOCK")].push_back({InputID::PAD_TRIGGER_L, InputID::NONE});

    // PAUSE
    m_actionMap.bindings[entt::hashed_string("PAUSE")].push_back({InputID::KEY_ESC, InputID::NONE});
    m_actionMap.bindings[entt::hashed_string("PAUSE")].push_back({InputID::PAD_START, InputID::NONE});
}

const char* DeviceManager::InputIDToString(fw::InputID key) {
    switch (key) {
        case fw::InputID::NONE: return "NESSUNO";
        case fw::InputID::MOUSE_LEFT: return "Click Sinistro";
        case fw::InputID::MOUSE_RIGHT: return "Click Destro";
        case fw::InputID::MOUSE_MIDDLE: return "Click Centrale";
        case fw::InputID::KEY_W: return "W";
        case fw::InputID::KEY_A: return "A";
        case fw::InputID::KEY_S: return "S";
        case fw::InputID::KEY_D: return "D";
        case fw::InputID::KEY_SPACE: return "Spazio";
        case fw::InputID::KEY_ESC: return "Esc";
        case fw::InputID::KEY_ENTER: return "Invio";
        case fw::InputID::KEY_SHIFT: return "Shift";
        case fw::InputID::KEY_CTRL: return "Ctrl";
        case fw::InputID::KEY_ALT: return "Alt";
        case fw::InputID::KEY_UP: return "Freccia Su";
        case fw::InputID::KEY_DOWN: return "Freccia Giu";
        case fw::InputID::KEY_LEFT: return "Freccia Sx";
        case fw::InputID::KEY_RIGHT: return "Freccia Dx";
        
        case fw::InputID::PAD_FACE_DOWN: return "A / Croce";
        case fw::InputID::PAD_FACE_RIGHT: return "B / Cerchio";
        case fw::InputID::PAD_FACE_LEFT: return "X / Quadrato";
        case fw::InputID::PAD_FACE_UP: return "Y / Triangolo";
        case fw::InputID::PAD_TRIGGER_L: return "Grilletto Sinistro";
        case fw::InputID::PAD_TRIGGER_R: return "Grilletto Destro";
        case fw::InputID::PAD_BUMPER_L: return "Dorsale Sinistro";
        case fw::InputID::PAD_BUMPER_R: return "Dorsale Destro";
        case fw::InputID::PAD_DPAD_UP: return "D-PAD Su";
        case fw::InputID::PAD_DPAD_DOWN: return "D-PAD Giu";
        case fw::InputID::PAD_DPAD_LEFT: return "D-PAD Sx";
        case fw::InputID::PAD_DPAD_RIGHT: return "D-PAD Dx";
        case fw::InputID::PAD_THUMB_L: return "Analogico L3";
        case fw::InputID::PAD_THUMB_R: return "Analogico R3";
        case fw::InputID::PAD_START: return "Start";
        case fw::InputID::PAD_SELECT: return "Select";
        default: return "?";
    }
}
