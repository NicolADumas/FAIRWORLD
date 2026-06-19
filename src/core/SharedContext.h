#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "InputTypes.h"
#include "entt/entt.hpp"
#include <glm/glm.hpp>

// Forward declarations
struct HWND__;
using WindowHandle = HWND__*;
class StateManager;
class FairWorldEngine;

// Struttura dati per il monitoraggio delle onde cerebrali (BCI)
struct BciData {
    float alpha = 0.0f;
    float beta  = 0.0f;
    float gamma = 0.0f;
    float theta = 0.0f;
};

// === TIPI DEL NAMESPACE fw (solo quelli che NON dipendono da SharedContext) ===
namespace fw {
    class JobSystem;
    class AsyncInput;
    class VulkanDmaManager;
    class VramSlabAllocator;
    class ForgeWorld;

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

// === IL BUS GLOBALE DELL'OS ===
// Struttura dati pura per il renderer, calcolata dai sistemi ECS
struct RenderViewData {
    glm::mat4 viewMatrix = glm::mat4(1.0f);
    glm::mat4 projectionMatrix = glm::mat4(1.0f);
    glm::vec3 cameraPosition = glm::vec3(0.0f);
    glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
};

// Struttura dati pura dell'input (Astratta e Indipendente dall'Hardware)
struct InputState {
    float moveForward = 0.0f; // -1.0 a 1.0 (W/S o Levetta Y sinistra)
    float moveRight   = 0.0f; // -1.0 a 1.0 (A/D o Levetta X sinistra)
    float lookYaw     = 0.0f; // Mouse X Delta o Levetta X destra
    float lookPitch   = 0.0f; // Mouse Y Delta o Levetta Y destra
    
    bool isJumping    = false; // true solo nel frame in cui premi
    bool isMining     = false; // true finché tenuto premuto
    bool isPlacing    = false; // true solo nel frame in cui premi (per piazzare blocchi/attaccare)
};

// Definito prima di IsActionActive perché quella funzione lo referenzia
struct SharedContext {
    // --- SISTEMA CORE ---
    WindowHandle window         = nullptr;
    StateManager* stateManager  = nullptr;
    FairWorldEngine* engine     = nullptr;
    
    // --- INFRASTRUTTURA ASINCRONA E RENDER ---
    fw::JobSystem* jobSystem    = nullptr;
    fw::AsyncInput* asyncInput  = nullptr;
    fw::VulkanDmaManager* dmaManager = nullptr;
    fw::VramSlabAllocator* vramAllocator = nullptr;
    fw::ForgeWorld* forgeWorld = nullptr;

    // --- RENDER DATA ---
    RenderViewData activeCameraView;

    // --- BUS DATI (LA CARTUCCIA) ---
    std::string targetGameJsonPath;

    // 1. MODULO VR (OpenXR)
    bool isVrSupported    = false;
    bool isVrActive       = false;
    void* xrSessionHandle = nullptr;

    // 2. MODULO ROBOTICA / SERIALE (DESKARM)
    void* serialPortHandle     = nullptr;
    bool isSerialConnected     = false;
    std::string serialPortName = "COM3";

    // 3. MODULO NEURALE / NETWORK SOCKET (BCI)
    unsigned __int64 bciSocket = 0xFFFFFFFFFFFFFFFFULL; // INVALID_SOCKET
    bool isBciConnected        = false;
    BciData neuralInput;

    // 4. MODULO GAMEPAD / CONTROLLER (XInput)
    // Tipi base, niente <xinput.h> in questo header
    struct GamepadData {
        float leftThumbX   = 0.0f;
        float leftThumbY   = 0.0f;
        float rightThumbX  = 0.0f;
        float rightThumbY  = 0.0f;
        float leftTrigger  = 0.0f;
        float rightTrigger = 0.0f;
        unsigned short buttons = 0; // Bitmask XInput grezzo
        // Bottoni decodificati (popolati da DeviceManager ogni frame)
        bool buttonA = false; bool buttonB = false;
        bool buttonX = false; bool buttonY = false;
        bool bumperLeft  = false; bool bumperRight  = false;
        bool thumbLeft   = false; bool thumbRight   = false;
        bool buttonStart = false; bool buttonSelect = false;
        bool dpadUp   = false; bool dpadDown  = false;
        bool dpadLeft = false; bool dpadRight = false;
    };
    bool isGamepadConnected = false;
    int  gamepadIndex       = -1;
    GamepadData gamepadInput;

    // 5. MODULO INPUT LOGICO (Action Mapping)
    fw::ActionMap actionMap;

    // 6. INPUT HAL (Hardware Abstraction Layer)
    InputState currentInput;
    bool requireFreeCursor = false; // Se true, il DeviceManager sblocca il cursore
};

// === HELPER DEL NAMESPACE fw (dichiarato DOPO SharedContext, che ora è completo) ===
namespace fw {
    // Interroga il bus input a costo zero tramite hash a compile-time
    bool IsActionActive(entt::id_type actionHash, SharedContext* ctx);

    // Helpers per la UI di remapping
    const char* InputIDToString(InputID key);
    InputID GetFirstPressedKey(SharedContext* ctx, bool checkForGamepad);
    void InitDefaultBindings(SharedContext* ctx);
}
