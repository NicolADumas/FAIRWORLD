#pragma once

struct SharedContext;

class DeviceManager {
public:
    DeviceManager() = default;
    ~DeviceManager() = default;

    // Disabilitiamo le copie (Strict Memory Safety)
    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    // Aggiorna tutti i dispositivi hardware e popola il context
    void Update(SharedContext* context);

private:
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
