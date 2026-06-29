#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "InputTypes.h"
#include "entt/entt.hpp"
#include <glm/glm.hpp>

// === FORWARD DECLARATIONS ===
struct HWND__;
using WindowHandle = HWND__*;

class StateManager;
class FairWorldEngine;
class DeviceManager;
class RenderManager;

// Classi che vivono nel namespace fw
namespace fw {
    class TimeManager;
    class JobSystem;
    class AsyncInput;
    class VulkanDmaManager;
    class VramSlabAllocator;
    class DiagnosticsManager;
    class ForgeWorld;
}

// Struttura dati per il monitoraggio delle onde cerebrali (BCI)
struct BciData {
    float alpha = 0.0f;
    float beta  = 0.0f;
    float gamma = 0.0f;
    float theta = 0.0f;
};

// Struttura dati pura per il renderer, calcolata dai sistemi ECS
struct RenderViewData {
    glm::mat4 viewMatrix       = glm::mat4(1.0f);
    glm::mat4 projectionMatrix = glm::mat4(1.0f);
    glm::vec3 cameraPosition   = glm::vec3(0.0f);
    glm::vec3 cameraFront      = glm::vec3(0.0f, 0.0f, -1.0f);
};

// === IL BUS GLOBALE DEL SERVICE LOCATOR ===
struct SharedContext {
    // --- SISTEMA CORE ---
    WindowHandle    window        = nullptr;
    StateManager*   stateManager  = nullptr;
    FairWorldEngine* engine       = nullptr;

    // --- SERVIZI FONDAMENTALI ---
    DeviceManager*          deviceManager       = nullptr;
    fw::TimeManager*        timeManager         = nullptr;
    fw::DiagnosticsManager* diagnosticsManager  = nullptr;

    // --- INFRASTRUTTURA ASINCRONA E RENDER ---
    fw::JobSystem*          jobSystem    = nullptr;
    fw::AsyncInput*         asyncInput   = nullptr;
    fw::VulkanDmaManager*   dmaManager   = nullptr;
    fw::VramSlabAllocator*  vramAllocator= nullptr;
    fw::ForgeWorld*         forgeWorld   = nullptr;

    // Sincronizzazione per RenderManager
    RenderViewData          activeCameraView;
    bool                    isForgeMode = false; // TRUE = siamo nell'editor Forge, FALSE = siamo in Game

    entt::registry          ecsRegistry;
    BciData                 latestBciData;

    glm::vec3 playerVelocity = glm::vec3(0.0f);

    // --- DEVMODE INVENTORY BUS ---
    std::string devSelectedBlock = ""; // Se vuota, usa inventario standard
    int devPlacementMode = 0; // 0 = Prefab, 1 = Minivoxel

    // --- BUS DATI ---
    std::string targetGameJsonPath;

    // 1. MODULO VR (OpenXR)
    bool  isVrSupported    = false;
    bool  isVrActive       = false;
    void* xrSessionHandle  = nullptr;

    // 2. MODULO ROBOTICA / SERIALE (DESKARM)
    void*       serialPortHandle  = nullptr;
    bool        isSerialConnected = false;
    std::string serialPortName    = "COM3";

    // 3. MODULO NEURALE / NETWORK SOCKET (BCI)
    unsigned __int64 bciSocket    = 0xFFFFFFFFFFFFFFFFULL; // INVALID_SOCKET
    bool             isBciConnected = false;
    BciData          neuralInput;

    // === DEBUG ===
    bool  showDebugUI         = false;

    // === RENDER INTERPOLATION ===
    float lastFrameTimeMs     = 0.0f;
    // Frazione di tempo residuo nell'accumulatore, normalizzata in [0.0, 1.0].
    // Calcolata in main.cpp come: accumulator / FIXED_DT
    // Usata da PlayState::Render per LERP posizione e SLERP rotazione.
    float interpolationAlpha  = 0.0f;
};
