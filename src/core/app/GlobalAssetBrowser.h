#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include "imgui.h"

class Player;

namespace fw {

class GlobalAssetBrowser {
public:
    struct AssetFile {
        std::string name;
        std::string path;
        std::string extension;
    };

    GlobalAssetBrowser() = default;
    
    void Initialize();
    void RefreshAssets();
    void DrawUI(bool* isOpen, ::Player* player = nullptr, class ForgeWorld* forgeWorld = nullptr);

    // Callbacks optional for specific modes (like "Spawn" in PlayMode)
    std::string GetSelectedAssetToSpawn() const { return m_assetToSpawn; }
    void ClearSelectedAsset() { m_assetToSpawn = ""; }

private:
    std::vector<AssetFile> m_availableAssets;
    std::string m_assetToSpawn = "";
    
    // UI state
    char m_renameBuffer[128] = "";
    std::string m_fileToRename = "";
    bool m_showRenamePopup = false;
    bool m_showDeletePopup = false;
    std::string m_fileToDelete = "";
};

} // namespace fw
