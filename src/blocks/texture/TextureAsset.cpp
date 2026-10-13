#include "pch.h"
#include "TextureAsset.h"
#include <chrono>
#include <filesystem>
#include <algorithm>

namespace fw {

// =============================================================================
// BM-TX2.3: TEXTURE ASSET IMPLEMENTATION
// =============================================================================

TextureAsset::TextureAsset()
    : m_assetId("unnamed_texture"), m_assetRevision(1), m_fingerprintDirty(true) {
}

TextureAsset::TextureAsset(const std::string& assetId, const std::string& sourcePath)
    : m_assetId(assetId), m_sourcePath(sourcePath), m_assetRevision(1), m_fingerprintDirty(true) {
}

void TextureAsset::SetImage(const ImageBitmap& image) {
    m_image = image;
    m_metadata.originalWidth = image.GetWidth();
    m_metadata.originalHeight = image.GetHeight();
    m_metadata.detectedColorSpace = image.GetColorSpace();
    MarkDirty();
}

void TextureAsset::SetImage(ImageBitmap&& image) {
    m_metadata.originalWidth = image.GetWidth();
    m_metadata.originalHeight = image.GetHeight();
    m_metadata.detectedColorSpace = image.GetColorSpace();
    m_image = std::move(image);
    MarkDirty();
}

void TextureAsset::MarkDirty() {
    m_assetRevision++;
    m_fingerprintDirty = true;
}

uint64_t TextureAsset::GetContentFingerprint() const {
    if (!m_fingerprintDirty) {
        return m_cachedFingerprint;
    }

    // FNV-1a hash combinato di assetId + metadati + content hash dell'immagine
    uint64_t hash = 14695981039346656037ULL;
    auto addBytes = [&hash](const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            hash ^= data[i];
            hash *= 1099511628211ULL;
        }
    };

    addBytes(reinterpret_cast<const uint8_t*>(m_assetId.data()), m_assetId.size());
    addBytes(reinterpret_cast<const uint8_t*>(m_sourcePath.data()), m_sourcePath.size());
    
    uint64_t imgHash = m_image.ComputeContentHash();
    addBytes(reinterpret_cast<const uint8_t*>(&imgHash), sizeof(imgHash));

    m_cachedFingerprint = hash;
    m_fingerprintDirty = false;
    return m_cachedFingerprint;
}

static bool IsPowerOfTwo(uint32_t x) {
    return x > 0 && (x & (x - 1)) == 0;
}

bool TextureAsset::Validate(bool requirePowerOfTwo, uint32_t maxDimension) {
    m_validation.Clear();

    if (m_assetId.empty()) {
        m_validation.AddError("Asset ID della texture vuoto.");
    }

    if (!m_image.IsValid()) {
        m_validation.AddError("Immagine bitmap non valida o priva di pixel.");
        return false;
    }

    uint32_t w = m_image.GetWidth();
    uint32_t h = m_image.GetHeight();

    if (!ImageBitmap::ValidateDimensions(w, h, maxDimension)) {
        m_validation.AddError("Dimensioni immagine non valide (" + std::to_string(w) + "x" + 
                              std::to_string(h) + "), limite max: " + std::to_string(maxDimension));
    }

    if (requirePowerOfTwo) {
        if (!IsPowerOfTwo(w) || !IsPowerOfTwo(h)) {
            m_validation.AddWarning("Dimensioni non potenze di due (" + std::to_string(w) + "x" + 
                                    std::to_string(h) + "). Potrebbero degradare il campionamento mipmap.");
        }
    }

    return m_validation.isValid;
}

bool TextureAsset::ImportFromFile(const std::string& filepath, 
                                  TextureColorSpace colorSpace,
                                  uint32_t maxDimension) {
    m_sourcePath = filepath;

    std::filesystem::path p(filepath);
    m_metadata.originalFilename = p.filename().string();
    m_metadata.originalExtension = p.extension().string();

    std::error_code ec;
    auto fSize = std::filesystem::file_size(p, ec);
    if (!ec) {
        m_metadata.fileSizeBytes = fSize;
    }

    auto now = std::chrono::system_clock::now();
    m_metadata.importTimestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count()
    );

    if (!m_image.LoadFromFile(filepath, colorSpace)) {
        m_validation.Clear();
        m_validation.AddError("Impossibile caricare o decodificare il file immagine: " + filepath);
        MarkDirty();
        return false;
    }

    m_metadata.originalWidth = m_image.GetWidth();
    m_metadata.originalHeight = m_image.GetHeight();
    m_metadata.originalChannels = m_image.GetBytesPerPixel();
    m_metadata.detectedColorSpace = colorSpace;

    Validate(false, maxDimension);
    MarkDirty();
    return m_validation.isValid;
}

bool TextureAsset::ImportFromMemory(const std::string& assetId,
                                    const uint8_t* data, size_t sizeBytes,
                                    TextureColorSpace colorSpace,
                                    uint32_t maxDimension) {
    m_assetId = assetId;
    m_sourcePath = "memory://" + assetId;

    m_metadata.originalFilename = assetId;
    m_metadata.originalExtension = ".png";
    m_metadata.fileSizeBytes = sizeBytes;

    auto now = std::chrono::system_clock::now();
    m_metadata.importTimestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count()
    );

    if (!m_image.DecodeFromMemory(data, sizeBytes, colorSpace)) {
        m_validation.Clear();
        m_validation.AddError("Decodifica memoria immagine fallita per asset: " + assetId);
        MarkDirty();
        return false;
    }

    m_metadata.originalWidth = m_image.GetWidth();
    m_metadata.originalHeight = m_image.GetHeight();
    m_metadata.originalChannels = m_image.GetBytesPerPixel();
    m_metadata.detectedColorSpace = colorSpace;

    Validate(false, maxDimension);
    MarkDirty();
    return m_validation.isValid;
}

} // namespace fw
