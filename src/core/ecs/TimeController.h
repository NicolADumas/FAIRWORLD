#pragma once
#include <imgui.h>
#include "SimulationManager.h"
#include "src/render/ThermodynamicsPipeline.h"

namespace fw {

enum class SimState { Paused, Playing };

class TimeController {
public:
    SimState state = SimState::Paused;
    float time_multiplier = 1.0f;
    float accumulator = 0.0f;
    const float TICK_RATE = 0.5f; // Mezzo secondo reale per tick termico
    
    bool debug_lens_active = false;
    
    void Update(float dt, SimulationManager& simManager, ThermodynamicsPipeline& thermoPipeline) {
        if (state == SimState::Paused) return;
        
        accumulator += (dt * time_multiplier);
        
        while (accumulator >= TICK_RATE) {
            accumulator -= TICK_RATE;
            
            // 1. Swap buffer ping-pong
            thermoPipeline.SwapBuffers();
            
            // 2. Dispatch Compute Shader (idealmente chiamato nel loop Vulkan prima del pass grafico)
            // L'esecuzione effettiva avverrà nel RenderManager::RecordCommandBuffer
            
            // 3. Leggi eventi (sincrono per test, ma asincrono in prod leggendo l'N-1 frame)
            auto events = thermoPipeline.FetchEvents();
            
            // MOCK registry per dimostrazione (andrebbe passato dal contesto ECS)
            entt::registry dummyRegistry; 
            simManager.ProcessGPUEvents(events, dummyRegistry);
        }
    }
    
    void RenderUI() {
        ImGui::Begin("DevMode: Environment");
        
        ImGui::Text("Simulation State: %s", state == SimState::Playing ? "PLAYING" : "PAUSED");
        
        if (ImGui::Button("Play")) state = SimState::Playing;
        ImGui::SameLine();
        if (ImGui::Button("Pause")) state = SimState::Paused;
        
        ImGui::Text("Time Speed:");
        if (ImGui::Button("x1")) time_multiplier = 1.0f;
        ImGui::SameLine();
        if (ImGui::Button("x2")) time_multiplier = 2.0f;
        ImGui::SameLine();
        if (ImGui::Button("x5")) time_multiplier = 5.0f;
        
        ImGui::Separator();
        
        ImGui::Checkbox("Attiva Lente Termica", &debug_lens_active);
        
        ImGui::End();
    }
};

} // namespace fw
