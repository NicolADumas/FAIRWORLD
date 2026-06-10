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
};
