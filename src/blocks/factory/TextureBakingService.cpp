#include "pch.h"
#include "TextureBakingService.h"
#include "ProductionStationAdapters.h"
#include <fstream>
#include <algorithm>
#include <cctype>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fw {

TextureBakingService::TextureBakingService() {
    m_telemetry.Reset();
}

void TextureBakingService::SetBaseDirectory(const std::string& baseDir) {
    m_baseDir = baseDir;
}

std::string TextureBakingService::SanitizeIdentifier(const std::string& id) {
    std::string sanitized;
    sanitized.reserve(id.size());
    for (char c : id) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
            sanitized.push_back(c);
        } else if (c == ':' || c == '/' || c == '\\' || c == '.' || c == ' ') {
            sanitized.push_back('_');
        }
    }
    if (sanitized.empty()) {
        sanitized = "unnamed_asset";
    }
    return sanitized;
}

std::string TextureBakingService::GenerateCanonicalRelativeUri(const std::string& assetId, const std::string& mapType) {
    std::string cleanId = SanitizeIdentifier(assetId);
    std::string cleanType = SanitizeIdentifier(mapType);
    return "textures/blocks/" + cleanId + "_" + cleanType + ".png";
}

bool TextureBakingService::ValidatePortableUri(const std::string& uri, std::string* outError) {
    if (uri.empty()) {
        if (outError) *outError = "URI texture vuoto.";
        return false;
    }

    // Divieto assoluto di percorsi con lettere di drive Windows (es. C:\, D:/)
    if (uri.size() >= 2 && uri[1] == ':') {
        if (outError) *outError = "Percorso non portabile: rilevata lettera di drive assoluta (" + uri.substr(0, 2) + ").";
        return false;
    }

    // Divieto di percorsi assoluti POSIX
    if (uri.front() == '/' || uri.front() == '\\') {
        if (outError) *outError = "Percorso non portabile: non puo' iniziare con separatore assoluto.";
        return false;
    }

    // Divieto di separatori Windows backslash nel contratto canonico
    if (uri.find('\\') != std::string::npos) {
        if (outError) *outError = "Percorso non portabile: contiene separatori backslash '\\'. Usare forward slash '/'.";
        return false;
    }

    // Divieto di path traversal escape (../)
    if (uri.find("..") != std::string::npos) {
        if (outError) *outError = "Violazione di sicurezza: rilevato path traversal '..' nell'URI.";
        return false;
    }

    // Verifica estensione PNG obbligatoria
    if (uri.size() < 4 || uri.substr(uri.size() - 4) != ".png") {
        if (outError) *outError = "Formato non supportato: l'URI dell'artefatto deve terminare con '.png'.";
        return false;
    }

    return true;
}

TextureBakeResult TextureBakingService::BakeAlbedoAtlas(
    const ImageBitmap& compositeAlbedo,
    const std::string& assetId,
    uint64_t revision,
    bool forceRewrite
) {
    TextureBakeResult result;

    // 1. Validazione integrità dell'immagine sorgente
    if (!compositeAlbedo.IsValid() || compositeAlbedo.IsEmpty()) {
        result.success = false;
        result.errorCode = BakingErrorCode::InvalidSourceImage;
        result.errorMessage = "Immagine sorgente CompositeAlbedo non valida o vuota.";
        return result;
    }

    uint32_t width = compositeAlbedo.GetWidth();
    uint32_t height = compositeAlbedo.GetHeight();
    if (!ImageBitmap::ValidateDimensions(width, height)) {
        result.success = false;
        result.errorCode = BakingErrorCode::InvalidDimensions;
        result.errorMessage = "Dimensioni CompositeAlbedo fuori dai limiti ammessi (" +
                              std::to_string(width) + "x" + std::to_string(height) + ").";
        return result;
    }

    // 2. Generazione e validazione URI relativo canonico
    std::string relUri = GenerateCanonicalRelativeUri(assetId, "albedo");
    std::string uriErr;
    if (!ValidatePortableUri(relUri, &uriErr)) {
        result.success = false;
        result.errorCode = BakingErrorCode::InvalidRelativePath;
        result.errorMessage = "Generazione URI fallita: " + uriErr;
        return result;
    }

    // Risoluzione percorso fisico locale
    std::filesystem::path basePath(m_baseDir);
    std::filesystem::path targetPhysicalPath = basePath / relUri;

    // 3. Calcolo FNV-1a Content Hash per invariante Zero-Work
    uint64_t contentHash = compositeAlbedo.ComputeContentHash();
    m_telemetry.hashScanCount++;

    // Verifica cache hit: se input invariato e file esiste su disco, Zero-Work totale
    std::error_code ec;
    bool fileExistsOnDisk = std::filesystem::exists(targetPhysicalPath, ec) &&
                            (std::filesystem::file_size(targetPhysicalPath, ec) > 0);

    auto hashIt = m_lastBakedHashes.find(assetId);
    if (!forceRewrite && hashIt != m_lastBakedHashes.end() && hashIt->second == contentHash && fileExistsOnDisk) {
        // Zero-Work Level 2 & 3: Nessun re-encoding PNG, nessuna riscrittura disco
        m_telemetry.cacheHitsNoEncoding++;
        m_telemetry.cacheHitsNoDiskWrite++;

        result.success = true;
        result.wasCacheHitNoEncoding = true;
        result.wasCacheHitNoDiskWrite = true;
        result.errorCode = BakingErrorCode::None;

        auto cachedArtIt = m_cachedArtifacts.find(assetId);
        if (cachedArtIt != m_cachedArtifacts.end()) {
            result.artifact = cachedArtIt->second;
            result.artifact.bakedRevision = revision;
        } else {
            result.artifact.relativeUri = relUri;
            result.artifact.physicalPath = targetPhysicalPath.string();
            result.artifact.contentHash = contentHash;
            result.artifact.width = width;
            result.artifact.height = height;
            result.artifact.channels = compositeAlbedo.GetBytesPerPixel();
            result.artifact.format = compositeAlbedo.GetPixelFormat();
            result.artifact.colorSpace = compositeAlbedo.GetColorSpace();
            result.artifact.fileSizeBytes = std::filesystem::file_size(targetPhysicalPath, ec);
            result.artifact.schemaVersion = 1;
            result.artifact.bakedRevision = revision;
            result.artifact.timestamp = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count()
            );
            result.artifact.isValid = true;
        }
        return result;
    }

    // 4. Codifica PNG in memoria buffer (Zero-Work miss o modifica rilevata)
    std::vector<uint8_t> pngBytes = compositeAlbedo.EncodeToPngMemory();
    m_telemetry.encodingCount++;

    if (pngBytes.empty()) {
        result.success = false;
        result.errorCode = BakingErrorCode::EncodingFailed;
        result.errorMessage = "Errore durante la codifica PNG in memoria.";
        return result;
    }

    // 5. Scrittura atomica su file temporaneo e pubblicazione
    // Creazione cartelle genitore se mancanti
    std::filesystem::path parentDir = targetPhysicalPath.parent_path();
    if (!parentDir.empty() && !std::filesystem::exists(parentDir, ec)) {
        if (!std::filesystem::create_directories(parentDir, ec) && ec) {
            result.success = false;
            result.errorCode = BakingErrorCode::DirectoryCreationFailed;
            result.errorMessage = "Impossibile creare la cartella di output: " + parentDir.string() + " (" + ec.message() + ")";
            return result;
        }
    }

    // Creazione del file temporaneo con suffisso univoco
    auto nowTicks = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::filesystem::path tempPath = targetPhysicalPath;
    tempPath += ".tmp." + std::to_string(nowTicks);

    {
        std::ofstream tempFile(tempPath, std::ios::binary | std::ios::trunc);
        if (!tempFile.is_open()) {
            result.success = false;
            result.errorCode = BakingErrorCode::TempFileWriteFailed;
            result.errorMessage = "Apertura file temporaneo fallita: " + tempPath.string();
            return result;
        }

        tempFile.write(reinterpret_cast<const char*>(pngBytes.data()), pngBytes.size());
        tempFile.flush();

        if (!tempFile.good()) {
            tempFile.close();
            std::filesystem::remove(tempPath, ec);
            result.success = false;
            result.errorCode = BakingErrorCode::TempFileWriteFailed;
            result.errorMessage = "Scrittura dati PNG su file temporaneo interrotta o fallita.";
            return result;
        }
        tempFile.close();
    }

    // Verifica che il file temporaneo sia stato scritto interamente
    uint64_t tempSize = std::filesystem::file_size(tempPath, ec);
    if (ec || tempSize != pngBytes.size()) {
        std::filesystem::remove(tempPath, ec);
        result.success = false;
        result.errorCode = BakingErrorCode::TempFileWriteFailed;
        result.errorMessage = "Dimensione del file temporaneo non corrispondente ai byte PNG codificati.";
        return result;
    }

    // Operazione Atomica di Rename/Publish
    bool renameOk = false;
#if defined(_WIN32)
    // Su Windows MoveFileExW con MOVEFILE_REPLACE_EXISTING e MOVEFILE_WRITE_THROUGH
    // garantisce atomicita e sovrascrittura sicura senza corrompere il file precedente
    if (MoveFileExW(tempPath.wstring().c_str(), targetPhysicalPath.wstring().c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        renameOk = true;
    } else {
        // Fallback standard C++ filesystem rename
        std::filesystem::rename(tempPath, targetPhysicalPath, ec);
        renameOk = !ec;
    }
#else
    std::filesystem::rename(tempPath, targetPhysicalPath, ec);
    renameOk = !ec;
#endif

    if (!renameOk) {
        // In caso di fallimento della sostituzione atomica, rimuovere il file temporaneo
        std::filesystem::remove(tempPath, ec);
        result.success = false;
        result.errorCode = BakingErrorCode::AtomicPublishFailed;
        result.errorMessage = "Pubblicazione atomica dell'artefatto fallita per: " + targetPhysicalPath.string();
        return result;
    }

    m_telemetry.diskWriteCount++;

    // 6. Verifica presenza e consistenza finale del file su disco
    if (!std::filesystem::exists(targetPhysicalPath, ec)) {
        result.success = false;
        result.errorCode = BakingErrorCode::FileNotFoundAfterBake;
        result.errorMessage = "Artefatto non rintracciabile su disco dopo la pubblicazione atomica.";
        return result;
    }

    // 7. Costruzione metadati artefatto persistente
    BakedTextureArtifact artifact;
    artifact.relativeUri = relUri;
    artifact.physicalPath = targetPhysicalPath.string();
    artifact.contentHash = contentHash;
    artifact.width = width;
    artifact.height = height;
    artifact.channels = compositeAlbedo.GetBytesPerPixel();
    artifact.format = compositeAlbedo.GetPixelFormat();
    artifact.colorSpace = compositeAlbedo.GetColorSpace();
    artifact.fileSizeBytes = std::filesystem::file_size(targetPhysicalPath, ec);
    artifact.schemaVersion = 1;
    artifact.bakedRevision = revision;
    artifact.timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
    artifact.isValid = true;

    // Aggiornamento cache
    m_lastBakedHashes[assetId] = contentHash;
    m_cachedArtifacts[assetId] = artifact;

    result.success = true;
    result.errorCode = BakingErrorCode::None;
    result.artifact = artifact;
    result.wasCacheHitNoEncoding = false;
    result.wasCacheHitNoDiskWrite = false;

    return result;
}

TextureBakeResult TextureBakingService::BakeDraftAlbedo(
    RawAssetDraft& draft,
    const PainterStationAdapter& painterStation,
    bool forceRewrite
) {
    TextureBakeResult result;

    // 1. Verifica disponibilità del CompositeAlbedo da PainterStationAdapter
    if (!painterStation.HasCompositeAlbedo()) {
        result.success = false;
        result.errorCode = BakingErrorCode::InvalidSourceImage;
        result.errorMessage = "PainterStationAdapter non possiede un CompositeAlbedo valido. Eseguire prima la stazione Painter.";
        return result;
    }

    const ImageBitmap& albedo = painterStation.GetCompositeAlbedo();

    // 2. Esecuzione baking
    result = BakeAlbedoAtlas(albedo, draft.manifest.assetId, draft.manifest.revision, forceRewrite);

    // 3. Regola contrattuale fondamentale (Requisito 8):
    // Non modificare il Draft originale in caso di fallimento
    if (result.success && result.artifact.isValid) {
        draft.albedoPath = result.artifact.relativeUri;
    }

    return result;
}

bool TextureBakingService::VerifyArtifactRoundTrip(
    const std::string& physicalPath,
    const ImageBitmap& sourceBitmap,
    std::string* outError
) {
    if (!sourceBitmap.IsValid()) {
        if (outError) *outError = "Source bitmap non valida per il confronto.";
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(physicalPath, ec)) {
        if (outError) *outError = "File artefatto non esistente sul disco: " + physicalPath;
        return false;
    }

    ImageBitmap decoded;
    if (!decoded.LoadFromFile(physicalPath, sourceBitmap.GetColorSpace())) {
        if (outError) *outError = "Decodifica del file PNG fallita: " + physicalPath;
        return false;
    }

    if (decoded.GetWidth() != sourceBitmap.GetWidth() || decoded.GetHeight() != sourceBitmap.GetHeight()) {
        if (outError) *outError = "Discrepanza dimensioni round-trip: atteso " +
                                  std::to_string(sourceBitmap.GetWidth()) + "x" + std::to_string(sourceBitmap.GetHeight()) +
                                  ", letto " + std::to_string(decoded.GetWidth()) + "x" + std::to_string(decoded.GetHeight());
        return false;
    }

    // Confronto pixel per pixel di tutti i canali (R, G, B, A)
    uint32_t w = sourceBitmap.GetWidth();
    uint32_t h = sourceBitmap.GetHeight();
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            glm::vec4 src = sourceBitmap.GetPixel(x, y);
            glm::vec4 dec = decoded.GetPixel(x, y);

            // PNG e' compressione lossless per interi a 8-bit [0, 255]
            // Tolleranza minima per precisione a virgola mobile (1.0f / 255.0f ~= 0.00392f)
            float diffR = std::abs(src.r - dec.r);
            float diffG = std::abs(src.g - dec.g);
            float diffB = std::abs(src.b - dec.b);
            float diffA = std::abs(src.a - dec.a);

            if (diffR > 0.005f || diffG > 0.005f || diffB > 0.005f || diffA > 0.005f) {
                if (outError) {
                    *outError = "Discrepanza pixel a (" + std::to_string(x) + "," + std::to_string(y) + "): " +
                                "Atteso RGBA(" + std::to_string(src.r) + "," + std::to_string(src.g) + "," + std::to_string(src.b) + "," + std::to_string(src.a) + ") vs " +
                                "Letto RGBA(" + std::to_string(dec.r) + "," + std::to_string(dec.g) + "," + std::to_string(dec.b) + "," + std::to_string(dec.a) + ")";
                }
                return false;
            }
        }
    }

    return true;
}

void TextureBakingService::InvalidateCache() {
    m_lastBakedHashes.clear();
    m_cachedArtifacts.clear();
}

bool TextureBakingService::HasCachedArtifact(const std::string& assetId) const {
    return m_cachedArtifacts.find(assetId) != m_cachedArtifacts.end();
}

const BakedTextureArtifact* TextureBakingService::GetCachedArtifact(const std::string& assetId) const {
    auto it = m_cachedArtifacts.find(assetId);
    return (it != m_cachedArtifacts.end()) ? &it->second : nullptr;
}

uint64_t TextureBakingService::GetLastBakedHash(const std::string& assetId) const {
    auto it = m_lastBakedHashes.find(assetId);
    return (it != m_lastBakedHashes.end()) ? it->second : 0;
}

} // namespace fw
