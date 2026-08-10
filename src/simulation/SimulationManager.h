#pragma once
#include <vector>
#include <memory>
#include <entt/entt.hpp>
#include "SimDataLayer.h"
#include "SimComponents.h"
#include "EventBridge.h"
#include "ZoneRegistry.h"

namespace fw {

class SimulationManager {
public:
    SimulationManager() = default;
    
    // Inizializza la griglia
    void InitializeGrid(int chunksX, int chunksY) {
        m_chunksX = chunksX;
        m_chunksY = chunksY;
        m_grid.resize(chunksX * chunksY);
    }
    
    void Initialize() {
        m_zoneRegistry.Initialize();
        // Altre inzializzazioni...
    }

    const ZoneRegistry& GetZoneRegistry() const { return m_zoneRegistry; }
    ZoneRegistry& GetZoneRegistry() { return m_zoneRegistry; }
    
    // Recupera un Tile globale
    Tile& GetGlobalTile(int worldX, int worldY) {
        int cx = worldX / CITY_CHUNK_SIZE;
        int cy = worldY / CITY_CHUNK_SIZE;
        int localX = worldX % CITY_CHUNK_SIZE;
        int localY = worldY % CITY_CHUNK_SIZE;
        
        return m_grid[cy * m_chunksX + cx].GetTile(localX, localY);
    }
    
    // ECS Reaction: Processa gli eventi critici dalla GPU (Readback Buffer)
    void ProcessGPUEvents(const std::vector<ReadbackEvent>& events, entt::registry& registry) {
        for (const auto& ev : events) {
            if (ev.event_type == 0) { // Fusione termica (Max Heat Superato)
                int worldX = ev.cell_index % (m_chunksX * CITY_CHUNK_SIZE);
                int worldY = ev.cell_index / (m_chunksX * CITY_CHUNK_SIZE);
                Tile& tile = GetGlobalTile(worldX, worldY);
                
                if (tile.entity_id != entt::null) {
                    auto entity = static_cast<entt::entity>(tile.entity_id);
                    if (registry.valid(entity) && registry.all_of<AffinityComponent>(entity)) {
                        auto& affinity = registry.get<AffinityComponent>(entity);
                        affinity.degradation_counter += static_cast<int>(ev.event_magnitude);
                    }
                }
            }
        }
    }

private:
    int m_chunksX = 0;
    int m_chunksY = 0;
    std::vector<CityChunk> m_grid; // Array lineare contiguo = Zero cache misses
    ZoneRegistry m_zoneRegistry;
};

} // namespace fw
