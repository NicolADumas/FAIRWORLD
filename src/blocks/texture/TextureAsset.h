#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include "ImageBitmap.h"

namespace fw {

// =============================================================================
// BM-TX2.3 — TEXTURE ASSET: CONTRATTO ASSET CPU HEADLESS
// =============================================================================

struct TextureImportMetadata {
    std::string originalFilename;
    std::string originalExtension;
    uint32_t originalWidth = 0;
    uint32_t originalHeight = 0;
    uint32_t originalChannels = 0;
    uint64_t fileSizeBytes = 0;
    uint64_t importTimestamp = 0;
    TextureColorSpace detectedColorSpace = TextureColorSpace::sRGB;
    std::string author = "LocalAuthor";
};

struct TextureValidationReport {
    bool isValid = false;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    void Clear() {
        isValid = true;
        errors.clear();
        warnings.clear();
    }

    void AddError(const std::string& err) {
        errors.push_back(err);
        isValid = false;
    }

    void AddWarning(const std::string& warn) {
        warnings.push_back(warn);
    }
};

class TextureAsset {
public:
    TextureAsset();
    explicit TextureAsset(const std::string& assetId, const std::string& sourcePath = "");

    // Identità stabile
    const std::string& GetAssetId() const { return m_assetId; }
    void SetAssetId(const std::string& id) { m_assetId = id; MarkDirty(); }

    const std::string& GetSourcePath() const { return m_sourcePath; }
    void SetSourcePath(const std::string& path) { m_sourcePath = path; MarkDirty(); }

    // Dati Immagine Decodificati
    const ImageBitmap& GetImage() const { return m_image; }
    ImageBitmap& GetImageMutable() { MarkDirty(); return m_image; }
    void SetImage(const ImageBitmap& image);
    void SetImage(ImageBitmap&& image);

    // Metadati e Provenienza
    const TextureImportMetadata& GetMetadata() const { return m_metadata; }
    TextureImportMetadata& GetMetadataMutable() { MarkDirty(); return m_metadata; }
    void SetMetadata(const TextureImportMetadata& meta) { m_metadata = meta; MarkDirty(); }

    // Revisione e Fingerprint
    uint64_t GetRevision() const { return m_assetRevision; }
    uint64_t GetContentFingerprint() const;

    // Validazione
    const TextureValidationReport& GetValidationReport() const { return m_validation; }
    bool Validate(bool requirePowerOfTwo = false, uint32_t maxDimension = 4096);

    // Caricamento / Importazione
    bool ImportFromFile(const std::string& filepath, 
                        TextureColorSpace colorSpace = TextureColorSpace::sRGB,
                        uint32_t maxDimension = 4096);

    bool ImportFromMemory(const std::string& assetId,
                          const uint8_t* data, size_t sizeBytes,
                          TextureColorSpace colorSpace = TextureColorSpace::sRGB,
                          uint32_t maxDimension = 4096);

    void MarkDirty();

private:
    std::string m_assetId;
    std::string m_sourcePath;
    ImageBitmap m_image;
    TextureImportMetadata m_metadata;
    TextureValidationReport m_validation;

    uint64_t m_assetRevision = 1;
    mutable uint64_t m_cachedFingerprint = 0;
    mutable bool m_fingerprintDirty = true;
};

} // namespace fw
