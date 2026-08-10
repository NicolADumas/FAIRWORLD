#include "GlobalAssetBrowser.h"
#include <iostream>
#include "Player.h"
#include "GameWorld.h"
#include "TimeManager.h" // Per struct ItemType (indiretto, o InventoryItem)
#include "SharedContext.h" // Per struct InventoryItem
#include <cstring>

namespace fw {

void GlobalAssetBrowser::Initialize() {
    RefreshAssets();
}

void GlobalAssetBrowser::RefreshAssets() {
    m_availableAssets.clear();
    try {
        if (std::filesystem::exists("assets/rigs")) {
            for (const auto& entry : std::filesystem::directory_iterator("assets/rigs")) {
                if (entry.path().extension() == ".fwrig") {
                    m_availableAssets.push_back({entry.path().stem().string(), entry.path().string(), ".fwrig"});
                }
            }
        }
        if (std::filesystem::exists("assets/microvoxels")) {
            for (const auto& entry : std::filesystem::directory_iterator("assets/microvoxels")) {
                if (entry.path().extension() == ".fwmv") {
                    m_availableAssets.push_back({entry.path().stem().string(), entry.path().string(), ".fwmv"});
                }
            }
        }
        if (std::filesystem::exists("assets/blocks")) {
            for (const auto& entry : std::filesystem::directory_iterator("assets/blocks")) {
                if (entry.path().extension() == ".fwblock") {
                    m_availableAssets.push_back({entry.path().stem().string(), entry.path().string(), ".fwblock"});
                }
            }
        }
    } catch (...) {}
}

void GlobalAssetBrowser::DrawUI(bool* isOpen, Player* player, GameWorld* gameWorld) {
    if (!*isOpen) return;

    ImGui::SetNextWindowSize(ImVec2(500, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Global Asset Browser", isOpen, ImGuiWindowFlags_MenuBar)) {
        
        if (ImGui::BeginMenuBar()) {
            if (ImGui::MenuItem("Refresh")) {
                RefreshAssets();
            }
            ImGui::EndMenuBar();
        }

        ImGui::Text("File disponibili (Rigs e MicroVoxels):");
        ImGui::Separator();
        
        for (const auto& asset : m_availableAssets) {
            ImGui::PushID(asset.path.c_str());
            
            // Colonna info file
            ImGui::Text("%s", asset.name.c_str());
            ImGui::SameLine(200);
            ImGui::TextDisabled("%s", asset.extension.c_str());
            
            ImGui::SameLine(300);
            // Azioni
            if (asset.extension == ".fwblock") {
                if (player) {
                    if (ImGui::Button("In Tasca (Prefab Voxel)")) {
                        int freeSlot = -1;
                        for (int i = 0; i < 9; i++) {
                            if (player->inventory.slots[i].IsEmpty()) { freeSlot = i; break; }
                        }
                        if (freeSlot != -1) {
                            InventoryItem item; item.count = 1; item.stringId = asset.path;
                            item.type = ItemType::Structure; // Inietta nel terreno
                            player->inventory.slots[freeSlot] = item;
                        }
                    }
                    ImGui::SameLine();
                    
                    if (ImGui::Button("In Tasca (Minivoxel Entity)")) {
                        int freeSlot = -1;
                        for (int i = 0; i < 9; i++) {
                            if (player->inventory.slots[i].IsEmpty()) { freeSlot = i; break; }
                        }
                        if (freeSlot != -1) {
                            InventoryItem item; item.count = 1; item.stringId = asset.path;
                            item.type = ItemType::MiniVoxel; // Crea una entity
                            player->inventory.slots[freeSlot] = item;
                        }
                    }
                    ImGui::SameLine();
                }
            } else if (player) {
                if (ImGui::Button("In Tasca (Play)")) {
                    // Cerca slot vuoto
                    int freeSlot = -1;
                    for (int i = 0; i < 9; i++) {
                        if (player->inventory.slots[i].IsEmpty()) {
                            freeSlot = i;
                            break;
                        }
                    }
                    if (freeSlot != -1) {
                        InventoryItem item;
                        item.count = 1;
                        item.stringId = asset.path;
                        item.type = (asset.extension == ".fwrig") ? ItemType::Structure : ItemType::MiniVoxel;
                        player->inventory.slots[freeSlot] = item;
                        std::cout << "Aggiunto " << asset.name << " allo slot " << freeSlot << "\n";
                    } else {
                        std::cout << "Inventario pieno!\n";
                    }
                }
                ImGui::SameLine();
            }
            
            if (ImGui::Button("Spawn")) {
                m_assetToSpawn = asset.path;
                *isOpen = false;
            }
            ImGui::SameLine();
            
            if (ImGui::Button("Rinomina")) {
                m_fileToRename = asset.path;
                strncpy_s(m_renameBuffer, asset.name.c_str(), sizeof(m_renameBuffer) - 1);
                m_showRenamePopup = true;
            }
            ImGui::SameLine();
            
            if (ImGui::Button("X")) {
                m_fileToDelete = asset.path;
                m_showDeletePopup = true;
            }
            
            ImGui::PopID();
            ImGui::Separator();
        }
        
        // Popup Rinomina
        if (m_showRenamePopup) {
            ImGui::OpenPopup("Rinomina File");
            m_showRenamePopup = false;
        }
        
        if (ImGui::BeginPopupModal("Rinomina File", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Nuovo nome:");
            ImGui::InputText("##newname", m_renameBuffer, sizeof(m_renameBuffer));
            if (ImGui::Button("Conferma", ImVec2(120, 0))) {
                try {
                    std::filesystem::path oldPath(m_fileToRename);
                    std::filesystem::path newPath = oldPath.parent_path() / (std::string(m_renameBuffer) + oldPath.extension().string());
                    std::filesystem::rename(oldPath, newPath);
                    RefreshAssets();
                } catch (const std::exception& e) {
                    std::cout << "Errore rinomina: " << e.what() << "\n";
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Annulla", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        
        // Popup Elimina
        if (m_showDeletePopup) {
            ImGui::OpenPopup("Elimina File");
            m_showDeletePopup = false;
        }
        
        if (ImGui::BeginPopupModal("Elimina File", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Vuoi davvero eliminare questo file?");
            ImGui::TextDisabled("%s", m_fileToDelete.c_str());
            ImGui::Separator();
            
            if (ImGui::Button("Sì, Elimina", ImVec2(120, 0))) {
                try {
                    std::filesystem::remove(m_fileToDelete);
                    RefreshAssets();
                } catch (...) {}
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("No", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}

} // namespace fw
