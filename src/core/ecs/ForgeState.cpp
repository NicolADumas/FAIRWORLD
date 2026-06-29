#include "pch.h"
#include "ForgeState.h"
#include "SharedContext.h"
#include "DeviceManager.h"
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
#include "Components.h"
#include "Systems.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include "RenderManager.h"
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
                    rm->GetStagingDeviceMemory(),
                    rm->GetMappedStagingData(),
                    rm->GetStagingBufferSize(),
                    rm->GetGlobalVramBuffer(),
                    rm->GetQueueMutex()
                );
        }
    }
    
    std::cout << "[ForgeState] Inizializzazione ECS per Arcball-Cam...\n";
    // Togliamo il GameMode::Dev per sbloccare il mouse
    m_context->engine->SetGameMode(GameMode::Play);
    // Ma chiediamo a DeviceManager un cursore libero
    m_context->deviceManager->requireFreeCursor = true;
    
    auto cameraEntity = m_registry.create();
    m_registry.emplace<NameComponent>(cameraEntity, "ForgeCamera");
    m_registry.emplace<TransformComponent>(cameraEntity, 8.0f, 8.0f, 24.0f);
    
    auto& cam = m_registry.emplace<CameraComponent>(cameraEntity);
    cam.yaw   = -90.0f;
    cam.pitch =   0.0f;
    
    // In Forge usiamo solo la Camera, niente PlayerMovementSystem
    m_systems.push_back(std::make_unique<fw::CameraSystem>());
    // m_systems.push_back(std::make_unique<fw::CameraSyncSystem>()); // Lo facciamo manuale


    std::cout << "[ForgeState] (ForgeWorld è ora globale e persistente in SharedContext)\n";
    // Generiamo la griglia visiva 16x16x16
    auto gridMesh = fw::MeshGenerators::MakeGridBox(16, 16, 16, 0.05f);
    m_context->forgeWorld->EnqueueDeferredMesh("WorkspaceGrid", {0.0f, 0.0f, 0.0f}, std::move(gridMesh));

    auto previewMesh = fw::MeshGenerators::MakeCube(1.01f); // Leggermente più grande di 1 voxel per evidenziarlo
    for (auto& v : previewMesh.vertices) {
        v.color = {1.0f, 1.0f, 1.0f}; // Colore base neutro, verrà sovrascritto
    }
    m_context->forgeWorld->EnqueueDeferredMesh("PreviewSphere", { 8.0f, 0.0f, 8.0f }, std::move(previewMesh));
    
    return {};
}

void ForgeState::Update(float dt) {
    if (!m_context) return;
    
    // Attiviamo il bypass della skybox nel renderer
    m_context->isForgeMode = true;

    // Aggiorna l'anteprima colore e posizione del cursore
    if (m_context->forgeWorld) {
        auto view = m_context->forgeWorld->GetRegistry().view<fw::MeshComponent, fw::TransformComponent>();
        for(auto e : view) {
            auto& mesh = view.get<fw::MeshComponent>(e);
            if (mesh.name == "PreviewSphere") {
                auto& mat = m_context->forgeWorld->GetPalette().materials[m_selectedColorIndex];
                mesh.colorOverride[0] = mat.baseColor.x;
                mesh.colorOverride[1] = mat.baseColor.y;
                mesh.colorOverride[2] = mat.baseColor.z;
                mesh.colorOverride[3] = 0.6f; // Semitrasparente
                
                auto& trans = view.get<fw::TransformComponent>(e);
                trans.location.x = m_cursorX + 0.5f;
                trans.location.y = m_cursorY + 0.5f;
                trans.location.z = m_cursorZ + 0.5f;
            }
        }
    }

    // --- ESECUZIONE SISTEMI ECS PER FREE-CAM ---
    for (auto& system : m_systems) {
        system->Update(m_registry, m_context, dt);
    }

    // Aggiorniamo l'engine (fisica, input)
    if (m_context->engine) {
        m_context->engine->Update(dt);
    }
    
    // --- TELECAMERA ORBITALE (SOFTWARE EDITOR) ---
    auto view = m_registry.view<CameraComponent, TransformComponent>();
    for (auto entity : view) {
        auto& cam = view.get<CameraComponent>(entity);
        auto& trans = view.get<TransformComponent>(entity);
        
        // Input ImGui per l'orbit
        ImGuiIO& io = ImGui::GetIO();
        m_isMouseOverUI = io.WantCaptureMouse;
        
        if (!m_isMouseOverUI && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            m_orbitYaw += io.MouseDelta.x * 0.5f;
            m_orbitPitch -= io.MouseDelta.y * 0.5f;
            m_orbitPitch = std::clamp(m_orbitPitch, -89.0f, 89.0f);
        }
        
        // Zoom con la rotellina
        if (!m_isMouseOverUI) {
            m_orbitDistance -= io.MouseWheel * 2.0f;
            m_orbitDistance = std::clamp(m_orbitDistance, 2.0f, 100.0f);
        }
        
        // Calcola la posizione orbitale
        float radYaw = glm::radians(m_orbitYaw);
        float radPitch = glm::radians(m_orbitPitch);
        
        glm::vec3 offset;
        offset.x = m_orbitDistance * cos(radPitch) * cos(radYaw);
        offset.y = m_orbitDistance * sin(radPitch);
        offset.z = m_orbitDistance * cos(radPitch) * sin(radYaw);
        
        glm::vec3 newPos = m_orbitTarget + offset;
        
        // Aggiorna Transform
        trans.x = newPos.x;
        trans.y = newPos.y;
        trans.z = newPos.z;
        
        // Aggiorna direzione della camera
        cam.front = glm::normalize(m_orbitTarget - newPos);
        cam.right = glm::normalize(glm::cross(cam.front, glm::vec3(0.0f, 1.0f, 0.0f)));
        cam.up    = glm::normalize(glm::cross(cam.right, cam.front));
        
        // Sincronizziamo col RenderManager (VITAL per far esistere l'ambiente 3D!)
        m_context->activeCameraView.cameraPosition = newPos;
        m_context->activeCameraView.cameraFront = cam.front;
        m_context->activeCameraView.viewMatrix = glm::lookAt(newPos, m_orbitTarget, cam.up);
        
        float aspect = 16.0f / 9.0f;
        if (m_context->engine && m_context->engine->GetRenderManager()) {
            uint32_t w = m_context->engine->GetRenderManager()->GetWindowWidth();
            uint32_t h = m_context->engine->GetRenderManager()->GetWindowHeight();
            if (h > 0) aspect = (float)w / (float)h;
        }
        m_context->activeCameraView.projectionMatrix = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f);
        m_context->activeCameraView.projectionMatrix[1][1] *= -1; // Y flip per Vulkan
        
        // --- INTERAZIONE / RAYCASTING (Software Style) ---
        if (!m_isMouseOverUI && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            ImVec2 mousePos = ImGui::GetMousePos();
            uint32_t width = m_context->engine->GetRenderManager()->GetWindowWidth();
            uint32_t height = m_context->engine->GetRenderManager()->GetWindowHeight();
            
            float x = (2.0f * mousePos.x) / width - 1.0f;
            float y = 1.0f - (2.0f * mousePos.y) / height;
            
            glm::mat4 invProj = glm::inverse(m_context->activeCameraView.projectionMatrix);
            glm::mat4 invView = glm::inverse(m_context->activeCameraView.viewMatrix);
            
            glm::vec4 rayClip = glm::vec4(x, y, -1.0, 1.0);
            glm::vec4 rayEye = invProj * rayClip;
            rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0, 0.0);
            glm::vec3 rayDir = glm::normalize(glm::vec3(invView * rayEye));
            
            glm::vec3 rayPos = m_context->activeCameraView.cameraPosition;
            
            // Simple Raymarch (0.05 steps, max 100 units)
            glm::vec3 currentPos = rayPos;
            glm::vec3 prevPos = rayPos;
            bool hit = false;
            
            for(int i = 0; i < 2000; ++i) {
                prevPos = currentPos;
                currentPos += rayDir * 0.05f;
                
                int bx = std::floor(currentPos.x);
                int by = std::floor(currentPos.y);
                int bz = std::floor(currentPos.z);
                
                if(bx >= 0 && bx < 16 && by >= 0 && by < 16 && bz >= 0 && bz < 16) {
                    if(m_context->forgeWorld->GetBlock(bx, by, bz) != fw::BlockType::Air) {
                        hit = true;
                        if (m_selectedTool == 1) { // Place
                            int px = std::floor(prevPos.x);
                            int py = std::floor(prevPos.y);
                            int pz = std::floor(prevPos.z);
                            if(px >= 0 && px < 16 && py >= 0 && py < 16 && pz >= 0 && pz < 16) {
                                m_context->forgeWorld->SetBlock(px, py, pz, (fw::BlockType)m_selectedColorIndex);
                            }
                        } else if (m_selectedTool == 2) { // Erase
                            m_context->forgeWorld->SetBlock(bx, by, bz, fw::BlockType::Air);
                        }
                        break;
                    }
                }
            }
            
            // Se hit = false e stiamo aggiungendo, permettiamo di piazzare sul "pavimento" base y=0
            if (!hit && m_selectedTool == 1 && rayDir.y < -0.001f) {
                float t = -rayPos.y / rayDir.y;
                glm::vec3 hitPos = rayPos + rayDir * t;
                int bx = std::floor(hitPos.x);
                int bz = std::floor(hitPos.z);
                if(bx >= 0 && bx < 16 && bz >= 0 && bz < 16) {
                    m_context->forgeWorld->SetBlock(bx, 0, bz, (fw::BlockType)m_selectedColorIndex);
                }
            }
        }
        
        break; // Only one camera
    }
    
    // Aggiorniamo il mondo ECS della Forge
    if (m_context->forgeWorld) {
        m_context->forgeWorld->Update(dt);
    }
}

void ForgeState::Render() {
    // L'aggiornamento della telecamera (viewMatrix, projectionMatrix) viene 
    // calcolato direttamente in Update() per la Forge.


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

    // --- TAVOLOZZA STRUMENTI ---
    ImGui::SetNextWindowPos(ImVec2(10.0f, 80.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(220.0f, 300.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Tavolozza")) {
        ImGui::Text("Strumenti Base");
        ImGui::Separator();
        
        ImGui::RadioButton("Seleziona", &m_selectedTool, 0);
        ImGui::RadioButton("Aggiungi Voxel", &m_selectedTool, 1);
        ImGui::RadioButton("Rimuovi Voxel", &m_selectedTool, 2);
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Cursore 3D (Coordinate)");
        ImGui::SliderInt("Asse X", &m_cursorX, 0, 15);
        ImGui::SliderInt("Asse Y", &m_cursorY, 0, 15);
        ImGui::SliderInt("Asse Z", &m_cursorZ, 0, 15);
        
        if (m_context && m_context->forgeWorld) {
            if (ImGui::Button("Piazza Blocco", ImVec2(100, 25))) {
                m_context->forgeWorld->SetBlock(m_cursorX, m_cursorY, m_cursorZ, (fw::BlockType)m_selectedColorIndex);
            }
            ImGui::SameLine();
            if (ImGui::Button("Rimuovi Blocco", ImVec2(100, 25))) {
                m_context->forgeWorld->SetBlock(m_cursorX, m_cursorY, m_cursorZ, fw::BlockType::Air);
            }
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Materiale PBR (Palette ID: %d)", m_selectedColorIndex);
        
        // Seleziona colore (1-255)
        if (ImGui::SliderInt("ID Colore", &m_selectedColorIndex, 1, 255)) {
            m_selectedColorIndex = std::clamp(m_selectedColorIndex, 1, 255);
        }
        
        if (m_context && m_context->forgeWorld) {
            auto& palette = m_context->forgeWorld->GetPalette();
            auto& mat = palette.materials[m_selectedColorIndex];
            
            // Converti fw::Vec3 a float array per ImGui
            float col[3] = {mat.baseColor.x, mat.baseColor.y, mat.baseColor.z};
            if (ImGui::ColorEdit3("Colore Base", col)) {
                mat.baseColor = {col[0], col[1], col[2]};
            }
            ImGui::SliderFloat("Roughness", &mat.roughness, 0.0f, 1.0f);
            ImGui::SliderFloat("Metallic", &mat.metallic, 0.0f, 1.0f);
            ImGui::SliderFloat("Emissive", &mat.emissiveStrength, 0.0f, 5.0f);
            
            if (mat.emissiveStrength > 0.01f) {
                float ecol[3] = {mat.emissiveColor.x, mat.emissiveColor.y, mat.emissiveColor.z};
                if (ImGui::ColorEdit3("Colore Luce", ecol)) {
                    mat.emissiveColor = {ecol[0], ecol[1], ecol[2]};
                }
            } else {
                // Sincronizza colore emissivo col base per comodità
                mat.emissiveColor = mat.baseColor;
            }
            
            ImGui::Spacing();
            ImGui::Text("Parametri Avanzati");
            ImGui::SliderFloat("Normal Intensity", &mat.normalIntensity, 0.0f, 5.0f);
            ImGui::SliderFloat("Alpha Cutoff", &mat.alphaCutoff, 0.0f, 1.0f);
            ImGui::SliderFloat("AO Strength", &mat.aoStrength, 0.0f, 1.0f);
            
            ImGui::Spacing();
            ImGui::Text("Trasparenza e Vernice");
            ImGui::SliderFloat("Clearcoat", &mat.clearcoat, 0.0f, 1.0f);
            ImGui::SliderFloat("Clearcoat Roughness", &mat.clearcoatRoughness, 0.0f, 1.0f);
            ImGui::SliderFloat("Transmission", &mat.transmission, 0.0f, 1.0f);
            if (mat.transmission > 0.0f) {
                ImGui::SliderFloat("IOR (Rifrazione)", &mat.ior, 1.0f, 3.0f);
            }
            
            ImGui::Spacing();
            
            // Forza rigenerazione del chunk (1 solo blocco in Forge)
            if (ImGui::Button("Applica Modifiche Materiale")) {
                m_context->forgeWorld->MarkChunkDirty(m_context->forgeWorld->GetRegistry().view<fw::VoxelChunkComponent>().front());
            }
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Salva Modello", ImVec2(-1, 30))) {
            std::cout << "[Forge] Salvataggio modello...\n";
            // TODO: Implementare esportazione .fwblock
        }
        } // Chiude if (ImGui::Begin("Tavolozza"))
        ImGui::End(); // Fine finestra Tavolozza
}
