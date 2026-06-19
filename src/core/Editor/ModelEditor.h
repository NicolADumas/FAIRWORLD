#pragma once
#include <vector>
#include <string>
#include <cstdint>

// Struttura voxel per l'Editor (Colori in formato RGBA, a=0 significa vuoto)
struct EditorVoxel {
    uint8_t r, g, b, a;
};

// Classe che fa parte del modulo logico 'Editor'
class ModelEditor {
public:
    ModelEditor();
    
    // Disegna l'interfaccia ImGui per l'editor di modelli
    void Draw(); 
    
    static const int GRID_SIZE = 16;
    EditorVoxel grid[GRID_SIZE][GRID_SIZE][GRID_SIZE];
    
    void Clear();
    bool SaveModel(const std::string& filepath);
    bool LoadModel(const std::string& filepath);

private:
    int m_currentLayer = 0; // Layer Z corrente o Y (dipende dall'asse di proiezione)
    float m_brushColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f }; // RGBA
};
