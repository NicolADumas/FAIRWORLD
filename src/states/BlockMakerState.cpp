#include "pch.h"
#include "BlockMakerState.h"
#include "SharedContext.h"
#include "StateManager.h"
#include "HubState.h"
#include "FAIRWORLD.h"
#include "RenderManager.h"
#include "GameWorld.h"
#include "JobSystem.h"
#include "AsyncInput.h"
#include "Systems.h"
#include "BlockRegistry.h"
#include "DeviceManager.h"
#include <shellapi.h>  // ShellExecuteA - apre cartelle/file con l'OS
#include "MaterialRegistry.h"
#include "VulkanDmaManager.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include "imgui.h"
#include "ImGuizmo.h"
#include <windows.h>
#include <commdlg.h>
#include <filesystem>
#include <thread>
#include <chrono>
#include "GeometryCompiler.h"

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

    const char* GetShapeTypeName(fw::ShapeType type) {
        switch (type) {
            case fw::ShapeType::Cube: return "Cube";
            case fw::ShapeType::Cuboid: return "Cuboid";
            case fw::ShapeType::Parallelepiped: return "Parallelepiped";
            case fw::ShapeType::Pyramid: return "Pyramid";
            case fw::ShapeType::PyramidFrustum: return "Pyramid Frustum";
            case fw::ShapeType::Cylinder: return "Cylinder";
            case fw::ShapeType::HollowCylinder: return "Hollow Cylinder";
            case fw::ShapeType::Cone: return "Cone";
            case fw::ShapeType::ConeFrustum: return "Cone Frustum";
            case fw::ShapeType::Sphere: return "Sphere";
            case fw::ShapeType::Capsule: return "Capsule";
            case fw::ShapeType::LegacySuperSphere: return "Legacy SuperSphere";
            case fw::ShapeType::SphericalZone: return "Spherical Zone";
            case fw::ShapeType::SphericalSegment: return "Spherical Segment";
            case fw::ShapeType::SphericalSector: return "Spherical Sector";
            case fw::ShapeType::SphereWithCylindricalBore: return "Sphere w/ Bore";
            case fw::ShapeType::SphereWithConicalCavities: return "Sphere w/ Cavities";
            case fw::ShapeType::SlicedCylinder: return "Sliced Cylinder";
            case fw::ShapeType::Ungula: return "Ungula";
            case fw::ShapeType::Barrel: return "Barrel";
            default: return "Custom / Other";
        }
    }
}

BlockMakerState::BlockMakerState(SharedContext* context) : m_context(context) {
    std::cout << "[BlockMakerState] Creato.\n";
}

BlockMakerState::~BlockMakerState() {
    std::cout << "[BlockMakerState] Attendiamo completamento job asincroni pendenti prima di distruggere...\n";
    if (m_context) {
        if (m_previewWorld && m_context->forgeWorld == m_previewWorld.get()) {
            m_context->forgeWorld = m_context->gameWorld;
        }
        if (m_previewWorld && m_context->activeRegistry == &m_previewWorld->GetRegistry()) {
            m_context->activeRegistry = m_context->gameWorld ? &m_context->gameWorld->GetRegistry() : nullptr;
        }
        m_context->isBlockMakerMode = false;
        if (m_context->jobSystem) {
            m_context->jobSystem->Shutdown();
            m_context->jobSystem->Initialize();
        }
    }
    std::cout << "[BlockMakerState] Distrutto.\n";
}

entt::registry* BlockMakerState::GetRegistry() {
    if (m_previewWorld) return &m_previewWorld->GetRegistry();
    return nullptr;
}

bool BlockMakerState::IsBlockTransformDirty(uint8_t id) const {
    if (!m_context || !m_context->blockRegistry) return false;
    const auto& bDef = m_context->blockRegistry->GetBlock(id);
    const auto& saved = m_savedTransforms[id];
    const float eps = 1e-5f;
    auto diff = [](const glm::vec3& a, const glm::vec3& b, float e) {
        return std::abs(a.x - b.x) > e || std::abs(a.y - b.y) > e || std::abs(a.z - b.z) > e;
    };
    return (diff(bDef.localPosition, saved.localPosition, eps) ||
            diff(bDef.localRotation, saved.localRotation, eps) ||
            diff(bDef.localScale, saved.localScale, eps) ||
            diff(bDef.pivotOffset, saved.pivotOffset, eps));
}

uint32_t BlockMakerState::CountUnsavedTransformBlocks() const {
    if (!m_context || !m_context->blockRegistry) return 0;
    uint32_t count = 0;
    for (int i = 1; i <= 255; ++i) {
        if (IsBlockTransformDirty(static_cast<uint8_t>(i))) {
            count++;
        }
    }
    return count;
}

void BlockMakerState::SynchronizeAllSavedTransformBaselines() {
    if (!m_context || !m_context->blockRegistry) return;
    for (int i = 0; i < 256; ++i) {
        const auto& def = m_context->blockRegistry->GetBlock(static_cast<uint8_t>(i));
        m_savedTransforms[i].localPosition = def.localPosition;
        m_savedTransforms[i].localRotation = def.localRotation;
        m_savedTransforms[i].localScale = def.localScale;
        m_savedTransforms[i].pivotOffset = def.pivotOffset;
    }
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
        m_context->vramAllocator = new fw::VramSlabAllocator(2048ULL * 1024ULL * 1024ULL);
        m_context->vramAllocator->SetAllocateCompartmentCallback([ctx = m_context](uint32_t compIdx) {
            if (ctx && ctx->engine && ctx->engine->GetRenderManager()) {
                ctx->engine->GetRenderManager()->AddVramCompartment();
                if (ctx->dmaManager) {
                    ctx->dmaManager->UpdateStagingBuffer(
                        ctx->engine->GetRenderManager()->GetStagingRingBuffer(),
                        ctx->engine->GetRenderManager()->GetStagingDeviceMemory(),
                        ctx->engine->GetRenderManager()->GetMappedStagingData()
                    );
                }
            }
        });
    }
    if (!m_context->dmaManager) {
        m_context->dmaManager = new fw::VulkanDmaManager();
        if (auto* rm = m_context->engine->GetRenderManager()) {
            m_context->dmaManager->Initialize(
                rm->GetDevice(), rm->GetTransferQueue(), rm->GetTransferCommandPool(),
                rm->GetStagingRingBuffer(), rm->GetStagingDeviceMemory(), rm->GetMappedStagingData(),
                rm->GetStagingBufferSize(), VK_NULL_HANDLE, rm->GetQueueMutex()
            );
        }
    }

    // Inizializziamo l'istanza isolata di GameWorld per il Block Maker
    m_previewWorld = std::make_unique<fw::GameWorld>();
    m_previewWorld->Initialize(m_context);
    m_previewWorld->InitializePhysics();

    m_context->forgeWorld = m_previewWorld.get();
    m_context->activeRegistry = &m_previewWorld->GetRegistry();

    // Pulisce la cache delle mesh GPU e CPU tramite il CacheManager centralizzato
    if (m_context && m_context->cacheManager) {
        m_context->cacheManager->FlushGpuRenderCaches(m_context);
        m_context->cacheManager->FlushCpuTransientCaches(m_context);
    }

    // Set the specific state flag for RenderManager to know we're in Void Room mode
    m_context->isBlockMakerMode = true;

    // Spawn preview environment (floor plane)
    auto floorMesh = fw::MeshGenerators::MakeCube(10.0f);
    for (auto& v : floorMesh.vertices) {
        v.color = {0.15f, 0.15f, 0.15f, 1.0f}; // Dark gray grid/floor
        v.roughMetal = {0.9f, 0.0f}; // Rough, non-metallic
    }
    
    entt::entity envEntity = m_previewWorld->GetRegistry().create();
    fw::TransformComponent envTrans;
    envTrans.location = fw::Vec3{0.0f, -0.55f, 0.0f}; // Top face at Y = -0.5f
    envTrans.scale = fw::Vec3{1.0f, 0.01f, 1.0f}; // Flatten to a plane
    m_previewWorld->GetRegistry().emplace<fw::TransformComponent>(envEntity, envTrans);
    m_previewWorld->GetRegistry().emplace<fw::MetadataComponent>(envEntity, "BlockMakerEnv");
    m_previewWorld->EnqueueDeferredMesh("BlockMakerEnv", glm::vec3(0.0f, -0.55f, 0.0f), std::move(floorMesh), nullptr, envEntity);

    // Spawn preview entity in registry
    if (m_context && m_context->blockRegistry) {
        auto& initDef = m_context->blockRegistry->GetBlock(m_selectedBlockId);
        strncpy(m_inputStringId, initDef.stringId.c_str(), sizeof(m_inputStringId) - 1);
        strncpy(m_inputDisplayName, initDef.displayName.c_str(), sizeof(m_inputDisplayName) - 1);
        SynchronizeAllSavedTransformBaselines();
    }
    if (m_context && m_context->cacheManager) {
        m_context->cacheManager->SyncMaterialGpuCache(m_selectedBlockId, m_context);
    }
    UpdatePreviewMesh();

    return true;
}

void BlockMakerState::Update(float dt) {
    if (!m_context) return;

    m_context->isForgeMode = false; // Usa RenderFairworld invece di RenderForge
    m_context->isBlockMakerMode = true; // Isolate rendering to Void Room
    m_context->previewLightDir = m_previewLightDir;
    
    if (m_context->deviceManager) {
        m_context->deviceManager->requireFreeCursor = true; // Impedisce che il cursore scompaia cliccando sullo schermo
    }

    // Handle Input for Orbital Camera via ImGui
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureMouse && !m_isGizmoInteracting) {
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
    m_projNoFlip = glm::perspective(glm::radians(m_cameraFov), aspect, 0.1f, 1000.0f);
    m_context->activeCameraView.projectionMatrix = m_projNoFlip;
    m_context->activeCameraView.projectionMatrix[1][1] *= -1; // Vulkan Y-flip
    
    m_context->forgeWorld = m_previewWorld.get();
    m_context->activeRegistry = &m_previewWorld->GetRegistry();
    auto& m_registry = m_previewWorld->GetRegistry();
    
    HandlePhysicsSimulation(dt);
    
    // Run local systems
    for (auto& sys : m_systems) {
        sys->Update(m_registry, m_context, dt);
    }

    if (m_previewWorld) {
        m_previewWorld->Update(dt);
    }

    if (m_saveMessageTimer > 0.0f) {
        m_saveMessageTimer -= dt;
    }
}

void BlockMakerState::HandlePhysicsSimulation(float dt) {
    if (!m_simulatePhysics) {
        // Modalita reset intenzionale: azzera quota e velocita di simulazione
        m_simPosY = 0.0f;
        m_simVelY = 0.0f;
    } else if (!m_isGizmoInteracting) {
        // Simulazione attiva: avanza fisica solo quando NON in pausa per manipolazione gizmo
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
    auto& m_registry = m_previewWorld->GetRegistry();
    
    // Aggiorna la Transform dell'entità preview
    if (m_previewBlockEntity != entt::null && m_registry.valid(m_previewBlockEntity) && m_context && m_context->blockRegistry) {
        const auto& def = m_context->blockRegistry->GetBlock(m_selectedBlockId);
        auto& transform = m_registry.get<fw::TransformComponent>(m_previewBlockEntity);
        
        // 1. Calculate effective transformed bounds using the 8 corners of canonical mesh AABB
        // and the shape-in-asset transformation: R(localRotation) * S(localScale) * T(-pivotOffset)
        float groundY = 0.0f;
        if (!m_cachedGeometryMesh.vertices.empty()) {
            fw::AABB bounds = m_cachedGeometryMesh.bounds();
            glm::vec3 corners[8] = {
                { bounds.min.x, bounds.min.y, bounds.min.z },
                { bounds.min.x, bounds.min.y, bounds.max.z },
                { bounds.min.x, bounds.max.y, bounds.min.z },
                { bounds.min.x, bounds.max.y, bounds.max.z },
                { bounds.max.x, bounds.min.y, bounds.min.z },
                { bounds.max.x, bounds.min.y, bounds.max.z },
                { bounds.max.x, bounds.max.y, bounds.min.z },
                { bounds.max.x, bounds.max.y, bounds.max.z }
            };

            glm::mat4 mShapeInAsset = glm::mat4(1.0f);
            mShapeInAsset = mShapeInAsset * glm::mat4_cast(glm::quat(glm::radians(def.localRotation)));
            mShapeInAsset = glm::scale(mShapeInAsset, def.localScale);
            mShapeInAsset = glm::translate(mShapeInAsset, -def.pivotOffset);

            float minShapeY = 1e9f;
            for (int i = 0; i < 8; ++i) {
                glm::vec4 tc = mShapeInAsset * glm::vec4(corners[i], 1.0f);
                if (tc.y < minShapeY) {
                    minShapeY = tc.y;
                }
            }
            // Floor top is at Y = -0.50f. Grounding places the shape's lowest point flush with the floor.
            groundY = -0.50f - minShapeY;
        }

        // 2. Turntable rotation (inspection preview around vertical Y axis)
        if (m_autoRotateBlock && !m_isGizmoInteracting) {
            m_blockEulerAngles.y += 45.0f * dt; // 45 gradi al secondo su Yaw
            if (m_blockEulerAngles.y >= 360.0f) m_blockEulerAngles.y -= 360.0f;
        }
        glm::quat qPreview = glm::quat(glm::radians(m_blockEulerAngles));
        glm::quat qAsset = glm::quat(glm::radians(def.localRotation));

        // 3. Compose ModelMatrix = PreviewMatrix * AssetMatrix
        // PreviewMatrix = T(0, groundY + simPosY, 0) * R_preview
        // AssetMatrix = T(localPosition) * R_asset * S_asset * T(-pivotOffset)
        // Combined location = (0, groundY + simPosY, 0) + (qPreview * def.localPosition)
        glm::vec3 newLoc = glm::vec3(0.0f, groundY + m_simPosY, 0.0f) + (qPreview * def.localPosition);
        glm::quat qTotal = qPreview * qAsset;
        fw::Vec3 targetLocation = fw::Vec3{ newLoc.x, newLoc.y, newLoc.z };
        fw::Quat targetRotation = fw::Quat{ qTotal.x, qTotal.y, qTotal.z, qTotal.w };
        fw::Vec3 targetScale = fw::Vec3{ def.localScale.x, def.localScale.y, def.localScale.z };
        fw::Vec3 targetPivot = fw::Vec3{ def.pivotOffset.x, def.pivotOffset.y, def.pivotOffset.z };

        // 4. Idle optimization: only write to ECS component if state actually changed
        bool hasChanged = (transform.location.x != targetLocation.x || transform.location.y != targetLocation.y || transform.location.z != targetLocation.z ||
                           transform.rotation.x != targetRotation.x || transform.rotation.y != targetRotation.y || transform.rotation.z != targetRotation.z || transform.rotation.w != targetRotation.w ||
                           transform.scale.x != targetScale.x || transform.scale.y != targetScale.y || transform.scale.z != targetScale.z ||
                           transform.pivot.x != targetPivot.x || transform.pivot.y != targetPivot.y || transform.pivot.z != targetPivot.z);
        if (hasChanged) {
            transform.location = targetLocation;
            transform.rotation = targetRotation;
            transform.scale = targetScale;
            transform.pivot = targetPivot;
        }
    }
}

void BlockMakerState::Render() {
    ImGuizmo::BeginFrame();
    DrawUI();
    DrawGizmo();
}

void BlockMakerState::UpdatePreviewMesh() {
    if (!m_context || !m_previewWorld || !m_context->blockRegistry || !m_context->materialRegistry) return;
    
    fw::SimBlockDef& def = m_context->blockRegistry->GetBlockMutable(m_selectedBlockId);
    fw::PBRMaterialDef& mat = m_context->materialRegistry->GetMaterialMutable(m_selectedBlockId);

    fw::MeshComponent previewMesh;
    m_lastCompileError.clear();

    bool needsRecompile = false;
    if (mat.sharedShape.IsValid()) {
        if (!m_lastGeometryWasParametric || m_lastGeometryShapeID != mat.sharedShape.id || m_lastGeometryRevision != mat.sharedShape.revision || m_cachedGeometryMesh.vertices.empty()) {
            needsRecompile = true;
        }
    } else {
        if (m_lastGeometryWasParametric || m_lastGeometryLegacyType != mat.shapeType || m_lastGeometryLegacyN != mat.superSphereN || m_cachedGeometryMesh.vertices.empty()) {
            needsRecompile = true;
        }
    }

    if (needsRecompile) {
        if (mat.sharedShape.IsValid()) {
            const fw::ShapeDefinition* shapeDef = m_context->shapeRegistry->GetShapeDef(mat.sharedShape);
            if (shapeDef) {
                fw::GeometryCompileOptions options;
                auto result = fw::GeometryCompiler::Compile(*shapeDef, options);
                if (result.success) {
                    m_cachedGeometryMesh = std::move(result.mesh);
                    m_lastGeometryShapeID = mat.sharedShape.id;
                    m_lastGeometryRevision = mat.sharedShape.revision;
                    m_lastGeometryWasParametric = true;
                } else {
                    m_lastCompileError = "Compile Error: " + result.errorMessage;
                    return; // Preserve last valid preview
                }
            } else {
                m_lastCompileError = "Shape ID not found in registry.";
                return; // Preserve last valid preview
            }
        } else {
            if (mat.shapeType == 1) {
                m_cachedGeometryMesh = fw::MeshGenerators::MakeSuperSphere(mat.superSphereN, 0.5f, 28);
            } else {
                m_cachedGeometryMesh = fw::MeshGenerators::MakeCube(1.0f);
            }
            m_lastGeometryLegacyType = mat.shapeType;
            m_lastGeometryLegacyN = mat.superSphereN;
            m_lastGeometryWasParametric = false;
        }
    }

    // Copy cached geometry
    previewMesh = m_cachedGeometryMesh;
    previewMesh.type = fw::MeshType::Chunk;
    previewMesh.colorOverride[3] = 1.0f;
    
    // Inietta i dati fisici (PBR) nei vertici della mesh in base al MaterialRegistry
    for (auto& v : previewMesh.vertices) {
        v.materialID = (uint32_t)m_selectedBlockId;
        v.color = {mat.baseColorFallback.x, mat.baseColorFallback.y, mat.baseColorFallback.z, 1.0f};
        v.roughMetal = {mat.roughnessFallback, mat.metallicFallback};
        v.emissive = mat.emissiveStrength;
        v.ao = 1.0f; // Default AO
    }

    auto& m_registry = m_previewWorld->GetRegistry();
    if (m_previewBlockEntity == entt::null || !m_registry.valid(m_previewBlockEntity)) {
        m_previewBlockEntity = m_registry.create();
        fw::TransformComponent trans;
        trans.location = fw::Vec3{0.0f, 0.0f, 0.0f};
        m_registry.emplace<fw::TransformComponent>(m_previewBlockEntity, trans);
        m_registry.emplace<fw::MetadataComponent>(m_previewBlockEntity, "PreviewBlock");
    }

    m_previewWorld->EnqueueDeferredMesh("PreviewBlock", glm::vec3(0.0f, 0.0f, 0.0f), std::move(previewMesh), nullptr, m_previewBlockEntity);
}

void BlockMakerState::DrawUI() {
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Block Maker (Data-Driven)")) {
        if (ImGui::Button("< TORNA ALL'HUB")) {
            if (m_previewWorld && m_context->forgeWorld == m_previewWorld.get()) {
                m_context->forgeWorld = m_context->gameWorld;
            }
            m_context->activeRegistry = m_context->gameWorld ? &m_context->gameWorld->GetRegistry() : nullptr;
            m_context->isBlockMakerMode = false;
            m_context->engine->SetGameMode(GameMode::Hub);
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
        
        // --- LAYOUT A DUE COLONNE ---
        ImGui::Columns(2, "BlockMakerColumns");
        ImGui::SetColumnWidth(0, 250.0f); // Larghezza colonna sinistra

        // COLONNA SINISTRA: Lista Blocchi
        ImGui::Text("Elenco Blocchi");
        ImGui::Separator();
        ImGui::BeginChild("BlockList", ImVec2(0, 0), true);
        for (int i = 1; i <= 255; i++) {
            fw::SimBlockDef& blockDef = m_context->blockRegistry->GetBlockMutable(i);
            bool isDirty = IsBlockTransformDirty(static_cast<uint8_t>(i));
            std::string label = std::to_string(i) + ": " + (blockDef.displayName.empty() ? (blockDef.stringId.empty() ? "Vuoto" : blockDef.stringId) : blockDef.displayName);
            if (isDirty) {
                label += " *";
            }
            
            // Colore sbiadito per i blocchi vuoti
            if (blockDef.stringId.empty()) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            
            if (ImGui::Selectable(label.c_str(), m_selectedBlockId == i)) {
                if (m_selectedBlockId != i) {
                    m_selectedBlockId = i;
                    m_simPosY = 0.0f;
                    m_simVelY = 0.0f;
                    strncpy(m_inputStringId, blockDef.stringId.c_str(), sizeof(m_inputStringId) - 1);
                    strncpy(m_inputDisplayName, blockDef.displayName.c_str(), sizeof(m_inputDisplayName) - 1);
                    if (m_context && m_context->cacheManager) {
                        m_context->cacheManager->SyncMaterialGpuCache(m_selectedBlockId, m_context);
                    }
                    UpdatePreviewMesh();
                }
            }
            
            if (blockDef.stringId.empty()) ImGui::PopStyleColor();
        }
        ImGui::EndChild();

        ImGui::NextColumn();

        // COLONNA DESTRA: Proprietà del Blocco Selezionato (con scroll dedicato)
        ImGui::BeginChild("BlockPropertiesScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

        ImGui::Text("Editing Block ID: %d", m_selectedBlockId);
        ImGui::Separator();

        fw::SimBlockDef& def = m_context->blockRegistry->GetBlockMutable(m_selectedBlockId);
        fw::PBRMaterialDef& mat = m_context->materialRegistry->GetMaterialMutable(m_selectedBlockId);

        // --- TABS ---
        if (ImGui::BeginTabBar("BlockTabs")) {
            
            // TAB: BLOCK TRANSFORM (BM-A2)
            if (ImGui::BeginTabItem("Transform")) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "BLOCK TRANSFORM — ID %u: %s", m_selectedBlockId, def.displayName.c_str());
                ImGui::Separator();

                // 1. Box Telemetria e Stato Salvataggio
                bool isCurrentDirty = IsBlockTransformDirty(m_selectedBlockId);
                uint32_t totalDirtyCount = CountUnsavedTransformBlocks();

                ImGui::BeginChild("TransformDiagnosticsBox", ImVec2(0, 135), true);
                ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "TELEMETRIA TRASFORMAZIONE ASSET (BM-A2.1)");
                ImGui::Text("Blocco: ID %u (%s) | String: %s", m_selectedBlockId, def.displayName.c_str(), def.stringId.c_str());
                if (mat.sharedShape.IsValid()) {
                    const fw::ShapeDefinition* shapeDef = m_context->shapeRegistry->GetShapeDef(mat.sharedShape);
                    const char* shapeTypeName = shapeDef ? GetShapeTypeName(shapeDef->type) : "Unknown";
                    ImGui::Text("ShapeHandle: ID %u (Rev %u) | Tipo: %s", mat.sharedShape.id, mat.sharedShape.revision, shapeTypeName);
                } else {
                    ImGui::Text("Shape: Legacy (Tipo %d)", mat.shapeType);
                }
                ImGui::Text("Pos: (%.3f, %.3f, %.3f) | Rot: (%.1f, %.1f, %.1f) deg", 
                    def.localPosition.x, def.localPosition.y, def.localPosition.z,
                    def.localRotation.x, def.localRotation.y, def.localRotation.z);
                ImGui::Text("Scale: (%.3f, %.3f, %.3f) | Pivot: (%.3f, %.3f, %.3f)", 
                    def.localScale.x, def.localScale.y, def.localScale.z,
                    def.pivotOffset.x, def.pivotOffset.y, def.pivotOffset.z);

                if (isCurrentDirty) {
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Stato Blocco: MODIFICHE NON SALVATE (In attesa di persistenza)");
                } else {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Stato Blocco: SINCRONIZZATO (Allineato alla baseline)");
                }

                if (totalDirtyCount > 0) {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "Registro: %u blocco/i con modifiche pendenti (*)", totalDirtyCount);
                } else {
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Registro: Tutte le trasformazioni sincronizzate a disco");
                }
                ImGui::EndChild();

                ImGui::Spacing();

                // Viewport Move Gizmo Control (BM-A3.1)
                ImGui::Checkbox("Viewport Move Gizmo (Assi XYZ)", &m_moveGizmoEnabled);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Attiva/disattiva il gizmo di traslazione 3D (assi X, Y, Z) nel viewport.\nPermette di manipolare direttamente la posizione con il mouse.");
                }
                ImGui::SameLine();
                if (m_moveGizmoEnabled) {
                    if (m_isGizmoInteracting) {
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[IN TRASCINAMENTO]");
                    } else {
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[ATTIVO]");
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[DISATTIVATO]");
                }

                ImGui::Spacing();

                // 2. POSITION
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "POSITION (Local Translation)");
                ImGui::SameLine();
                if (ImGui::Button("Reset Position##PosReset")) {
                    def.localPosition = glm::vec3(0.0f);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Reset Local Position to (0, 0, 0)");
                }
                ImGui::DragFloat3("##LocalPos", &def.localPosition.x, 0.01f, 0.0f, 0.0f, "%.3f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Local Position (X, Y, Z):\nTranslates the entire authored block asset in asset space.\nIndependent of shape-local pivot.");
                }

                ImGui::Spacing();

                // 3. ROTATION
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "ROTATION (Euler Degrees)");
                ImGui::SameLine();
                if (ImGui::Button("Reset Rotation##RotReset")) {
                    def.localRotation = glm::vec3(0.0f);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Reset Local Rotation to (0, 0, 0) deg");
                }
                if (ImGui::DragFloat3("##LocalRot", &def.localRotation.x, 0.5f, 0.0f, 0.0f, "%.1f deg")) {
                    for (int c = 0; c < 3; ++c) {
                        def.localRotation[c] = std::fmod(def.localRotation[c], 360.0f);
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Local Rotation (Pitch X, Yaw Y, Roll Z in degrees):\nRotates geometry around the shape pivot point.");
                }

                ImGui::Spacing();

                // 4. SCALE
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "SCALE (Non-Uniform Factors)");
                ImGui::SameLine();
                if (ImGui::Button("Reset Scale##ScaleReset")) {
                    def.localScale = glm::vec3(1.0f);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Reset Local Scale to (1, 1, 1)");
                }
                if (ImGui::DragFloat3("##LocalScale", &def.localScale.x, 0.01f, 0.0001f, 50.0f, "%.3f")) {
                    for (int c = 0; c < 3; ++c) {
                        if (std::abs(def.localScale[c]) < 1e-4f) {
                            def.localScale[c] = (def.localScale[c] < 0.0f) ? -1e-4f : 1e-4f;
                        }
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Local Scale (X, Y, Z):\nScales geometry along each axis relative to the pivot.\nEnforces |scale| >= 0.0001 to prevent degenerate matrices.");
                }

                ImGui::Spacing();

                // 5. PIVOT
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "PIVOT (Shape-Local Origin Offset)");
                ImGui::SameLine();
                if (ImGui::Button("Reset Pivot##PivotReset")) {
                    def.pivotOffset = glm::vec3(0.0f);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Reset Pivot Offset to (0, 0, 0)");
                }
                ImGui::DragFloat3("##PivotOffset", &def.pivotOffset.x, 0.01f, 0.0f, 0.0f, "%.3f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Pivot Offset (X, Y, Z):\nDefines the center of rotation and scale in shape-local coordinates.\nMoving the pivot changes how the block rotates without translating its position in the world.\nIndependent of Position.");
                }

                ImGui::Spacing();
                ImGui::Separator();

                // 6. Reset All Transforms & Revert to Baseline
                if (ImGui::Button("Reset All Transforms (Identity)", ImVec2(-1, 26))) {
                    def.localPosition = glm::vec3(0.0f);
                    def.localRotation = glm::vec3(0.0f);
                    def.localScale = glm::vec3(1.0f);
                    def.pivotOffset = glm::vec3(0.0f);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Imposta Position=(0,0,0), Rotation=(0,0,0) deg, Scale=(1,1,1), Pivot=(0,0,0).\nNota: se il blocco sul disco aveva valori diversi da identita, risultera NON SALVATO.");
                }

                if (ImGui::Button("Ripristina Baseline Salvata (Revert)", ImVec2(-1, 26))) {
                    const auto& saved = m_savedTransforms[m_selectedBlockId];
                    def.localPosition = saved.localPosition;
                    def.localRotation = saved.localRotation;
                    def.localScale = saved.localScale;
                    def.pivotOffset = saved.pivotOffset;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Ripristina tutti i valori di trasformazione all'ultimo stato salvato su disco per questo blocco.");
                }

                ImGui::Spacing();

                // 7. Explicit Save Button for Registry Block Definitions
                if (ImGui::Button("SALVA DEFINIZIONI REGISTRO BLOCCHI (JSON)", ImVec2(-1, 35))) {
                    m_context->blockRegistry->UpdateBlock(m_selectedBlockId, def);
                    m_context->materialRegistry->UpdateMaterial(m_selectedBlockId, mat);
                    bool s1 = m_context->blockRegistry->SaveToJson("assets/definitions/blocks.json");
                    m_context->shapeRegistry->SaveToJson("assets/definitions/shapes.json");
                    m_context->materialRegistry->SaveToJson("assets/definitions/materials.json");
                    bool s2 = true;
                    if (std::filesystem::exists("../assets/definitions")) {
                        s2 = m_context->blockRegistry->SaveToJson("../assets/definitions/blocks.json");
                        m_context->shapeRegistry->SaveToJson("../assets/definitions/shapes.json");
                        m_context->materialRegistry->SaveToJson("../assets/definitions/materials.json");
                    }
                    if (s1 && s2) {
                        SynchronizeAllSavedTransformBaselines();
                        m_saveMessageTimer = 3.0f;
                        std::cout << "[BlockMaker] Definizioni registro blocchi salvate su disco e baselines sincronizzate.\n";
                    } else {
                        std::cerr << "[BlockMaker] ERRORE: Salvataggio definizioni registro non riuscito! Baselines preservate non sincronizzate.\n";
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Salva l'intero registro dei blocchi (blocks.json, shapes.json, materials.json) su disco.\nSincronizza le baselines di tutti i blocchi modificati.");
                }

                ImGui::EndTabItem();
            }

            // TAB: IDENTITA E GAMEPLAY
            if (ImGui::BeginTabItem("Identity")) {
                ImGui::Spacing();
                
                if (ImGui::InputText("String ID", m_inputStringId, sizeof(m_inputStringId))) {
                    def.stringId = m_inputStringId;
                    m_context->blockRegistry->UpdateBlock(m_selectedBlockId, def);
                }
                if (ImGui::InputText("Display Name", m_inputDisplayName, sizeof(m_inputDisplayName))) {
                    def.displayName = m_inputDisplayName;
                    m_context->blockRegistry->UpdateBlock(m_selectedBlockId, def);
                }
                
                ImGui::Separator();
                ImGui::Checkbox("Is Solid", &def.isSolid);
                ImGui::Checkbox("Is Transparent", &def.isTransparent);
                ImGui::SliderFloat("Light Emission", &def.lightEmissionLevel, 0.0f, 15.0f);
                
                ImGui::EndTabItem();
            }
            
            // TAB: PBR MATERIAL (GRAFICA E TEXTURE PACK & GEOMETRIA PARAMETRICA PER-BLOCCO)
            if (ImGui::BeginTabItem("Graphics (Texture Pack)")) {
                ImGui::Spacing();
                
                bool isDirty = false;
                
                // Fallback Colors
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Fallback / Solid Colors");
                float color[3] = { mat.baseColorFallback.x, mat.baseColorFallback.y, mat.baseColorFallback.z };
                if (ImGui::ColorEdit3("Base Color", color)) {
                    mat.baseColorFallback = {color[0], color[1], color[2]};
                    isDirty = true;
                }
                
                if (ImGui::SliderFloat("Metallic", &mat.metallicFallback, 0.0f, 1.0f)) isDirty = true;
                if (ImGui::SliderFloat("Roughness", &mat.roughnessFallback, 0.0f, 1.0f)) isDirty = true;
                if (ImGui::SliderFloat("Emissive Strength", &mat.emissiveStrength, 0.0f, 10.0f)) isDirty = true;
                if (ImGui::SliderFloat("Alpha (Trasparenza)", &mat.alphaFallback, 0.0f, 1.0f)) isDirty = true;
                
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Comportamenti GPU (Behaviors)");
                
                bool isSeasonal = (mat.behaviors & fw::BLOCK_BEHAVIOR_SEASONAL) != 0;
                if (ImGui::Checkbox("Seasonal (Cambia colore in Inverno/Autunno)", &isSeasonal)) {
                    if (isSeasonal) mat.behaviors |= fw::BLOCK_BEHAVIOR_SEASONAL;
                    else mat.behaviors &= ~fw::BLOCK_BEHAVIOR_SEASONAL;
                    isDirty = true;
                }
                
                bool isAnimated = (mat.behaviors & fw::BLOCK_BEHAVIOR_ANIMATED) != 0;
                if (ImGui::Checkbox("Animated (Texture scorre / Vertex displace)", &isAnimated)) {
                    if (isAnimated) mat.behaviors |= fw::BLOCK_BEHAVIOR_ANIMATED;
                    else mat.behaviors &= ~fw::BLOCK_BEHAVIOR_ANIMATED;
                    isDirty = true;
                }

                bool isWindAffected = (mat.behaviors & fw::BLOCK_BEHAVIOR_WIND_AFFECTED) != 0;
                if (ImGui::Checkbox("Wind Affected (Fogliame/Erba si muove col vento)", &isWindAffected)) {
                    if (isWindAffected) mat.behaviors |= fw::BLOCK_BEHAVIOR_WIND_AFFECTED;
                    else mat.behaviors &= ~fw::BLOCK_BEHAVIOR_WIND_AFFECTED;
                    isDirty = true;
                }

                bool isEmissiveBehavior = (mat.behaviors & fw::BLOCK_BEHAVIOR_EMISSIVE) != 0;
                if (ImGui::Checkbox("Emissive (La texture emette luce propria in scena)", &isEmissiveBehavior)) {
                    if (isEmissiveBehavior) mat.behaviors |= fw::BLOCK_BEHAVIOR_EMISSIVE;
                    else mat.behaviors &= ~fw::BLOCK_BEHAVIOR_EMISSIVE;
                    isDirty = true;
                }
                
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Geometria Parametrica del Blocco");
                
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Geometria Parametrica del Blocco");

                struct TypeInfo { fw::ShapeType type; const char* name; bool supported; };
                static const TypeInfo supportedTypes[] = {
                    { fw::ShapeType::Cube, "Cube", true },
                    { fw::ShapeType::Cuboid, "Cuboid", true },
                    { fw::ShapeType::Parallelepiped, "Parallelepiped", true },
                    { fw::ShapeType::Pyramid, "Pyramid", true },
                    { fw::ShapeType::PyramidFrustum, "Pyramid Frustum", true },
                    { fw::ShapeType::Cylinder, "Cylinder", true },
                    { fw::ShapeType::HollowCylinder, "Hollow Cylinder", true },
                    { fw::ShapeType::Cone, "Cone", true },
                    { fw::ShapeType::ConeFrustum, "Cone Frustum", true },
                    { fw::ShapeType::Sphere, "Sphere", true },
                    { fw::ShapeType::Capsule, "Capsule", true },
                    { fw::ShapeType::LegacySuperSphere, "Legacy SuperSphere", true },
                    { fw::ShapeType::SphericalZone, "Spherical Zone", true },
                    { fw::ShapeType::SphericalSegment, "Spherical Segment", true },
                    { fw::ShapeType::SphericalSector, "Spherical Sector", true },
                    { fw::ShapeType::SphereWithCylindricalBore, "Sphere w/ Bore", true },
                    { fw::ShapeType::SphereWithConicalCavities, "Sphere w/ Cavities", true },
                    { fw::ShapeType::SlicedCylinder, "Sliced Cylinder", true },
                    { fw::ShapeType::Ungula, "Ungula", true },
                    { fw::ShapeType::Barrel, "Barrel", true },
                    { fw::ShapeType::Compound, "Compound", false },
                    { fw::ShapeType::ConvexHull, "Convex Hull", false },
                    { fw::ShapeType::Heightfield, "Heightfield", false },
                    { fw::ShapeType::SignedDistanceField, "Signed Distance Field", false },
                    { fw::ShapeType::TriangleMesh, "Triangle Mesh", false }
                };
                
                fw::ShapeType currentType = fw::ShapeType::None;
                if (mat.sharedShape.IsValid()) {
                    const fw::ShapeDefinition* currentDef = m_context->shapeRegistry->GetShapeDef(mat.sharedShape);
                    if (currentDef) {
                        currentType = currentDef->type;
                    }
                } else if (mat.shapeType == 1) {
                    currentType = fw::ShapeType::LegacySuperSphere;
                } else {
                    currentType = fw::ShapeType::Cube;
                }

                int currentIndex = -1;
                for (int i = 0; i < std::size(supportedTypes); ++i) {
                    if (supportedTypes[i].type == currentType) {
                        currentIndex = i;
                        break;
                    }
                }

                if (ImGui::BeginCombo("Tipo Geometria", currentIndex >= 0 ? supportedTypes[currentIndex].name : "Sconosciuto")) {
                    for (int i = 0; i < std::size(supportedTypes); ++i) {
                        bool is_selected = (currentIndex == i);
                        bool supported = supportedTypes[i].supported;
                        
                        if (!supported) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                        }
                        
                        std::string label = supportedTypes[i].name;
                        if (!supported) label += " (NON IMPLEMENTATO)";
                        
                        if (ImGui::Selectable(label.c_str(), is_selected) && supported && currentIndex != i) {
                            fw::ShapeDefinition newDef;
                            newDef.type = supportedTypes[i].type;
                            switch (newDef.type) {
                                case fw::ShapeType::Cube: newDef.parameters = std::monostate{}; break;
                                case fw::ShapeType::Cuboid: newDef.parameters = fw::ShapeParamsCuboid{{1,1,1}}; break;
                                case fw::ShapeType::Parallelepiped: newDef.parameters = fw::ShapeParamsParallelepiped{{1,0,0},{0,1,0},{0,0,1}}; break;
                                case fw::ShapeType::Pyramid: newDef.parameters = fw::ShapeParamsPyramid{1,1,1,{0,0}}; break;
                                case fw::ShapeType::PyramidFrustum: newDef.parameters = fw::ShapeParamsPyramidFrustum{1,1,0.5f,0.5f,1,{0,0}}; break;
                                case fw::ShapeType::Cylinder: newDef.parameters = fw::ShapeParamsCylinder{0.5f, 1.0f}; break;
                                case fw::ShapeType::HollowCylinder: newDef.parameters = fw::ShapeParamsHollowCylinder{0.5f, 0.25f, 1.0f}; break;
                                case fw::ShapeType::Cone: newDef.parameters = fw::ShapeParamsCone{0.5f, 1.0f}; break;
                                case fw::ShapeType::ConeFrustum: newDef.parameters = fw::ShapeParamsConeFrustum{0.5f, 0.25f, 1.0f}; break;
                                case fw::ShapeType::Sphere: newDef.parameters = fw::ShapeParamsSphere{0.5f}; break;
                                case fw::ShapeType::Capsule: newDef.parameters = fw::ShapeParamsCapsule{0.5f, 0.5f}; break;
                                case fw::ShapeType::LegacySuperSphere: newDef.parameters = fw::ShapeParamsLegacySuperSphere{2.0f}; break;
                                case fw::ShapeType::SphericalZone: newDef.parameters = fw::ShapeParamsSphericalZone{1.0f, -0.5f, 0.5f}; break;
                                case fw::ShapeType::SphericalSegment: newDef.parameters = fw::ShapeParamsSphericalSegment{1.0f, 0.0f}; break;
                                case fw::ShapeType::SphericalSector: newDef.parameters = fw::ShapeParamsSphericalSector{1.0f, 3.14159f, 1.57079f}; break;
                                case fw::ShapeType::SphereWithCylindricalBore: newDef.parameters = fw::ShapeParamsSphereWithCylindricalBore{1.0f, 0.5f, 2.0f}; break;
                                case fw::ShapeType::SphereWithConicalCavities: newDef.parameters = fw::ShapeParamsSphereWithConicalCavities{1.0f, 0.5f, 0.5f}; break;
                                case fw::ShapeType::SlicedCylinder: newDef.parameters = fw::ShapeParamsSlicedCylinder{0.5f, 1.0f, {0.0f, 1.0f, 1.0f}}; break;
                                case fw::ShapeType::Ungula: newDef.parameters = fw::ShapeParamsUngula{0.5f, 1.0f, 0.78539f}; break;
                                case fw::ShapeType::Barrel: newDef.parameters = fw::ShapeParamsBarrel{1.0f, 0.8f, 2.0f}; break;
                                default: break;
                            }
                            newDef.supportsVisualCompilation = true;
                            mat.sharedShape = m_context->shapeRegistry->RegisterShape(newDef);
                            mat.shapeType = (newDef.type == fw::ShapeType::LegacySuperSphere) ? 1 : 0;
                            isDirty = true;
                        }
                        
                        if (!supported) {
                            ImGui::PopStyleColor();
                        }
                        if (is_selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                
                if (mat.sharedShape.IsValid()) {
                    const fw::ShapeDefinition* currentDef = m_context->shapeRegistry->GetShapeDef(mat.sharedShape);
                    if (currentDef) {
                        if (m_editorShapeID != mat.sharedShape.id) {
                            m_editorShapeID = mat.sharedShape.id;
                            m_editorParams = currentDef->parameters;
                            m_isolateSharedShape = true;
                            m_sharedEditConfirmed = false;
                        }
                        
                        uint32_t users = m_context->materialRegistry->CountShapeUsers(mat.sharedShape.id);
                        if (users > 1) {
                            ImGui::Separator();
                            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Shape #%u \xe2\x80\x94 Shared by %u materials", mat.sharedShape.id, users);
                            ImGui::TextDisabled("Editing mode: %s", m_isolateSharedShape ? "Isolated (Copy-on-Write)" : "Global (Affects all)");
                            
                            if (ImGui::RadioButton("Edit Selected Block", m_isolateSharedShape)) {
                                m_isolateSharedShape = true;
                                m_sharedEditConfirmed = false;
                            }
                            ImGui::SameLine();
                            if (ImGui::RadioButton("Edit Shared Shape", !m_isolateSharedShape)) {
                                m_isolateSharedShape = false;
                            }
                            
                            if (!m_isolateSharedShape && !m_sharedEditConfirmed) {
                                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "WARNING: This will affect all %u blocks.", users);
                                if (ImGui::Button("Confirm Shared Edit")) {
                                    m_sharedEditConfirmed = true;
                                }
                            }
                            ImGui::Separator();
                        } else {
                            m_isolateSharedShape = true;
                            m_sharedEditConfirmed = false;
                        }

                        bool paramsChanged = false;
                        
                        if (auto* p = std::get_if<fw::ShapeParamsLegacySuperSphere>(&m_editorParams)) {
                            if (ImGui::SliderFloat("Esponente SuperSfera (n)", &p->n, 0.2f, 10.0f)) paramsChanged = true;
                            ImGui::TextDisabled("n=0.6: Astroide | n=1: Ottaedro | n=2: Sfera | n=4: Cubo Smussato");
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsCuboid>(&m_editorParams)) {
                            if (ImGui::DragFloat3("Dimensioni (X,Y,Z)", &p->size.x, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsParallelepiped>(&m_editorParams)) {
                            if (ImGui::DragFloat3("Base X Vector", &p->basisX.x, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat3("Base Y Vector", &p->basisY.x, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat3("Base Z Vector", &p->basisZ.x, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsPyramid>(&m_editorParams)) {
                            if (ImGui::DragFloat("Base Width", &p->baseWidth, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Base Depth", &p->baseDepth, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Height", &p->height, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat2("Apex Offset", &p->apexOffset.x, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsPyramidFrustum>(&m_editorParams)) {
                            if (ImGui::DragFloat("Bottom Width", &p->bottomWidth, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Bottom Depth", &p->bottomDepth, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Top Width", &p->topWidth, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Top Depth", &p->topDepth, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Height", &p->height, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat2("Apex Offset", &p->apexOffset.x, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsCylinder>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio", &p->radius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Altezza", &p->height, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsCone>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio Base", &p->radius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Altezza", &p->height, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsConeFrustum>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio Base", &p->bottomRadius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Raggio Superiore", &p->topRadius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Altezza", &p->height, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsHollowCylinder>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio Esterno", &p->outerRadius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Raggio Interno", &p->innerRadius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Altezza", &p->height, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsCapsule>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio", &p->radius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Mezza Altezza Cilindro", &p->halfHeight, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsSphere>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio", &p->radius, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsSphericalZone>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio Sfera", &p->radius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Taglio Inferiore (Y)", &p->bottomY, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Taglio Superiore (Y)", &p->topY, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsSphericalSegment>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio Sfera", &p->radius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Taglio Base (Y)", &p->baseY, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsSphericalSector>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio", &p->radius, 0.05f)) paramsChanged = true;
                            if (ImGui::SliderAngle("Range Polare (Theta)", &p->thetaRange, 0.0f, 360.0f)) paramsChanged = true;
                            if (ImGui::SliderAngle("Range Azimutale (Phi)", &p->phiRange, 0.0f, 180.0f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsSphereWithCylindricalBore>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio Sfera", &p->sphereRadius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Raggio Foro (Cilindro)", &p->cylinderRadius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Altezza Cilindro (Riservato)", &p->cylinderHeight, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsSphereWithConicalCavities>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio Sfera", &p->sphereRadius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Raggio Cavita' (Cono)", &p->coneRadius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Profondita' Cavita' (Cono)", &p->coneHeight, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsSlicedCylinder>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio", &p->radius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Altezza", &p->height, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat3("Normale di Taglio", &p->cutNormal.x, 0.05f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsUngula>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio", &p->radius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Altezza", &p->height, 0.05f)) paramsChanged = true;
                            if (ImGui::SliderAngle("Angolo di Taglio", &p->cutAngle, 0.0f, 89.9f)) paramsChanged = true;
                        }
                        else if (auto* p = std::get_if<fw::ShapeParamsBarrel>(&m_editorParams)) {
                            if (ImGui::DragFloat("Raggio Centrale", &p->midRadius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Raggio Estremo", &p->endRadius, 0.05f)) paramsChanged = true;
                            if (ImGui::DragFloat("Altezza", &p->height, 0.05f)) paramsChanged = true;
                        }
                        
                        if (paramsChanged) {
                            auto valid = m_context->shapeRegistry->ValidateParameters(currentDef->type, m_editorParams, mat.sharedShape.id);
                            if (valid.isValid) {
                                m_lastValidationError = "";
                                bool canEdit = true;
                                
                                if (users > 1) {
                                    if (m_isolateSharedShape) {
                                        fw::ShapeDefinition clone = *currentDef;
                                        clone.id = 0; 
                                        
                                        fw::ShapeHandle newHandle = m_context->shapeRegistry->RegisterShape(clone);
                                        if (newHandle.IsValid()) {
                                            mat.sharedShape = newHandle;
                                            m_editorShapeID = newHandle.id;
                                        } else {
                                            canEdit = false;
                                        }
                                    } else {
                                        if (!m_sharedEditConfirmed) {
                                            canEdit = false;
                                        }
                                    }
                                }
                                
                                if (canEdit) {
                                    if (m_context->shapeRegistry->UpdateShape(mat.sharedShape, m_editorParams)) {
                                        mat.sharedShape.revision++;
                                        isDirty = true;
                                    }
                                }
                            } else {
                                m_lastValidationError = valid.errorMessage;
                            }
                        }
                        
                        if (!m_lastValidationError.empty()) {
                            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Parametri non validi:");
                            ImGui::TextWrapped("%s", m_lastValidationError.c_str());
                        }
                    }
                } else {
                    if (mat.shapeType == 1) {
                        ImGui::TextDisabled("Modalita' Legacy. Converti selezionando un tipo dal menu.");
                        if (ImGui::SliderFloat("Esponente SuperSfera (n) [Legacy]", &mat.superSphereN, 0.2f, 10.0f)) {
                            isDirty = true;
                        }
                        ImGui::TextDisabled("n=0.6: Astroide | n=1: Ottaedro | n=2: Sfera | n=4: Cubo Smussato");
                    } else {
                        ImGui::TextDisabled("Standard Voxel Cube [Legacy]");
                    }
                }
                
                if (!m_lastCompileError.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Errore di Compilazione Geometria:");
                    ImGui::TextWrapped("%s", m_lastCompileError.c_str());
                }

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "PBR Texture Maps");

                if (ImGui::Button("Apri Cartella Texture OS", ImVec2(-1, 30))) {
                    std::filesystem::create_directories("assets/textures");
                    ShellExecuteA(NULL, "open", "assets\\textures", NULL, NULL, SW_SHOWDEFAULT);
                }
                ImGui::Spacing();
                
                auto drawTextureField = [&](const char* label, std::string& pathRef, const std::string& typeSuffix, RenderManager::PBRTextureType pbrType) {
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
                            if (std::filesystem::exists("../assets/definitions")) {
                                m_context->materialRegistry->SaveToJson("../assets/definitions/materials.json");
                            }
                            
                            // Sincronizza la cache GPU del materiale tramite il CacheManager!
                            if (m_context && m_context->cacheManager) {
                                m_context->cacheManager->SyncMaterialGpuCache(m_selectedBlockId, m_context);
                            }
                            
                            m_copyProgress = 1.0f;
                            m_saveMessageTimer = 3.0f;
                        }
                    }
                    ImGui::PopID();
                    ImGui::Text("%s", label);
                };

                drawTextureField("Albedo Map", mat.albedoPath, "albedo", RenderManager::PBRTextureType::ALBEDO);
                drawTextureField("Normal Map", mat.normalPath, "normal", RenderManager::PBRTextureType::NORMAL);
                drawTextureField("ORM Map (Occlusion, Roughness, Metallic)", mat.ormPath, "orm", RenderManager::PBRTextureType::ORM);
                
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
                    if (m_context && m_context->cacheManager) {
                        m_context->cacheManager->SyncMaterialGpuCache(m_selectedBlockId, m_context);
                    }
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
                
                ImGui::Separator();
                
                ImGui::Text("Preview Options");
                ImGui::Checkbox("Simulate Physics (Bouncing)", &m_simulatePhysics);
                ImGui::Checkbox("Auto-Rotate Block", &m_autoRotateBlock);
                if (!m_autoRotateBlock) {
                    ImGui::SliderFloat3("Manual Rotation (Euler)", &m_blockEulerAngles.x, 0.0f, 360.0f);
                }
                
                if (ImGui::Button("SALVA TUTTE LE DEFINIZIONI BLOCCHI", ImVec2(-1, 40))) {
                    m_context->blockRegistry->UpdateBlock(m_selectedBlockId, def);
                    m_context->materialRegistry->UpdateMaterial(m_selectedBlockId, mat);
                    bool s1 = m_context->blockRegistry->SaveToJson("assets/definitions/blocks.json");
                    m_context->shapeRegistry->SaveToJson("assets/definitions/shapes.json");
                    m_context->materialRegistry->SaveToJson("assets/definitions/materials.json");
                    bool s2 = true;
                    if (std::filesystem::exists("../assets/definitions")) {
                        s2 = m_context->blockRegistry->SaveToJson("../assets/definitions/blocks.json");
                        m_context->shapeRegistry->SaveToJson("../assets/definitions/shapes.json");
                        m_context->materialRegistry->SaveToJson("../assets/definitions/materials.json");
                    }
                    if (m_context->cacheManager) {
                        m_context->cacheManager->SyncMaterialGpuCache(m_selectedBlockId, m_context);
                    }
                    if (m_context->engine && m_context->engine->GetRenderManager()) {
                        m_context->engine->GetRenderManager()->InvalidateForgeCache();
                    }
                    UpdatePreviewMesh();
                    if (s1 && s2) {
                        SynchronizeAllSavedTransformBaselines();
                        m_saveMessageTimer = 3.0f;
                        std::cout << "[BlockMaker] Dati blocco " << (int)m_selectedBlockId << " e registro salvati in assets/definitions/\n";
                    } else {
                        std::cerr << "[BlockMaker] ERRORE durante il salvataggio delle definizioni su disco!\n";
                    }
                }
                
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }
        
        ImGui::Separator();
        
        if (ImGui::Button("SALVA DEFINIZIONE & ASSET BLOCCO (JSON/GPU)", ImVec2(-1, 35))) {
            m_context->blockRegistry->UpdateBlock(m_selectedBlockId, def);
            m_context->materialRegistry->UpdateMaterial(m_selectedBlockId, mat);
            bool s1 = m_context->blockRegistry->SaveToJson("assets/definitions/blocks.json");
            m_context->shapeRegistry->SaveToJson("assets/definitions/shapes.json");
            m_context->materialRegistry->SaveToJson("assets/definitions/materials.json");
            bool s2 = true;
            if (std::filesystem::exists("../assets/definitions")) {
                s2 = m_context->blockRegistry->SaveToJson("../assets/definitions/blocks.json");
                m_context->shapeRegistry->SaveToJson("../assets/definitions/shapes.json");
                m_context->materialRegistry->SaveToJson("../assets/definitions/materials.json");
            }
            
            // Esporta asset file in assets/blocks/ per l'Asset Browser di FORGE
            std::filesystem::create_directories("assets/blocks");
            std::string cleanName = def.stringId;
            std::replace(cleanName.begin(), cleanName.end(), ':', '_');
            if (cleanName.empty()) cleanName = "block_" + std::to_string(m_selectedBlockId);
            
            std::unordered_map<glm::ivec3, fw::StructureBlock> bMap;
            bMap[glm::ivec3(0,0,0)] = { (int)m_selectedBlockId, glm::vec4(mat.baseColorFallback, 1.0f) };
            m_previewWorld->GetStructureManager().SaveStructure(cleanName, bMap, 0, 0, 0, 0);

            if (m_context->cacheManager) {
                m_context->cacheManager->SyncMaterialGpuCache(m_selectedBlockId, m_context);
            }
            if (m_context->engine && m_context->engine->GetRenderManager()) {
                m_context->engine->GetRenderManager()->InvalidateForgeCache();
            }
            UpdatePreviewMesh();
            if (s1 && s2) {
                SynchronizeAllSavedTransformBaselines();
                m_saveMessageTimer = 3.0f;
                std::cout << "[BlockMaker] Definizioni fisiche, grafiche e geometriche del blocco " << (int)m_selectedBlockId << " salvate con successo su disco e sincronizzate in GPU!\n";
            } else {
                std::cerr << "[BlockMaker] ERRORE durante il salvataggio su disco!\n";
            }
        }
        
        if (m_saveMessageTimer > 0.0f) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Definizione Blocco ID %d (%s) salvata ed esportata per FORGE!", m_selectedBlockId, def.displayName.c_str());
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

        ImGui::EndChild(); // Fine BlockPropertiesScrollRegion
    }
    ImGui::Columns(1);
    ImGui::End();
}

void BlockMakerState::DrawGizmo() {
    if (!m_moveGizmoEnabled) {
        m_isGizmoInteracting = false;
        return;
    }
    if (!m_context || !m_context->blockRegistry || m_previewBlockEntity == entt::null) {
        m_isGizmoInteracting = false;
        return;
    }

    fw::SimBlockDef& def = m_context->blockRegistry->GetBlockMutable(m_selectedBlockId);

    // 1. Calcolo grounding della presentazione identico a HandlePhysicsSimulation
    float groundY = 0.0f;
    if (!m_cachedGeometryMesh.vertices.empty()) {
        fw::AABB bounds = m_cachedGeometryMesh.bounds();
        glm::vec3 corners[8] = {
            { bounds.min.x, bounds.min.y, bounds.min.z },
            { bounds.min.x, bounds.min.y, bounds.max.z },
            { bounds.min.x, bounds.max.y, bounds.min.z },
            { bounds.min.x, bounds.max.y, bounds.max.z },
            { bounds.max.x, bounds.min.y, bounds.min.z },
            { bounds.max.x, bounds.min.y, bounds.max.z },
            { bounds.max.x, bounds.max.y, bounds.min.z },
            { bounds.max.x, bounds.max.y, bounds.max.z }
        };

        glm::mat4 mShapeInAsset = glm::mat4(1.0f);
        mShapeInAsset = mShapeInAsset * glm::mat4_cast(glm::quat(glm::radians(def.localRotation)));
        mShapeInAsset = glm::scale(mShapeInAsset, def.localScale);
        mShapeInAsset = glm::translate(mShapeInAsset, -def.pivotOffset);

        float minShapeY = 1e9f;
        for (int i = 0; i < 8; ++i) {
            glm::vec4 tc = mShapeInAsset * glm::vec4(corners[i], 1.0f);
            if (tc.y < minShapeY) {
                minShapeY = tc.y;
            }
        }
        groundY = -0.50f - minShapeY;
    }

    glm::quat qPreview = glm::quat(glm::radians(m_blockEulerAngles));
    glm::quat qAsset = glm::quat(glm::radians(def.localRotation));

    // 2. PresentationMatrix = T(0, groundY + simPosY, 0) * R_preview
    glm::mat4 presMat = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, groundY + m_simPosY, 0.0f))
                      * glm::mat4_cast(qPreview);

    // 3. AssetMatrix = T(localPosition) * R_asset * S_asset * T(-pivotOffset)
    glm::mat4 assetMat = glm::translate(glm::mat4(1.0f), def.localPosition)
                       * glm::mat4_cast(qAsset)
                       * glm::scale(glm::mat4(1.0f), def.localScale)
                       * glm::translate(glm::mat4(1.0f), -def.pivotOffset);

    // 4. ManipulatedWorldMatrix = PresentationMatrix * AssetMatrix
    glm::mat4 worldMat = presMat * assetMat;

    ImGuiIO& io = ImGui::GetIO();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
    if (vp) {
        ImGuizmo::SetRect(vp->Pos.x, vp->Pos.y, vp->Size.x, vp->Size.y);
    } else {
        ImGuizmo::SetRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);
    }

    // Protezione overlay ImGui: se il mouse e' sopra finestre/widget UI e non stiamo gia' trascinando, disabilita il gizmo
    bool canInteract = m_moveGizmoEnabled && (!io.WantCaptureMouse || m_isGizmoInteracting);
    ImGuizmo::Enable(canInteract);

    // 5. Move gizmo only: TRANSLATE operation in WORLD mode
    ImGuizmo::Manipulate(
        glm::value_ptr(m_context->activeCameraView.viewMatrix),
        glm::value_ptr(m_projNoFlip),
        ImGuizmo::TRANSLATE,
        ImGuizmo::WORLD,
        glm::value_ptr(worldMat)
    );

    if (ImGuizmo::IsUsing()) {
        m_isGizmoInteracting = true;
        // 6. Conversione corretta della matrice manipolata tenendo conto del pivot:
        // AssetTRS = inverse(PresentationMatrix) * ManipulatedWorldMatrix * T(pivotOffset)
        glm::mat4 assetTRS = glm::inverse(presMat) * worldMat * glm::translate(glm::mat4(1.0f), def.pivotOffset);
        // 7. Decomposizione deterministica: la colonna 3 di AssetTRS è esattamente localPosition.
        // Rotazione, scala e pivot rimangono inalterati e congelati.
        def.localPosition = glm::vec3(assetTRS[3]);
    } else {
        m_isGizmoInteracting = false;
    }
}
