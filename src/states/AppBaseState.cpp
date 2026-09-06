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
#include "RuntimeManager.h"
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
    m_appInitialized = false;
    m_loadingSpinnerAngle = 0.0f;
    if (m_context && m_context->runtimeManager) {
        m_context->runtimeManager->RequireFeaturesAsync(GetRequiredFeatures());
    }
    return true; // Ritorna subito per evitare freeze UI
}

void AppBaseState::Update(float dt) {
    if (!m_appInitialized) {
        if (m_context && m_context->runtimeManager && m_context->runtimeManager->IsReady()) {
            m_appInitialized = InitApp();
        } else {
            m_loadingSpinnerAngle += dt * 5.0f;
        }
        return;
    }

    if (m_context && m_context->deviceManager) {
        m_context->deviceManager->requireFreeCursor = true;
    }
    UpdateApp(dt);
}

void AppBaseState::Render() {
    if (!m_appInitialized) {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.08f, 1.0f));
        if (ImGui::Begin("LoadingScreen", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs)) {
            ImVec2 center(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f);
            ImGui::SetCursorPos(ImVec2(viewport->Size.x * 0.5f - 200.0f, viewport->Size.y * 0.5f - 80.0f));
            ImGui::SetWindowFontScale(2.0f);
            std::string loadMsg = "CARICAMENTO " + m_appName + " IN CORSO...";
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), loadMsg.c_str());
            ImGui::SetWindowFontScale(1.0f);
            
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            float radius = 40.0f;
            drawList->PathClear();
            for (int i = 0; i < 30; i++) {
                float a = m_loadingSpinnerAngle + (i / 30.0f) * 3.14159f * 1.5f;
                drawList->PathLineTo(ImVec2(center.x + cosf(a) * radius, center.y + sinf(a) * radius));
            }
            drawList->PathStroke(ImColor(100, 200, 255, 255), false, 6.0f);
        }
        ImGui::End();
        ImGui::PopStyleColor();
        return;
    }
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
            m_context->stateManager->PopState();
        }
    }
}
