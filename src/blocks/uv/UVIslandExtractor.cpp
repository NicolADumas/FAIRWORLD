#include "pch.h"
#include "UVIslandExtractor.h"
#include <queue>
#include <algorithm>

namespace fw {

std::vector<UVIsland> UVIslandExtractor::ExtractIslands(const EditableMesh& mesh, 
                                                        const SeamGraph& seamGraph, 
                                                        std::unordered_map<FaceID, UVIslandID>& outFaceToIslandMap, 
                                                        IslandExtractionReport& outReport) {
    outReport = IslandExtractionReport{};
    outFaceToIslandMap.clear();

    std::vector<UVIsland> islands;
    const auto& activeFaces = mesh.GetActiveFaceIDs();
    if (activeFaces.empty()) {
        outReport.warnings.push_back("Nessuna faccia attiva trovata nella mesh.");
        return islands;
    }

    std::unordered_set<FaceID> visitedFaces;
    visitedFaces.reserve(activeFaces.size());

    for (FaceID fId : activeFaces) {
        if (visitedFaces.count(fId)) continue;

        UVIsland island;
        island.id = static_cast<UVIslandID>(islands.size());

        const auto* startFace = mesh.GetFace(fId);
        if (startFace) {
            island.semanticSurfaceId = startFace->surfaceId;
            island.semanticName = "Island_" + std::to_string(island.id) + "_Surf" + std::to_string(startFace->surfaceId);
        }

        std::queue<FaceID> queue;
        queue.push(fId);
        visitedFaces.insert(fId);

        std::unordered_set<EdgeID> seamEdgeSet;

        while (!queue.empty()) {
            FaceID curF = queue.front();
            queue.pop();

            island.faces.push_back(curF);
            outFaceToIslandMap[curF] = island.id;

            const auto* face = mesh.GetFace(curF);
            if (!face || !IsValid(face->halfEdge)) continue;

            island.surfaceArea3D += face->area;

            HalfEdgeID startHe = face->halfEdge;
            HalfEdgeID curHe = startHe;
            do {
                const auto* he = mesh.GetHalfEdge(curHe);
                if (!he) break;

                EdgeID eId = he->edge;
                bool isCut = seamGraph.IsSeamOrBoundary(eId);

                if (isCut) {
                    island.boundaryHalfEdges.push_back(curHe);
                    seamEdgeSet.insert(eId);
                } else if (IsValid(he->twin)) {
                    const auto* twinHe = mesh.GetHalfEdge(he->twin);
                    if (twinHe && IsValid(twinHe->face)) {
                        FaceID neighborFace = twinHe->face;
                        if (visitedFaces.find(neighborFace) == visitedFaces.end()) {
                            visitedFaces.insert(neighborFace);
                            queue.push(neighborFace);
                        }
                    } else {
                        // Confine geometrico aperto
                        island.boundaryHalfEdges.push_back(curHe);
                    }
                } else {
                    // Confine senza gemello
                    island.boundaryHalfEdges.push_back(curHe);
                }

                curHe = he->next;
            } while (curHe != startHe && IsValid(curHe));
        }

        island.seamEdges.assign(seamEdgeSet.begin(), seamEdgeSet.end());

        // Validazione topologica dell'isola
        int chi = 0;
        size_t bndLoops = 0;
        bool validTopology = ValidateIslandTopology(mesh, island, seamGraph, chi, bndLoops);

        if (bndLoops == 0) {
            outReport.closedIslandsDetected++;
            outReport.warnings.push_back("Isola #" + std::to_string(island.id) + 
                " e topologicamente chiusa (0 bordi). Richiede seam di taglio per proiezione planare.");
        } else if (bndLoops > 1) {
            outReport.multiBoundaryIslandsDetected++;
            outReport.warnings.push_back("Isola #" + std::to_string(island.id) + 
                " possiede bordi multipli / fori interni (" + std::to_string(bndLoops) + " cicli).");
        }

        islands.push_back(std::move(island));
    }

    outReport.totalFacesProcessed = static_cast<uint32_t>(visitedFaces.size());
    outReport.totalIslandsExtracted = static_cast<uint32_t>(islands.size());
    outReport.success = (outReport.errors.empty());

    return islands;
}

bool UVIslandExtractor::ValidateIslandTopology(const EditableMesh& mesh, 
                                              const UVIsland& island, 
                                              const SeamGraph& seamGraph, 
                                              int& outEulerCharacteristic, 
                                              size_t& outBoundaryLoopCount) {
    std::unordered_set<VertexID> islandVertices;
    std::unordered_set<EdgeID> islandEdges;

    for (FaceID fId : island.faces) {
        const auto* face = mesh.GetFace(fId);
        if (!face || !IsValid(face->halfEdge)) continue;

        HalfEdgeID startHe = face->halfEdge;
        HalfEdgeID curHe = startHe;
        do {
            const auto* he = mesh.GetHalfEdge(curHe);
            if (!he) break;
            islandVertices.insert(he->origin);
            islandEdges.insert(he->edge);
            curHe = he->next;
        } while (curHe != startHe && IsValid(curHe));
    }

    int V = static_cast<int>(islandVertices.size());
    int E = static_cast<int>(islandEdges.size());
    int F = static_cast<int>(island.faces.size());

    // Eulero = V - E + F
    outEulerCharacteristic = V - E + F;

    // Rileva numero cicli di bordo orientati
    std::unordered_set<HalfEdgeID> remainingBnd(island.boundaryHalfEdges.begin(), island.boundaryHalfEdges.end());
    outBoundaryLoopCount = 0;

    while (!remainingBnd.empty()) {
        HalfEdgeID start = *remainingBnd.begin();
        remainingBnd.erase(start);
        outBoundaryLoopCount++;

        const auto* startHe = mesh.GetHalfEdge(start);
        if (!startHe) continue;

        // Cerca di camminare lungo il confine dell'isola
        VertexID targetV = INVALID_VERTEX_ID;
        if (IsValid(startHe->next)) {
            const auto* nextHe = mesh.GetHalfEdge(startHe->next);
            if (nextHe) targetV = nextHe->origin;
        }

        // Segui i boundary half-edges connessi
        size_t safetySteps = 0;
        const size_t maxSteps = remainingBnd.size() + 10;
        while (IsValid(targetV) && safetySteps++ < maxSteps) {
            HalfEdgeID foundNext = INVALID_HALFEDGE_ID;
            for (HalfEdgeID cand : remainingBnd) {
                const auto* candHe = mesh.GetHalfEdge(cand);
                if (candHe && candHe->origin == targetV) {
                    foundNext = cand;
                    break;
                }
            }

            if (IsValid(foundNext)) {
                remainingBnd.erase(foundNext);
                const auto* candHe = mesh.GetHalfEdge(foundNext);
                if (candHe && IsValid(candHe->next)) {
                    const auto* nxt = mesh.GetHalfEdge(candHe->next);
                    targetV = nxt ? nxt->origin : INVALID_VERTEX_ID;
                } else {
                    break;
                }
            } else {
                break;
            }
        }
    }

    return true;
}

} // namespace fw
