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
    
    // Assicuriamoci che il mondo sia pulito da altri stati (es. PlayState)
    if (m_context && m_context->forgeWorld) {
        m_context->forgeWorld->ClearWorld();
        m_context->forgeWorld->CreateChunkEntity("WorkspaceBlock", {0.0f, 0.0f, 0.0f});
    }

    // Grid Mesh Setup
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
        auto view = m_context->forgeWorld->GetRegistry().view<fw::MeshComponent, fw::TransformComponent, fw::MetadataComponent>();
        for(auto e : view) {
            auto& meta = view.get<fw::MetadataComponent>(e);
            if (meta.name == "PreviewSphere") {
                auto& mesh = view.get<fw::MeshComponent>(e);
                auto& mat = m_context->forgeWorld->GetPalette().materials[m_selectedColorIndex];
                mesh.colorOverride[0] = mat.baseColor.x;
                mesh.colorOverride[1] = mat.baseColor.y;
                mesh.colorOverride[2] = mat.baseColor.z;
                mesh.colorOverride[3] = 0.6f; // Semitrasparente
                
                auto& trans = view.get<fw::TransformComponent>(e);
                trans.location.x = m_cursorX;
                trans.location.y = m_cursorY;
                trans.location.z = m_cursorZ;
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
        // --- GESTIONE TELECAMERA (Orbitale vs Prima Persona POV) ---
        if (m_isFirstPerson) {
            // First Person POV (WASD + Mouse Look tenendo premuto il tasto destro)
            if (!m_isMouseOverUI && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                m_fpYaw += io.MouseDelta.x * 0.2f;
                m_fpPitch -= io.MouseDelta.y * 0.2f;
                m_fpPitch = std::clamp(m_fpPitch, -89.0f, 89.0f);
                
                float speed = 15.0f * dt;
                if (ImGui::IsKeyDown(ImGuiKey_W)) m_fpPosition += cam.front * speed;
                if (ImGui::IsKeyDown(ImGuiKey_S)) m_fpPosition -= cam.front * speed;
                if (ImGui::IsKeyDown(ImGuiKey_A)) m_fpPosition -= cam.right * speed;
                if (ImGui::IsKeyDown(ImGuiKey_D)) m_fpPosition += cam.right * speed;
                if (ImGui::IsKeyDown(ImGuiKey_Space)) m_fpPosition += cam.up * speed;
                if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) m_fpPosition -= cam.up * speed;
            }
            
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
        
        // --- INTERAZIONE / RAYCASTING (Software Style) ---
        if (!m_isMouseOverUI && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            glm::vec3 rayPos = m_context->activeCameraView.cameraPosition;
            glm::vec3 rayDir;
            
            if (m_isFirstPerson) {
                // In POV spariamo il raggio esattamente in mezzo allo schermo (FPS style)
                rayDir = m_context->activeCameraView.cameraFront;
            } else {
                // In Orbitale usiamo la posizione del mouse
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
                rayDir = glm::normalize(glm::vec3(invView * rayEye));
            }
            
            // Simple Raymarch (0.05 steps, max 50 units in FPS, 100 in Orbit)
            float maxDist = m_isFirstPerson ? 10.0f : 100.0f; // POV ha range più corto, stile Minecraft
            glm::vec3 currentPos = rayPos;
            glm::vec3 prevPos = rayPos;
            bool hit = false;
            fw::BlockType hitBlock = fw::BlockType::Air;
            glm::vec3 normal = glm::vec3(0.0f);
            
            int maxSteps = (int)(maxDist / 0.05f);
            for (int i = 0; i < maxSteps; ++i) {
                currentPos += rayDir * 0.05f;
                int bx = std::floor(currentPos.x);
                int by = std::floor(currentPos.y);
                int bz = std::floor(currentPos.z);
                
                if (by >= 0 && by < 128) {
                    fw::BlockType block = m_context->forgeWorld->GetBlock(bx, by, bz);
                    if (block != fw::BlockType::Air) {
                        hit = true;
                        hitBlock = block;
                        
                        // Calcola normale basata sulla differenza tra prevPos e currentPos
                        int pbx = std::floor(prevPos.x);
                        int pby = std::floor(prevPos.y);
                        int pbz = std::floor(prevPos.z);
                        normal = glm::vec3(pbx - bx, pby - by, pbz - bz);
                        break;
                    }
                }
                prevPos = currentPos;
            }
            
            if (hit) {
                if (m_selectedTool == 1) { // Place
                    int px = std::floor(prevPos.x);
                    int py = std::floor(prevPos.y);
                    int pz = std::floor(prevPos.z);
                    if(px >= 0 && px < 16 && py >= 0 && py < 128 && pz >= 0 && pz < 16) {
                        m_context->forgeWorld->SetBlock(px, py, pz, (fw::BlockType)m_selectedColorIndex);
                    }
                } else if (m_selectedTool == 2) { // Erase
                    int bx = std::floor(currentPos.x);
                    int by = std::floor(currentPos.y);
                    int bz = std::floor(currentPos.z);
                    if(bx >= 0 && bx < 16 && by >= 0 && by < 128 && bz >= 0 && bz < 16) {
                        m_context->forgeWorld->SetBlock(bx, by, bz, fw::BlockType::Air);
                    }
                }
            } else if (m_selectedTool == 1) { // Place (nel vuoto)
                glm::vec3 placePos = rayPos + rayDir * (m_isFirstPerson ? 3.0f : m_orbitDistance);
                int px = std::floor(placePos.x);
                int py = std::floor(placePos.y);
                int pz = std::floor(placePos.z);
                if(px >= 0 && px < 16 && py >= 0 && py < 128 && pz >= 0 && pz < 16) {
                    m_context->forgeWorld->SetBlock(px, py, pz, (fw::BlockType)m_selectedColorIndex);
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
        ImGui::Text("Materiali PBR (Selezionato: %d / %d)", m_selectedColorIndex, m_activeMaterialsCount);
        
        // Pulsanti di navigazione tra i materiali attivi
        if (ImGui::ArrowButton("##left", ImGuiDir_Left)) {
            if (m_selectedColorIndex > 1) m_selectedColorIndex--;
        }
        ImGui::SameLine();
        ImGui::Text("ID %d", m_selectedColorIndex);
        ImGui::SameLine();
        if (ImGui::ArrowButton("##right", ImGuiDir_Right)) {
            if (m_selectedColorIndex < m_activeMaterialsCount) m_selectedColorIndex++;
        }
        
        ImGui::SameLine();
        if (ImGui::Button("+ Nuovo Materiale")) {
            if (m_activeMaterialsCount < 255) {
                m_activeMaterialsCount++;
                m_selectedColorIndex = m_activeMaterialsCount;
                
                // Inizializza il nuovo materiale copiando quello di default
                if (m_context && m_context->forgeWorld) {
                    auto& palette = m_context->forgeWorld->GetPalette();
                    palette.materials[m_activeMaterialsCount] = fw::ForgeMaterial(); // Reset ai default
                }
            }
        }
        
        if (m_context && m_context->forgeWorld) {
            auto& palette = m_context->forgeWorld->GetPalette();
            auto& mat = palette.materials[m_selectedColorIndex];
            
            // --- ANTEPRIMA DEL BLOCCO SELEZIONATO ---
            ImGui::Text("Anteprima Materiale (ID: %d)", m_selectedColorIndex);
            ImVec2 p = ImGui::GetCursorScreenPos();
            float previewSize = 100.0f;
            // Calcoliamo un colore approssimato considerando anche la roughness e metallic
            ImVec4 previewColor = ImVec4(mat.baseColor.x, mat.baseColor.y, mat.baseColor.z, 1.0f);
            
            // Disegniamo un grande "blocco" (quadrato smussato)
            ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + previewSize, p.y + previewSize), ImColor(previewColor), 8.0f);
            // Bordo
            ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + previewSize, p.y + previewSize), ImColor(255, 255, 255, 100), 8.0f, 0, 2.0f);
            
            // Aggiungiamo un po' di "brillantezza" se metallic/emissive sono alti
            if (mat.metallic > 0.5f || mat.emissiveStrength > 0.5f) {
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x + 10, p.y + 10), ImVec2(p.x + 30, p.y + 30), ImColor(255, 255, 255, 150), 4.0f);
            }
            
            ImGui::Dummy(ImVec2(previewSize, previewSize)); // Spazio occupato dall'anteprima
            ImGui::Spacing();
            ImGui::Separator();
            // ------------------------------------------

            bool paletteChanged = false;
            
            // Converti fw::Vec3 a float array per ImGui
            float col[3] = {mat.baseColor.x, mat.baseColor.y, mat.baseColor.z};
            if (ImGui::ColorEdit3("Colore Base", col)) {
                mat.baseColor = {col[0], col[1], col[2]};
                paletteChanged = true;
            }
            if (ImGui::SliderFloat("Roughness", &mat.roughness, 0.0f, 1.0f)) paletteChanged = true;
            if (ImGui::SliderFloat("Metallic", &mat.metallic, 0.0f, 1.0f)) paletteChanged = true;
            if (ImGui::SliderFloat("Emissive", &mat.emissiveStrength, 0.0f, 5.0f)) paletteChanged = true;
            
            if (mat.emissiveStrength > 0.01f) {
                float ecol[3] = {mat.emissiveColor.x, mat.emissiveColor.y, mat.emissiveColor.z};
                if (ImGui::ColorEdit3("Colore Luce", ecol)) {
                    mat.emissiveColor = {ecol[0], ecol[1], ecol[2]};
                    paletteChanged = true;
                }
            } else {
                // Sincronizza colore emissivo col base per comodità
                mat.emissiveColor = mat.baseColor;
            }
            
            if (paletteChanged) {
                // Notifica a tutti i chunk di rigenerare la mesh coi nuovi colori
                auto& reg = m_context->forgeWorld->GetRegistry();
                auto chunks = reg.view<fw::VoxelChunkComponent>();
                for (auto e : chunks) {
                    if (!reg.all_of<fw::ChunkDirtyComponent>(e)) {
                        reg.emplace<fw::ChunkDirtyComponent>(e);
                    } else {
                        reg.get<fw::ChunkDirtyComponent>(e).needsRebuild = true;
                    }
                }
            }

            ImGui::Spacing();
            ImGui::Text("Parametri Avanzati");
            if (ImGui::SliderFloat("Normal Intensity", &mat.normalIntensity, 0.0f, 5.0f)) paletteChanged = true;
            if (ImGui::SliderFloat("Alpha Cutoff", &mat.alphaCutoff, 0.0f, 1.0f)) paletteChanged = true;
            if (ImGui::SliderFloat("AO Strength", &mat.aoStrength, 0.0f, 1.0f)) paletteChanged = true;
            
            ImGui::Spacing();
            ImGui::Text("Trasparenza e Vernice");
            if (ImGui::SliderFloat("Clearcoat", &mat.clearcoat, 0.0f, 1.0f)) paletteChanged = true;
            if (ImGui::SliderFloat("Clearcoat Roughness", &mat.clearcoatRoughness, 0.0f, 1.0f)) paletteChanged = true;
            if (ImGui::SliderFloat("Transmission", &mat.transmission, 0.0f, 1.0f)) paletteChanged = true;
            if (mat.transmission > 0.0f) {
                if (ImGui::SliderFloat("IOR (Rifrazione)", &mat.ior, 1.0f, 3.0f)) paletteChanged = true;
            }
            
            ImGui::Spacing();
            
            // Forza rigenerazione del chunk se richiesto esplicitamente
            if (ImGui::Button("Applica Modifiche Materiale")) {
                m_context->forgeWorld->MarkChunkDirty(m_context->forgeWorld->GetRegistry().view<fw::VoxelChunkComponent>().front());
            }
        } // Chiude if (m_context && m_context->forgeWorld)
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Esportazione Asset");
        
        ImGui::InputText("Nome", m_structureNameBuffer, IM_ARRAYSIZE(m_structureNameBuffer));
        ImGui::RadioButton("Struttura PBR (Mondo)", &m_exportPlacementMode, 0);
        ImGui::RadioButton("MiniVoxel (Oggetto)", &m_exportPlacementMode, 1);

        if (ImGui::Button("Salva in Inventario DEV", ImVec2(-1, 30))) {
            std::string nameStr(m_structureNameBuffer);
            if (!nameStr.empty()) {
                std::cout << "[Forge] Salvataggio modello: " << nameStr << "\n";
                // Salvataggio fisico su disco nella cartella assets/blocks/
                m_context->forgeWorld->SaveStructure(nameStr, m_exportPlacementMode);
                m_context->devInventory.push_back({nameStr, m_exportPlacementMode});
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
}
