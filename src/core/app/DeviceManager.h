#pragma once
#include "InputTypes.h"

struct SharedContext;

class DeviceManager {
public:
    DeviceManager() = default;
    ~DeviceManager() = default;

    // Disabilitiamo le copie (Strict Memory Safety)
    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    // Aggiorna tutti i dispositivi hardware
    void Update(SharedContext* context);

    // Getters per l'input
    InputState& GetInput() { return m_currentInput; }
    const InputState& GetInput() const { return m_currentInput; }
    fw::ActionMap& GetActionMap() { return m_actionMap; }

    bool requireFreeCursor = false;

    // Helper globale o metodi per polling UI
    fw::InputID GetFirstPressedKey(bool checkForGamepad);
    bool IsActionActive(entt::id_type actionHash);
    
    // Ritorna true finché il tasto è tenuto premuto
    bool IsPhysicalKeyPressed(fw::InputID key);
    
    // Ritorna true SOLO nel frame esatto in cui il tasto viene abbassato
    bool IsKeyJustPressed(fw::InputID key);
    
    // Ritorna true nel frame esatto in cui il tasto viene rilasciato
    bool IsKeyReleased(fw::InputID key);
    
    void InitDefaultBindings();
    const char* InputIDToString(fw::InputID key);

    struct GamepadData {
        bool isConnected = false;
        int  index = -1;
        float leftThumbX = 0; float leftThumbY = 0;
        float rightThumbX= 0; float rightThumbY= 0;
        float leftTrigger= 0; float rightTrigger=0;
        bool buttonA=false; bool buttonB=false; bool buttonX=false; bool buttonY=false;
        bool bumperLeft=false; bool bumperRight=false;
        bool thumbLeft=false; bool thumbRight=false;
        bool buttonStart=false; bool buttonSelect=false;
        bool dpadUp=false; bool dpadDown=false; bool dpadLeft=false; bool dpadRight=false;
        unsigned int buttons = 0;
    };
    
    const GamepadData& GetGamepadData() const { return m_gamepadInput; }

private:
    fw::ActionMap m_actionMap;
    InputState m_currentInput;
    
    // Hardware State
    unsigned char m_currentKeyboardState[256] = {0};
    unsigned char m_previousKeyboardState[256] = {0};
    
    bool m_ignoreGameInput = false; // Flag per bloccare l'input se ImGui ha il focus
    
    GamepadData m_gamepadInput;
    int m_gamepadIndex = -1;
    // Stato per il Mouse
    bool m_firstMouse = true;
    bool m_cursorLocked = false;
    bool m_cursorVisible = true;
    bool m_lButtonWasDown = false;
    bool m_rButtonWasDown = false;
    bool m_escWasDown = false;

    // Stato precedente per azioni "Just Pressed"
    bool m_jumpWasDown = false;
};

namespace fw {
    bool IsActionActive(entt::id_type actionHash, DeviceManager* dm);
}
