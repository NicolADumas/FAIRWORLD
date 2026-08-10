#pragma once
#include <string>
#include <iostream>
#include "MapDocument.h"

namespace fw {
    class BlockRegistry;

    class WorldProjectManager {
    public:
        explicit WorldProjectManager(const std::string& defaultPath = "saves/map/world_map.json");
        ~WorldProjectManager();

        bool LoadProject(const std::string& path = "", BlockRegistry* registry = nullptr);
        bool SaveProject(const std::string& path = "");

        void ValidateBlocks(BlockRegistry* registry);
        void EnsureDefaultPlanetExists();

        MapDocument& GetDocumentMutable() {
            m_isDirty = true;
            return m_document;
        }

        const MapDocument& GetDocument() const {
            return m_document;
        }

        bool IsDirty() const { return m_isDirty; }
        void MarkClean() { m_isDirty = false; }
        void MarkDirty() { m_isDirty = true; }

        void SetDefaultPath(const std::string& path) { m_defaultPath = path; }
        const std::string& GetCurrentPath() const { return m_currentPath; }

    private:
        MapDocument m_document;
        std::string m_defaultPath;
        std::string m_currentPath;
        bool m_isDirty = false;
    };
}
