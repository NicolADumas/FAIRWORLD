#include "pch.h"
#include "ForgeState.h"
#include "SharedContext.h"
#include "DeviceManager.h"
#include "StateManager.h"
#include "HubState.h"
#include "FAIRWORLD.h"
#include "RenderManager.h"
#include "ForgeWorld.h"
#include "BlockRegistry.h"
#include "MaterialRegistry.h"
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
#include <fstream>
#include "json.hpp"

ForgeState::ForgeState(SharedContext* context) : m_context(context) {
    std::cout << "[ForgeState] Costruito.\n";
}

ForgeState::~ForgeState() {
    std::cout << "[ForgeState] Distrutto.\n";
    // Ripristina il ForgeWorld precedente nel context e rilascia il mondo privato
    if (m_context) {
        // Salva il workspace Forge prima di uscire
        if (m_ownedForgeWorld) {
            m_ownedForgeWorld->SetSaveDirectory("saves/forge");
            m_ownedForgeWorld->ClearWorld(true); // Salva le sculture correnti
        }
        m_context->forgeWorld = m_previousForgeWorld; // Ripristina il mondo precedente
        m_context->isForgeMode = false;
    }
    m_ownedForgeWorld.reset(); // Libera il ForgeWorld privato
}

bool ForgeState::Init() {
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
    m_context->engine->SetGameMode(GameMode::Play);
    m_context->deviceManager->requireFreeCursor = true;
    
    auto cameraEntity = m_registry.create();
    m_registry.emplace<NameComponent>(cameraEntity, "ForgeCamera");
    m_registry.emplace<TransformComponent>(cameraEntity, 8.0f, 8.0f, 24.0f);
    
    auto& cam = m_registry.emplace<CameraComponent>(cameraEntity);
    cam.yaw   = -90.0f;
    cam.pitch =   0.0f;
    
    m_systems.push_back(std::make_unique<fw::CameraSyncSystem>());
    m_systems.push_back(std::make_unique<fw::CameraSystem>());

    // --- BUG FIX #3: ForgeWorld ISOLATO ---
    // Salva il ForgeWorld precedente (PlayState/MapState) e crea uno NUOVO e PULITO
    m_previousForgeWorld = m_context->forgeWorld;
    m_ownedForgeWorld = std::make_unique<fw::ForgeWorld>();
    m_ownedForgeWorld->Initialize(m_context);
    m_ownedForgeWorld->SetSaveDirectory("saves/forge");
    // Prima carica eventuali sculture salvate (senza creare chunk sporchi)
    // poi punta il context al nuovo mondo vergine
    m_context->forgeWorld = m_ownedForgeWorld.get();
    std::cout << "[ForgeState] ForgeWorld privato e isolato creato. Nessuna contaminazione da stati precedenti.\n";

    m_assetBrowser.Initialize();

    // Grid Mesh Setup
    auto gridMesh = fw::MeshGenerators::MakeGridBox(16, 16, 16, 0.05f);
    m_context->forgeWorld->EnqueueDeferredMesh("WorkspaceGrid", {0.0f, 0.0f, 0.0f}, std::move(gridMesh));

    m_lastPreviewIndex = m_selectedColorIndex;
    auto previewMesh = fw::MeshGenerators::MakeVoxelPreview(m_selectedColorIndex, m_context);
    m_context->forgeWorld->EnqueueDeferredMesh("PreviewSphere", { 8.0f, 0.0f, 8.0f }, std::move(previewMesh));
    
    // --- CARICAMENTO NOMI BLOCCHI DA JSON ---
    std::ifstream file("assets/definitions/blocks.json");
    if (file.is_open()) {
        try {
            nlohmann::json j = nlohmann::json::parse(file);
            if (j.contains("blocks") && j["blocks"].is_array()) {
                m_activeMaterialsCount = 0;
                for (const auto& b : j["blocks"]) {
                    if (b.contains("id") && b.contains("name")) {
                        int id = b["id"].get<int>();
                        std::string name = b["name"].get<std::string>();
                        m_blockNames[id] = name;
                        if (id > m_activeMaterialsCount) m_activeMaterialsCount = id;
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[ForgeState] Errore caricamento blocks.json: " << e.what() << "\n";
        }
    }
    
    if (m_activeMaterialsCount == 0) m_activeMaterialsCount = 1; // Fallback

    return true;
}

void ForgeState::Update(float dt) {
    if (!m_context) return;
    
    // Attiviamo il bypass della skybox nel renderer
    m_context->isForgeMode = true;

    if (m_selectedColorIndex != m_lastPreviewIndex) {
        UpdatePreviewMesh(m_selectedColorIndex);
        m_lastPreviewIndex = m_selectedColorIndex;
    }

    // --- ESECUZIONE SISTEMI ECS PER FREE-CAM ---
    for (auto& system : m_systems) {
        system->Update(m_registry, m_context, dt);
    }

    // Aggiorniamo l'engine (fisica, input)
    if (m_context->engine) {
        m_context->engine->Update(dt);
    }
    
    // --- ASSET BROWSER (Key 'B') ---
    static bool bKeyWasDown = false;
    bool bKeyDown = (GetAsyncKeyState('B') & 0x8000) != 0;
    if (bKeyDown && !bKeyWasDown) {
        m_showAssetBrowser = !m_showAssetBrowser;
        if (m_showAssetBrowser) m_assetBrowser.RefreshAssets();
    }
    bKeyWasDown = bKeyDown;
    
    glm::vec3 rayTargetPos(-1000.0f);
    bool validRayTarget = false;
    
    // --- TELECAMERA ORBITALE E RAYCASTING ---
    auto view = m_registry.view<CameraComponent, TransformComponent>();
    for (auto entity : view) {
        auto& cam = view.get<CameraComponent>(entity);
        auto& trans = view.get<TransformComponent>(entity);
        
        ImGuiIO& io = ImGui::GetIO();
        m_isMouseOverUI = io.WantCaptureMouse;
        
        // Toggle camera with V
        if (ImGui::IsKeyPressed(ImGuiKey_V)) {
            m_isFirstPerson = !m_isFirstPerson;
        }
        
        // --- GESTIONE TELECAMERA (Orbitale vs Prima Persona POV) ---
        if (m_isFirstPerson) {
            // First Person POV (WASD + Mouse Look con Toggle Tasto Destro)
            if (!m_isMouseOverUI && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                m_fpCursorLocked = !m_fpCursorLocked;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                m_fpCursorLocked = false;
            }
            
            if (m_context->deviceManager) {
                m_context->deviceManager->requireFreeCursor = !m_fpCursorLocked;
            }

            if (m_fpCursorLocked) {
                m_fpYaw += io.MouseDelta.x * 0.2f;
                m_fpPitch -= io.MouseDelta.y * 0.2f;
                m_fpPitch = std::clamp(m_fpPitch, -89.0f, 89.0f);
            }
            
            float speed = 15.0f * dt;
            if (ImGui::IsKeyDown(ImGuiKey_W)) m_fpPosition += cam.front * speed;
            if (ImGui::IsKeyDown(ImGuiKey_S)) m_fpPosition -= cam.front * speed;
            if (ImGui::IsKeyDown(ImGuiKey_A)) m_fpPosition -= cam.right * speed;
            if (ImGui::IsKeyDown(ImGuiKey_D)) m_fpPosition += cam.right * speed;
            if (ImGui::IsKeyDown(ImGuiKey_Space)) m_fpPosition += cam.up * speed;
            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) m_fpPosition -= cam.up * speed;
            
            float radYaw = glm::radians(m_fpYaw);
            float radPitch = glm::radians(m_fpPitch);
            cam.front.x = cos(radYaw) * cos(radPitch);
            cam.front.y = sin(radPitch);
            cam.front.z = sin(radYaw) * cos(radPitch);
            cam.front = glm::normalize(cam.front);
            cam.right = glm::normalize(glm::cross(cam.front, glm::vec3(0.0f, 1.0f, 0.0f)));
            cam.up    = glm::normalize(glm::cross(cam.right, cam.front));
            
            trans.x = m_fpPosition.x;
            trans.y = m_fpPosition.y;
            trans.z = m_fpPosition.z;
            
            m_context->activeCameraView.cameraPosition = m_fpPosition;
            m_context->activeCameraView.cameraFront = cam.front;
            m_context->activeCameraView.viewMatrix = glm::lookAt(m_fpPosition, m_fpPosition + cam.front, cam.up);
            
        } else {
            // Modalità Orbitale (CAD)
            if (!m_isMouseOverUI && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                m_orbitYaw += io.MouseDelta.x * 0.5f;
                m_orbitPitch += io.MouseDelta.y * 0.5f;
                m_orbitPitch = std::clamp(m_orbitPitch, -89.0f, 89.0f);
            }
            if (!m_isMouseOverUI) {
                m_orbitDistance -= io.MouseWheel * 2.0f;
                m_orbitDistance = std::clamp(m_orbitDistance, 2.0f, 100.0f);
            }
            if (!m_isMouseOverUI && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                float pitchRad2 = glm::radians(m_orbitPitch);
                float yawRad2 = glm::radians(m_orbitYaw);
                glm::vec3 camPos2;
                camPos2.x = m_orbitTarget.x + m_orbitDistance * cos(pitchRad2) * cos(yawRad2);
                camPos2.y = m_orbitTarget.y + m_orbitDistance * sin(pitchRad2);
                camPos2.z = m_orbitTarget.z + m_orbitDistance * cos(pitchRad2) * sin(yawRad2);
                
                glm::vec3 front2 = glm::normalize(m_orbitTarget - camPos2);
                glm::vec3 right2 = glm::normalize(glm::cross(front2, glm::vec3(0,1,0)));
                glm::vec3 up2 = glm::cross(right2, front2);
                
                float panSpeed = m_orbitDistance * 0.005f;
                m_orbitTarget -= right2 * io.MouseDelta.x * panSpeed;
                m_orbitTarget += up2 * io.MouseDelta.y * panSpeed;
            }
            
            float radYaw = glm::radians(m_orbitYaw);
            float radPitch = glm::radians(m_orbitPitch);
            
            glm::vec3 offset;
            offset.x = m_orbitDistance * cos(radPitch) * cos(radYaw);
            offset.y = m_orbitDistance * sin(radPitch);
            offset.z = m_orbitDistance * cos(radPitch) * sin(radYaw);
            
            glm::vec3 newPos = m_orbitTarget + offset;
            
            trans.x = newPos.x;
            trans.y = newPos.y;
            trans.z = newPos.z;
            
            cam.front = glm::normalize(m_orbitTarget - newPos);
            cam.right = glm::normalize(glm::cross(cam.front, glm::vec3(0.0f, 1.0f, 0.0f)));
            cam.up    = glm::normalize(glm::cross(cam.right, cam.front));
            
            m_context->activeCameraView.cameraPosition = newPos;
            m_context->activeCameraView.cameraFront = cam.front;
            m_context->activeCameraView.viewMatrix = glm::lookAt(newPos, m_orbitTarget, cam.up);
        }
        
        float aspect = 16.0f / 9.0f;
        if (m_context->engine && m_context->engine->GetRenderManager()) {
            uint32_t w = m_context->engine->GetRenderManager()->GetWindowWidth();
            uint32_t h = m_context->engine->GetRenderManager()->GetWindowHeight();
            if (h > 0) aspect = (float)w / (float)h;
        }
        m_context->activeCameraView.projectionMatrix = glm::perspective(glm::radians(m_cameraFov), aspect, 0.1f, 1000.0f);
        m_context->activeCameraView.projectionMatrix[1][1] *= -1; // Y flip per Vulkan
        
        // --- CALCOLO RAYCAST (OGNI FRAME) ---
        if (!m_isMouseOverUI) {
            glm::vec3 rayPos = m_context->activeCameraView.cameraPosition;
            glm::vec3 rayDir;
            
            if (m_isFirstPerson) {
                rayDir = m_context->activeCameraView.cameraFront;
            } else {
                ImVec2 mousePos = ImGui::GetMousePos();
                uint32_t width = m_context->engine->GetRenderManager()->GetWindowWidth();
                uint32_t height = m_context->engine->GetRenderManager()->GetWindowHeight();
                
                float x = (2.0f * mousePos.x) / width - 1.0f;
                float y = 1.0f - (2.0f * mousePos.y) / height;
                
                glm::mat4 unFlippedProj = glm::perspective(glm::radians(m_cameraFov), aspect, 0.1f, 1000.0f);
                glm::mat4 invProj = glm::inverse(unFlippedProj);
                glm::mat4 invView = glm::inverse(m_context->activeCameraView.viewMatrix);
                
                glm::vec4 rayClip = glm::vec4(x, y, -1.0, 1.0);
                glm::vec4 rayEye = invProj * rayClip;
                rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0, 0.0);
                rayDir = glm::normalize(glm::vec3(invView * rayEye));
            }
            
            // Simple Raymarch (0.05 steps, max 50 units in FPS, 100 in Orbit)
            float maxDist = m_isFirstPerson ? 10.0f : 100.0f;
            glm::vec3 currentPos = rayPos;
            glm::vec3 prevPos = rayPos;
            bool hit = false;
            
            int maxSteps = (int)(maxDist / 0.05f);
            for (int i = 0; i < maxSteps; ++i) {
                currentPos += rayDir * 0.05f;
                int bx = std::floor(currentPos.x);
                int by = std::floor(currentPos.y);
                int bz = std::floor(currentPos.z);
                
                if (by >= 0 && by < 128 && bx >= 0 && bx < 16 && bz >= 0 && bz < 16) {
                    if (m_context->forgeWorld->GetBlock(bx, by, bz) != fw::BlockType::Air) {
                        hit = true;
                        break;
                    }
                }
                prevPos = currentPos;
            }
            
            if (hit) {
                validRayTarget = true;
                if (m_selectedTool == 1) { // Place
                    rayTargetPos = glm::vec3(std::floor(prevPos.x), std::floor(prevPos.y), std::floor(prevPos.z));
                } else if (m_selectedTool == 2) { // Erase
                    rayTargetPos = glm::vec3(std::floor(currentPos.x), std::floor(currentPos.y), std::floor(currentPos.z));
                } else { // Select (Default)
                    rayTargetPos = glm::vec3(std::floor(currentPos.x), std::floor(currentPos.y), std::floor(currentPos.z));
                }
            } else if (m_selectedTool == 1) { // Place (nel vuoto)
                glm::vec3 placePos = rayPos + rayDir * (m_isFirstPerson ? 3.0f : m_orbitDistance);
                rayTargetPos = glm::vec3(std::floor(placePos.x), std::floor(placePos.y), std::floor(placePos.z));
                validRayTarget = true;
            }
            
            // --- KEYBOARD CURSOR LOGIC (Camera-Relative) ---
            static float move_timer = 0.0f;
            move_timer -= dt;
            
            bool isUp = (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
            bool isDown = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
            bool isLeft = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
            bool isRight = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
            bool isTab = (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
            bool isShift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            bool isEnter = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
            
            static bool enter_was_down = false;

            if ((m_controlMode == 0 || m_controlMode == 2) && (isUp || isDown || isLeft || isRight || isTab || isShift) && move_timer <= 0.0f) {
                // Determina la direzione cardinale in base alla telecamera
                glm::vec3 flatFront = glm::normalize(glm::vec3(cam.front.x, 0.0f, cam.front.z));
                glm::vec3 flatRight = glm::normalize(glm::vec3(cam.right.x, 0.0f, cam.right.z));
                
                glm::vec3 cardinalFront(0,0,0);
                if (std::abs(flatFront.x) > std::abs(flatFront.z)) {
                    cardinalFront.x = flatFront.x > 0 ? 1.0f : -1.0f;
                } else {
                    cardinalFront.z = flatFront.z > 0 ? 1.0f : -1.0f;
                }
                
                glm::vec3 cardinalRight(0,0,0);
                if (std::abs(flatRight.x) > std::abs(flatRight.z)) {
                    cardinalRight.x = flatRight.x > 0 ? 1.0f : -1.0f;
                } else {
                    cardinalRight.z = flatRight.z > 0 ? 1.0f : -1.0f;
                }
                
                if (isUp) m_keyboardCursorPos += cardinalFront;
                if (isDown) m_keyboardCursorPos -= cardinalFront;
                if (isRight) m_keyboardCursorPos += cardinalRight;
                if (isLeft) m_keyboardCursorPos -= cardinalRight;
                if (isTab) m_keyboardCursorPos.y += 1.0f;
                if (isShift) m_keyboardCursorPos.y -= 1.0f;
                
                if (m_controlMode == 0) m_useKeyboardCursor = true;
                move_timer = 0.15f; // Cooldown per non schizzare via velocemente
            }
            
            if (m_controlMode == 0 || m_controlMode == 1) {
                if (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f || ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    if (m_controlMode == 0) m_useKeyboardCursor = false;
                }
            }

            if (m_controlMode == 1) m_useKeyboardCursor = false;
            else if (m_controlMode == 2) m_useKeyboardCursor = true;

            if (m_useKeyboardCursor) {
                validRayTarget = true;
                rayTargetPos = m_keyboardCursorPos;
            } else {
                // Sincronizza il cursore testiera con l'ultimo hit del mouse
                if (validRayTarget) {
                    m_keyboardCursorPos = rayTargetPos;
                }
            }

            // --- AZIONE AL CLICK O INVIO ---
            if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) && validRayTarget) || (m_useKeyboardCursor && isEnter && !enter_was_down)) {
                int px = (int)rayTargetPos.x;
                int py = (int)rayTargetPos.y;
                int pz = (int)rayTargetPos.z;
                
                if(px >= 0 && px < 16 && py >= 0 && py < 128 && pz >= 0 && pz < 16) {
                    if (m_selectedTool == 1) { // Place
                        m_context->forgeWorld->SetBlock(px, py, pz, (fw::BlockType)m_selectedColorIndex);
                    } else if (m_selectedTool == 2) { // Erase
                        m_context->forgeWorld->SetBlock(px, py, pz, fw::BlockType::Air);
                    }
                }
            }
            enter_was_down = isEnter;
        }
        
        break; // Only one camera
    }
    
    // --- AGGIORNA ANTEPRIMA VISIVA ---
    if (m_context->forgeWorld) {
        auto& registry = m_context->forgeWorld->GetRegistry();
        if (m_previewEntity == entt::null || !registry.valid(m_previewEntity)) {
            auto viewWorld = registry.view<fw::MeshComponent, fw::MetadataComponent>();
            for(auto e : viewWorld) {
                auto& meta = viewWorld.get<fw::MetadataComponent>(e);
                if (meta.name == "PreviewSphere") {
                    m_previewEntity = e;
                    break;
                }
            }
        }
        
        if (m_previewEntity != entt::null && registry.valid(m_previewEntity)) {
            auto& mesh = registry.get<fw::MeshComponent>(m_previewEntity);
            auto& trans = registry.get<fw::TransformComponent>(m_previewEntity);
            
            if (validRayTarget) {
                // Offset di 0.5f per centrare il wireframe/cubo sulle coordinate intere
                trans.location.x = rayTargetPos.x + 0.5f;
                trans.location.y = rayTargetPos.y + 0.5f;
                trans.location.z = rayTargetPos.z + 0.5f;
                
                auto& mat = m_context->materialRegistry->GetMaterial(m_selectedColorIndex);
                mesh.colorOverride[0] = mat.baseColorFallback.x;
                mesh.colorOverride[1] = mat.baseColorFallback.y;
                mesh.colorOverride[2] = mat.baseColorFallback.z;
                mesh.colorOverride[3] = 1.0f; // Alpha
            } else {
                trans.location.y = -100.0f; // Nascondi
            }
        }
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
        if (ImGui::Button("Asset Browser Globale", ImVec2(-1, 30))) {
            m_showAssetBrowser = true;
        }
        if (ImGui::Button("Biome Designer", ImVec2(-1, 30))) {
            m_showBiomeDesigner = true;
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Telecamera / POV");
        
        // Modalità Telecamera
        int camMode = m_isFirstPerson ? 1 : 0;
        if (ImGui::RadioButton("Orbitale (CAD)", &camMode, 0)) {
            if (m_isFirstPerson) {
                // Switching from First Person to Orbital
                m_orbitTarget = glm::vec3(8.0f, 8.0f, 8.0f); // Sempre al centro della griglia
                
                glm::vec3 offset = m_fpPosition - m_orbitTarget;
                m_orbitPitch = glm::degrees(asin(std::clamp(offset.y / m_orbitDistance, -1.0f, 1.0f)));
                m_orbitYaw = glm::degrees(atan2(offset.z, offset.x));
            }
            m_isFirstPerson = false;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Prima Persona (POV)", &camMode, 1)) {
            if (!m_isFirstPerson) {
                // Switching from Orbital to First Person
                m_fpPosition = m_context->activeCameraView.cameraPosition;
                glm::vec3 front = m_context->activeCameraView.cameraFront;
                
                m_fpPitch = glm::degrees(asin(std::clamp(front.y, -1.0f, 1.0f)));
                m_fpYaw = glm::degrees(atan2(front.z, front.x));
            }
            m_isFirstPerson = true;
        }
        
        ImGui::SliderFloat("FOV (Field of View)", &m_cameraFov, 30.0f, 110.0f);
        
        ImGui::Spacing();
        
        ImGui::Text("Metodo di Posizionamento");
        ImGui::RadioButton("Automatico", &m_controlMode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Mouse (Raycast)", &m_controlMode, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Tastiera (Frecce)", &m_controlMode, 2);
        
        ImGui::Spacing();
        ImGui::Separator();

        const auto& blocks = m_context->blockRegistry->GetAllBlocks();
        int activeBlocksCount = (int)blocks.size();
        
        std::string blockName = "Sconosciuto";
        if (m_selectedColorIndex > 0 && m_selectedColorIndex <= activeBlocksCount) {
            blockName = blocks[m_selectedColorIndex - 1].displayName;
        }

        ImGui::Text("Blocco PBR Selezionato: %d - %s", m_selectedColorIndex, blockName.c_str());
        
        // Pulsanti di navigazione tra i materiali attivi
        if (ImGui::ArrowButton("##left", ImGuiDir_Left)) {
            if (m_selectedColorIndex > 1) m_selectedColorIndex--;
        }
        ImGui::SameLine();
        ImGui::Text("ID %d", m_selectedColorIndex);
        ImGui::SameLine();
        if (ImGui::ArrowButton("##right", ImGuiDir_Right)) {
            if (m_selectedColorIndex < activeBlocksCount) m_selectedColorIndex++;
        }
        
        if (m_selectedColorIndex > 0 && m_selectedColorIndex <= activeBlocksCount) {
            const auto& def = blocks[m_selectedColorIndex - 1];
            
            // --- ANTEPRIMA DEL BLOCCO SELEZIONATO ---
            ImGui::Text("Anteprima Materiale (ID: %d)", def.id);
            ImVec2 p = ImGui::GetCursorScreenPos();
            float previewSize = 100.0f;
            auto& mat = m_context->materialRegistry->GetMaterial(def.id);
            ImVec4 previewColor = ImVec4(mat.baseColorFallback.x, mat.baseColorFallback.y, mat.baseColorFallback.z, 1.0f);
            
            ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + previewSize, p.y + previewSize), ImColor(previewColor), 8.0f);
            ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + previewSize, p.y + previewSize), ImColor(255, 255, 255, 100), 8.0f, 0, 2.0f);
            
            if (mat.metallicFallback > 0.5f || mat.emissiveStrength > 0.5f) {
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x + 10, p.y + 10), ImVec2(p.x + 30, p.y + 30), ImColor(255, 255, 255, 150), 4.0f);
            }
            
            ImGui::Dummy(ImVec2(previewSize, previewSize)); // Spazio occupato dall'anteprima
        }
        ImGui::Separator();
        ImGui::Text("Controlli Telecamera");
        int cameraModeInt = m_isFirstPerson ? 0 : 1;
        if (ImGui::RadioButton("Mouse (Volo Libero)", &cameraModeInt, 0)) {
            m_isFirstPerson = true;
        }
        if (ImGui::RadioButton("Frecce (Orbit)", &cameraModeInt, 1)) {
            m_isFirstPerson = false;
        }
        ImGui::TextDisabled("Puoi usare 'V' per scambiare modalita'");
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Esportazione Asset");
        
        ImGui::InputText("Nome", m_structureNameBuffer, IM_ARRAYSIZE(m_structureNameBuffer));
        ImGui::RadioButton("Struttura PBR (Mondo)", &m_exportPlacementMode, 0);
        ImGui::RadioButton("MiniVoxel (Oggetto)", &m_exportPlacementMode, 1);

        if (ImGui::Button("Salva Voxel Asset", ImVec2(-1, 30))) {
            std::string nameStr(m_structureNameBuffer);
            if (!nameStr.empty()) {
                std::cout << "[Forge] Salvataggio FWBLOCK: " << nameStr << "\n";
                m_context->forgeWorld->SaveStructure(nameStr, m_exportPlacementMode, 0, 0, 0); // Add Pivot parameters if needed
                m_context->devInventory.push_back({nameStr, m_exportPlacementMode});
            }
        }
        
        if (ImGui::Button("Carica (JSON)", ImVec2(-1, 30))) {
            std::string nameStr(m_structureNameBuffer);
            if (!nameStr.empty()) {
                std::cout << "[Forge] Caricamento JSON: " << nameStr << "\n";
                m_context->forgeWorld->LoadStructureJSON(nameStr);
            }
        }
    } // Chiude if (ImGui::Begin("Tavolozza"))
    ImGui::End(); // Fine finestra Tavolozza

    // --- NUOVA FINESTRA: GESTIONE INVENTARIO DEV/PLAY ---
    ImGui::SetNextWindowPos(ImVec2(10.0f, 400.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350.0f, 300.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Gestione Inventario")) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Inventario DEV (Forge)");
        ImGui::Separator();
        
        if (m_context->devInventory.empty()) {
            ImGui::TextDisabled("Nessun oggetto creato in DevMode.");
        } else {
            for (size_t i = 0; i < m_context->devInventory.size(); ++i) {
                auto& item = m_context->devInventory[i];
                ImGui::Text("%s [%s]", item.name.c_str(), item.type == 0 ? "Struttura" : "MiniVoxel");
                ImGui::SameLine(ImGui::GetWindowWidth() - 140);
                
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Button("Sposta -> PlayMode")) {
                    InventoryItem newItem;
                    newItem.type = (item.type == 0) ? ItemType::Structure : ItemType::MiniVoxel;
                    newItem.stringId = item.name;
                    newItem.count = 1;
                    
                    if (m_context->engine->GetPlayer().inventory.AddItem(newItem)) {
                        std::cout << "[Forge] Spostato " << item.name << " nello zaino del giocatore.\n";
                        m_context->devInventory.erase(m_context->devInventory.begin() + i);
                    } else {
                        std::cerr << "[Forge] Zaino pieno!\n";
                    }
                    ImGui::PopID();
                    break; // Interrompi ciclo per evitare invalidazione iteratore
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::End();

    if (m_showAssetBrowser) {
        m_assetBrowser.DrawUI(&m_showAssetBrowser, nullptr, m_context->forgeWorld);
        std::string spawnTarget = m_assetBrowser.GetSelectedAssetToSpawn();
        if (!spawnTarget.empty()) {
            if (m_context->forgeWorld) {
                std::filesystem::path p(spawnTarget);
                m_context->forgeWorld->LoadStructureAsVoxels(p.stem().string(), 8, 8, 8);
            }
            m_assetBrowser.ClearSelectedAsset();
            m_showAssetBrowser = false;
        }
    }
}

void ForgeState::UpdatePreviewMesh(int colorIndex) {
    if (!m_context || !m_context->forgeWorld) return;
    auto previewMesh = fw::MeshGenerators::MakeVoxelPreview(colorIndex, m_context);
    previewMesh.colorOverride[3] = 0.6f;
    
    auto& registry = m_context->forgeWorld->GetRegistry();
    if (m_previewEntity != entt::null && registry.valid(m_previewEntity)) {
        auto& mesh = registry.get<fw::MeshComponent>(m_previewEntity);
        mesh.vertices = std::move(previewMesh.vertices);
        m_context->forgeWorld->MarkChunkDirty(m_previewEntity);
    } else {
        // Fallback if not cached yet
        auto view = registry.view<fw::MeshComponent, fw::MetadataComponent>();
        for(auto e : view) {
            auto& meta = registry.get<fw::MetadataComponent>(e);
            if (meta.name == "PreviewSphere") {
                m_previewEntity = e;
                auto& mesh = registry.get<fw::MeshComponent>(e);
                mesh.vertices = std::move(previewMesh.vertices);
                m_context->forgeWorld->MarkChunkDirty(e);
                break;
            }
        }
    }
}
