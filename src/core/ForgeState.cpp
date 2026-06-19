#include "pch.h"
#include "ForgeState.h"
#include "SharedContext.h"
#include "StateManager.h"
#include "HubState.h"
#include "FAIRWORLD.h"
#include "RenderManager.h"
#include "ForgeWorld.h"
#include "JobSystem.h"
#include "AsyncInput.h"
#include "VulkanDmaManager.h"
#include "VramSlabAllocator.h"
#include "imgui.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

ForgeState::ForgeState(SharedContext* context) : m_context(context) {
    std::cout << "[ForgeState] Costruito.\n";
}

ForgeState::~ForgeState() {
    std::cout << "[ForgeState] Distrutto.\n";
}

std::expected<void, std::string> ForgeState::Init() {
    std::cout << "[ForgeState] Inizializzazione completata. Preparazione infrastruttura VRAM/DMA...\n";
    
    // Inizializzazione infrastruttura asincrona se non è già stata fatta a livello globale
    if (!m_context->jobSystem) {
        m_context->jobSystem = new fw::JobSystem();
        m_context->jobSystem->Initialize();
    }
    if (!m_context->asyncInput) {
        m_context->asyncInput = new fw::AsyncInput();
    }
    
    if (!m_context->vramAllocator) {
        // Alloca un "VRAM monolite" logico da 512MB per i Chunk
        m_context->vramAllocator = new fw::VramSlabAllocator(512 * 1024 * 1024);
    }
    
    if (!m_context->dmaManager) {
        m_context->dmaManager = new fw::VulkanDmaManager();
        if (auto* rm = m_context->engine->GetRenderManager()) {
            m_context->dmaManager->Initialize(
                rm->GetDevice(),
                rm->GetTransferQueue(),
                rm->GetTransferCommandPool(),
                rm->GetStagingRingBuffer(),
                rm->GetMappedStagingData(),
                rm->GetStagingBufferSize(),
                rm->GetGlobalVramBuffer()
            );
        }
    }
    
    std::cout << "[ForgeState] (ForgeWorld è ora globale e persistente in SharedContext)\n";
    // Test: Aggiungiamo un cubo sul thread principale
    m_context->forgeWorld->CreatePrimitive("MyCube", {0, 0, 0}, "Cube");
    
    // Test Asincrono: Generiamo una sfera ultra-dettagliata in background
    std::cout << "[ForgeState] Sottometto la generazione di una Sfera al Job System...\n";
    m_context->jobSystem->Execute([this]() {
        std::cout << "\n[Worker Thread] Inizio generazione procedurale della sfera...\n";
        
        // Simuliamo un carico pesante generato artificialmente (dormiamo 1 secondo)
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        auto sphereMesh = fw::MeshGenerators::MakeSphere(128, 64, 5.0f);
        
        std::cout << "[Worker Thread] Sfera generata! Accodo l'aggiunta all'ECS (Thread-Safe)...\n";
        m_context->forgeWorld->EnqueueDeferredMesh("AsyncSphere", {5.0f, 0.0f, 0.0f}, std::move(sphereMesh));
    });
    
    return {};
}

void ForgeState::Update(float dt) {
    if (!m_context) return;
    
    // --- FREE-CAM (NOCLIP) FORGE EDITOR ---
    float forward = m_context->currentInput.moveForward;
    float right = m_context->currentInput.moveRight;
    float yawDelta = m_context->currentInput.lookYaw;
    float pitchDelta = m_context->currentInput.lookPitch;

    static float yaw = -90.0f;
    static float pitch = 0.0f;
    
    yaw += yawDelta;
    pitch += pitchDelta;
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    front = glm::normalize(front);
    
    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 camRight = glm::normalize(glm::cross(front, worldUp));
    glm::vec3 camUp = glm::normalize(glm::cross(camRight, front));
    
    float flySpeed = 20.0f;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
        flySpeed = 50.0f; // Boost di velocità
    }
    
    glm::vec3 moveVec = (front * forward) + (camRight * right);
    // Controllo altezza assoluta con Spazio e Control
    if (m_context->currentInput.isJumping) moveVec.y += 1.0f;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) moveVec.y -= 1.0f;
    
    if (glm::length(moveVec) > 0.0f) {
        moveVec = glm::normalize(moveVec);
    }
    
    m_context->activeCameraView.cameraPosition += moveVec * flySpeed * dt;
    m_context->activeCameraView.cameraFront = front;
    m_context->activeCameraView.viewMatrix = glm::lookAt(m_context->activeCameraView.cameraPosition, m_context->activeCameraView.cameraPosition + front, camUp);
    // FIXME: Manteniamo 16/9 hardcoded per ora, ma andrebbe letto dalla finestra
    m_context->activeCameraView.projectionMatrix = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);

    // Aggiorniamo l'engine (fisica, input, telecamera vecchia rimpiazzata)
    if (m_context->engine) {
        m_context->engine->Update(dt);
    }
    
    // Aggiorniamo il mondo ECS della Forge
    if (m_context->forgeWorld) {
        m_context->forgeWorld->Update(dt);
    }
}

void ForgeState::Render() {
    // La telecamera 3D e il mondo vengono renderizzati dal RenderManager chiamato dall'engine

    // Disegniamo la UI della Forge sovrapposta al 3D
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    
    // Un piccolo pannello in alto a sinistra per tornare indietro
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(150.0f, 60.0f), ImGuiCond_Always);
    if (ImGui::Begin("Navigazione", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground)) {
        if (ImGui::Button("< TORNA ALL'HUB", ImVec2(130.0f, 40.0f))) {
            std::cout << "[ForgeState] Ritorno all'Hub richiesto.\n";
            m_context->stateManager->ChangeState(std::make_unique<HubState>(m_context));
        }
    }
    ImGui::End();
}
