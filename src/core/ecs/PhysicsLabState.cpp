#define NOMINMAX
#include "PhysicsLabState.h"
#include "FAIRWORLD.h"
#include "imgui.h"
#include "ImGuizmo.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <filesystem>
#include <fstream>
#include "Components.h"
#include "ForgeComponents.h"
#include "ForgeWorld.h"
#include "RenderManager.h"
#include "DeviceManager.h"

PhysicsLabState::PhysicsLabState(SharedContext* context) : m_context(context) {
}

bool PhysicsLabState::Init() {
    std::cout << "[PhysicsLabState] Init\n";
    m_originalWorld = m_context->forgeWorld;
    m_labWorld = new fw::ForgeWorld();
    m_context->forgeWorld = m_labWorld;

    RefreshDevStructures();
    ScanTemplates();

    m_orbitTarget = glm::vec3(0, 2.0f, 0);
    m_orbitDistance = 15.0f;
    m_orbitYaw = 45.0f;
    m_orbitPitch = 30.0f;
    return true;
}

void PhysicsLabState::RefreshDevStructures() {
    m_devStructures.clear();
    try {
        if (std::filesystem::exists("assets/blocks")) {
            for (const auto& entry : std::filesystem::directory_iterator("assets/blocks")) {
                if (entry.path().extension() == ".fwblock") {
                    DevStructure ds;
                    ds.name = entry.path().stem().string();
                    ds.type = DevStructureType::Voxel;
                    m_devStructures.push_back(ds);
                }
            }
        }
    } catch(...) {}
}

void PhysicsLabState::ScanTemplates() {
    m_customTemplates.clear();
    try {
        if (!std::filesystem::exists("saves/lab/templates")) {
            std::filesystem::create_directories("saves/lab/templates");
        }
        for (const auto& entry : std::filesystem::directory_iterator("saves/lab/templates")) {
            if (entry.path().extension() == ".json") {
                m_customTemplates.push_back(entry.path().stem().string());
            }
        }
    } catch (...) {}
}

void PhysicsLabState::SaveRig(const std::string& path) {}
void PhysicsLabState::LoadRig(const std::string& path) {}
void PhysicsLabState::StartSimulation() { m_simulateMode = true; }
void PhysicsLabState::StopSimulation() { m_simulateMode = false; }

void PhysicsLabState::DrawLeftPanel() {
    // Il pannello Dev Inventory è già gestito nel Render(), qui mettiamo l'Hierarchy
}

void PhysicsLabState::DrawRightPanel() {
    // Gestito parzialmente in Render() per ora
}

void PhysicsLabState::DrawSkeletonHierarchy() {
    ImGui::SetNextWindowPos(ImVec2(10, 480), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Skeleton Hierarchy")) {
        for (size_t i = 0; i < m_skeleton.m_joints.size(); ++i) {
            std::string label = m_skeleton.m_joints[i].name + "##" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), m_selectedJointIndex == (int)i)) {
                m_selectedJointIndex = (int)i;
            }
        }
    }
    ImGui::End();
}

void PhysicsLabState::DrawJointProperties(fw::JointData& joint) {
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 360, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Joint Properties")) {
        ImGui::Text("Nome: %s", joint.name.c_str());
        ImGui::Text("Mesh: %s", joint.meshPath.empty() ? "Nessuna" : joint.meshPath.c_str());
        
        int type = (int)joint.type;
        const char* types[] = { "HINGE", "UNIVERSAL", "BALL" };
        if (ImGui::Combo("Tipo Giunto", &type, types, 3)) {
            joint.type = (fw::RigJointType)type;
        }
        
        ImGui::DragFloat3("Limit Min", joint.limitMin, 1.0f, -180.0f, 180.0f);
        ImGui::DragFloat3("Limit Max", joint.limitMax, 1.0f, -180.0f, 180.0f);
        
        ImGui::Separator();
        ImGui::Text("Gizmo Mode:");
        ImGui::RadioButton("Translate", &m_gizmoMode, 1); ImGui::SameLine();
        ImGui::RadioButton("Rotate", &m_gizmoMode, 2); ImGui::SameLine();
        ImGui::RadioButton("Off", &m_gizmoMode, 0);
        
        if (ImGui::Button("BAKE & EXPORT", ImVec2(-1, 40))) {
            SaveRig(std::string("saves/lab/templates/") + m_newTemplateName + "_baked.fwanimal");
        }
    }
    ImGui::End();
}

void PhysicsLabState::DrawTimeline() {
    ImGui::SetNextWindowPos(ImVec2(370, ImGui::GetIO().DisplaySize.y - 150), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x - 740, 140), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Animation Timeline & Notifies")) {
        ImGui::SliderInt("Frame", &m_currentFrame, 0, m_maxFrames);
        if (ImGui::Button(m_isPlaying ? "Pause" : "Play")) m_isPlaying = !m_isPlaying;
        ImGui::SameLine();
        ImGui::Text("Markers: Right-click on timeline to add Hitbox events.");
        
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 size = ImVec2(ImGui::GetContentRegionAvail().x, 40);
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(50, 50, 50, 255));
        
        // Disegna i marker
        for (auto& notify : m_skeleton.m_notifies) {
            float xPos = p.x + (notify.frame / (float)m_maxFrames) * size.x;
            ImU32 col = (notify.type == fw::NotifyType::START_HITBOX) ? IM_COL32(255, 0, 0, 255) : IM_COL32(0, 0, 255, 255);
            ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(xPos - 5, p.y + 10), ImVec2(xPos + 5, p.y + 10), ImVec2(xPos, p.y + 20), col);
        }
        
        if (ImGui::IsMouseHoveringRect(p, ImVec2(p.x + size.x, p.y + size.y)) && ImGui::IsMouseClicked(1)) {
            ImGui::OpenPopup("AddNotifyMenu");
        }
        
        if (ImGui::BeginPopup("AddNotifyMenu")) {
            if (ImGui::MenuItem("START_HITBOX")) {
                m_skeleton.m_notifies.push_back({m_currentFrame, fw::NotifyType::START_HITBOX, m_selectedJointIndex});
            }
            if (ImGui::MenuItem("END_HITBOX")) {
                m_skeleton.m_notifies.push_back({m_currentFrame, fw::NotifyType::END_HITBOX, m_selectedJointIndex});
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}

void PhysicsLabState::DrawViewportOverlay() {
    if (m_renderMode != 1 && m_renderMode != 2) return;
    
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    glm::mat4 vp = m_context->activeCameraView.projectionMatrix * m_context->activeCameraView.viewMatrix;
    float w = (float)m_context->engine->GetRenderManager()->GetWindowWidth();
    float h = (float)m_context->engine->GetRenderManager()->GetWindowHeight();
    
    auto Project = [&](const glm::vec3& p) -> ImVec2 {
        glm::vec4 clip = vp * glm::vec4(p, 1.0f);
        if (clip.w > 0.001f) {
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            return ImVec2((ndc.x * 0.5f + 0.5f) * w, (1.0f - (ndc.y * 0.5f + 0.5f)) * h);
        }
        return ImVec2(-1, -1);
    };

    const auto& transforms = m_skeleton.GetGlobalTransforms();
    
    // Disegna scheletro
    if (m_renderMode == 1) {
        for (size_t i = 0; i < m_skeleton.m_joints.size(); ++i) {
            glm::vec3 p1 = glm::vec3(transforms[i][3]);
            ImVec2 sp1 = Project(p1);
            if (sp1.x >= 0) drawList->AddCircleFilled(sp1, 5.0f, IM_COL32(255, 255, 0, 255));
            
            if (m_skeleton.m_joints[i].parentIndex >= 0) {
                glm::vec3 p0 = glm::vec3(transforms[m_skeleton.m_joints[i].parentIndex][3]);
                ImVec2 sp0 = Project(p0);
                if (sp0.x >= 0 && sp1.x >= 0) {
                    drawList->AddLine(sp0, sp1, IM_COL32(200, 200, 200, 255), 2.0f);
                }
            }
        }
    }
}

void PhysicsLabState::DrawAngularLimitsGizmo(fw::JointData& joint, const glm::mat4& globalMat) {
    if (m_renderMode != 2) return;
    // Disegno archi in viewport overlay gestito separatamente o qui
}

void PhysicsLabState::HandleArcPicking(fw::JointData& joint, const glm::mat4& globalMat, const glm::vec3& rayOrigin, const glm::vec3& rayDir) {}

void PhysicsLabState::GenerateBipedSkeleton() {}
void PhysicsLabState::GenerateCentipedeSkeleton(int segments) {}
void PhysicsLabState::GenerateSnakeSkeleton(int segments) {}
void PhysicsLabState::GenerateSpiderSkeleton() {}

void PhysicsLabState::Update(float dt) {
    using namespace entt::literals;
    if (m_context->deviceManager->IsActionActive("PAUSE"_hs)) {
        if (m_originalWorld) {
            m_context->forgeWorld = m_originalWorld;
            m_originalWorld = nullptr;
            if (m_context->engine && m_context->engine->GetRenderManager()) {
                m_context->engine->GetRenderManager()->InvalidateForgeCache();
            }
        }
        m_context->deviceManager->requireFreeCursor = false;
        m_context->engine->SetGameMode(GameMode::Hub);
        return;
    }

    if (!m_simulateMode) {
        if (!ImGui::GetIO().WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            uint32_t width = m_context->engine->GetRenderManager()->GetWindowWidth();
            uint32_t height = m_context->engine->GetRenderManager()->GetWindowHeight();
            
            if (width > 0 && height > 0) {
                ImVec2 mousePos = ImGui::GetMousePos();
                float ndcX = (2.0f * mousePos.x) / width - 1.0f;
                float ndcY = 1.0f - (2.0f * mousePos.y) / height;
                
                glm::vec4 rayClip = glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
                glm::mat4 invProj = glm::inverse(m_context->activeCameraView.projectionMatrix);
                glm::mat4 invView = glm::inverse(m_context->activeCameraView.viewMatrix);
                
                glm::vec4 rayEye = invProj * rayClip;
                rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
                
                glm::vec3 rayDir = glm::normalize(glm::vec3(invView * rayEye));
                glm::vec3 rayOrigin = m_context->activeCameraView.cameraPosition;
                
                float bestDist = 1000.0f;
                int bestJoint = -1;
                const auto& transforms = m_skeleton.GetGlobalTransforms();
                
                for (size_t i = 0; i < transforms.size(); ++i) {
                    glm::vec3 jointPos = glm::vec3(transforms[i][3]);
                    glm::vec3 L = jointPos - rayOrigin;
                    float tca = glm::dot(L, rayDir);
                    if (tca < 0) continue;
                    float d2 = glm::dot(L, L) - tca * tca;
                    float radius2 = 0.5f * 0.5f; 
                    if (d2 > radius2) continue;
                    
                    float thc = std::sqrt(radius2 - d2);
                    float t0 = tca - thc;
                    if (t0 < bestDist) {
                        bestDist = t0;
                        bestJoint = static_cast<int>(i);
                    }
                }
                if (bestJoint != -1) {
                    m_selectedJointIndex = bestJoint;
                }
            }
        }
        
        if (m_skeleton.m_joints.size() > 0) {
            if (m_previewAnimation && !m_simulateMode) {
                m_animationTime += dt;
                auto& dofState = m_skeleton.GetDofState();
                
                if (m_animationPreset == 0) { 
                    float speed = 5.0f;
                    float waveScale = 1.5f; 
                    int dofOffset = 0;
                    for (size_t i = 0; i < m_skeleton.m_joints.size(); ++i) {
                        const auto& joint = m_skeleton.m_joints[i];
                        int dofs = joint.GetDofCount();
                        if (dofs > 0) {
                            if (joint.name.find("Body") != std::string::npos) {
                                float phase = i * waveScale;
                                dofState[dofOffset] = sin(m_animationTime * speed + phase) * 0.4f; 
                            } else if (joint.name.find("Leg") != std::string::npos) {
                                float phase = joint.parentIndex * waveScale;
                                if (joint.name.find("L") != std::string::npos) {
                                    dofState[dofOffset] = sin(m_animationTime * speed + phase) * 0.8f;
                                } else {
                                    dofState[dofOffset] = sin(m_animationTime * speed + phase + 3.1415f) * 0.8f;
                                }
                            }
                        }
                        dofOffset += dofs;
                    }
                } else if (m_animationPreset == 1) { 
                    float speed = 6.0f;
                    int dofOffset = 0;
                    for (size_t i = 0; i < m_skeleton.m_joints.size(); ++i) {
                        const auto& joint = m_skeleton.m_joints[i];
                        int dofs = joint.GetDofCount();
                        if (dofs > 0) {
                            if (joint.name == "ShoulderL" || joint.name == "HipR") {
                                dofState[dofOffset] = sin(m_animationTime * speed) * 0.8f;
                            } else if (joint.name == "ShoulderR" || joint.name == "HipL") {
                                dofState[dofOffset] = sin(m_animationTime * speed + 3.1415f) * 0.8f;
                            } else if (joint.name == "KneeL") {
                                dofState[dofOffset] = abs(sin(m_animationTime * speed + 3.1415f)) * 1.0f;
                            } else if (joint.name == "KneeR") {
                                dofState[dofOffset] = abs(sin(m_animationTime * speed)) * 1.0f;
                            }
                        }
                        dofOffset += dofs;
                    }
                }
            }
            
            m_skeleton.UpdateForwardKinematics();
            
            for (size_t i = 0; i < m_skeleton.m_joints.size(); ++i) {
                auto& joint = m_skeleton.m_joints[i];
                if (joint.voxelEntity != 0xFFFFFFFF) {
                    entt::entity e = static_cast<entt::entity>(joint.voxelEntity);
                    if (m_labWorld->GetRegistry().valid(e)) {
                        auto* tc = m_labWorld->GetRegistry().try_get<TransformComponent>(e);
                        if (tc) {
                            glm::mat4 finalMat = m_skeleton.GetGlobalTransforms()[i] * joint.meshOffset;
                            glm::vec3 scale, translation, skew;
                            glm::vec4 perspective;
                            glm::quat orientation;
                            glm::decompose(finalMat, scale, orientation, translation, skew, perspective);
                            
                            tc->x = translation.x;
                            tc->y = translation.y;
                            tc->z = translation.z;
                            tc->rotation = glm::conjugate(orientation); 
                            
                            auto* meshComp = m_labWorld->GetRegistry().try_get<fw::MeshComponent>(e);
                            if (meshComp) {
                                if (m_renderMode == 1) { 
                                    meshComp->colorOverride[0] = 0.5f;
                                    meshComp->colorOverride[1] = 0.5f;
                                    meshComp->colorOverride[2] = 0.5f;
                                    meshComp->colorOverride[3] = 0.3f; 
                                } else { 
                                    meshComp->colorOverride[0] = 1.0f;
                                    meshComp->colorOverride[1] = 1.0f;
                                    meshComp->colorOverride[2] = 1.0f;
                                    meshComp->colorOverride[3] = 1.0f; 
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    ImGuiIO& io = ImGui::GetIO();
    bool mouseOverUI = io.WantCaptureMouse;

    if (!mouseOverUI && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        m_orbitYaw   -= io.MouseDelta.x * 0.4f;
        m_orbitPitch += io.MouseDelta.y * 0.4f;
    }
    if (!mouseOverUI) {
        m_orbitDistance -= io.MouseWheel * 1.5f;
        m_orbitDistance = glm::clamp(m_orbitDistance, 1.0f, 80.0f);
    }
    if (!mouseOverUI && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        glm::vec3 camRight = glm::normalize(glm::cross(glm::normalize(m_orbitTarget - glm::vec3(0,0,0)), glm::vec3(0,1,0)));
        float yawRad2 = glm::radians(m_orbitYaw);
        float pitchRad2 = glm::radians(m_orbitPitch);
        glm::vec3 camPos2;
        camPos2.x = m_orbitTarget.x + m_orbitDistance * cos(pitchRad2) * cos(yawRad2);
        camPos2.y = m_orbitTarget.y + m_orbitDistance * sin(pitchRad2);
        camPos2.z = m_orbitTarget.z + m_orbitDistance * cos(pitchRad2) * sin(yawRad2);
        glm::vec3 front2 = glm::normalize(m_orbitTarget - camPos2);
        glm::vec3 right2 = glm::normalize(glm::cross(front2, glm::vec3(0,1,0)));
        glm::vec3 up2 = glm::cross(right2, front2);
        float panSpeed = m_orbitDistance * 0.001f;
        m_orbitTarget -= right2 * io.MouseDelta.x * panSpeed;
        m_orbitTarget += up2 * io.MouseDelta.y * panSpeed;
    }

    m_orbitPitch = glm::clamp(m_orbitPitch, -89.0f, 89.0f);

    float yawRad = glm::radians(m_orbitYaw);
    float pitchRad = glm::radians(m_orbitPitch);
    glm::vec3 camPos;
    camPos.x = m_orbitTarget.x + m_orbitDistance * cos(pitchRad) * cos(yawRad);
    camPos.y = m_orbitTarget.y + m_orbitDistance * sin(pitchRad);
    camPos.z = m_orbitTarget.z + m_orbitDistance * cos(pitchRad) * sin(yawRad);

    glm::vec3 camFront = glm::normalize(m_orbitTarget - camPos);
    m_context->activeCameraView.viewMatrix = glm::lookAt(camPos, m_orbitTarget, glm::vec3(0,1,0));
    m_context->activeCameraView.cameraPosition = camPos;
    m_context->activeCameraView.cameraFront = camFront;
    float aspect = 16.0f / 9.0f;
    uint32_t width = m_context->engine->GetRenderManager()->GetWindowWidth();
    uint32_t height = m_context->engine->GetRenderManager()->GetWindowHeight();
    if (height > 0) aspect = (float)width / (float)height;
    
    m_context->activeCameraView.projectionMatrix = glm::perspective(glm::radians(m_labFov), aspect, 0.1f, 1000.0f);
    m_context->activeCameraView.projectionMatrix[1][1] *= -1; 
}

void PhysicsLabState::Render() {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 250), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Rigging Editor", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Anteprima Animazione");
        ImGui::Separator();
        
        bool prevSim = m_simulateMode;
        if (m_simulateMode) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
        
        if (ImGui::Button(m_simulateMode ? "STOP SIMULATION" : "SIMULATE (Jolt Physics)", ImVec2(-1, 30))) {
            m_simulateMode = !m_simulateMode;
        }
        ImGui::PopStyleColor();
        
        if (m_simulateMode != prevSim) {
            if (m_simulateMode) StartSimulation();
            else StopSimulation();
        }
        
        ImGui::Separator();
        if (!m_simulateMode) {
            ImGui::Checkbox("Preview Procedural Anim", &m_previewAnimation);
            ImGui::Combo("Anim Preset", &m_animationPreset, "Centipede\0Biped Walk\0");
            ImGui::Separator();
            
            ImGui::Text("Model Setting");
            if (ImGui::Button("Genera Bipede Standard", ImVec2(-1, 25))) {
                GenerateBipedSkeleton();
            }
            if (ImGui::Button("Genera Centopiedi (10 segmenti)", ImVec2(-1, 25))) {
                GenerateCentipedeSkeleton(10);
            }
            if (ImGui::Button("Genera Serpente (12 vertebre)", ImVec2(-1, 25))) {
                GenerateSnakeSkeleton(12);
            }
            if (ImGui::Button("Genera Ragno (8 zampe)", ImVec2(-1, 25))) {
                GenerateSpiderSkeleton();
            }
            ImGui::Separator();
            
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Template Utente");
            ImGui::InputText("Nome", m_newTemplateName, sizeof(m_newTemplateName));
            if (ImGui::Button("Salva come Nuovo Template", ImVec2(-1, 25))) {
                std::string name = m_newTemplateName;
                if (!name.empty()) {
                    SaveRig("saves/lab/templates/" + name + ".json");
                    m_newTemplateName[0] = '\0';
                    ScanTemplates();
                }
            }
            
            for (const auto& tmpl : m_customTemplates) {
                if (ImGui::Button(("Carica " + tmpl).c_str(), ImVec2(-1, 22))) {
                    LoadRig("saves/lab/templates/" + tmpl + ".json");
                }
            }
            ImGui::Separator();
            
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.8f, 1.0f));
            if (ImGui::Button("Vai al Forge (Crea Modelli Voxel)", ImVec2(-1, 35))) {
                if (m_originalWorld) {
                    m_context->forgeWorld = m_originalWorld;
                    m_originalWorld = nullptr;
                    if (m_context->engine && m_context->engine->GetRenderManager()) {
                        m_context->engine->GetRenderManager()->InvalidateForgeCache();
                    }
                }
                m_context->deviceManager->requireFreeCursor = false;
                m_context->engine->SetGameMode(GameMode::Dev);
            }
            ImGui::PopStyleColor();
            ImGui::Separator();
        }
        
        ImGui::Text("Render Mode");
        ImGui::RadioButton("Mesh Mode", &m_renderMode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Skeleton (X-Ray)", &m_renderMode, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Physics Debug", &m_renderMode, 2);
        ImGui::SameLine();
        ImGui::RadioButton("Texture Mode (WIP)", &m_renderMode, 3);
        
        ImGui::Separator();
        ImGui::Text("Camera");
        ImGui::SliderFloat("FOV##lab", &m_labFov, 20.0f, 120.0f);
        ImGui::Text("Tasto Destro: Ruota | Scroll: Zoom | Tasto Medio: Pan");
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(10, 270), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 200), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Dev Inventory (.fwblock)", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextDisabled("Doppio clic per assegnare al giunto");
        if (ImGui::Button("Aggiorna Lista")) RefreshDevStructures();
        
        ImGui::Separator();
        ImGui::BeginChild("DevInvList", ImVec2(0, 0), true);
        if (m_devStructures.empty()) {
            ImGui::TextDisabled("Nessun Voxel in assets/blocks/");
        } else {
            for (size_t i = 0; i < m_devStructures.size(); ++i) {
                if (ImGui::Selectable(m_devStructures[i].name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0) && m_selectedJointIndex >= 0 && m_selectedJointIndex < m_skeleton.m_joints.size()) {
                        fw::JointData& j = m_skeleton.m_joints[m_selectedJointIndex];
                        j.meshPath = m_devStructures[i].name; 
                        
                        if (j.voxelEntity != 0xFFFFFFFF) {
                            entt::entity e = static_cast<entt::entity>(j.voxelEntity);
                            if (m_labWorld->GetRegistry().valid(e)) m_labWorld->GetRegistry().destroy(e);
                        }
                        
                        entt::entity spawned = m_labWorld->LoadStructureAsPrefab(j.meshPath, fw::Vec3(0,0,0));
                        j.voxelEntity = static_cast<uint32_t>(spawned);
                        std::cout << "[PhysicsLab] Assegnato " << m_devStructures[i].name << " al giunto " << m_selectedJointIndex << "\n";
                    }
                }
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();

    DrawSkeletonHierarchy();
    if (m_selectedJointIndex >= 0 && m_selectedJointIndex < static_cast<int>(m_skeleton.m_joints.size())) {
        fw::JointData& joint = m_skeleton.m_joints[m_selectedJointIndex];
        DrawJointProperties(joint);
        
        uint32_t width = m_context->engine->GetRenderManager()->GetWindowWidth();
        uint32_t height = m_context->engine->GetRenderManager()->GetWindowHeight();
        
        if (width > 0 && height > 0) {
            ImVec2 mousePos = ImGui::GetMousePos();
            float ndcX = (2.0f * mousePos.x) / width - 1.0f;
            float ndcY = 1.0f - (2.0f * mousePos.y) / height;
            
            glm::vec4 rayClip = glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
            glm::mat4 invProj = glm::inverse(m_context->activeCameraView.projectionMatrix);
            glm::mat4 invView = glm::inverse(m_context->activeCameraView.viewMatrix);
            
            glm::vec4 rayEye = invProj * rayClip;
            rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
            
            glm::vec3 rayDir = glm::normalize(glm::vec3(invView * rayEye));
            glm::vec3 rayOrigin = m_context->activeCameraView.cameraPosition;
            
            glm::mat4 baseGlobalMat;
            if (joint.parentIndex >= 0) {
                baseGlobalMat = m_skeleton.GetGlobalTransforms()[joint.parentIndex] * joint.localRestTransform;
            } else {
                baseGlobalMat = joint.localRestTransform;
            }
            
            HandleArcPicking(joint, baseGlobalMat, rayOrigin, rayDir);
            DrawAngularLimitsGizmo(joint, baseGlobalMat);
            
            if (m_gizmoMode > 0) {
                ImGuizmo::SetOrthographic(false);
                ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
                float windowWidth = (float)ImGui::GetWindowWidth();
                float windowHeight = (float)ImGui::GetWindowHeight();
                ImGuizmo::SetRect(0, 0, windowWidth, windowHeight);

                glm::mat4 parentGlobalMat = glm::mat4(1.0f);
                if (joint.parentIndex >= 0) {
                    parentGlobalMat = m_skeleton.GetGlobalTransforms()[joint.parentIndex];
                }
                
                glm::mat4 workMat = parentGlobalMat * joint.meshOffset;
                
                ImGuizmo::OPERATION op = (m_gizmoMode == 1) ? ImGuizmo::TRANSLATE : ImGuizmo::ROTATE;
                ImGuizmo::Manipulate(glm::value_ptr(m_context->activeCameraView.viewMatrix),
                                     glm::value_ptr(m_context->activeCameraView.projectionMatrix),
                                     op, ImGuizmo::LOCAL, glm::value_ptr(workMat));
                                     
                if (ImGuizmo::IsUsing()) {
                    joint.meshOffset = glm::inverse(parentGlobalMat) * workMat;
                }
            }
        }
    }
    
    DrawTimeline();
    DrawViewportOverlay();
}
