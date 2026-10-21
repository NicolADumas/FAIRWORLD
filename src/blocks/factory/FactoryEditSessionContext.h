#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <optional>
#include "RawAssetContract.h"
#include "blocks/sculpt/EditableMesh.h"
#include "blocks/uv/UVLayout.h"
#include "blocks/uv/UVEditorDocument.h"
#include "blocks/texture/TexturePaintingService.h"

namespace fw {

// =============================================================================
// FACTORY EDIT SESSION CONTEXT (BM-UI2.3-B1)
// Contesto di sessione condiviso in sola lettura per referenziare in modo sicuro:
// - Draft attivo (RawAssetDraft autoritativo)
// - Mesh poligonale attiva (EditableMesh senza dangling reference)
// - Layout UV attivo
// - Servizio pittura e layer stack
// =============================================================================
struct FactoryEditSessionContext {
    RawAssetDraft* activeDraft = nullptr;
    EditableMesh* activeMesh = nullptr;
    UVLayout* activeUVLayout = nullptr;
    UVEditorDocument* uvDocument = nullptr;
    TexturePaintingService* paintingService = nullptr;
    uint32_t activeSurfaceId = 0;
    uint8_t activeBlockId = 1;
    bool isSessionValid = false;

    bool HasActiveDraft() const { return isSessionValid && activeDraft != nullptr; }
    bool HasActiveMesh() const { return isSessionValid && activeMesh != nullptr; }
    bool HasUVLayout() const { return HasActiveDraft() && activeUVLayout != nullptr && activeUVLayout->isValid; }
    bool HasPaintingService() const { return isSessionValid && paintingService != nullptr; }
};

} // namespace fw
