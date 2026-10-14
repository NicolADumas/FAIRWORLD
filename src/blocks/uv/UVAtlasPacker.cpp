#include "pch.h"
#include "UVAtlasPacker.h"
#include <algorithm>
#include <cmath>
#include <climits>

namespace fw {

glm::vec2 UVAtlasPacker::LocalToAtlas(const glm::vec2& localUV, const UVIsland& island) {
    glm::vec2 rotUV = localUV;
    float r = island.rotationDegrees;

    if (std::abs(r - 90.0f) < 1.0f) {
        rotUV = glm::vec2(localUV.y, 1.0f - localUV.x);
    } else if (std::abs(r - 180.0f) < 1.0f) {
        rotUV = glm::vec2(1.0f - localUV.x, 1.0f - localUV.y);
    } else if (std::abs(r - 270.0f) < 1.0f) {
        rotUV = glm::vec2(1.0f - localUV.y, localUV.x);
    }

    glm::vec2 scale(island.atlasTransform.x, island.atlasTransform.y);
    glm::vec2 offset(island.atlasTransform.z, island.atlasTransform.w);
    return offset + rotUV * scale;
}

glm::vec2 UVAtlasPacker::AtlasToLocal(const glm::vec2& atlasUV, const UVIsland& island) {
    glm::vec2 scale(island.atlasTransform.x, island.atlasTransform.y);
    glm::vec2 offset(island.atlasTransform.z, island.atlasTransform.w);

    float sx = (std::abs(scale.x) > 1e-7f) ? scale.x : 1.0f;
    float sy = (std::abs(scale.y) > 1e-7f) ? scale.y : 1.0f;
    glm::vec2 rotUV = (atlasUV - offset) / glm::vec2(sx, sy);

    glm::vec2 localUV = rotUV;
    float r = island.rotationDegrees;

    if (std::abs(r - 90.0f) < 1.0f) {
        localUV = glm::vec2(1.0f - rotUV.y, rotUV.x);
    } else if (std::abs(r - 180.0f) < 1.0f) {
        localUV = glm::vec2(1.0f - rotUV.x, 1.0f - rotUV.y);
    } else if (std::abs(r - 270.0f) < 1.0f) {
        localUV = glm::vec2(rotUV.y, 1.0f - rotUV.x);
    }

    return localUV;
}

glm::vec2 UVAtlasPacker::AtlasToPixel(const glm::vec2& atlasUV, uint32_t atlasW, uint32_t atlasH) {
    return glm::vec2(atlasUV.x * static_cast<float>(atlasW), atlasUV.y * static_cast<float>(atlasH));
}

glm::vec2 UVAtlasPacker::PixelToAtlas(const glm::vec2& pixelUV, uint32_t atlasW, uint32_t atlasH) {
    float w = (atlasW > 0) ? static_cast<float>(atlasW) : 1024.0f;
    float h = (atlasH > 0) ? static_cast<float>(atlasH) : 1024.0f;
    return glm::vec2(pixelUV.x / w, pixelUV.y / h);
}

bool UVAtlasPacker::ValidateAtlasLayout(const UVLayout& layout, 
                                       float paddingPixels, 
                                       std::vector<std::string>& outErrors) {
    outErrors.clear();
    bool valid = true;

    // 1. Verifica confini atlas [0, 1] per tutte le isole
    for (size_t i = 0; i < layout.islands.size(); ++i) {
        const auto& isl = layout.islands[i];
        float uMin = isl.atlasTransform.z;
        float vMin = isl.atlasTransform.w;
        float uMax = uMin + isl.atlasTransform.x;
        float vMax = vMin + isl.atlasTransform.y;

        if (uMin < -1e-4f || vMin < -1e-4f || uMax > 1.0001f || vMax > 1.0001f) {
            outErrors.push_back("Isola #" + std::to_string(isl.id) + " eccede i confini dell'atlas ([uMin=" +
                std::to_string(uMin) + ", vMin=" + std::to_string(vMin) + ", uMax=" + std::to_string(uMax) + ", vMax=" + std::to_string(vMax) + "]).");
            valid = false;
        }
    }

    // 2. Verifica assenza di sovrapposizioni tra coppie di isole sulla stessa pagina
    float eps = 1e-5f;
    for (size_t i = 0; i < layout.islands.size(); ++i) {
        const auto& a = layout.islands[i];
        float aMinU = a.atlasTransform.z;
        float aMinV = a.atlasTransform.w;
        float aMaxU = aMinU + a.atlasTransform.x;
        float aMaxV = aMinV + a.atlasTransform.y;

        for (size_t j = i + 1; j < layout.islands.size(); ++j) {
            const auto& b = layout.islands[j];
            if (a.atlasPage != b.atlasPage) continue; // Pagine atlas distinte -> nessun overlap

            float bMinU = b.atlasTransform.z;
            float bMinV = b.atlasTransform.w;
            float bMaxU = bMinU + b.atlasTransform.x;
            float bMaxV = bMinV + b.atlasTransform.y;

            bool overlapU = (std::max(aMinU, bMinU) < std::min(aMaxU, bMaxU) - eps);
            bool overlapV = (std::max(aMinV, bMinV) < std::min(aMaxV, bMaxV) - eps);

            if (overlapU && overlapV) {
                outErrors.push_back("Rilevata sovrapposizione tra isola #" + std::to_string(a.id) + 
                    " e isola #" + std::to_string(b.id) + " sulla pagina " + std::to_string(a.atlasPage));
                valid = false;
            }
        }
    }

    return valid;
}

uint64_t UVAtlasPacker::ComputeFingerprint(const UVLayout& layout, const AtlasPackOptions& options) const {
    uint64_t h = 14695981039346656037ULL;
    auto hashVal = [&](uint64_t v) {
        h ^= v;
        h *= 1099511628211ULL;
    };

    hashVal(layout.meshTopologyRevision);
    hashVal(layout.meshAttributeRevision);
    hashVal(layout.uvRevision);
    hashVal(static_cast<uint64_t>(layout.islands.size()));
    hashVal(options.targetWidth);
    hashVal(options.targetHeight);
    hashVal(static_cast<uint64_t>(options.paddingPixels * 100.0f));
    hashVal(options.allowRotation ? 1 : 0);
    hashVal(options.allowMultiAtlas ? 1 : 0);
    hashVal(static_cast<uint64_t>(options.densityOptions.mode));
    return h;
}

bool UVAtlasPacker::PackSkylinePage(std::vector<RectToPack*>& rects, 
                                    int binWidth, 
                                    int binHeight, 
                                    bool allowRotation) {
    std::vector<SkylineNode> skyline;
    skyline.push_back({ 0, 0, binWidth });

    for (auto* rect : rects) {
        int bestIdx = -1;
        int bestY = INT_MAX;
        int bestX = 0;
        int bestW = 0;
        int bestH = 0;
        bool bestRot = false;

        auto testPlacement = [&](int w, int h, bool isRot) {
            for (size_t i = 0; i < skyline.size(); ++i) {
                int x = skyline[i].x;
                if (x + w > binWidth) continue;

                // Calcola l'altezza massima dello skyline nell'intervallo [x, x + w]
                int yMax = skyline[i].y;
                int widthLeft = w;
                size_t j = i;
                while (widthLeft > 0 && j < skyline.size()) {
                    yMax = std::max(yMax, skyline[j].y);
                    widthLeft -= skyline[j].width;
                    j++;
                }

                if (yMax + h <= binHeight) {
                    if (yMax < bestY || (yMax == bestY && x < bestX)) {
                        bestY = yMax;
                        bestX = x;
                        bestW = w;
                        bestH = h;
                        bestIdx = static_cast<int>(i);
                        bestRot = isRot;
                    }
                }
            }
        };

        // Valuta orientamento naturale a 0 gradi
        testPlacement(rect->width, rect->height, false);

        // Se abilitata la rotazione, valuta orientamento ortogonale a 90 gradi
        if (allowRotation && rect->width != rect->height) {
            testPlacement(rect->height, rect->width, true);
        }

        if (bestIdx == -1) {
            return false; // Spazio esaurito per questo rettangolo su questa pagina
        }

        rect->x = bestX;
        rect->y = bestY;
        rect->rotated = bestRot;

        // Aggiorna lo skyline inserendo il nuovo segmento
        SkylineNode newNode{ bestX, bestY + bestH, bestW };
        std::vector<SkylineNode> nextSkyline;

        for (size_t i = 0; i < skyline.size(); ++i) {
            const auto& node = skyline[i];
            int nodeLeft = node.x;
            int nodeRight = node.x + node.width;
            int newLeft = newNode.x;
            int newRight = newNode.x + newNode.width;

            if (nodeRight <= newLeft || nodeLeft >= newRight) {
                nextSkyline.push_back(node);
            } else {
                if (nodeLeft < newLeft) {
                    nextSkyline.push_back({ nodeLeft, node.y, newLeft - nodeLeft });
                }
                if (nodeRight > newRight) {
                    nextSkyline.push_back({ newRight, node.y, nodeRight - newRight });
                }
            }
        }
        nextSkyline.push_back(newNode);

        // Ordina lo skyline lungo X
        std::sort(nextSkyline.begin(), nextSkyline.end(), [](const SkylineNode& a, const SkylineNode& b) {
            return a.x < b.x;
        });

        // Fonde nodi adiacenti con la stessa altezza
        skyline.clear();
        for (const auto& node : nextSkyline) {
            if (!skyline.empty() && skyline.back().y == node.y && (skyline.back().x + skyline.back().width) == node.x) {
                skyline.back().width += node.width;
            } else {
                skyline.push_back(node);
            }
        }
    }

    return true;
}

AtlasPackResult UVAtlasPacker::PackLayout(EditableMesh& mesh, 
                                         UVLayout& layout, 
                                         const AtlasPackOptions& options) {
    AtlasPackResult result;

    if (layout.islands.empty()) {
        result.warnings.push_back("Nessuna isola UV da impacchettare.");
        result.success = true;
        return result;
    }

    // 1. Verifica Zero-Work Cache (BM-UV4.8)
    uint64_t fp = ComputeFingerprint(layout, options);
    if (options.enableZeroWork && m_cacheValid && m_lastFingerprint == fp) {
        result = m_cachedResult;
        result.zeroWorkCacheHit = true;
        return result;
    }

    uint32_t atlasW = options.targetWidth;
    uint32_t atlasH = options.targetHeight;
    float padding = options.paddingPixels;
    float margin = options.marginPixels;

    // 2. Calcolo proporzioni metriche di texel density (BM-UV4.4)
    float calculatedGlobalDensity = 0.0f;
    auto texelDims = TexelDensityCalculator::ComputeIslandTexelDimensions(mesh, 
                                                                         layout.islands, 
                                                                         options.densityOptions, 
                                                                         atlasW, 
                                                                         atlasH, 
                                                                         calculatedGlobalDensity);

    // 3. Preparazione dei rettangoli di ingombro con gutter padding (BM-UV4.5)
    std::vector<RectToPack> allRects;
    allRects.reserve(layout.islands.size());

    for (size_t i = 0; i < layout.islands.size(); ++i) {
        RectToPack r;
        r.islandIndex = static_cast<UVIslandID>(i);
        r.width = static_cast<int>(std::ceil(texelDims[i].x)) + static_cast<int>(std::ceil(padding * 2.0f));
        r.height = static_cast<int>(std::ceil(texelDims[i].y)) + static_cast<int>(std::ceil(padding * 2.0f));
        allRects.push_back(r);
    }

    // 4. Ordinamento deterministico delle isole (BM-UV4.2)
    // Primario: dimensione massima decrescente. Secondario: area decrescente. Tie-breaker: ID crescente
    std::vector<RectToPack*> sortedRects;
    sortedRects.reserve(allRects.size());
    for (auto& r : allRects) sortedRects.push_back(&r);

    std::sort(sortedRects.begin(), sortedRects.end(), [](const RectToPack* a, const RectToPack* b) {
        int maxDimA = std::max(a->width, a->height);
        int maxDimB = std::max(b->width, b->height);
        if (maxDimA != maxDimB) return maxDimA > maxDimB;
        int areaA = a->width * a->height;
        int areaB = b->width * b->height;
        if (areaA != areaB) return areaA > areaB;
        return a->islandIndex < b->islandIndex;
    });

    // 5. Esecuzione Skyline Bin Packing con gestione Multi-Atlas e Downscale (BM-UV4.2 / BM-UV4.6)
    int binW = static_cast<int>(atlasW) - static_cast<int>(std::ceil(margin * 2.0f));
    int binH = static_cast<int>(atlasH) - static_cast<int>(std::ceil(margin * 2.0f));

    bool packingComplete = false;
    uint32_t currentPagesUsed = 1;
    float downscaleFactor = 1.0f;

    for (int attempt = 0; attempt < 6 && !packingComplete; ++attempt) {
        currentPagesUsed = 1;
        std::vector<RectToPack*> unplaced = sortedRects;
        std::vector<RectToPack*> pageRects;

        while (!unplaced.empty() && currentPagesUsed <= options.maxAtlasPages) {
            pageRects.clear();
            // Cerca di inserire quanti piu rettangoli possibile sulla pagina corrente
            std::vector<RectToPack*> remaining;
            for (auto* r : unplaced) {
                pageRects.push_back(r);
                if (!PackSkylinePage(pageRects, binW, binH, options.allowRotation)) {
                    pageRects.pop_back(); // Non entra sulla pagina corrente
                    remaining.push_back(r);
                } else {
                    r->page = currentPagesUsed - 1;
                }
            }

            unplaced = remaining;
            if (!unplaced.empty()) {
                if (options.allowMultiAtlas && currentPagesUsed < options.maxAtlasPages) {
                    currentPagesUsed++;
                } else {
                    break;
                }
            }
        }

        if (unplaced.empty()) {
            packingComplete = true;
        } else if (options.allowDensityDownscale) {
            // Scala verso il basso le dimensioni dei rettangoli per tentare di farli entrare
            downscaleFactor *= 0.85f;
            for (auto* r : sortedRects) {
                r->width = std::max(static_cast<int>(std::ceil(r->width * 0.85f)), static_cast<int>(padding * 2.0f + 2.0f));
                r->height = std::max(static_cast<int>(std::ceil(r->height * 0.85f)), static_cast<int>(padding * 2.0f + 2.0f));
            }
        } else {
            break;
        }
    }

    if (!packingComplete) {
        result.success = false;
        result.errors.push_back("Impossibile impacchettare tutte le isole: spazio atlas esaurito (ATLAS_OVERFLOW).");
        return result;
    }

    // 6. Applicazione delle trasformazioni e aggiornamento di EditableMesh (BM-UV4.3)
    float totalUsefulArea = 0.0f;
    float invAtlasW = 1.0f / static_cast<float>(atlasW);
    float invAtlasH = 1.0f / static_cast<float>(atlasH);

    for (const auto& r : allRects) {
        auto& isl = layout.islands[r.islandIndex];

        float contentW = static_cast<float>(r.rotated ? r.height : r.width) - (padding * 2.0f);
        float contentH = static_cast<float>(r.rotated ? r.width : r.height) - (padding * 2.0f);

        float pxX = static_cast<float>(r.x) + margin + padding;
        float pxY = static_cast<float>(r.y) + margin + padding;

        float scaleU = contentW * invAtlasW;
        float scaleV = contentH * invAtlasH;
        float offsetU = pxX * invAtlasW;
        float offsetV = pxY * invAtlasH;

        isl.atlasTransform = glm::vec4(scaleU, scaleV, offsetU, offsetV);
        isl.rotationDegrees = r.rotated ? 90.0f : 0.0f;
        isl.atlasPage = static_cast<uint32_t>(r.page);

        totalUsefulArea += contentW * contentH;

        // Scrittura diretta delle coordinate atlas normalizzate nei corner DCEL di EditableMesh
        for (FaceID fId : isl.faces) {
            const auto* f = mesh.GetFace(fId);
            if (!f || !IsValid(f->halfEdge)) continue;
            HalfEdgeID h = f->halfEdge;
            HalfEdgeID startH = h;
            do {
                auto* he = mesh.GetHalfEdgeMutable(h);
                if (!he) break;

                // Calcolo trasformazione locale -> atlas
                glm::vec2 localUV = he->corner.uv;
                glm::vec2 atlasUV = LocalToAtlas(localUV, isl);
                he->corner.uv = atlasUV;

                h = he->next;
            } while (h != startH && IsValid(h));
        }
    }

    mesh.IncrementAttributeRevision();

    // 7. Calcolo statistiche finali atlas
    float totalPageArea = static_cast<float>(currentPagesUsed * atlasW * atlasH);
    layout.atlasOccupancyRatio = (totalPageArea > 0.0f) ? (totalUsefulArea / totalPageArea) : 0.0f;
    layout.atlasTargetWidth = atlasW;
    layout.atlasTargetHeight = atlasH;
    layout.totalAtlasPages = currentPagesUsed;
    layout.paddingPixels = padding;
    layout.globalTexelDensity = calculatedGlobalDensity * downscaleFactor;
    layout.atlasRevision++;
    layout.meshAttributeRevision = mesh.GetAttributeRevision();

    // 8. Validazione layout e overlap (BM-UV4.7)
    std::vector<std::string> valErrors;
    bool isValid = ValidateAtlasLayout(layout, padding, valErrors);
    if (!isValid) {
        result.overlapsDetected = true;
        result.errors = valErrors;
    }

    result.success = isValid;
    result.pagesUsed = currentPagesUsed;
    result.occupancyRatio = layout.atlasOccupancyRatio;
    result.globalTexelDensity = layout.globalTexelDensity;

    // Aggiornamento cache Zero-Work
    m_lastFingerprint = fp;
    m_cachedResult = result;
    m_cacheValid = true;

    return result;
}

} // namespace fw
