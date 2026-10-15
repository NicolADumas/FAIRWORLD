#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <chrono>
#include "blocks/texture/ImageBitmap.h"
#include "RawAssetContract.h"

namespace fw {

class PainterStationAdapter;

// =============================================================================
// BM-F2B: TELEMETRIA ZERO-WORK BAKING
// =============================================================================
struct TextureBakingTelemetry {
    uint32_t compositingCount = 0;
    uint32_t encodingCount = 0;
    uint32_t diskWriteCount = 0;
    uint32_t hashScanCount = 0;
    uint32_t cacheHitsNoCompositing = 0;
    uint32_t cacheHitsNoEncoding = 0;
    uint32_t cacheHitsNoDiskWrite = 0;

    void Reset() {
        compositingCount = 0;
        encodingCount = 0;
        diskWriteCount = 0;
        hashScanCount = 0;
        cacheHitsNoCompositing = 0;
        cacheHitsNoEncoding = 0;
        cacheHitsNoDiskWrite = 0;
    }
};

// =============================================================================
// BM-F2B: ARTEFATTO TEXTURE PERSISTENTE E METADATI
// =============================================================================
struct BakedTextureArtifact {
    std::string relativeUri;        // Percorso relativo e portabile (es: "textures/blocks/my_block_albedo.png")
    std::string physicalPath;       // Percorso fisico risolto sul filesystem host
    uint64_t contentHash = 0;       // Hash deterministico FNV-1a 64-bit del contenuto dei pixel
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 4;
    TexturePixelFormat format = TexturePixelFormat::RGBA8;
    TextureColorSpace colorSpace = TextureColorSpace::sRGB;
    uint64_t fileSizeBytes = 0;
    uint32_t schemaVersion = 1;
    uint64_t bakedRevision = 0;
    uint64_t timestamp = 0;
    bool isValid = false;
};

// =============================================================================
// BM-F2B: CODICI DI ERRORE DIAGNOSTICI BAKING
// =============================================================================
enum class BakingErrorCode : uint8_t {
    None = 0,
    InvalidSourceImage,
    InvalidDimensions,
    EncodingFailed,
    DirectoryCreationFailed,
    TempFileWriteFailed,
    AtomicPublishFailed,
    FileNotFoundAfterBake,
    ReadbackVerificationFailed,
    InvalidRelativePath,
    SecurityPathEscapeAttempt,
    UnknownError
};

struct TextureBakeResult {
    bool success = false;
    BakingErrorCode errorCode = BakingErrorCode::None;
    std::string errorMessage;
    BakedTextureArtifact artifact;
    bool wasCacheHitNoEncoding = false;
    bool wasCacheHitNoDiskWrite = false;
};

// =============================================================================
// BM-F2B: TEXTURE BAKING SERVICE
// Servizio atomico e zero-work per l'esportazione di CompositeAlbedo in PNG
// =============================================================================
class TextureBakingService {
public:
    TextureBakingService();
    ~TextureBakingService() = default;

    // Configurazione cartella di esportazione base (default: "assets")
    void SetBaseDirectory(const std::string& baseDir);
    const std::string& GetBaseDirectory() const { return m_baseDir; }

    // Generazione del percorso relativo canonico e portabile
    static std::string GenerateCanonicalRelativeUri(const std::string& assetId, const std::string& mapType = "albedo");

    // Validazione portabilità: vieta percorsi assoluti, lettere di drive (C:), backslash o escape traversal
    static bool ValidatePortableUri(const std::string& uri, std::string* outError = nullptr);

    // Baking di una ImageBitmap generica (es. CompositeAlbedo estratto)
    TextureBakeResult BakeAlbedoAtlas(
        const ImageBitmap& compositeAlbedo,
        const std::string& assetId,
        uint64_t revision = 1,
        bool forceRewrite = false
    );

    // Integrazione diretta con RawAssetDraft e PainterStationAdapter
    TextureBakeResult BakeDraftAlbedo(
        RawAssetDraft& draft,
        const PainterStationAdapter& painterStation,
        bool forceRewrite = false
    );

    // Verifica round-trip decodifica PNG
    static bool VerifyArtifactRoundTrip(
        const std::string& physicalPath,
        const ImageBitmap& sourceBitmap,
        std::string* outError = nullptr
    );

    // Pulizia e gestione cache Zero-Work
    void InvalidateCache();
    bool HasCachedArtifact(const std::string& assetId) const;
    const BakedTextureArtifact* GetCachedArtifact(const std::string& assetId) const;
    uint64_t GetLastBakedHash(const std::string& assetId) const;

    // Telemetria Zero-Work
    TextureBakingTelemetry& GetTelemetry() { return m_telemetry; }
    const TextureBakingTelemetry& GetTelemetry() const { return m_telemetry; }
    void ResetTelemetry() { m_telemetry.Reset(); }

    // Sanificazione identificatore stringa
    static std::string SanitizeIdentifier(const std::string& id);

private:
    std::string m_baseDir = "assets";
    TextureBakingTelemetry m_telemetry;
    std::unordered_map<std::string, uint64_t> m_lastBakedHashes;
    std::unordered_map<std::string, BakedTextureArtifact> m_cachedArtifacts;
};

} // namespace fw
