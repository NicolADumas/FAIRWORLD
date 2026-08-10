#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "MaterialRegistry.h" // Per PBRMaterialDef

namespace fw {

struct PackedTextureData {
    std::vector<uint8_t> albedoData;
    std::vector<uint8_t> normalData;
    std::vector<uint8_t> ormData;
    
    int layerCount = 0;
    int width = 512;
    int height = 512;
    int channels = 4; // Costretto a RGBA
    size_t totalBytesPerArray = 0;
};

class TexturePacker {
public:
    TexturePacker(int targetResolution = 512);
    
    // Itererà sull'array dei materiali registrati e impacchetterà tutto
    PackedTextureData PackMaterials(const std::vector<PBRMaterialDef>& materials);

private:
    int m_targetResolution;
    
    // Helper per caricare, ridimensionare (se serve) e concatenare i pixel in coda al destBuffer
    void LoadAndAppendTexture(const std::string& path, std::vector<uint8_t>& destBuffer, const std::vector<uint8_t>& fallbackColorRGBA);
};

} // namespace fw
