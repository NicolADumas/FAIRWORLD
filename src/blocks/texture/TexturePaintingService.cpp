#include "TexturePaintingService.h"
#include <cmath>
#include <algorithm>

namespace fw {

// =============================================================================
// BM-TX3.5 — BRUSH FALLOFF
// =============================================================================

float PaintBrush::EvaluateFalloff(float distNormalized) const {
    if (distNormalized >= 1.0f) return 0.0f;
    float h = std::clamp(hardness, 0.0f, 0.999f);
    if (distNormalized <= h) return 1.0f;

    // Parametro normalizzato tra bordo del nucleo rigido (h) e bordo esterno (1.0)
    float t = (distNormalized - h) / (1.0f - h);
    t = std::clamp(t, 0.0f, 1.0f);

    switch (falloff) {
        case BrushFalloff::SmoothHermite:
            // Curva S Hermite 3t^2 - 2t^3 invertita
            return 1.0f - (3.0f * t * t - 2.0f * t * t * t);
        case BrushFalloff::Linear:
            return 1.0f - t;
        case BrushFalloff::Sphere:
            return std::sqrt(std::max(0.0f, 1.0f - t * t));
        case BrushFalloff::Sharp:
            return (1.0f - t) * (1.0f - t);
        case BrushFalloff::Constant:
            return 1.0f;
        default:
            return 1.0f - t;
    }
}

// =============================================================================
// BM-TX3.5 — TEXTURE PAINTING SERVICE CONSTRUCTOR & INITIALIZATION
// =============================================================================

TexturePaintingService::TexturePaintingService() {
    InitializeCanvas(512, 512, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
}

TexturePaintingService::TexturePaintingService(uint32_t width, uint32_t height) {
    InitializeCanvas(width, height, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
}

void TexturePaintingService::InitializeCanvas(uint32_t width, uint32_t height, const glm::vec4& baseColor) {
    if (m_activeTransaction.isActive) {
        CancelStroke();
    }

    m_canvasWidth = std::clamp(width, 16u, 4096u);
    m_canvasHeight = std::clamp(height, 16u, 4096u);
    m_layers.clear();
    m_undoStack.clear();
    m_redoStack.clear();
    m_nextLayerId = 1;

    // Livello 1 base predefinito
    AddLayer("Background", baseColor);
    m_compositionRevision = 1;
}

// =============================================================================
// BM-TX3.2 — GESTIONE LAYER
// =============================================================================

uint32_t TexturePaintingService::AddLayer(const std::string& name, const glm::vec4& fillColor) {
    uint32_t id = m_nextLayerId++;
    m_layers.emplace_back(id, name, m_canvasWidth, m_canvasHeight, fillColor);
    m_compositionRevision++;
    return id;
}

bool TexturePaintingService::RemoveLayer(uint32_t layerId) {
    if (m_layers.size() <= 1) return false; // Almeno un layer deve esistere sempre
    if (m_activeTransaction.isActive && m_activeTransaction.layerId == layerId) {
        CancelStroke();
    }

    size_t idx = FindLayerIndex(layerId);
    if (idx < m_layers.size()) {
        m_layers.erase(m_layers.begin() + idx);
        m_compositionRevision++;
        return true;
    }
    return false;
}

bool TexturePaintingService::MoveLayerUp(uint32_t layerId) {
    size_t idx = FindLayerIndex(layerId);
    if (idx < m_layers.size() && idx + 1 < m_layers.size()) {
        std::swap(m_layers[idx], m_layers[idx + 1]);
        m_compositionRevision++;
        return true;
    }
    return false;
}

bool TexturePaintingService::MoveLayerDown(uint32_t layerId) {
    size_t idx = FindLayerIndex(layerId);
    if (idx > 0 && idx < m_layers.size()) {
        std::swap(m_layers[idx], m_layers[idx - 1]);
        m_compositionRevision++;
        return true;
    }
    return false;
}

bool TexturePaintingService::SetLayerOrder(uint32_t layerId, size_t newIndex) {
    size_t curIdx = FindLayerIndex(layerId);
    if (curIdx >= m_layers.size()) return false;
    newIndex = std::min(newIndex, m_layers.size() - 1);
    if (curIdx == newIndex) return true;

    PaintLayer layer = std::move(m_layers[curIdx]);
    m_layers.erase(m_layers.begin() + curIdx);
    m_layers.insert(m_layers.begin() + newIndex, std::move(layer));
    m_compositionRevision++;
    return true;
}

uint32_t TexturePaintingService::DuplicateLayer(uint32_t layerId) {
    size_t idx = FindLayerIndex(layerId);
    if (idx >= m_layers.size()) return 0;

    PaintLayer clone = m_layers[idx];
    clone.layerId = m_nextLayerId++;
    clone.name = m_layers[idx].name + " Copy";
    clone.revision = 1;

    uint32_t newId = clone.layerId;
    m_layers.insert(m_layers.begin() + idx + 1, std::move(clone));
    m_compositionRevision++;
    return newId;
}

bool TexturePaintingService::MergeDown(uint32_t layerId) {
    size_t idx = FindLayerIndex(layerId);
    if (idx == 0 || idx >= m_layers.size()) return false; // Impossibile fondere il layer più basso

    PaintLayer& topLayer = m_layers[idx];
    PaintLayer& bottomLayer = m_layers[idx - 1];

    // Fonde topLayer su bottomLayer pixel per pixel
    for (uint32_t y = 0; y < m_canvasHeight; ++y) {
        for (uint32_t x = 0; x < m_canvasWidth; ++x) {
            glm::vec4 topColor = topLayer.pixelBuffer.GetPixel(x, y);
            float topAlpha = topColor.a * topLayer.opacity;
            if (topLayer.mask.has_value() && topLayer.mask->IsValid()) {
                topAlpha *= topLayer.mask->GetNormalizedValue(x, y);
            }

            if (topAlpha > 1e-6f) {
                glm::vec4 bottomColor = bottomLayer.pixelBuffer.GetPixel(x, y);
                glm::vec4 blended = BlendPixelsLinear(topColor, topAlpha, bottomColor, bottomColor.a, topLayer.blendMode);
                bottomLayer.pixelBuffer.SetPixel(x, y, blended);
            }
        }
    }

    bottomLayer.revision++;
    m_layers.erase(m_layers.begin() + idx);
    m_compositionRevision++;
    return true;
}

PaintLayer* TexturePaintingService::GetLayer(uint32_t layerId) {
    size_t idx = FindLayerIndex(layerId);
    return (idx < m_layers.size()) ? &m_layers[idx] : nullptr;
}

const PaintLayer* TexturePaintingService::GetLayer(uint32_t layerId) const {
    size_t idx = FindLayerIndex(layerId);
    return (idx < m_layers.size()) ? &m_layers[idx] : nullptr;
}

void TexturePaintingService::ClearAllLayers() {
    if (m_activeTransaction.isActive) {
        CancelStroke();
    }
    m_layers.clear();
    m_nextLayerId = 1;
    AddLayer("Background", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    m_compositionRevision++;
}

size_t TexturePaintingService::FindLayerIndex(uint32_t layerId) const {
    for (size_t i = 0; i < m_layers.size(); ++i) {
        if (m_layers[i].layerId == layerId) return i;
    }
    return m_layers.size();
}

// =============================================================================
// BM-TX3.3 — GESTIONE MASCHERE
// =============================================================================

bool TexturePaintingService::AddMaskToLayer(uint32_t layerId, float defaultFill) {
    PaintLayer* l = GetLayer(layerId);
    if (!l) return false;
    l->EnsureMask(defaultFill);
    m_compositionRevision++;
    return true;
}

bool TexturePaintingService::RemoveMaskFromLayer(uint32_t layerId) {
    PaintLayer* l = GetLayer(layerId);
    if (!l || !l->mask.has_value()) return false;
    l->RemoveMask();
    m_compositionRevision++;
    return true;
}

bool TexturePaintingService::InvertLayerMask(uint32_t layerId) {
    PaintLayer* l = GetLayer(layerId);
    if (!l || !l->mask.has_value()) return false;
    l->mask->Invert();
    l->revision++;
    m_compositionRevision++;
    return true;
}

// =============================================================================
// BM-TX3.5 & BM-TX3.6 — STROKE TRANSACTIONS
// =============================================================================

bool TexturePaintingService::BeginStroke(uint32_t layerId, bool targetMask) {
    if (layerId == 0 && !m_layers.empty()) {
        layerId = m_layers[0].layerId;
    }
    PaintLayer* l = GetLayer(layerId);
    if (!l || l->isLocked) return false;
    if (targetMask && (!l->mask.has_value() || !l->mask->IsValid())) return false;

    if (m_activeTransaction.isActive) {
        EndStroke();
    }

    m_activeTransaction.isActive = true;
    m_activeTransaction.layerId = layerId;
    m_activeTransaction.isMaskStroke = targetMask;
    m_activeTransaction.dirtyRect.Clear();

    // Snapshot pre-stroke per garanzia di rollback deterministico
    if (targetMask) {
        m_activeTransaction.preStrokeBackup = l->mask->GetBitmap();
    } else {
        m_activeTransaction.preStrokeBackup = l->pixelBuffer;
    }

    return true;
}

bool TexturePaintingService::ApplyDab(glm::vec2 pixelCoords, const PaintBrush& brush) {
    if (!m_activeTransaction.isActive) return false;
    PaintLayer* l = GetLayer(m_activeTransaction.layerId);
    if (!l) return false;

    ApplyDabInternal(*l, pixelCoords, brush, m_activeTransaction.isMaskStroke);
    return true;
}

bool TexturePaintingService::UpdateStroke(glm::vec2 prevCoords, glm::vec2 curCoords, const PaintBrush& brush) {
    if (!m_activeTransaction.isActive) return false;
    PaintLayer* l = GetLayer(m_activeTransaction.layerId);
    if (!l) return false;

    glm::vec2 delta = curCoords - prevCoords;
    float dist = glm::length(delta);
    float step = std::max(1.0f, brush.radiusPixels * std::clamp(brush.spacing, 0.02f, 1.0f));

    if (dist < 1e-4f) {
        ApplyDabInternal(*l, curCoords, brush, m_activeTransaction.isMaskStroke);
        return true;
    }

    int steps = static_cast<int>(std::ceil(dist / step));
    for (int i = 0; i <= steps; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(steps);
        glm::vec2 pt = prevCoords + delta * t;
        ApplyDabInternal(*l, pt, brush, m_activeTransaction.isMaskStroke);
    }

    return true;
}

void TexturePaintingService::ApplyDabInternal(PaintLayer& layer, glm::vec2 center, const PaintBrush& brush, bool isMask) {
    float r = brush.radiusPixels;
    if (r <= 0.0f) return;

    int32_t x0 = std::max(0, static_cast<int32_t>(std::floor(center.x - r)));
    int32_t y0 = std::max(0, static_cast<int32_t>(std::floor(center.y - r)));
    int32_t x1 = std::min(static_cast<int32_t>(m_canvasWidth - 1), static_cast<int32_t>(std::ceil(center.x + r)));
    int32_t y1 = std::min(static_cast<int32_t>(m_canvasHeight - 1), static_cast<int32_t>(std::ceil(center.y + r)));

    if (x0 > x1 || y0 > y1) return;

    for (int32_t y = y0; y <= y1; ++y) {
        for (int32_t x = x0; x <= x1; ++x) {
            float dx = (x + 0.5f) - center.x;
            float dy = (y + 0.5f) - center.y;
            float dist = std::sqrt(dx * dx + dy * dy);

            if (dist <= r) {
                float falloffFactor = brush.EvaluateFalloff(dist / r);
                float factor = falloffFactor * brush.opacity * brush.strength;

                if (factor > 1e-6f) {
                    if (isMask && layer.mask.has_value()) {
                        float curVal = layer.mask->GetNormalizedValue(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
                        float targetVal = brush.eraseMode ? 0.0f : brush.color.r;
                        float newVal = curVal + (targetVal - curVal) * factor;
                        layer.mask->SetNormalizedValue(static_cast<uint32_t>(x), static_cast<uint32_t>(y), newVal);
                    } else {
                        glm::vec4 curCol = layer.pixelBuffer.GetPixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
                        glm::vec4 newCol;
                        if (brush.eraseMode) {
                            newCol = curCol;
                            newCol.a = std::clamp(curCol.a * (1.0f - factor), 0.0f, 1.0f);
                        } else {
                            // Blending progressivo del dab sul pixel corrente del layer
                            float dabAlpha = brush.color.a * factor;
                            newCol = BlendPixelsLinear(brush.color, dabAlpha, curCol, curCol.a, PaintLayerBlendMode::Normal);
                        }
                        layer.pixelBuffer.SetPixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y), newCol);
                    }
                    m_activeTransaction.dirtyRect.ExpandToInclude(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
                }
            }
        }
    }
}

PaintDirtyRect TexturePaintingService::EndStroke() {
    if (!m_activeTransaction.isActive) {
        return PaintDirtyRect{};
    }

    PaintDirtyRect resultDirty = m_activeTransaction.dirtyRect;
    resultDirty.ClipTo(m_canvasWidth, m_canvasHeight);

    if (!resultDirty.isEmpty) {
        PaintLayer* l = GetLayer(m_activeTransaction.layerId);
        if (l) {
            l->revision++;
            m_compositionRevision++;

            // Registra comando per Undo/Redo
            PaintUndoRecord record;
            record.layerId = m_activeTransaction.layerId;
            record.isMask = m_activeTransaction.isMaskStroke;
            record.dirtyRect = resultDirty;
            record.beforeSnapshot = std::move(m_activeTransaction.preStrokeBackup);
            if (record.isMask && l->mask.has_value()) {
                record.afterSnapshot = l->mask->GetBitmap();
            } else {
                record.afterSnapshot = l->pixelBuffer;
            }

            m_undoStack.push_back(std::move(record));
            if (m_undoStack.size() > kMaxHistoryRecords) {
                m_undoStack.pop_front();
            }
            m_redoStack.clear();
        }
    }

    m_activeTransaction.isActive = false;
    m_activeTransaction.dirtyRect.Clear();
    return resultDirty;
}

bool TexturePaintingService::CancelStroke() {
    if (!m_activeTransaction.isActive) return false;

    if (!m_activeTransaction.dirtyRect.isEmpty) {
        PaintLayer* l = GetLayer(m_activeTransaction.layerId);
        if (l) {
            if (m_activeTransaction.isMaskStroke && l->mask.has_value()) {
                l->mask->GetBitmapMutable() = std::move(m_activeTransaction.preStrokeBackup);
            } else {
                l->pixelBuffer = std::move(m_activeTransaction.preStrokeBackup);
            }
            l->revision++;
            m_compositionRevision++;
        }
    }

    m_activeTransaction.isActive = false;
    m_activeTransaction.dirtyRect.Clear();
    return true;
}

// =============================================================================
// BM-TX3.4 — BLEND MODES (MATEMATICA IN SPAZIO LINEARE DETERMINISTICO)
// =============================================================================

glm::vec4 TexturePaintingService::BlendPixelsLinear(const glm::vec4& srcColor, float srcAlpha, 
                                                   const glm::vec4& dstColor, float dstAlpha, 
                                                   PaintLayerBlendMode mode) 
{
    // Se la sorgente è trasparente, non altera la destinazione
    if (srcAlpha <= 1e-6f) {
        return glm::vec4(dstColor.r, dstColor.g, dstColor.b, dstAlpha);
    }
    // Se la destinazione è completamente vuota, la sorgente prevale integralmente
    if (dstAlpha <= 1e-6f) {
        return glm::vec4(srcColor.r, srcColor.g, srcColor.b, srcAlpha);
    }

    glm::vec3 Cs = glm::clamp(glm::vec3(srcColor), 0.0f, 1.0f);
    glm::vec3 Cd = glm::clamp(glm::vec3(dstColor), 0.0f, 1.0f);
    glm::vec3 B(0.0f);

    switch (mode) {
        case PaintLayerBlendMode::Normal:
            B = Cs;
            break;
        case PaintLayerBlendMode::Multiply:
            B = Cs * Cd;
            break;
        case PaintLayerBlendMode::Additive:
            B = glm::min(glm::vec3(1.0f), Cs + Cd);
            break;
        case PaintLayerBlendMode::Screen:
            B = glm::vec3(1.0f) - (glm::vec3(1.0f) - Cs) * (glm::vec3(1.0f) - Cd);
            break;
        case PaintLayerBlendMode::Overlay: {
            for (int i = 0; i < 3; ++i) {
                if (Cd[i] < 0.5f) {
                    B[i] = 2.0f * Cs[i] * Cd[i];
                } else {
                    B[i] = 1.0f - 2.0f * (1.0f - Cs[i]) * (1.0f - Cd[i]);
                }
            }
            break;
        }
    }
    B = glm::clamp(B, 0.0f, 1.0f);

    // Compositing Porter-Duff Over generalizzato con funzione di blend
    float outAlpha = srcAlpha + dstAlpha * (1.0f - srcAlpha);
    outAlpha = glm::clamp(outAlpha, 0.0f, 1.0f);

    glm::vec3 outColor(0.0f);
    if (outAlpha > 1e-6f) {
        outColor = (B * srcAlpha + Cd * dstAlpha * (1.0f - srcAlpha)) / outAlpha;
    }
    outColor = glm::clamp(outColor, 0.0f, 1.0f);

    return glm::vec4(outColor, outAlpha);
}

// =============================================================================
// BM-TX3.8 — COMPOSITING INCREMENTALE
// =============================================================================

bool TexturePaintingService::CompositeLayers(ImageBitmap& outComposite, const PaintDirtyRect* subRect) const {
    if (m_layers.empty()) return false;

    // Assicura allocazione corretta del buffer di output
    if (!outComposite.IsValid() || outComposite.GetWidth() != m_canvasWidth || outComposite.GetHeight() != m_canvasHeight) {
        outComposite = ImageBitmap(m_canvasWidth, m_canvasHeight, TexturePixelFormat::RGBA8, 
                                   TextureColorSpace::Linear, glm::vec4(0.0f));
    }

    uint32_t x0 = 0, y0 = 0, x1 = m_canvasWidth - 1, y1 = m_canvasHeight - 1;
    if (subRect && !subRect->isEmpty) {
        x0 = std::min(subRect->xMin, m_canvasWidth - 1);
        y0 = std::min(subRect->yMin, m_canvasHeight - 1);
        x1 = std::min(subRect->xMax, m_canvasWidth - 1);
        y1 = std::min(subRect->yMax, m_canvasHeight - 1);
        if (x0 > x1 || y0 > y1) return true; // Zero-Work per dirty rect vuoto
    }

    for (uint32_t y = y0; y <= y1; ++y) {
        for (uint32_t x = x0; x <= x1; ++x) {
            glm::vec4 accColor(0.0f);
            float accAlpha = 0.0f;

            for (const auto& layer : m_layers) {
                if (!layer.isVisible || layer.opacity <= 1e-6f) continue;

                glm::vec4 srcColor = layer.pixelBuffer.GetPixel(x, y);
                float srcAlpha = srcColor.a * layer.opacity;

                if (layer.mask.has_value() && layer.mask->IsValid()) {
                    srcAlpha *= layer.mask->GetNormalizedValue(x, y);
                }

                if (srcAlpha > 1e-6f) {
                    accColor = BlendPixelsLinear(srcColor, srcAlpha, accColor, accAlpha, layer.blendMode);
                    accAlpha = accColor.a;
                }
            }

            outComposite.SetPixel(x, y, accColor);
        }
    }

    outComposite.IncrementRevision();
    return true;
}

// =============================================================================
// ZERO-WORK & FINGERPRINTING
// =============================================================================

uint64_t TexturePaintingService::ComputeCombinedFingerprint() const {
    const uint64_t kFnvOffsetBasis = 14695981039346656037ull;
    const uint64_t kFnvPrime = 1099511628211ull;

    uint64_t hash = kFnvOffsetBasis;
    auto hashBytes = [&](const void* ptr, size_t len) {
        const uint8_t* p = static_cast<const uint8_t*>(ptr);
        for (size_t i = 0; i < len; ++i) {
            hash ^= p[i];
            hash *= kFnvPrime;
        }
    };

    hashBytes(&m_canvasWidth, sizeof(m_canvasWidth));
    hashBytes(&m_canvasHeight, sizeof(m_canvasHeight));
    hashBytes(&m_compositionRevision, sizeof(m_compositionRevision));

    for (const auto& l : m_layers) {
        uint64_t lHash = l.ComputeFingerprint();
        hashBytes(&lHash, sizeof(lHash));
    }

    return hash;
}

// =============================================================================
// UNDO / REDO
// =============================================================================

bool TexturePaintingService::Undo() {
    if (m_undoStack.empty()) return false;
    if (m_activeTransaction.isActive) CancelStroke();

    PaintUndoRecord record = std::move(m_undoStack.back());
    m_undoStack.pop_back();

    PaintLayer* l = GetLayer(record.layerId);
    if (l) {
        if (record.isMask && l->mask.has_value()) {
            l->mask->GetBitmapMutable() = record.beforeSnapshot;
        } else {
            l->pixelBuffer = record.beforeSnapshot;
        }
        l->revision++;
        m_compositionRevision++;
    }

    m_redoStack.push_back(std::move(record));
    return true;
}

bool TexturePaintingService::Redo() {
    if (m_redoStack.empty()) return false;
    if (m_activeTransaction.isActive) CancelStroke();

    PaintUndoRecord record = std::move(m_redoStack.back());
    m_redoStack.pop_back();

    PaintLayer* l = GetLayer(record.layerId);
    if (l) {
        if (record.isMask && l->mask.has_value()) {
            l->mask->GetBitmapMutable() = record.afterSnapshot;
        } else {
            l->pixelBuffer = record.afterSnapshot;
        }
        l->revision++;
        m_compositionRevision++;
    }

    m_undoStack.push_back(std::move(record));
    return true;
}

void TexturePaintingService::ClearHistory() {
    m_undoStack.clear();
    m_redoStack.clear();
}

// =============================================================================
// BM-TX3.9 — SERIALIZZAZIONE
// =============================================================================

nlohmann::json TexturePaintingService::ToJson() const {
    nlohmann::json j;
    j["canvasWidth"] = m_canvasWidth;
    j["canvasHeight"] = m_canvasHeight;
    j["compositionRevision"] = m_compositionRevision;

    nlohmann::json layersArray = nlohmann::json::array();
    for (const auto& l : m_layers) {
        layersArray.push_back(l.ToJson());
    }
    j["layers"] = layersArray;

    return j;
}

bool TexturePaintingService::FromJson(const nlohmann::json& j) {
    if (!j.contains("canvasWidth") || !j.contains("canvasHeight")) return false;

    if (m_activeTransaction.isActive) CancelStroke();

    m_canvasWidth = j["canvasWidth"].get<uint32_t>();
    m_canvasHeight = j["canvasHeight"].get<uint32_t>();
    m_compositionRevision = j.value("compositionRevision", 1ull);

    m_layers.clear();
    m_undoStack.clear();
    m_redoStack.clear();

    uint32_t maxId = 0;
    if (j.contains("layers") && j["layers"].is_array()) {
        for (const auto& lj : j["layers"]) {
            PaintLayer l = PaintLayer::FromJson(lj);
            maxId = std::max(maxId, l.layerId);
            m_layers.push_back(std::move(l));
        }
    }

    m_nextLayerId = maxId + 1;
    if (m_layers.empty()) {
        AddLayer("Background", glm::vec4(1.0f));
    }

    return true;
}

} // namespace fw
