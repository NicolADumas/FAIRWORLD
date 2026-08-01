#include "pch.h"
#include "AppBaseState.h"
#include "SharedContext.h"
#include "StateManager.h"
#include "HubState.h"
#include "FAIRWORLD.h"
#include "DeviceManager.h"
#include "VramSlabAllocator.h"
#include "VulkanDmaManager.h"
#include "JobSystem.h"
#include "WorldProjectManager.h"
#include "RenderManager.h"
#include "imgui.h"
#include <iostream>

AppBaseState::AppBaseState(SharedContext* context) : m_context(context) {
    std::cout << "[AppBaseState] Costruttore Architettura Madre invocato.\n";
}

AppBaseState::~AppBaseState() {
    if (m_context && m_previewWorld && m_context->forgeWorld == m_previewWorld.get()) {
        m_context->forgeWorld = m_context->gameWorld;
    }
    std::cout << "[AppBaseState] Distruttore Architettura Madre: memoria isolata del previewWorld rilasciata in sicurezza.\n";
}

bool AppBaseState::Init() {
    if (m_context) {
        if (!m_context->vramAllocator) {
            m_context->vramAllocator = new fw::VramSlabAllocator(2048ULL * 1024ULL * 1024ULL);
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
        if (!m_context->jobSystem) {
            m_context->jobSystem = new fw::JobSystem();
            m_context->jobSystem->Initialize();
        }
    }
    return InitApp();
}

void AppBaseState::Update(float dt) {
    if (m_context && m_context->deviceManager) {
        m_context->deviceManager->requireFreeCursor = true;
    }
    UpdateApp(dt);
}

void AppBaseState::Render() {
    RenderApp();
}

bool AppBaseState::DrawMotherHeader(const char* appTitle) {
    if (ImGui::Button("< TORNA ALL'HUB PRINCIPALE", ImVec2(-1, 25))) {
        ReturnToHub();
        return true;
    }
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.2f, 0.9f, 1.0f, 1.0f), "%s", appTitle);
    ImGui::Separator();
    return false;
}

void AppBaseState::ReturnToHub() {
    if (m_previewWorld && m_context && m_context->forgeWorld == m_previewWorld.get()) {
        m_context->forgeWorld = m_context->gameWorld;
    }
    if (m_context) {
        m_context->activeRegistry = m_context->gameWorld ? &m_context->gameWorld->GetRegistry() : nullptr;
        m_context->isMapBuilderMode = false;
        if (m_context->engine) {
            m_context->engine->SetGameMode(GameMode::Hub);
        }
        if (m_context->stateManager) {
            m_context->stateManager->ChangeState(std::make_unique<HubState>(m_context));
        }
    }
}
