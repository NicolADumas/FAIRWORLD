#include "pch.h"
#include "BlockMakerState.h"
#include "SharedContext.h"
#include "StateManager.h"
#include "HubState.h"
#include "FAIRWORLD.h"
#include "RenderManager.h"
#include "ForgeWorld.h"
#include "JobSystem.h"
#include "AsyncInput.h"
#include "Systems.h"
#include "BlockRegistry.h"
#include <shellapi.h>  // ShellExecuteA - apre cartelle/file con l'OS
#include "MaterialRegistry.h"
#include "VulkanDmaManager.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <iostream>
#include "imgui.h"
#include <windows.h>
#include <commdlg.h>
#include <filesystem>
#include <thread>
#include <chrono>

namespace {
    std::string BrowseForImage() {
        OPENFILENAMEA ofn;
        CHAR szFile[260] = { 0 };
        ZeroMemory(&ofn, sizeof(OPENFILENAMEA));
        ofn.lStructSize = sizeof(OPENFILENAMEA);
        ofn.hwndOwner = NULL;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = "Immagini\0*.png;*.jpg;*.jpeg;*.tga\0Tutti i file\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

        if (GetOpenFileNameA(&ofn) == TRUE) {
            return std::string(ofn.lpstrFile);
        }
        return "";
    }

    std::string CopyTextureToAssets(const std::string& srcPath, uint8_t blockId, const std::string& typeSuffix) {
        if (srcPath.empty()) return "";
        try {
            std::filesystem::create_directories("assets/textures");
            std::string ext = std::filesystem::path(srcPath).extension().string();
            std::string destName = "block_" + std::to_string(blockId) + "_" + typeSuffix + ext;
            std::string destPath = "assets/textures/" + destName;
            
            // Sovrascrivi se esiste
            std::filesystem::copy_file(srcPath, destPath, std::filesystem::copy_options::overwrite_existing);
            return destPath;
        } catch (const std::exception& e) {
            std::cerr << "[BlockMaker] Errore copia file: " << e.what() << "\n";
            return srcPath; // Fallback al path originale se la copia fallisce
        }
    }
}

BlockMakerState::BlockMakerState(SharedContext* context) : m_context(context) {
    std::cout << "[BlockMakerState] Creato.\n";
}

BlockMakerState::~BlockMakerState() {
    std::cout << "[BlockMakerState] Distrutto.\n";
}

entt::registry* BlockMakerState::GetRegistry() {
    return &m_context->forgeWorld->GetRegistry();
}

bool BlockMakerState::Init() {
    std::cout << "[BlockMakerState] Inizializzazione...\n";

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
                rm->GetDevice(), rm->GetTransferQueue(), rm->GetTransferCommandPool(),
                rm->GetStagingRingBuffer(), rm->GetStagingDeviceMemory(), rm->GetMappedStagingData(),
                rm->GetStagingBufferSize(), rm->GetGlobalVramBuffer(), rm->GetQueueMutex()
            );
        }
    }

    if (!m_context->forgeWorld) {
        m_context->forgeWorld = new fw::ForgeWorld();
        m_context->forgeWorld->Initialize(m_context);
    }

    // Set the specific state flag for RenderManager to know we're in Void Room mode
    m_context->isBlockMakerMode = true;

    // Spawn preview environment (floor plane)
    auto floorMesh = fw::MeshGenerators::MakeCube(10.0f);
    for (auto& v : floorMesh.vertices) {
        v.color = {0.15f, 0.15f, 0.15f, 1.0f}; // Dark gray grid/floor
        v.roughMetal = {0.9f, 0.0f}; // Rough, non-metallic
    }
    
    entt::entity envEntity = m_context->forgeWorld->GetRegistry().create();
    fw::TransformComponent envTrans;
    envTrans.location = fw::Vec3{0.0f, -0.55f, 0.0f}; // Top face at Y = -0.5f
    envTrans.scale = fw::Vec3{1.0f, 0.01f, 1.0f}; // Flatten to a plane
    m_context->forgeWorld->GetRegistry().emplace<fw::TransformComponent>(envEntity, envTrans);
    m_context->forgeWorld->GetRegistry().emplace<fw::MetadataComponent>(envEntity, "BlockMakerEnv");
    m_context->forgeWorld->EnqueueDeferredMesh("BlockMakerEnv", glm::vec3(0.0f, -0.55f, 0.0f), std::move(floorMesh), nullptr, envEntity);

    // Spawn preview entity in registry
    UpdatePreviewMesh();

    return true;
}

void BlockMakerState::Update(float dt) {
    if (!m_context) return;

    m_context->isForgeMode = true; // Use forge rendering pipeline
    m_context->isBlockMakerMode = true; // Isolate rendering to Void Room
    m_context->previewLightDir = m_previewLightDir;

    // Handle Input for Orbital Camera via ImGui
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureMouse) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            m_orbitYaw += io.MouseDelta.x * 0.2f;
            m_orbitPitch += io.MouseDelta.y * 0.2f;
            m_orbitPitch = glm::clamp(m_orbitPitch, -89.0f, 89.0f);
        }
        if (io.MouseWheel != 0.0f) {
            m_orbitDistance -= io.MouseWheel * 0.5f;
            m_orbitDistance = glm::clamp(m_orbitDistance, 2.0f, 20.0f);
        }
    }

    // Calculate Orbital Camera
    glm::vec3 offset;
    offset.x = m_orbitDistance * cos(glm::radians(m_orbitPitch)) * cos(glm::radians(m_orbitYaw));
    offset.y = m_orbitDistance * sin(glm::radians(m_orbitPitch));
    offset.z = m_orbitDistance * cos(glm::radians(m_orbitPitch)) * sin(glm::radians(m_orbitYaw));
    
    glm::vec3 cameraPos = m_orbitTarget + offset;
    m_context->activeCameraView.viewMatrix = glm::lookAt(cameraPos, m_orbitTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    m_context->activeCameraView.cameraPosition = cameraPos;
    m_context->activeCameraView.cameraFront = glm::normalize(m_orbitTarget - cameraPos);

    float aspect = 16.0f / 9.0f;
    if (m_context->engine && m_context->engine->GetRenderManager()) {
        uint32_t w = m_context->engine->GetRenderManager()->GetWindowWidth();
        uint32_t h = m_context->engine->GetRenderManager()->GetWindowHeight();
        if (h > 0) aspect = (float)w / (float)h;
    }
    m_context->activeCameraView.projectionMatrix = glm::perspective(glm::radians(m_cameraFov), aspect, 0.1f, 1000.0f);
    m_context->activeCameraView.projectionMatrix[1][1] *= -1; // Vulkan Y-flip
    
    m_context->activeRegistry = &m_context->forgeWorld->GetRegistry();
    auto& m_registry = m_context->forgeWorld->GetRegistry();
    
    HandlePhysicsSimulation(dt);
    
    // Run local systems
    for (auto& sys : m_systems) {
        sys->Update(m_registry, m_context, dt);
    }

    if (m_saveMessageTimer > 0.0f) {
        m_saveMessageTimer -= dt;
    }
}

void BlockMakerState::HandlePhysicsSimulation(float dt) {
    if (!m_simulatePhysics) {
        m_simPosY = 0.0f;
        m_simVelY = 0.0f;
    } else {
        // Simple Physics simulation (Gravity + Bounciness)
        if (m_context && m_context->blockRegistry) {
            auto& def = m_context->blockRegistry->GetBlock(m_selectedBlockId);
            
            // Applica gravità scalandola per la massa se desiderato (più pesante = cade più velocemente in simulazioni non realistiche, 
            // ma nella fisica reale cade uguale. Facciamo cadere più velocemente per impatto visivo se massa è grande).
            // Oppure teniamo la gravità fissa e modifichiamo solo l'inerzia, ma per un cubo singolo facciamo una cosa visiva:
            float currentGrav = m_simGravity * (1.0f + (def.mass * 0.1f));
            
            m_simVelY += currentGrav * dt;
            m_simPosY += m_simVelY * dt;
            
            // Floor collision at Y = 0
            if (m_simPosY <= 0.0f) {
                m_simPosY = 0.0f;
                // Bounce
                if (def.bounciness > 0.0f) {
                    m_simVelY = -m_simVelY * def.bounciness;
                    // Stop jittering
                    if (abs(m_simVelY) < 0.5f) {
                        m_simVelY = 0.0f;
                    }
                } else {
                    m_simVelY = 0.0f;
                }
            }
        }
    }
    auto& m_registry = m_context->forgeWorld->GetRegistry();
    
    // Aggiorna la Transform dell'entità preview
    if (m_previewBlockEntity != entt::null && m_registry.valid(m_previewBlockEntity)) {
        auto& transform = m_registry.get<fw::TransformComponent>(m_previewBlockEntity);
        transform.location = fw::Vec3{0.0f, m_simPosY, 0.0f};
    }
}

void BlockMakerState::Render() {
    DrawUI();
}

void BlockMakerState::UpdatePreviewMesh() {
    if (!m_context || !m_context->forgeWorld || !m_context->blockRegistry) return;
    
    auto previewMesh = fw::MeshGenerators::MakeCube(1.0f);
    previewMesh.colorOverride[3] = 1.0f; // Enable color override? Wait, the shader uses colorOverride only if useColorOverride is set. Let's just set the vertex colors directly.
    
    fw::SimBlockDef& def = m_context->blockRegistry->GetBlockMutable(m_selectedBlockId);
    fw::PBRMaterialDef& mat = m_context->materialRegistry->GetMaterialMutable(m_selectedBlockId);
    
    // Inietta i dati fisici (PBR) nei vertici della mesh in base al MaterialRegistry
    for (auto& v : previewMesh.vertices) {
        v.materialID = (uint32_t)m_selectedBlockId;
        v.color = {mat.baseColorFallback.x, mat.baseColorFallback.y, mat.baseColorFallback.z, 1.0f};
        v.roughMetal = {mat.roughnessFallback, mat.metallicFallback};
        v.emissive = mat.emissiveStrength;
        v.ao = 1.0f; // Default AO
    }

    auto& m_registry = m_context->forgeWorld->GetRegistry();
    if (m_previewBlockEntity == entt::null || !m_registry.valid(m_previewBlockEntity)) {
        m_previewBlockEntity = m_registry.create();
        fw::TransformComponent trans;
        trans.location = fw::Vec3{0.0f, 0.0f, 0.0f};
        m_registry.emplace<fw::TransformComponent>(m_previewBlockEntity, trans);
        m_registry.emplace<fw::MetadataComponent>(m_previewBlockEntity, "PreviewBlock");
    }

    m_context->forgeWorld->EnqueueDeferredMesh("PreviewBlock", glm::vec3(0.0f, 0.0f, 0.0f), std::move(previewMesh), nullptr, m_previewBlockEntity);
}

void BlockMakerState::DrawUI() {
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400.0f, 600.0f), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Block Maker (Data-Driven)")) {
        if (ImGui::Button("< TORNA ALL'HUB")) {
            m_context->isBlockMakerMode = false;
            m_context->stateManager->ChangeState(std::make_unique<HubState>(m_context));
            ImGui::End();
            return;
        }
        
        ImGui::Separator();

        if (!m_context->blockRegistry) {
            ImGui::TextColored(ImVec4(1,0,0,1), "ERROR: BlockRegistry not found!");
            ImGui::End();
            return;
        }
        
        ImGui::Text("Editing Block ID: %d", m_selectedBlockId);
        int blockIdInt = (int)m_selectedBlockId;
        if (ImGui::SliderInt("Block ID", &blockIdInt, 0, 255)) {
            m_selectedBlockId = (uint8_t)blockIdInt;
            UpdatePreviewMesh();
        }

        // Recupera il blocco selezionato e aggiorna i campi di input temporanei se l'ID cambia (todo: serve caching)
        fw::SimBlockDef& def = m_context->blockRegistry->GetBlockMutable(m_selectedBlockId);
        
        // --- TABS ---
        if (ImGui::BeginTabBar("BlockTabs")) {
            
            // TAB: IDENTITA E GAMEPLAY
            if (ImGui::BeginTabItem("Identity")) {
                ImGui::Spacing();
                
                // Truncate per sicurezza 
                strncpy(m_inputStringId, def.stringId.c_str(), sizeof(m_inputStringId) - 1);
                strncpy(m_inputDisplayName, def.displayName.c_str(), sizeof(m_inputDisplayName) - 1);
                
                if (ImGui::InputText("String ID", m_inputStringId, sizeof(m_inputStringId))) {
                    def.stringId = m_inputStringId;
                }
                if (ImGui::InputText("Display Name", m_inputDisplayName, sizeof(m_inputDisplayName))) {
                    def.displayName = m_inputDisplayName;
                }
                
                ImGui::Separator();
                ImGui::Checkbox("Is Solid", &def.isSolid);
                ImGui::Checkbox("Is Transparent", &def.isTransparent);
                ImGui::SliderFloat("Light Emission", &def.lightEmissionLevel, 0.0f, 15.0f);
                
                ImGui::EndTabItem();
            }
            
            // TAB: PBR MATERIAL
            // TAB: PBR MATERIAL (GRAFICA E TEXTURE PACK)
            if (ImGui::BeginTabItem("Graphics (Texture Pack)")) {
                ImGui::Spacing();
                
                fw::PBRMaterialDef& mat = m_context->materialRegistry->GetMaterialMutable(m_selectedBlockId);
                
                bool isDirty = false;
                
                // Fallback Colors (utili prima dell'implementazione TexturePacker)
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Fallback / Solid Colors");
                float color[3] = { mat.baseColorFallback.x, mat.baseColorFallback.y, mat.baseColorFallback.z };
                if (ImGui::ColorEdit3("Base Color", color)) {
                    mat.baseColorFallback = {color[0], color[1], color[2]};
                    isDirty = true;
                }
                
                if (ImGui::SliderFloat("Metallic", &mat.metallicFallback, 0.0f, 1.0f)) isDirty = true;
                if (ImGui::SliderFloat("Roughness", &mat.roughnessFallback, 0.0f, 1.0f)) isDirty = true;
                if (ImGui::SliderFloat("Emissive Strength", &mat.emissiveStrength, 0.0f, 10.0f)) isDirty = true;
                
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "PBR Texture Maps");

                if (ImGui::Button("Apri Cartella Texture OS", ImVec2(-1, 30))) {
                    std::filesystem::create_directories("assets/textures");
                    ShellExecuteA(NULL, "open", "assets\\textures", NULL, NULL, SW_SHOWDEFAULT);
                }
                ImGui::Spacing();
                
                auto drawTextureField = [&](const char* label, std::string& pathRef, const std::string& typeSuffix) {
                    char buf[256];
                    strncpy(buf, pathRef.c_str(), sizeof(buf));
                    
                    ImGui::PushID(label);
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 100.0f);
                    if (ImGui::InputText("##path", buf, sizeof(buf))) {
                        pathRef = buf;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Sfoglia...", ImVec2(80, 0))) {
                        std::string picked = BrowseForImage();
                        if (!picked.empty()) {
                            m_isCopying = true;
                            m_copyProgress = 0.0f;
                            
                            // Copia il file e salva automaticamente
                            pathRef = CopyTextureToAssets(picked, m_selectedBlockId, typeSuffix);
                            m_context->materialRegistry->SaveToJson("assets/definitions/materials.json");
                            
                            m_copyProgress = 1.0f;
                            m_saveMessageTimer = 3.0f;
                        }
                    }
                    ImGui::PopID();
                    ImGui::Text("%s", label);
                };

                drawTextureField("Albedo Map", mat.albedoPath, "albedo");
                drawTextureField("Normal Map", mat.normalPath, "normal");
                drawTextureField("ORM Map (Occlusion, Roughness, Metallic)", mat.ormPath, "orm");
                
                // Barra di progresso simulata / Feedback visivo
                if (m_isCopying) {
                    ImGui::Spacing();
                    ImGui::ProgressBar(m_copyProgress, ImVec2(-1, 0), "Copia Texture in corso...");
                    if (m_copyProgress >= 1.0f) {
                        m_isCopying = false;
                    }
                }
                
                if (m_saveMessageTimer > 0.0f) {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Texture caricata e Materiale salvato automaticamente!");
                }
                
                if (isDirty) {
                    UpdatePreviewMesh();
                }
                
                ImGui::EndTabItem();
            }

            // TAB: PHYSICS & SIMULATION
            if (ImGui::BeginTabItem("Physics")) {
                ImGui::Spacing();
                
                ImGui::SliderFloat("Massa (kg)", &def.mass, 0.0f, 500.0f, "%.1f");
                ImGui::SliderFloat("Attrito", &def.friction, 0.0f, 1.0f);
                ImGui::SliderFloat("Rimbalzo", &def.bounciness, 0.0f, 1.0f);
                
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Termodinamica");
                ImGui::SliderFloat("Resistenza Termica", &def.thermal_resistance, 0.1f, 100.0f, "%.1f");
                ImGui::SliderFloat("Capacita' Termica", &def.thermal_capacity, 0.1f, 100.0f, "%.1f");
                
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.8f, 0.4f, 1.0f, 1.0f), "Simulation");
                
                if (ImGui::Button("Drop Block!")) {
                    m_simulatePhysics = true;
                    m_simPosY = 5.0f; // Sgancia il blocco da 5 metri
                    m_simVelY = 0.0f;
                }
                
                if (m_simulatePhysics) {
                    ImGui::SameLine();
                    if (ImGui::Button("Reset Drop")) {
                        m_simulatePhysics = false;
                        m_simPosY = 0.0f;
                    }
                }
                
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }
        
        ImGui::Separator();
        
        if (ImGui::Button("SAVE TO DISK (JSON)", ImVec2(-1, 30))) {
            m_context->blockRegistry->SaveToJson("assets/definitions/blocks.json");
            m_context->materialRegistry->SaveToJson("assets/definitions/materials.json");
            std::cout << "[BlockMaker] Dati salvati in assets/definitions/\n";
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Preview Lighting");
        
        // Gizmo-like Control per la Luce
        if (ImGui::DragFloat3("Light Direction", &m_previewLightDir.x, 0.01f, -1.0f, 1.0f)) {
            if (glm::length(m_previewLightDir) > 0.001f) {
                m_previewLightDir = glm::normalize(m_previewLightDir);
            } else {
                m_previewLightDir = glm::vec3(0, -1, 0);
            }
        }
    }
    ImGui::End();
}
