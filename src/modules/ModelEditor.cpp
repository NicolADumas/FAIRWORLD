#include "pch.h"
#include "ModelEditor.h"
#include <imgui.h>
#include <fstream>
#include <iostream>
#include "json.hpp"

using json = nlohmann::json;

ModelEditor::ModelEditor() {
    Clear();
}

void ModelEditor::Clear() {
    for (int x = 0; x < GRID_SIZE; x++) {
        for (int y = 0; y < GRID_SIZE; y++) {
            for (int z = 0; z < GRID_SIZE; z++) {
                grid[x][y][z] = {0, 0, 0, 0};
            }
        }
    }
}

void ModelEditor::Draw() {
    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Scultore Voxel In-Game (16x16x16)");
    ImGui::Separator();
    
    ImGui::ColorEdit4("Colore Pennello", m_brushColor);
    
    ImGui::SliderInt("Layer Z (Profondità)", &m_currentLayer, 0, GRID_SIZE - 1);
    
    ImGui::BeginChild("VoxelGrid", ImVec2(320, 320), true);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float sz = 20.0f; // cell size
    
    for (int y = 0; y < GRID_SIZE; y++) {
        for (int x = 0; x < GRID_SIZE; x++) {
            ImVec2 pmin = ImVec2(p.x + x * sz, p.y + y * sz);
            ImVec2 pmax = ImVec2(pmin.x + sz, pmin.y + sz);
            
            // Renderizza il layer sottostante come semi-trasparente per dare senso di profondità
            if (m_currentLayer > 0) {
                EditorVoxel below = grid[x][y][m_currentLayer - 1];
                if (below.a > 0) {
                    draw_list->AddRectFilled(pmin, pmax, IM_COL32(below.r, below.g, below.b, 80));
                }
            }
            
            EditorVoxel v = grid[x][y][m_currentLayer];
            if (v.a > 0) {
                draw_list->AddRectFilled(pmin, pmax, IM_COL32(v.r, v.g, v.b, v.a));
            }
            
            draw_list->AddRect(pmin, pmax, IM_COL32(100, 100, 100, 50));
            
            if (ImGui::IsMouseHoveringRect(pmin, pmax)) {
                if (ImGui::IsMouseDown(0)) { // Paint
                    grid[x][y][m_currentLayer] = { 
                        (uint8_t)(m_brushColor[0] * 255), 
                        (uint8_t)(m_brushColor[1] * 255), 
                        (uint8_t)(m_brushColor[2] * 255), 
                        (uint8_t)(m_brushColor[3] * 255) 
                    };
                }
                if (ImGui::IsMouseDown(1)) { // Erase
                    grid[x][y][m_currentLayer] = {0, 0, 0, 0};
                }
            }
        }
    }
    ImGui::EndChild();
    
    ImGui::SameLine();
    ImGui::BeginChild("ModelTools", ImVec2(0, 320), false);
    if (ImGui::Button("Svuota Modello", ImVec2(150, 30))) {
        Clear();
    }
    
    static char filename[64] = "assets/models/mob.vox";
    ImGui::InputText("Salvataggio", filename, IM_ARRAYSIZE(filename));
    
    if (ImGui::Button("Salva .vox", ImVec2(150, 30))) {
        SaveModel(filename);
    }
    if (ImGui::Button("Carica .vox", ImVec2(150, 30))) {
        LoadModel(filename);
    }
    ImGui::EndChild();
}

bool ModelEditor::SaveModel(const std::string& filepath) {
    json j;
    j["size"] = GRID_SIZE;
    j["voxels"] = json::array();
    
    for (int x = 0; x < GRID_SIZE; x++) {
        for (int y = 0; y < GRID_SIZE; y++) {
            for (int z = 0; z < GRID_SIZE; z++) {
                EditorVoxel v = grid[x][y][z];
                if (v.a > 0) {
                    j["voxels"].push_back({
                        {"x", x}, {"y", y}, {"z", z},
                        {"r", v.r}, {"g", v.g}, {"b", v.b}, {"a", v.a}
                    });
                }
            }
        }
    }
    
    std::ofstream file(filepath);
    if (file.is_open()) {
        file << j.dump(4);
        file.close();
        std::cout << "[EDITOR] Modello salvato in " << filepath << std::endl;
        return true;
    }
    return false;
}

bool ModelEditor::LoadModel(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;
    
    json j;
    file >> j;
    file.close();
    
    Clear();
    if (j.contains("voxels")) {
        for (auto& item : j["voxels"]) {
            int x = item["x"];
            int y = item["y"];
            int z = item["z"];
            if (x >= 0 && x < GRID_SIZE && y >= 0 && y < GRID_SIZE && z >= 0 && z < GRID_SIZE) {
                grid[x][y][z] = {
                    item["r"], item["g"], item["b"], item["a"]
                };
            }
        }
    }
    std::cout << "[EDITOR] Modello caricato da " << filepath << std::endl;
    return true;
}
