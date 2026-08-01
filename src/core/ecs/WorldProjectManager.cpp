#include "pch.h"
#include "WorldProjectManager.h"
#include "BlockRegistry.h"
#include <iostream>

namespace fw {

    WorldProjectManager::WorldProjectManager(const std::string& defaultPath) 
        : m_defaultPath(defaultPath), m_currentPath(defaultPath), m_isDirty(false) {
        std::cout << "[WorldProjectManager] Inizializzato con percorso predefinito: " << defaultPath << "\n";
    }

    WorldProjectManager::~WorldProjectManager() {
        std::cout << "[WorldProjectManager] Distrutto.\n";
    }

    void WorldProjectManager::EnsureDefaultPlanetExists() {
        if (m_document.planets.empty()) {
            fw::PlanetMap megaPlanet;
            megaPlanet.type = ::PlanetType::EarthPrime;
            megaPlanet.name = "Fairworld Prime";
            megaPlanet.planetRadius = 50.0f;
            megaPlanet.minX = -16;
            megaPlanet.maxX = 16;
            megaPlanet.minZ = -16;
            megaPlanet.maxZ = 16;
            m_document.planets.push_back(megaPlanet);
            std::cout << "[WorldProjectManager] Nessun salvataggio pianeti trovato. Creato nuovo 'Fairworld Prime'.\n";
            m_isDirty = true;
        }
        if (m_document.terrainLibrary.empty()) {
            fw::TerrainTemplate defaultTmpl;
            defaultTmpl.id = "default_terrain";
            defaultTmpl.name = "Terreno Standard";
            defaultTmpl.baseType = fw::MapRegionType::Forest;
            defaultTmpl.basePerlinFrequency = 0.03f;
            defaultTmpl.baseGravityModifier = 1.0f;
            defaultTmpl.baseAngularRadius = 0.25f;
            m_document.terrainLibrary.push_back(defaultTmpl);
            std::cout << "[WorldProjectManager] Libreria terreni vuota. Creato 'Terreno Standard'.\n";
            m_isDirty = true;
        }
    }

    void WorldProjectManager::ValidateBlocks(BlockRegistry* registry) {
        if (!registry) return;

        uint8_t idGrass = registry->GetBlock("fairworld:grass").id;
        uint8_t idDirt = registry->GetBlock("fairworld:dirt").id;
        if (idGrass == 0) idGrass = 1;
        if (idDirt == 0) idDirt = 2;

        auto checkAndFixBlock = [&](uint8_t& id, uint8_t defaultId) {
            const auto& def = registry->GetBlock(id);
            if (def.stringId == "fairworld:unknown" && id != 0) {
                std::cout << "[WorldProjectManager] Trovato Block ID obsoleto o sconosciuto: " << (int)id 
                          << ". Ripristinato a ID predefinito (" << (int)defaultId << ").\n";
                id = defaultId;
                m_isDirty = true;
            }
        };

        for (auto& planet : m_document.planets) {
            for (auto& reg : planet.regions) {
                checkAndFixBlock(reg.surfaceBlockId, idGrass);
                checkAndFixBlock(reg.subsurfaceBlockId, idDirt);
            }
        }

        for (auto& tmpl : m_document.terrainLibrary) {
            for (auto& sub : tmpl.subRegions) {
                checkAndFixBlock(sub.surfaceBlockId, idGrass);
                checkAndFixBlock(sub.subsurfaceBlockId, idDirt);
            }
        }
        std::cout << "[WorldProjectManager] Validazione BlockRegistry completata con successo.\n";
    }

    bool WorldProjectManager::LoadProject(const std::string& path, BlockRegistry* registry) {
        std::string targetPath = path.empty() ? m_defaultPath : path;
        m_currentPath = targetPath;

        bool success = m_document.LoadJSON(targetPath);
        if (success) {
            std::cout << "[WorldProjectManager] Progetto caricato correttamente da: " << targetPath << "\n";
        } else {
            std::cout << "[WorldProjectManager] File progetto non trovato o non valido in: " << targetPath << ". Inizializzazione predefinita.\n";
        }

        EnsureDefaultPlanetExists();
        if (registry) {
            ValidateBlocks(registry);
        }

        m_isDirty = false;
        return success;
    }

    bool WorldProjectManager::SaveProject(const std::string& path) {
        std::string targetPath = path.empty() ? m_currentPath : path;
        if (targetPath.empty()) targetPath = m_defaultPath;

        bool success = m_document.SaveJSON(targetPath);
        if (success) {
            std::cout << "[WorldProjectManager] Progetto salvato con successo su: " << targetPath << "\n";
            m_currentPath = targetPath;
            m_isDirty = false;
        } else {
            std::cerr << "[WorldProjectManager ERRORE] Impossibile salvare il progetto su: " << targetPath << "\n";
        }
        return success;
    }

} // namespace fw
