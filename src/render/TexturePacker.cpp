#include "pch.h"
#include "TexturePacker.h"
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <limits>
#include <array>
#include <cstring>
#include <utility>
#include "blocks/factory/Sha256.h"

// STB_IMAGE_IMPLEMENTATION è già definito in RenderManager.cpp
#include <stb_image.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize.h>

namespace fw {

namespace {
constexpr std::array<char, 8> kCacheMagic = {'F', 'W', 'P', 'B', 'R', 'C', 'H', '2'};
constexpr uint32_t kCacheFormatVersion = 2;
constexpr uint32_t kPackingAlgorithmVersion = 1;
constexpr size_t kDigestBytes = 32;

void UpdateField(Sha256& hash, const std::string& label, const void* data, size_t size) {
    auto updateU64 = [&hash](uint64_t value) {
        uint8_t encoded[8]{};
        for (size_t i = 0; i < 8; ++i) encoded[i] = static_cast<uint8_t>((value >> (i * 8)) & 0xff);
        hash.Update(encoded, sizeof(encoded));
    };
    updateU64(static_cast<uint64_t>(label.size()));
    hash.Update(label.data(), label.size());
    updateU64(static_cast<uint64_t>(size));
    hash.Update(data, size);
}

void UpdateString(Sha256& hash, const std::string& label, const std::string& value) {
    UpdateField(hash, label, value.data(), value.size());
}

void UpdateU32(Sha256& hash, const std::string& label, uint32_t value) {
    const uint8_t encoded[4] = {
        static_cast<uint8_t>(value & 0xff),
        static_cast<uint8_t>((value >> 8) & 0xff),
        static_cast<uint8_t>((value >> 16) & 0xff),
        static_cast<uint8_t>((value >> 24) & 0xff)
    };
    UpdateField(hash, label, encoded, sizeof(encoded));
}

bool HashFile(const std::filesystem::path& path, std::array<uint8_t, 32>& digest) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;

    Sha256 hash;
    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize readCount = input.gcount();
        if (readCount > 0) hash.Update(buffer.data(), static_cast<size_t>(readCount));
    }
    if (!input.eof()) return false;
    digest = hash.Finalize();
    return true;
}

bool WriteU32(std::ostream& output, uint32_t value) {
    const char encoded[4] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff)
    };
    output.write(encoded, sizeof(encoded));
    return output.good();
}

std::filesystem::path ResolveAssetPath(const std::filesystem::path& root, const std::string& path) {
    const std::filesystem::path assetPath(path);
    return assetPath.is_absolute() ? assetPath : (root / assetPath);
}
} // namespace

TexturePacker::TexturePacker(int targetResolution, std::filesystem::path assetRoot)
    : m_targetResolution(targetResolution), m_assetRoot(std::move(assetRoot)) {}

bool TexturePacker::BuildCacheInputDigest(
    const std::vector<PBRMaterialDef>& materials,
    std::array<uint8_t, 32>& outDigest
) const {
    Sha256 hash;
    UpdateString(hash, "cache-domain", "FAIRWORLD-PBR-TEXTURE-CACHE");
    UpdateU32(hash, "format-version", kCacheFormatVersion);
    UpdateU32(hash, "packing-algorithm-version", kPackingAlgorithmVersion);
    UpdateU32(hash, "resolution", static_cast<uint32_t>(m_targetResolution));
    UpdateU32(hash, "channels", 4);
    UpdateU32(hash, "layer-count", static_cast<uint32_t>(materials.size()));

    const std::filesystem::path materialDefinitions = m_assetRoot / "assets/definitions/materials.json";
    std::error_code ec;
    const bool definitionsExist = std::filesystem::exists(materialDefinitions, ec);
    if (ec) return false;
    const bool definitionsAreRegular = definitionsExist && std::filesystem::is_regular_file(materialDefinitions, ec);
    if (ec) return false;
    UpdateString(hash, "materials-json-state", !definitionsExist ? "missing" : (definitionsAreRegular ? "present" : "not-regular-file"));
    UpdateString(hash, "materials-json-path", materialDefinitions.lexically_normal().generic_string());
    if (definitionsAreRegular) {
        std::array<uint8_t, 32> definitionsDigest{};
        if (!HashFile(materialDefinitions, definitionsDigest)) return false;
        UpdateField(hash, "materials-json-sha256", definitionsDigest.data(), definitionsDigest.size());
    }

    auto addTextureDependency = [&](const std::string& fieldName, const std::string& texturePath) -> bool {
        UpdateString(hash, fieldName + "-path", texturePath);
        if (texturePath.empty()) {
            UpdateString(hash, fieldName + "-state", "unused");
            return true;
        }

        const std::filesystem::path resolvedPath = ResolveAssetPath(m_assetRoot, texturePath);
        std::error_code pathError;
        const bool exists = std::filesystem::exists(resolvedPath, pathError);
        if (pathError) return false;
        if (!exists) {
            UpdateString(hash, fieldName + "-state", "missing");
            return true;
        }
        const bool isRegularFile = std::filesystem::is_regular_file(resolvedPath, pathError);
        if (pathError) return false;
        if (!isRegularFile) {
            UpdateString(hash, fieldName + "-state", "not-regular-file");
            return true;
        }
        UpdateString(hash, fieldName + "-state", "present");

        std::array<uint8_t, 32> textureDigest{};
        if (!HashFile(resolvedPath, textureDigest)) return false;
        UpdateField(hash, fieldName + "-sha256", textureDigest.data(), textureDigest.size());
        return true;
    };

    for (size_t index = 0; index < materials.size(); ++index) {
        const PBRMaterialDef& material = materials[index];
        const std::string prefix = "material-" + std::to_string(index) + "-";
        UpdateU32(hash, prefix + "target-block-id", material.target_block_id);
        const uint8_t fallback[4] = {
            static_cast<uint8_t>(material.baseColorFallback.x * 255.0f),
            static_cast<uint8_t>(material.baseColorFallback.y * 255.0f),
            static_cast<uint8_t>(material.baseColorFallback.z * 255.0f),
            255
        };
        UpdateField(hash, prefix + "albedo-fallback-rgba", fallback, sizeof(fallback));
        if (!addTextureDependency(prefix + "albedo", material.albedoPath) ||
            !addTextureDependency(prefix + "normal", material.normalPath) ||
            !addTextureDependency(prefix + "orm", material.ormPath)) {
            return false;
        }
    }

    outDigest = hash.Finalize();
    return true;
}

PackedTextureData TexturePacker::PackMaterials(const std::vector<PBRMaterialDef>& materials, const std::atomic<bool>* cancelRequested) {
    PackedTextureData result;
    if (m_targetResolution <= 0 || materials.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        std::cerr << "[TexturePacker] Risoluzione o numero di layer non valido; packing annullato.\n";
        return result;
    }
    result.layerCount = static_cast<int>(materials.size());
    result.width = m_targetResolution;
    result.height = m_targetResolution;
    result.channels = 4;

    auto checkCancel = [&](const char* phase) -> bool {
        if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) {
            result.isCancelled = true;
            result.cancellationPhase = phase;
            result.albedoData.clear();
            result.albedoData.shrink_to_fit();
            result.normalData.clear();
            result.normalData.shrink_to_fit();
            result.ormData.clear();
            result.ormData.shrink_to_fit();
            result.layerCount = 0;
            result.totalBytesPerArray = 0;
            std::cout << "[TexturePacker] [Cooperative Cancel] Cancellazione intercettata nella fase: "
                      << phase << ". Rilascio immediato risorse parziali.\n";
            return true;
        }
        return false;
    };

    if (checkCancel("Fase 1: Pre-Cache / Check iniziale")) {
        return result;
    }
    
    const std::filesystem::path cacheFile = m_assetRoot / "assets/texture_cache.bin";
    std::array<uint8_t, 32> inputDigest{};
    const bool fingerprintAvailable = m_targetResolution > 0 &&
        materials.size() <= std::numeric_limits<uint32_t>::max() &&
        BuildCacheInputDigest(materials, inputDigest);
    const uint64_t resolution64 = static_cast<uint64_t>(m_targetResolution);
    const uint64_t layerBytes64 = resolution64 * resolution64 * 4ULL;
    const bool payloadMultiplicationValid = materials.empty() ||
        layerBytes64 <= std::numeric_limits<uint64_t>::max() / static_cast<uint64_t>(materials.size());
    const uint64_t payloadBytes64 = payloadMultiplicationValid ? layerBytes64 * static_cast<uint64_t>(materials.size()) : 0;
    constexpr uint64_t headerBytes = 8 + 5 * 4 + 2 * kDigestBytes;
    const bool sizesValid = payloadMultiplicationValid &&
        layerBytes64 <= std::numeric_limits<size_t>::max() &&
        payloadBytes64 <= std::numeric_limits<size_t>::max() &&
        payloadBytes64 <= (std::numeric_limits<uint64_t>::max() - headerBytes) / 3;
    const size_t layerBytes = sizesValid ? static_cast<size_t>(layerBytes64) : 0;
    result.totalBytesPerArray = sizesValid ? static_cast<size_t>(payloadBytes64) : 0;

    if (!sizesValid) {
        std::cerr << "[TexturePacker] Risoluzione o dimensione cache non valida; packing annullato.\n";
        result.layerCount = 0;
        return result;
    }

    if (fingerprintAvailable) {
        std::ifstream cache(cacheFile, std::ios::binary | std::ios::ate);
        const uint64_t expectedFileBytes = headerBytes + payloadBytes64 * 3ULL;
        if (cache && cache.tellg() >= 0 && static_cast<uint64_t>(cache.tellg()) == expectedFileBytes) {
            cache.seekg(0, std::ios::beg);
            std::array<char, 8> magic{};
            cache.read(magic.data(), static_cast<std::streamsize>(magic.size()));
            uint32_t version = 0, cachedRes = 0, cachedLayers = 0, cachedChannels = 0, algorithmVersion = 0;
            std::array<uint8_t, 32> cachedInputDigest{}, cachedPayloadDigest{};
            cache.read(reinterpret_cast<char*>(&version), sizeof(version));
            cache.read(reinterpret_cast<char*>(&cachedRes), sizeof(cachedRes));
            cache.read(reinterpret_cast<char*>(&cachedLayers), sizeof(cachedLayers));
            cache.read(reinterpret_cast<char*>(&cachedChannels), sizeof(cachedChannels));
            cache.read(reinterpret_cast<char*>(&algorithmVersion), sizeof(algorithmVersion));
            cache.read(reinterpret_cast<char*>(cachedInputDigest.data()), cachedInputDigest.size());
            cache.read(reinterpret_cast<char*>(cachedPayloadDigest.data()), cachedPayloadDigest.size());

            // Header integers are stored little-endian. Decode explicitly for cross-toolchain stability.
            auto decodeLe32 = [](uint32_t raw) {
                const uint8_t* b = reinterpret_cast<const uint8_t*>(&raw);
                return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) |
                    (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
            };
            version = decodeLe32(version);
            cachedRes = decodeLe32(cachedRes);
            cachedLayers = decodeLe32(cachedLayers);
            cachedChannels = decodeLe32(cachedChannels);
            algorithmVersion = decodeLe32(algorithmVersion);

            const bool headerValid = cache.good() && magic == kCacheMagic &&
                version == kCacheFormatVersion && cachedRes == static_cast<uint32_t>(m_targetResolution) &&
                cachedLayers == static_cast<uint32_t>(materials.size()) && cachedChannels == 4 &&
                algorithmVersion == kPackingAlgorithmVersion && cachedInputDigest == inputDigest;
            if (headerValid) {
                result.albedoData.resize(result.totalBytesPerArray);
                result.normalData.resize(result.totalBytesPerArray);
                result.ormData.resize(result.totalBytesPerArray);
                cache.read(reinterpret_cast<char*>(result.albedoData.data()), static_cast<std::streamsize>(result.totalBytesPerArray));
                cache.read(reinterpret_cast<char*>(result.normalData.data()), static_cast<std::streamsize>(result.totalBytesPerArray));
                cache.read(reinterpret_cast<char*>(result.ormData.data()), static_cast<std::streamsize>(result.totalBytesPerArray));
                if (cache.good()) {
                    Sha256 payloadHash;
                    payloadHash.Update(result.albedoData.data(), result.albedoData.size());
                    payloadHash.Update(result.normalData.data(), result.normalData.size());
                    payloadHash.Update(result.ormData.data(), result.ormData.size());
                    if (payloadHash.Finalize() == cachedPayloadDigest) {
                        std::cout << "[TexturePacker] Cache PBR valida (versione " << kCacheFormatVersion << ").\n";
                        return result;
                    }
                }
                result.albedoData.clear();
                result.normalData.clear();
                result.ormData.clear();
            }
        }
        std::cout << "[TexturePacker] Cache PBR assente, incompatibile o non valida; rigenerazione.\n";
    }
    
    if (checkCancel("Fase 3A: Pre-allocazione per packing da zero")) {
        return result;
    }

    if (result.layerCount == 0) return result;

    result.albedoData.reserve(result.totalBytesPerArray);
    result.normalData.reserve(result.totalBytesPerArray);
    result.ormData.reserve(result.totalBytesPerArray);
    
    // Fallback statici per Albedo (Magenta per layer mancanti), Normal e ORM
    std::vector<uint8_t> defaultAlbedo = { 255, 0, 255, 255 };    // Magenta for missing
    std::vector<uint8_t> fallbackNormal = { 128, 128, 255, 255 }; // Flat normal
    std::vector<uint8_t> fallbackOrm = { 255, 128, 0, 255 };      // AO=255, Rough=128, Metal=0

    for (const auto& mat : materials) {
        if (checkCancel("Fase 3B: Iterazione materiali (packing da zero)")) {
            return result;
        }

        if (!mat.albedoPath.empty() || !mat.normalPath.empty() || !mat.ormPath.empty()) {
            std::cout << "[TexturePacker] Imballaggio avvenuto per materiale [" << (int)mat.target_block_id << "]\n";
        }
        
        // Calcolo Albedo Fallback dinamico dal materiale
        uint8_t r = static_cast<uint8_t>(mat.baseColorFallback.x * 255.0f);
        uint8_t g = static_cast<uint8_t>(mat.baseColorFallback.y * 255.0f);
        uint8_t b = static_cast<uint8_t>(mat.baseColorFallback.z * 255.0f);
        std::vector<uint8_t> fallbackAlbedo = { r, g, b, 255 };

        LoadAndAppendTexture(mat.albedoPath.empty() ? std::string() : ResolveAssetPath(m_assetRoot, mat.albedoPath).string(), result.albedoData, fallbackAlbedo);
        LoadAndAppendTexture(mat.normalPath.empty() ? std::string() : ResolveAssetPath(m_assetRoot, mat.normalPath).string(), result.normalData, fallbackNormal);
        LoadAndAppendTexture(mat.ormPath.empty() ? std::string() : ResolveAssetPath(m_assetRoot, mat.ormPath).string(), result.ormData, fallbackOrm);
    }

    if (checkCancel("Fase 3C: Pre-scrittura cache binaria")) {
        return result;
    }
    
    // Persist only if inputs remained stable during packing; old-format cache is replaced by v2.
    std::array<uint8_t, 32> finalInputDigest{};
    if (fingerprintAvailable && BuildCacheInputDigest(materials, finalInputDigest) && finalInputDigest == inputDigest) {
        Sha256 payloadHash;
        payloadHash.Update(result.albedoData.data(), result.albedoData.size());
        payloadHash.Update(result.normalData.data(), result.normalData.size());
        payloadHash.Update(result.ormData.data(), result.ormData.size());
        const std::array<uint8_t, 32> payloadDigest = payloadHash.Finalize();
        std::error_code directoryError;
        std::filesystem::create_directories(cacheFile.parent_path(), directoryError);
        std::ofstream cache(cacheFile, std::ios::binary | std::ios::trunc);
        if (cache) {
            cache.write(kCacheMagic.data(), static_cast<std::streamsize>(kCacheMagic.size()));
            const bool headerWritten = WriteU32(cache, kCacheFormatVersion) &&
                WriteU32(cache, static_cast<uint32_t>(m_targetResolution)) &&
                WriteU32(cache, static_cast<uint32_t>(result.layerCount)) &&
                WriteU32(cache, 4) && WriteU32(cache, kPackingAlgorithmVersion);
            if (headerWritten) {
                cache.write(reinterpret_cast<const char*>(inputDigest.data()), static_cast<std::streamsize>(inputDigest.size()));
                cache.write(reinterpret_cast<const char*>(payloadDigest.data()), static_cast<std::streamsize>(payloadDigest.size()));
                cache.write(reinterpret_cast<const char*>(result.albedoData.data()), static_cast<std::streamsize>(result.albedoData.size()));
                cache.write(reinterpret_cast<const char*>(result.normalData.data()), static_cast<std::streamsize>(result.normalData.size()));
                cache.write(reinterpret_cast<const char*>(result.ormData.data()), static_cast<std::streamsize>(result.ormData.size()));
            }
            if (cache.good()) std::cout << "[TexturePacker] Cache PBR v2 salvata.\n";
            else std::cerr << "[TexturePacker] Scrittura cache PBR incompleta.\n";
        }
    } else if (fingerprintAvailable) {
        std::cerr << "[TexturePacker] Input modificati durante il packing; cache non salvata.\n";
    } else {
        std::cerr << "[TexturePacker] Fingerprint input non disponibile; cache non salvata.\n";
    }
    
    // Pad array to 256 layers to avoid out-of-bounds reads during memcpy in RenderManager
    size_t requiredSize = result.totalBytesPerArray;
    if (result.albedoData.size() < requiredSize) {
        while (result.albedoData.size() < requiredSize) {
            if (checkCancel("Fase 3D: Padding Albedo")) return result;
            LoadAndAppendTexture("", result.albedoData, defaultAlbedo);
        }
    }
    if (result.normalData.size() < requiredSize) {
        while (result.normalData.size() < requiredSize) {
            if (checkCancel("Fase 3E: Padding Normal")) return result;
            LoadAndAppendTexture("", result.normalData, fallbackNormal);
        }
    }
    if (result.ormData.size() < requiredSize) {
        while (result.ormData.size() < requiredSize) {
            if (checkCancel("Fase 3F: Padding ORM")) return result;
            LoadAndAppendTexture("", result.ormData, fallbackOrm);
        }
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
