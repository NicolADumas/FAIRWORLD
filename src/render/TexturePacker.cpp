#include "pch.h"
#include "TexturePacker.h"
#include <iostream>
#include <stdexcept>

// STB_IMAGE_IMPLEMENTATION è già definito in RenderManager.cpp
#include <stb_image.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize.h>

namespace fw {

TexturePacker::TexturePacker(int targetResolution) : m_targetResolution(targetResolution) {}

PackedTextureData TexturePacker::PackMaterials(const std::vector<PBRMaterialDef>& materials) {
    PackedTextureData result;
    result.layerCount = (int)materials.size();
    result.width = m_targetResolution;
    result.height = m_targetResolution;
    result.channels = 4;
    
    size_t layerBytes = m_targetResolution * m_targetResolution * 4;
    result.totalBytesPerArray = layerBytes * materials.size();
    
    if (result.layerCount == 0) return result;

    result.albedoData.reserve(result.totalBytesPerArray);
    result.normalData.reserve(result.totalBytesPerArray);
    result.ormData.reserve(result.totalBytesPerArray);
    
    // Fallback statici per Normal e ORM
    std::vector<uint8_t> fallbackNormal = { 128, 128, 255, 255 }; // Flat normal
    std::vector<uint8_t> fallbackOrm = { 255, 128, 0, 255 };      // AO=255, Rough=128, Metal=0

    for (const auto& mat : materials) {
        std::cout << "[TexturePacker] Imballaggio ID " << (int)mat.target_block_id << "\n";
        
        // Calcolo Albedo Fallback dinamico dal materiale
        uint8_t r = static_cast<uint8_t>(mat.baseColorFallback.x * 255.0f);
        uint8_t g = static_cast<uint8_t>(mat.baseColorFallback.y * 255.0f);
        uint8_t b = static_cast<uint8_t>(mat.baseColorFallback.z * 255.0f);
        std::vector<uint8_t> fallbackAlbedo = { r, g, b, 255 };

        LoadAndAppendTexture(mat.albedoPath, result.albedoData, fallbackAlbedo);
        LoadAndAppendTexture(mat.normalPath, result.normalData, fallbackNormal);
        LoadAndAppendTexture(mat.ormPath, result.ormData, fallbackOrm);
    }
    
    std::cout << "==========================================\n";
    std::cout << "[TexturePacker] ASSEMBLAGGIO COMPLETATO\n";
    std::cout << "[TexturePacker] Layer processati: " << result.layerCount << "\n";
    float mb = (float)(result.totalBytesPerArray * 3) / (1024.0f * 1024.0f);
    std::cout << "[TexturePacker] RAM Totale Allocata: " << mb << " MB\n";
    std::cout << "==========================================\n";
              
    return result;
}

void TexturePacker::LoadAndAppendTexture(const std::string& path, std::vector<uint8_t>& destBuffer, const std::vector<uint8_t>& fallbackColorRGBA) {
    int w = 0, h = 0, channels = 0;
    uint8_t* pixels = nullptr;
    
    if (!path.empty()) {
        pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    }
    
    size_t layerBytes = m_targetResolution * m_targetResolution * 4;

    if (pixels) {
        if (w == m_targetResolution && h == m_targetResolution) {
            // Risoluzione esatta: copia veloce
            destBuffer.insert(destBuffer.end(), pixels, pixels + layerBytes);
        } else {
            // Resize necessario in RAM prima del caricamento
            std::vector<uint8_t> resizedPixels(layerBytes);
            stbir_resize_uint8(pixels, w, h, 0, 
                               resizedPixels.data(), m_targetResolution, m_targetResolution, 0, 4);
            destBuffer.insert(destBuffer.end(), resizedPixels.begin(), resizedPixels.end());
        }
        stbi_image_free(pixels);
    } else {
        if (!path.empty()) {
            std::cerr << "  [Warning] Mappa non trovata (" << path << "). Iniezione fallback color.\n";
        }
        // Iniezione fallback (Ottimizzata per non bloccarsi in Debug)
        size_t startIdx = destBuffer.size();
        destBuffer.resize(startIdx + layerBytes);
        uint8_t* ptr = destBuffer.data() + startIdx;
        uint8_t r = fallbackColorRGBA[0], g = fallbackColorRGBA[1], b = fallbackColorRGBA[2], a = fallbackColorRGBA[3];
        for (size_t i = 0; i < m_targetResolution * m_targetResolution; ++i) {
            ptr[i*4 + 0] = r;
            ptr[i*4 + 1] = g;
            ptr[i*4 + 2] = b;
            ptr[i*4 + 3] = a;
        }
    }
}

} // namespace fw
