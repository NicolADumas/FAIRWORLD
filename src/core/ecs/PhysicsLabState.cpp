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
#include "JoltPhysicsSystem.h"
#include "StateManager.h"
#include "ForgeState.h"
#include "HubState.h"
#include "BlockRegistry.h"
#include "MaterialRegistry.h"

PhysicsLabState::PhysicsLabState(SharedContext* context) : m_context(context) {
}

PhysicsLabState::~PhysicsLabState() {
    delete m_joltSystem;
    m_joltSystem = nullptr;
}

bool PhysicsLabState::Init() {
    std::cout << "[PhysicsLabState] Init\n";
    m_originalWorld = m_context->forgeWorld;
    m_labWorld = new fw::ForgeWorld();
    m_context->forgeWorld = m_labWorld;

    m_joltSystem = new fw::JoltPhysicsSystem();

    // Il Lab è un editor: il cursore deve essere sempre libero e visibile.
    // Lo forziamo qui invece di affidarci al DeviceManager frame-per-frame,
    // che altrimenti blocca il cursore al primo clic sinistro su ImGui.
    if (m_context->deviceManager) {
        m_context->deviceManager->requireFreeCursor = true;
    }

    // RefreshDevStructures(); // Removed, replaced by GlobalAssetBrowser
    ScanTemplates();

    m_orbitTarget = glm::vec3(0, 2.0f, 0);
    m_orbitDistance = 15.0f;
    m_orbitYaw = 45.0f;
    m_orbitPitch = 30.0f;
    m_assetBrowser.Initialize();
    
    return true;
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
void PhysicsLabState::StartSimulation() { 
    if (!m_joltSystem || m_skeleton.m_joints.empty()) return;
    m_simulateMode = true; 
    
    auto* physSys = m_joltSystem->GetSystem();
    auto& bodyInterface = physSys->GetBodyInterface();
    
    // Create static floor
    JPH::BoxShapeSettings floorShapeSettings(JPH::Vec3(100.0f, 1.0f, 100.0f));
    JPH::ShapeSettings::ShapeResult floorShapeResult = floorShapeSettings.Create();
    JPH::ShapeRefC floorShape = floorShapeResult.Get();
    JPH::BodyCreationSettings floorSettings(floorShape, JPH::RVec3(0.0, -1.0, 0.0), JPH::Quat::sIdentity(), JPH::EMotionType::Static, fw::Layers::NON_MOVING);
    JPH::Body* floor = bodyInterface.CreateBody(floorSettings);
    bodyInterface.AddBody(floor->GetID(), JPH::EActivation::DontActivate);
    m_joltBodies.push_back(floor->GetID().GetIndexAndSequenceNumber());
    
    // Create dynamic bodies for joints
    for (auto& joint : m_skeleton.m_joints) {
        JPH::BoxShapeSettings boxSettings(JPH::Vec3(0.5f, 0.5f, 0.5f)); // TODO: use actual bounds
        JPH::ShapeRefC boxShape = boxSettings.Create().Get();
        
        glm::vec3 pos = glm::vec3(joint.globalRestTransform[3]);
        glm::quat rot = glm::quat_cast(joint.globalRestTransform);
        
        JPH::BodyCreationSettings boxCS(boxShape, JPH::RVec3(pos.x, pos.y, pos.z), JPH::Quat(rot.x, rot.y, rot.z, rot.w), JPH::EMotionType::Dynamic, fw::Layers::MOVING);
        JPH::Body* body = bodyInterface.CreateBody(boxCS);
        bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
        
        joint.joltBodyID = body->GetID().GetIndexAndSequenceNumber();
        m_joltBodies.push_back(joint.joltBodyID);
    }
}

void PhysicsLabState::StopSimulation() { 
    if (!m_joltSystem) return;
    m_simulateMode = false; 
    
    auto* physSys = m_joltSystem->GetSystem();
    auto& bodyInterface = physSys->GetBodyInterface();
    
    for (uint32_t id : m_joltBodies) {
        JPH::BodyID bodyID(id);
        bodyInterface.RemoveBody(bodyID);
        bodyInterface.DestroyBody(bodyID);
    }
    m_joltBodies.clear();
    
    // Restore skeleton
    m_skeleton.UpdateForwardKinematics();
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
    ImGui::SetNextWindowSize(ImVec2(350, 480), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Joint Properties")) {
        ImGui::TextColored(ImVec4(1.0f,0.85f,0.1f,1.0f), "%s", joint.name.c_str());
        ImGui::TextDisabled("Mesh: %s", joint.meshPath.empty() ? "Nessuna" : joint.meshPath.c_str());
        
        // Selezione Voxel ora gestita tramite Global Asset Browser (Tasto B)
        ImGui::TextDisabled("Premi 'B' per assegnare una Mesh dal Global Asset Browser");
        
        // --- TEXTURE/MATERIAL SELETTORE ---
        int bType = (int)joint.blockType;
        const char* bTypes[] = { "Nessuno (Air)", "Erba", "Roccia", "Terra", "Legno", "Foglie", "Acqua", "Sabbia" };
        if (ImGui::Combo("Texture Voxel", &bType, bTypes, 8)) {
            joint.blockType = (uint8_t)bType;
            
            if (bType != 0 && joint.meshPath.empty() && m_labWorld) {
                if (joint.voxelEntity != 0xFFFFFFFF) {
                    entt::entity e = static_cast<entt::entity>(joint.voxelEntity);
                    if (m_labWorld->GetRegistry().valid(e)) m_labWorld->GetRegistry().destroy(e);
                }
                
                entt::entity cube = m_labWorld->CreatePrimitive("JointCube", fw::Vec3(0,0,0), "Chunk_Cube");
                auto* meshComp = m_labWorld->GetRegistry().try_get<fw::MeshComponent>(cube);
                if (meshComp && m_context && m_context->blockRegistry) {
                    auto& mat = m_context->materialRegistry->GetMaterial((uint8_t)bType);
                    meshComp->colorOverride[0] = mat.baseColorFallback.x;
                    meshComp->colorOverride[1] = mat.baseColorFallback.y;
                    meshComp->colorOverride[2] = mat.baseColorFallback.z;
                    meshComp->colorOverride[3] = 1.0f; // Enable color override (alpha > 0)
                }
                joint.voxelEntity = static_cast<uint32_t>(cube);
            }
        }
        
        ImGui::Separator();

        // Tipo giunto
        int type = (int)joint.type;
        const char* types[] = { "HINGE", "UNIVERSAL", "BALL" };
        if (ImGui::Combo("Tipo Giunto", &type, types, 3))
            joint.type = (fw::RigJointType)type;
        
        ImGui::DragFloat3("Limit Min", joint.limitMin, 1.0f, -180.0f, 180.0f);
        ImGui::DragFloat3("Limit Max", joint.limitMax, 1.0f, -180.0f, 180.0f);
        
        ImGui::Separator();
        // Offset posizione locale del giunto
        glm::vec3 pos = glm::vec3(joint.localRestTransform[3]);
        if (ImGui::DragFloat3("Posizione##jpos", &pos.x, 0.05f)) {
            joint.localRestTransform[3] = glm::vec4(pos, 1.0f);
            m_skeleton.UpdateForwardKinematics();
        }

        ImGui::Separator();
        // --- BUG FIX #4: Color Picker ---
        ImGui::TextColored(ImVec4(0.4f,0.8f,1.0f,1.0f), "Colore Voxel");
        glm::vec4& col = m_jointColors[m_selectedJointIndex];
        // Inizializza al bianco se non esiste ancora
        if (col.a == 0.0f && col.r == 0.0f) col = glm::vec4(1,1,1,1);
        if (ImGui::ColorEdit4("##colpick", &col.x)) {
            // Applica subito al voxel associato
            if (joint.voxelEntity != 0xFFFFFFFF && m_labWorld) {
                entt::entity e = static_cast<entt::entity>(joint.voxelEntity);
                if (m_labWorld->GetRegistry().valid(e)) {
                    auto* mesh = m_labWorld->GetRegistry().try_get<fw::MeshComponent>(e);
                    if (mesh) {
                        mesh->colorOverride[0] = col.r;
                        mesh->colorOverride[1] = col.g;
                        mesh->colorOverride[2] = col.b;
                        mesh->colorOverride[3] = col.a;
                    }
                }
            }
        }
        if (ImGui::Button("Reset Colore", ImVec2(-1,20))) {
            col = glm::vec4(1,1,1,0); // 0 alpha = usa colore originale del voxel
            if (joint.voxelEntity != 0xFFFFFFFF && m_labWorld) {
                entt::entity e = static_cast<entt::entity>(joint.voxelEntity);
                if (m_labWorld->GetRegistry().valid(e)) {
                    auto* mesh = m_labWorld->GetRegistry().try_get<fw::MeshComponent>(e);
                    if (mesh) { mesh->colorOverride[3] = 0.0f; }
                }
            }
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f,1.0f,0.4f,1.0f), "Gizmo 3D");
        ImGui::RadioButton("Off",       &m_gizmoMode, 0); ImGui::SameLine();
        ImGui::RadioButton("Sposta",    &m_gizmoMode, 1); ImGui::SameLine();
        ImGui::RadioButton("Ruota",     &m_gizmoMode, 2);
        ImGui::TextDisabled("Attiva Sposta/Ruota, poi trascina le frecce nel viewport");
        
        ImGui::Separator();
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
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    float w = (float)m_context->engine->GetRenderManager()->GetWindowWidth();
    float h = (float)m_context->engine->GetRenderManager()->GetWindowHeight();
    if (w <= 0 || h <= 0) return;

    // Usa m_projEditor (senza Y-flip Vulkan) per proiezione corretta
    glm::mat4 vp = m_projEditor * m_context->activeCameraView.viewMatrix;

    auto Project = [&](const glm::vec3& pt, bool& inFront) -> ImVec2 {
        glm::vec4 clip = vp * glm::vec4(pt, 1.0f);
        inFront = clip.w > 0.001f;
        if (inFront) {
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            return ImVec2((ndc.x * 0.5f + 0.5f) * w, (0.5f - ndc.y * 0.5f) * h);
        }
        return ImVec2(-1, -1);
    };

    // --- BUG FIX #3: Griglia di riferimento Y=0 ---
    const int GRID_HALF = 10;
    const float GRID_STEP = 1.0f;
    ImU32 gridCol = IM_COL32(80, 80, 80, 120);
    ImU32 originCol = IM_COL32(120, 120, 120, 200);
    for (int i = -GRID_HALF; i <= GRID_HALF; ++i) {
        bool f1, f2;
        glm::vec3 a(-GRID_HALF * GRID_STEP, 0, i * GRID_STEP);
        glm::vec3 b( GRID_HALF * GRID_STEP, 0, i * GRID_STEP);
        glm::vec3 c(i * GRID_STEP, 0, -GRID_HALF * GRID_STEP);
        glm::vec3 d(i * GRID_STEP, 0,  GRID_HALF * GRID_STEP);
        ImVec2 sa = Project(a, f1), sb = Project(b, f2);
        if (f1 && f2) drawList->AddLine(sa, sb, (i == 0) ? originCol : gridCol, (i == 0) ? 2.0f : 1.0f);
        ImVec2 sc = Project(c, f1), sd = Project(d, f2);
        if (f1 && f2) drawList->AddLine(sc, sd, (i == 0) ? originCol : gridCol, (i == 0) ? 2.0f : 1.0f);
    }

    // --- Assi XYZ all'origine ---
    {
        bool f0, fX, fY, fZ;
        glm::vec3 orig(0,0,0);
        ImVec2 sO  = Project(orig,          f0);
        ImVec2 sX  = Project({2,0,0},       fX);
        ImVec2 sY  = Project({0,2,0},       fY);
        ImVec2 sZ  = Project({0,0,2},       fZ);
        if (f0 && fX) drawList->AddLine(sO, sX, IM_COL32(220, 60,  60,  255), 3.0f);
        if (f0 && fY) drawList->AddLine(sO, sY, IM_COL32(60,  220, 60,  255), 3.0f);
        if (f0 && fZ) drawList->AddLine(sO, sZ, IM_COL32(60,  100, 220, 255), 3.0f);
        // Label
        if (f0 && fX) drawList->AddText(sX, IM_COL32(220,80,80,255), "X");
        if (f0 && fY) drawList->AddText(sY, IM_COL32(80,220,80,255), "Y");
        if (f0 && fZ) drawList->AddText(sZ, IM_COL32(80,120,220,255), "Z");
    }

    // --- Overlay ossa (Skeleton X-Ray e Physics Debug) ---
    if (m_renderMode == 1 || m_renderMode == 2) {
        const auto& transforms = m_skeleton.GetGlobalTransforms();
        for (size_t i = 0; i < m_skeleton.m_joints.size(); ++i) {
            glm::vec3 p1 = glm::vec3(transforms[i][3]);
            bool f1;
            ImVec2 sp1 = Project(p1, f1);

            // Colore: giunto selezionato = arancione, altrimenti giallo
            bool isSelected = (m_selectedJointIndex == (int)i);
            ImU32 jointCol = isSelected ? IM_COL32(255, 140, 0, 255) : IM_COL32(255, 230, 0, 220);
            float radius = isSelected ? 8.0f : 5.0f;

            if (f1) drawList->AddCircleFilled(sp1, radius, jointCol);

            if (m_skeleton.m_joints[i].parentIndex >= 0) {
                glm::vec3 p0 = glm::vec3(transforms[m_skeleton.m_joints[i].parentIndex][3]);
                bool f0;
                ImVec2 sp0 = Project(p0, f0);
                if (f0 && f1) drawList->AddLine(sp0, sp1, IM_COL32(200, 200, 200, 200), 2.0f);
            }

            // Physics debug: capsule wireframe semplice
            if (m_renderMode == 2 && f1) {
                float r = 12.0f;
                drawList->AddCircle(sp1, r, IM_COL32(0, 255, 100, 180), 16, 1.5f);
                drawList->AddCircle(sp1, r * 0.5f, IM_COL32(0, 200, 80, 120), 8, 1.0f);
            }

            // Etichetta nome sopra il giunto
            if (f1 && isSelected) {
                drawList->AddText(ImVec2(sp1.x + 8, sp1.y - 12), IM_COL32(255,200,0,255),
                    m_skeleton.m_joints[i].name.c_str());
            }
        }
    }
}

void PhysicsLabState::DrawAngularLimitsGizmo(fw::JointData& joint, const glm::mat4& globalMat) {
    if (m_renderMode != 2) return;
    // Disegno archi in viewport overlay gestito separatamente o qui
}

void PhysicsLabState::HandleArcPicking(fw::JointData& joint, const glm::mat4& globalMat, const glm::vec3& rayOrigin, const glm::vec3& rayDir) {}



void PhysicsLabState::Update(float dt) {
    if (m_context && m_context->deviceManager) {
        m_context->deviceManager->requireFreeCursor = true;
    }
    m_context->isForgeMode = true; // Forza il rendering del ForgeWorld

    using namespace entt::literals;
    if (m_context->deviceManager->IsActionActive("PAUSE"_hs)) {
        if (m_originalWorld) {
            m_context->forgeWorld = m_originalWorld;
            m_originalWorld = nullptr;
            if (m_context->engine && m_context->engine->GetRenderManager()) {
                m_context->engine->GetRenderManager()->InvalidateForgeCache();
            }
        }
        // Nel LAB vogliamo SEMPRE il cursore libero, tranne in casi specifici FPS.
        // m_context->deviceManager->requireFreeCursor = false; 
        if (m_context->stateManager) {
            m_context->stateManager->ChangeState(std::make_unique<HubState>(m_context));
        }
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
                
                glm::mat4 invProj = glm::inverse(m_projEditor);
                glm::mat4 invView = glm::inverse(m_context->activeCameraView.viewMatrix);
                
                glm::vec4 rayClip = glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
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
            if (m_isPlaying) {
                m_animationTime += dt;
                if (m_animationTime > 1.0f/60.0f) {
                    m_currentFrame++;
                    if (m_currentFrame > m_maxFrames) m_currentFrame = 0;
                    m_animationTime = 0;
                }
            }
        }
    } else {
        // Step Jolt physics
        if (m_joltSystem) {
            auto* physSys = m_joltSystem->GetSystem();
            physSys->Update(dt, 1, m_joltSystem->GetTempAllocator(), m_joltSystem->GetJobSystem());
            
            auto& bodyInterface = physSys->GetBodyInterface();
            for (auto& joint : m_skeleton.m_joints) {
                if (joint.joltBodyID != 0xFFFFFFFF) {
                    JPH::BodyID bodyID(joint.joltBodyID);
                    JPH::RVec3 pos = bodyInterface.GetPosition(bodyID);
                    JPH::Quat rot = bodyInterface.GetRotation(bodyID);
                    
                    glm::mat4 transform = glm::mat4_cast(glm::quat(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ()));
                    transform[3] = glm::vec4(pos.GetX(), pos.GetY(), pos.GetZ(), 1.0f);
                    joint.globalRestTransform = transform;
                }
            }
        }
    }

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
        float panSpeed = m_orbitDistance * 0.020f;
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
    uint32_t width  = m_context->engine->GetRenderManager()->GetWindowWidth();
    uint32_t height = m_context->engine->GetRenderManager()->GetWindowHeight();
    if (height > 0) aspect = (float)width / (float)height;

    // BUG FIX #2: proiezione SENZA flip Y → usata da ImGuizmo, overlay, raycast
    m_projEditor = glm::perspective(glm::radians(m_labFov), aspect, 0.1f, 1000.0f);
    // Proiezione CON flip Y → passata al renderer Vulkan
    m_context->activeCameraView.projectionMatrix = m_projEditor;
    m_context->activeCameraView.projectionMatrix[1][1] *= -1;

    // Aggiornamento asincrono dei chunk caricati nel Lab
    if (m_context && m_context->forgeWorld) {
        m_context->forgeWorld->Update(dt);
    }
    
    // --- ASSET BROWSER (Key 'B') ---
    static bool bKeyWasDown = false;
    bool bKeyDown = (GetAsyncKeyState('B') & 0x8000) != 0;
    if (bKeyDown && !bKeyWasDown) {
        m_showAssetBrowser = !m_showAssetBrowser;
        if (m_showAssetBrowser) m_assetBrowser.RefreshAssets();
    }
    bKeyWasDown = bKeyDown;
}

void PhysicsLabState::Render() {
    // BUG FIX #1: ImGuizmo deve essere inizializzato all'inizio di ogni Render()
    ImGuizmo::BeginFrame();

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
                // Lasciamo il cursore libero al passaggio
                // m_context->deviceManager->requireFreeCursor = false;
                if (m_context->stateManager) {
                    m_context->stateManager->ChangeState(std::make_unique<ForgeState>(m_context));
                }
            }
            ImGui::PopStyleColor();
            ImGui::Separator();
            
            if (ImGui::Button("Asset Browser Globale", ImVec2(-1, 30))) {
                m_showAssetBrowser = true;
            }
        }
        
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.1f, 1.0f), "Render Mode");
        if (ImGui::RadioButton("Mesh", &m_renderMode, 0)) {}
        ImGui::SameLine();
        if (ImGui::RadioButton("Skeleton X-Ray", &m_renderMode, 1)) {}
        ImGui::SameLine();
        if (ImGui::RadioButton("Physics Debug", &m_renderMode, 2)) {}
        ImGui::SameLine();
        if (ImGui::RadioButton("Textured", &m_renderMode, 3)) {}
        
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Camera Orbitale");
        ImGui::SliderFloat("FOV##lab", &m_labFov, 20.0f, 120.0f);
        ImGui::TextDisabled("Tasto Destro: Ruota | Scroll: Zoom | Medio: Pan");
    }
    ImGui::End();

    if (m_showAssetBrowser) {
        m_assetBrowser.DrawUI(&m_showAssetBrowser, nullptr, m_labWorld);
        std::string spawnTarget = m_assetBrowser.GetSelectedAssetToSpawn();
        if (!spawnTarget.empty()) {
            if (m_selectedJointIndex >= 0 && m_selectedJointIndex < m_skeleton.m_joints.size()) {
                fw::JointData& j = m_skeleton.m_joints[m_selectedJointIndex];
                
                // Estrai solo il nome (stem) perché LoadStructureAsPrefab aggiunge già directory ed estensione
                std::filesystem::path p(spawnTarget);
                j.meshPath = p.stem().string(); 
                
                if (j.voxelEntity != 0xFFFFFFFF) {
                    entt::entity e = static_cast<entt::entity>(j.voxelEntity);
                    if (m_labWorld->GetRegistry().valid(e)) m_labWorld->GetRegistry().destroy(e);
                }
                entt::entity spawned = m_labWorld->LoadStructureAsPrefab(j.meshPath, fw::Vec3(0,0,0));
                j.voxelEntity = static_cast<uint32_t>(spawned);
            }
            m_assetBrowser.ClearSelectedAsset();
            m_showAssetBrowser = false;
        }
    }

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
                // BUG FIX #1: SetRect con dimensioni reali dello schermo
                uint32_t sw = m_context->engine->GetRenderManager()->GetWindowWidth();
                uint32_t sh = m_context->engine->GetRenderManager()->GetWindowHeight();

                ImGuizmo::SetOrthographic(false);
                ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
                ImGuizmo::SetRect(0, 0, (float)sw, (float)sh);

                // BUG FIX #2: m_projEditor senza Y-flip per ImGuizmo
                // BUG FIX: gizmo opera su localRestTransform (muove l'osso,
                //          non solo il voxel attaccato)
                glm::mat4 parentGlobalMat = glm::mat4(1.0f);
                if (joint.parentIndex >= 0) {
                    parentGlobalMat = m_skeleton.GetGlobalTransforms()[joint.parentIndex];
                }
                // worldMatrix = globale del padre * transform locale dell'osso
                glm::mat4 worldMat = parentGlobalMat * joint.localRestTransform;

                ImGuizmo::OPERATION op = (m_gizmoMode == 1) ? ImGuizmo::TRANSLATE : ImGuizmo::ROTATE;
                ImGuizmo::Manipulate(
                    glm::value_ptr(m_context->activeCameraView.viewMatrix),
                    glm::value_ptr(m_projEditor),  // <-- senza flip
                    op, ImGuizmo::LOCAL,
                    glm::value_ptr(worldMat));

                if (ImGuizmo::IsUsing()) {
                    // Riproietta in spazio locale del padre
                    joint.localRestTransform = glm::inverse(parentGlobalMat) * worldMat;
                    m_skeleton.UpdateForwardKinematics();
                }
            }
        }
    }
    
    DrawTimeline();
    DrawViewportOverlay();

    // --- Pannello Aggiungi Giunto Manuale ---
    ImGui::SetNextWindowPos(ImVec2(370, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 160), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Aggiungi Giunto", nullptr, ImGuiWindowFlags_NoCollapse)) {
        static char jointName[64] = "Nuovo_Giunto";
        ImGui::InputText("Nome##jname", jointName, sizeof(jointName));

        int parentOpt = m_selectedJointIndex;
        std::string parentLabel = (parentOpt >= 0 && parentOpt < (int)m_skeleton.m_joints.size())
            ? m_skeleton.m_joints[parentOpt].name
            : "(Radice)";
        ImGui::Text("Padre: %s (seleziona dall'albero)", parentLabel.c_str());

        static int newJointType = 0;
        ImGui::Combo("Tipo##njt", &newJointType, "HINGE\0UNIVERSAL\0BALL\0");

        if (ImGui::Button("+ Aggiungi al Padre Selezionato", ImVec2(-1, 30))) {
            fw::JointData nj;
            nj.name = jointName;
            nj.parentIndex = m_selectedJointIndex;
            nj.type = (fw::RigJointType)newJointType;
            // Offset di default verso il basso rispetto al padre
            glm::vec3 defaultOff = (m_selectedJointIndex >= 0) ? glm::vec3(0, -0.5f, 0) : glm::vec3(0, 0, 0);
            nj.localRestTransform = glm::translate(glm::mat4(1.0f), defaultOff);
            m_skeleton.m_joints.push_back(nj);
            m_skeleton.GetDofState().resize(m_skeleton.m_joints.size() * 3, 0.0f);
            m_selectedJointIndex = (int)m_skeleton.m_joints.size() - 1;
            m_skeleton.UpdateForwardKinematics();
            std::cout << "[PhysicsLab] Aggiunto giunto: " << nj.name << " (padre: " << nj.parentIndex << ")\n";
        }

        if (m_selectedJointIndex >= 0 && !m_skeleton.m_joints.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
            if (ImGui::Button("Rimuovi Giunto Selezionato", ImVec2(-1, 25))) {
                m_skeleton.m_joints.erase(m_skeleton.m_joints.begin() + m_selectedJointIndex);
                // Aggiusta gli indici dei figli
                for (auto& j : m_skeleton.m_joints) {
                    if (j.parentIndex == m_selectedJointIndex) j.parentIndex = -1;
                    else if (j.parentIndex > m_selectedJointIndex) j.parentIndex--;
                }
                m_skeleton.GetDofState().resize(m_skeleton.m_joints.size() * 3, 0.0f);
                m_selectedJointIndex = -1;
                m_skeleton.UpdateForwardKinematics();
            }
            ImGui::PopStyleColor();
        }
    }
    ImGui::End();

    if (m_showAssetBrowser) {
        m_assetBrowser.DrawUI(&m_showAssetBrowser, nullptr, m_labWorld);
        std::string spawnTarget = m_assetBrowser.GetSelectedAssetToSpawn();
        if (!spawnTarget.empty()) {
            if (m_selectedJointIndex >= 0 && m_selectedJointIndex < m_skeleton.m_joints.size()) {
                auto& joint = m_skeleton.m_joints[m_selectedJointIndex];
                joint.meshPath = spawnTarget;
                if (joint.voxelEntity != 0xFFFFFFFF && m_labWorld) {
                    entt::entity e = static_cast<entt::entity>(joint.voxelEntity);
                    if (m_labWorld->GetRegistry().valid(e)) m_labWorld->GetRegistry().destroy(e);
                }
                entt::entity spawned = m_labWorld->LoadStructureAsPrefab(joint.meshPath, fw::Vec3(0,0,0));
                joint.voxelEntity = static_cast<uint32_t>(spawned);
            }
            m_assetBrowser.ClearSelectedAsset();
            m_showAssetBrowser = false;
        }
    }
}

void PhysicsLabState::GenerateBipedSkeleton() {
    m_skeleton.m_joints.clear();
    m_skeleton.GetDofState().clear();
    m_selectedJointIndex = -1;

    auto add = [&](const std::string& name, int parent, glm::vec3 off, fw::RigJointType type = fw::RigJointType::HINGE) {
        fw::JointData j;
        j.name = name; j.parentIndex = parent; j.type = type;
        j.localRestTransform = glm::translate(glm::mat4(1.0f), off);
        m_skeleton.m_joints.push_back(j);
        m_skeleton.GetDofState().resize(m_skeleton.m_joints.size() * 3, 0.0f);
    };

    add("Root",      -1, {0,  0,    0},    fw::RigJointType::BALL);
    add("Spine",      0, {0,  1.0f, 0},    fw::RigJointType::UNIVERSAL);
    add("Chest",      1, {0,  0.8f, 0},    fw::RigJointType::UNIVERSAL);
    add("Neck",       2, {0,  0.5f, 0},    fw::RigJointType::UNIVERSAL);
    add("Head",       3, {0,  0.4f, 0},    fw::RigJointType::BALL);
    add("ShoulderL",  2, {-0.6f, 0.4f, 0}, fw::RigJointType::BALL);
    add("ElbowL",     5, {-0.6f, 0, 0},    fw::RigJointType::HINGE);
    add("WristL",     6, {-0.5f, 0, 0},    fw::RigJointType::UNIVERSAL);
    add("ShoulderR",  2, { 0.6f, 0.4f, 0}, fw::RigJointType::BALL);
    add("ElbowR",     8, { 0.6f, 0, 0},    fw::RigJointType::HINGE);
    add("WristR",     9, { 0.5f, 0, 0},    fw::RigJointType::UNIVERSAL);
    add("HipL",       0, {-0.3f,-0.2f, 0}, fw::RigJointType::BALL);
    add("KneeL",     11, {0, -0.8f, 0},    fw::RigJointType::HINGE);
    add("AnkleL",    12, {0, -0.7f, 0},    fw::RigJointType::UNIVERSAL);
    add("HipR",       0, { 0.3f,-0.2f, 0}, fw::RigJointType::BALL);
    add("KneeR",     14, {0, -0.8f, 0},    fw::RigJointType::HINGE);
    add("AnkleR",    15, {0, -0.7f, 0},    fw::RigJointType::UNIVERSAL);

    m_skeleton.UpdateForwardKinematics();
    m_animationPreset = 1;
    m_renderMode = 1; // Mostra subito skeleton X-Ray
    std::cout << "[PhysicsLab] Bipede: " << m_skeleton.m_joints.size() << " giunti\n";
}

void PhysicsLabState::GenerateCentipedeSkeleton(int segments) {
    m_skeleton.m_joints.clear();
    m_skeleton.GetDofState().clear();
    m_selectedJointIndex = -1;

    fw::JointData head;
    head.name = "Head"; head.parentIndex = -1; head.type = fw::RigJointType::BALL;
    head.localRestTransform = glm::mat4(1.0f);
    m_skeleton.m_joints.push_back(head);
    m_skeleton.GetDofState().resize(3, 0.0f);

    for (int i = 0; i < segments; ++i) {
        int bodyIdx = (int)m_skeleton.m_joints.size();
        fw::JointData body;
        body.name = "Body_" + std::to_string(i);
        body.parentIndex = (i == 0) ? 0 : (bodyIdx - 3);
        body.type = fw::RigJointType::UNIVERSAL;
        body.localRestTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-0.8f, 0, 0));
        m_skeleton.m_joints.push_back(body);

        fw::JointData legL;
        legL.name = "Leg_L_" + std::to_string(i);
        legL.parentIndex = bodyIdx;
        legL.type = fw::RigJointType::HINGE;
        legL.localRestTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0,-0.5f, 0.4f));
        m_skeleton.m_joints.push_back(legL);

        fw::JointData legR;
        legR.name = "Leg_R_" + std::to_string(i);
        legR.parentIndex = bodyIdx;
        legR.type = fw::RigJointType::HINGE;
        legR.localRestTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0,-0.5f,-0.4f));
        m_skeleton.m_joints.push_back(legR);

        m_skeleton.GetDofState().resize(m_skeleton.m_joints.size() * 3, 0.0f);
    }
    m_skeleton.UpdateForwardKinematics();
    m_animationPreset = 0;
    m_renderMode = 1;
    std::cout << "[PhysicsLab] Centopiedi: " << segments << " seg, " << m_skeleton.m_joints.size() << " giunti\n";
}

void PhysicsLabState::GenerateSnakeSkeleton(int segments) {
    m_skeleton.m_joints.clear();
    m_skeleton.GetDofState().clear();
    m_selectedJointIndex = -1;

    for (int i = 0; i < segments; ++i) {
        fw::JointData j;
        j.name = (i == 0) ? "Head" : ("Spine_" + std::to_string(i));
        j.parentIndex = i - 1;
        j.type = fw::RigJointType::UNIVERSAL;
        j.limitMin[0] = -30.0f; j.limitMax[0] = 30.0f;
        j.limitMin[1] = -15.0f; j.limitMax[1] = 15.0f;
        j.localRestTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-0.6f, 0, 0));
        m_skeleton.m_joints.push_back(j);
    }
    m_skeleton.GetDofState().resize(m_skeleton.m_joints.size() * 3, 0.0f);
    m_skeleton.UpdateForwardKinematics();
    m_animationPreset = 0;
    m_renderMode = 1;
    std::cout << "[PhysicsLab] Serpente: " << segments << " vertebre\n";
}

void PhysicsLabState::GenerateSpiderSkeleton() {
    m_skeleton.m_joints.clear();
    m_skeleton.GetDofState().clear();
    m_selectedJointIndex = -1;

    auto add = [&](const std::string& name, int parent, glm::vec3 off, fw::RigJointType type = fw::RigJointType::HINGE) -> int {
        fw::JointData j;
        j.name = name; j.parentIndex = parent; j.type = type;
        j.localRestTransform = glm::translate(glm::mat4(1.0f), off);
        m_skeleton.m_joints.push_back(j);
        m_skeleton.GetDofState().resize(m_skeleton.m_joints.size() * 3, 0.0f);
        return (int)m_skeleton.m_joints.size() - 1;
    };

    add("Thorax",  -1, {0, 0, 0},    fw::RigJointType::BALL);
    add("Abdomen",  0, {-0.8f, 0, 0}, fw::RigJointType::UNIVERSAL);

    const float degs[4] = { 30.0f, 60.0f, 100.0f, 130.0f };
    for (int i = 0; i < 4; ++i) {
        float a = glm::radians(degs[i]);
        std::string s = std::to_string(i + 1);
        int cL = add("CoxaL_"+s,  0, { cosf(a)*0.6f,0, sinf(a)*0.6f}, fw::RigJointType::BALL);
        int fL = add("FemurL_"+s, cL, {0.7f,-0.2f,0});
              add("TibiaL_"+s, fL, {0.7f,-0.3f,0});
        int cR = add("CoxaR_"+s,  0, { cosf(a)*0.6f,0,-sinf(a)*0.6f}, fw::RigJointType::BALL);
        int fR = add("FemurR_"+s, cR, {0.7f,-0.2f,0});
              add("TibiaR_"+s, fR, {0.7f,-0.3f,0});
    }
    m_skeleton.UpdateForwardKinematics();
    m_animationPreset = 0;
    m_renderMode = 1;
    std::cout << "[PhysicsLab] Ragno: " << m_skeleton.m_joints.size() << " giunti\n";
}
