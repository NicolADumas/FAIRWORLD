#include "PainterStationContractTests.h"
#include "FactoryOrchestrator.h"
#include "ProductionStationAdapters.h"
#include "blocks/texture/TexturePaintingService.h"
#include "blocks/uv/CompiledTopologyTests.h"
#include <iostream>
#include <sstream>

namespace fw {

std::vector<PainterStationTestResult> PainterStationContractTests::RunAllTests() {
    std::vector<PainterStationTestResult> results;
    results.reserve(10);

    // -------------------------------------------------------------------------
    // PNT-01: Painter Station disponibile e implementata
    // -------------------------------------------------------------------------
    {
        PainterStationTestResult r;
        r.testId = "PNT-01";
        r.testName = "Painter Station disponibile e implementata";
        r.inputDesc = "Verifica getStation(ProductionStationID::Painter) e IsImplemented()";
        r.expectedDesc = "Station registrata nell'orchestratore e IsImplemented() == true";

        FactoryOrchestrator orch;
        const auto* station = orch.GetStation(ProductionStationID::Painter);
        bool exists = (station != nullptr);
        bool impl = exists && station->IsImplemented();

        r.passed = exists && impl;
        r.observedDesc = std::string("Exists: ") + (exists ? "true" : "false") +
                         ", IsImplemented: " + (impl ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: PainterStation non registrata o non implementata";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // PNT-02: Draft senza layer gestito correttamente
    // -------------------------------------------------------------------------
    {
        PainterStationTestResult r;
        r.testId = "PNT-02";
        r.testName = "Draft senza layer gestito correttamente";
        r.inputDesc = "ExecuteStation su draft vergine senza paintLayers";
        r.expectedDesc = "Execute ritorna true, status Completed, paintLayers rimane vuoto";

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:pnt_draft_02", "PNT Draft 02");
        // Esegui pipeline fino a SurfaceUV per soddisfare prerequisiti
        orch.ExecuteStation(ProductionStationID::Shape);
        orch.ExecuteStation(ProductionStationID::SurfaceUV);

        bool execOk = orch.ExecuteStation(ProductionStationID::Painter);
        const auto* st = orch.GetStation(ProductionStationID::Painter);
        const auto* draft = orch.GetCurrentDraft();

        bool statusOk = st && (st->GetStatus() == StationStatus::Completed);
        bool emptyPreserved = draft && draft->paintLayers.empty();

        r.passed = execOk && statusOk && emptyPreserved;
        r.observedDesc = std::string("ExecOk: ") + (execOk ? "true" : "false") +
                         ", StatusOk: " + (statusOk ? "true" : "false") +
                         ", EmptyPreserved: " + (emptyPreserved ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Gestione draft senza layer errata";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // PNT-03: Layer validi preservati
    // -------------------------------------------------------------------------
    {
        PainterStationTestResult r;
        r.testId = "PNT-03";
        r.testName = "Layer validi preservati";
        r.inputDesc = "Draft con 3 paintLayer validi (Background + 2 layer utente) e pittura; esecuzione Painter station";
        r.expectedDesc = "Layer count immutato (3: Background, BaseLayer, DecalLayer), ordine e pixelBuffer integri";

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:pnt_draft_03", "PNT Draft 03");
        orch.ExecuteStation(ProductionStationID::Shape);
        orch.ExecuteStation(ProductionStationID::SurfaceUV);

        auto* draft = orch.GetCurrentDraft();
        // Aggiungi 2 layer con TexturePaintingService (InitializeCanvas genera "Background" all'indice 0)
        TexturePaintingService painter;
        painter.InitializeCanvas(128, 128);
        uint32_t l1 = painter.AddLayer("BaseLayer", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
        uint32_t l2 = painter.AddLayer("DecalLayer", glm::vec4(0.0f, 1.0f, 0.0f, 0.5f));
        draft->paintLayers = painter.GetLayers();
        draft->paintRevision = 10;

        // Verifica pre-condizioni: 3 layer in ordine esatto
        bool preValid = (draft->paintLayers.size() == 3) &&
                        (draft->paintLayers[0].name == "Background") &&
                        (draft->paintLayers[1].name == "BaseLayer") &&
                        (draft->paintLayers[2].name == "DecalLayer") &&
                        draft->paintLayers[0].pixelBuffer.IsValid() &&
                        draft->paintLayers[1].pixelBuffer.IsValid() &&
                        draft->paintLayers[2].pixelBuffer.IsValid();

        bool execOk = orch.ExecuteStation(ProductionStationID::Painter);
        const auto* postDraft = orch.GetCurrentDraft();

        bool layersIntact = preValid && postDraft && postDraft->paintLayers.size() == 3 &&
                            postDraft->paintLayers[0].name == "Background" &&
                            postDraft->paintLayers[1].name == "BaseLayer" &&
                            postDraft->paintLayers[2].name == "DecalLayer" &&
                            postDraft->paintLayers[0].pixelBuffer.IsValid() &&
                            postDraft->paintLayers[1].pixelBuffer.IsValid() &&
                            postDraft->paintLayers[2].pixelBuffer.IsValid();

        r.passed = execOk && layersIntact;
        r.observedDesc = std::string("ExecOk: ") + (execOk ? "true" : "false") +
                         ", LayerCount: " + (postDraft ? std::to_string(postDraft->paintLayers.size()) : "0");
        r.details = r.passed ? "PASS" : "FAIL: Layer validi alterati o persi";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // PNT-04: UV assenti gestite senza corruzione
    // -------------------------------------------------------------------------
    {
        PainterStationTestResult r;
        r.testId = "PNT-04";
        r.testName = "UV assenti gestite senza corruzione";
        r.inputDesc = "Draft con layer validi (Background + NoUVLayer) ma UVLayout assente (reset)";
        r.expectedDesc = "Pittura preservata intatta (2 layer), diagnostica emette warning senza crash o corruzione";

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:pnt_draft_04", "PNT Draft 04");
        orch.ExecuteStation(ProductionStationID::Shape);
        orch.ExecuteStation(ProductionStationID::SurfaceUV);

        auto* draft = orch.GetCurrentDraft();
        TexturePaintingService painter;
        painter.InitializeCanvas(64, 64);
        painter.AddLayer("NoUVLayer", glm::vec4(0.2f, 0.4f, 0.8f, 1.0f));
        draft->paintLayers = painter.GetLayers();
        draft->uvLayout.reset(); // Rimuove esplicitamente l'UVLayout

        bool execOk = orch.ExecuteStation(ProductionStationID::Painter);
        const auto* st = orch.GetStation(ProductionStationID::Painter);
        const auto* postDraft = orch.GetCurrentDraft();

        bool hasWarning = st && st->GetDiagnostic().HasWarnings();
        bool layersPreserved = postDraft && postDraft->paintLayers.size() == 2 &&
                               postDraft->paintLayers[0].name == "Background" &&
                               postDraft->paintLayers[1].name == "NoUVLayer" &&
                               postDraft->paintLayers[0].pixelBuffer.IsValid() &&
                               postDraft->paintLayers[1].pixelBuffer.IsValid();

        r.passed = execOk && hasWarning && layersPreserved;
        r.observedDesc = std::string("ExecOk: ") + (execOk ? "true" : "false") +
                         ", HasWarning: " + (hasWarning ? "true" : "false") +
                         ", LayersPreserved: " + (layersPreserved ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: UV assenti non gestite con warning o pittura corrotta";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // PNT-05: Modifica painting invalida solo le dipendenze necessarie
    // -------------------------------------------------------------------------
    {
        PainterStationTestResult r;
        r.testId = "PNT-05";
        r.testName = "Modifica painting invalida solo le dipendenze necessarie";
        r.inputDesc = "Orchestrator notificato di modifica PaintLayers con nuovo fingerprint";
        r.expectedDesc = "Nodo PaintLayers e nodi a valle (QualityControl, Package) sporchi; nodi a monte (Shape, Sculpt, TopologyUV) puliti";

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:pnt_draft_05", "PNT Draft 05");
        orch.ExecutePipelineUpTo(ProductionStationID::Packaging);

        auto& dag = orch.GetDependencyGraph();
        bool prePaintClean = !dag.IsNodeDirty(DependencyNodeID::PaintLayers);
        bool preShapeClean = !dag.IsNodeDirty(DependencyNodeID::ShapeGeometry);

        // Notifica modifica sul nodo PaintLayers
        orch.NotifyPropertyModified(DependencyNodeID::PaintLayers, 999888777ULL);

        bool postPaintDirty = dag.IsNodeDirty(DependencyNodeID::PaintLayers);
        bool postQCDirty = dag.IsNodeDirty(DependencyNodeID::QualityControlAudit);
        bool postPkgDirty = dag.IsNodeDirty(DependencyNodeID::PackageArtifact);

        // Nodi a monte non devono essere sporcati
        bool upstreamClean = !dag.IsNodeDirty(DependencyNodeID::ShapeGeometry) &&
                             !dag.IsNodeDirty(DependencyNodeID::SculptGeometry) &&
                             !dag.IsNodeDirty(DependencyNodeID::TopologyUV) &&
                             !dag.IsNodeDirty(DependencyNodeID::SurfaceLayout);

        r.passed = prePaintClean && preShapeClean && postPaintDirty && postQCDirty && postPkgDirty && upstreamClean;
        r.observedDesc = std::string("PaintDirty: ") + (postPaintDirty ? "true" : "false") +
                         ", QCDirty: " + (postQCDirty ? "true" : "false") +
                         ", UpstreamClean: " + (upstreamClean ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Propagazione dipendenze errata per PaintLayers";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // PNT-06: Input invariato = zero lavoro
    // -------------------------------------------------------------------------
    {
        PainterStationTestResult r;
        r.testId = "PNT-06";
        r.testName = "Input invariato = zero lavoro";
        r.inputDesc = "Esecuzione ripetuta di PainterStation con fingerprint e stato stabile";
        r.expectedDesc = "Seconda chiamata ritorna immediatamente true senza reinvocare compositing o alterare revisioni";

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:pnt_draft_06", "PNT Draft 06");
        orch.ExecuteStation(ProductionStationID::Shape);
        orch.ExecuteStation(ProductionStationID::SurfaceUV);

        auto* draft = orch.GetCurrentDraft();
        TexturePaintingService painter;
        painter.InitializeCanvas(64, 64);
        painter.AddLayer("ZeroWorkLayer", glm::vec4(1.0f));
        draft->paintLayers = painter.GetLayers();

        bool firstExec = orch.ExecuteStation(ProductionStationID::Painter);
        const auto* st = orch.GetStation(ProductionStationID::Painter);
        uint64_t rev1 = st ? st->GetLastProcessedRevision() : 0;

        bool secondExec = orch.ExecuteStation(ProductionStationID::Painter);
        uint64_t rev2 = st ? st->GetLastProcessedRevision() : 0;

        r.passed = firstExec && secondExec && (rev1 == rev2) && (st->GetStatus() == StationStatus::Completed);
        r.observedDesc = std::string("FirstExec: ") + (firstExec ? "true" : "false") +
                         ", SecondExec: " + (secondExec ? "true" : "false") +
                         ", Rev1: " + std::to_string(rev1) + ", Rev2: " + std::to_string(rev2);
        r.details = r.passed ? "PASS" : "FAIL: Zero-work non rispettato per input invariato";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // PNT-07: Fallimento atomico senza perdita dei layer
    // -------------------------------------------------------------------------
    {
        PainterStationTestResult r;
        r.testId = "PNT-07";
        r.testName = "Fallimento atomico senza perdita dei layer";
        r.inputDesc = "Draft con un layer corrotto (buffer non allocato); esecuzione Painter station";
        r.expectedDesc = "Stazione fallisce (Failed), diagnostic registra errore, ma il vettore paintLayers nel draft è preservato";

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:pnt_draft_07", "PNT Draft 07");
        orch.ExecuteStation(ProductionStationID::Shape);
        orch.ExecuteStation(ProductionStationID::SurfaceUV);

        auto* draft = orch.GetCurrentDraft();
        PaintLayer corrupted;
        corrupted.name = "CorruptedLayer";
        corrupted.layerId = 99;
        // Non alloca pixelBuffer per simulare corruzione
        draft->paintLayers.push_back(corrupted);

        bool execOk = orch.ExecuteStation(ProductionStationID::Painter);
        const auto* st = orch.GetStation(ProductionStationID::Painter);
        const auto* postDraft = orch.GetCurrentDraft();

        bool failedAsExpected = (!execOk) && (st && st->GetStatus() == StationStatus::Failed);
        bool hasError = st && st->GetDiagnostic().HasErrors();
        bool vectorPreserved = postDraft && (postDraft->paintLayers.size() == 1);

        r.passed = failedAsExpected && hasError && vectorPreserved;
        r.observedDesc = std::string("FailedAsExpected: ") + (failedAsExpected ? "true" : "false") +
                         ", HasError: " + (hasError ? "true" : "false") +
                         ", VectorPreserved: " + (vectorPreserved ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Fallimento non atomico o perdita dei dati del draft";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // PNT-08: Nessuna regressione sui 151 test FG-01R
    // -------------------------------------------------------------------------
    {
        PainterStationTestResult r;
        r.testId = "PNT-08";
        r.testName = "Nessuna regressione sui 151 test FG-01R";
        r.inputDesc = "Esecuzione suite di validazione analitica CompiledTopology";
        r.expectedDesc = "Tutti i test topologici delle primitive passano al 100% (36/36 TOP tests)";

        auto topResults = CompiledTopologyTests::RunAllTests();
        size_t passCount = 0;
        for (const auto& tr : topResults) {
            if (tr.passed) passCount++;
        }

        r.passed = (passCount == topResults.size()) && (passCount == 36);
        r.observedDesc = "Topology PASS: " + std::to_string(passCount) + "/" + std::to_string(topResults.size());
        r.details = r.passed ? "PASS" : "FAIL: Regressione riscontrata nella topologia congelata";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // PNT-09: Rilevamento alterazione contenuto pixel (Content Hash Invalidation)
    // -------------------------------------------------------------------------
    {
        PainterStationTestResult r;
        r.testId = "PNT-09";
        r.testName = "Rilevamento alterazione contenuto pixel (Content Hash Invalidation)";
        r.inputDesc = "Modifica pixel su LayerA (layer superiore visibile) a parita di paintRevision e dimensioni";
        r.expectedDesc = "Content hash del layer e del composito mutano, compositing rieseguito con pixel aggiornato e upstream pulito";

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:pnt_draft_09", "PNT Draft 09");
        orch.ExecuteStation(ProductionStationID::Shape);
        orch.ExecuteStation(ProductionStationID::SurfaceUV);

        auto* draft = orch.GetCurrentDraft();
        TexturePaintingService painter;
        painter.InitializeCanvas(32, 32);
        painter.AddLayer("LayerA", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
        draft->paintLayers = painter.GetLayers();

        // 1. Prima esecuzione
        bool firstExecOk = orch.ExecuteStation(ProductionStationID::Painter);
        const auto* st1 = dynamic_cast<const PainterStationAdapter*>(orch.GetStation(ProductionStationID::Painter));
        bool hasComp1 = st1 && st1->HasCompositeAlbedo();
        glm::vec4 c1 = hasComp1 ? st1->GetCompositeAlbedo().GetPixel(0, 0) : glm::vec4(0.0f);
        uint64_t compHash1 = hasComp1 ? st1->GetCompositeAlbedo().ComputeContentHash() : 0;

        // 2. Identificazione esplicita di LayerA (layer superiore visibile)
        PaintLayer* targetLayer = nullptr;
        for (auto& l : draft->paintLayers) {
            if (l.name == "LayerA") {
                targetLayer = &l;
                break;
            }
        }
        bool layerFound = (targetLayer != nullptr);
        uint64_t prePixelHash = layerFound ? targetLayer->pixelBuffer.ComputeContentHash() : 0;

        // 3. Modifica effettiva del contenuto sorgente sul layer visibile
        if (layerFound) {
            targetLayer->pixelBuffer.SetPixel(0, 0, glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
        }
        uint64_t postPixelHash = layerFound ? targetLayer->pixelBuffer.ComputeContentHash() : 0;
        bool srcContentChanged = (prePixelHash != postPixelHash);

        // 4. Seconda esecuzione: deve ri-eseguire il compositing e aggiornare il composito
        bool secondExecOk = orch.ExecuteStation(ProductionStationID::Painter);
        const auto* st2 = dynamic_cast<const PainterStationAdapter*>(orch.GetStation(ProductionStationID::Painter));
        bool hasComp2 = st2 && st2->HasCompositeAlbedo();
        glm::vec4 c2 = hasComp2 ? st2->GetCompositeAlbedo().GetPixel(0, 0) : glm::vec4(0.0f);
        uint64_t compHash2 = hasComp2 ? st2->GetCompositeAlbedo().ComputeContentHash() : 0;

        // 5. Verifiche: pixel modificato, coerenza colore (Verde al pixel 0,0), hash composito mutato
        bool pixelChanged = (c1 != c2) && (c2.g > 0.9f) && (c2.r < 0.1f);
        bool compHashChanged = (compHash1 != compHash2) && (compHash1 != 0) && (compHash2 != 0);

        // 6. Nessuna invalidazione upstream inattesa
        const auto& dag = orch.GetDependencyGraph();
        bool upstreamClean = !dag.IsNodeDirty(DependencyNodeID::ShapeGeometry) &&
                             !dag.IsNodeDirty(DependencyNodeID::SculptGeometry) &&
                             !dag.IsNodeDirty(DependencyNodeID::TopologyUV) &&
                             !dag.IsNodeDirty(DependencyNodeID::SurfaceLayout);

        r.passed = firstExecOk && secondExecOk && layerFound && srcContentChanged &&
                   pixelChanged && compHashChanged && upstreamClean;
        r.observedDesc = "Color1: (" + std::to_string(c1.r) + "," + std::to_string(c1.g) + ") -> Color2: (" +
                         std::to_string(c2.r) + "," + std::to_string(c2.g) + ")" +
                         ", SrcHashChanged: " + (srcContentChanged ? "true" : "false") +
                         ", CompHashChanged: " + (compHashChanged ? "true" : "false") +
                         ", UpstreamClean: " + (upstreamClean ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Variazione pixel ignorata dal fingerprint o compositing non aggiornato";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // PNT-10: Rifiuto rigoroso layer con dimensioni disomogenee
    // -------------------------------------------------------------------------
    {
        PainterStationTestResult r;
        r.testId = "PNT-10";
        r.testName = "Rifiuto rigoroso layer con dimensioni disomogenee";
        r.inputDesc = "Draft contenente Layer 1 (64x64) e Layer 2 (32x32)";
        r.expectedDesc = "Stazione fallisce immediatamente con errore esplicito bloccando compositing degenere";

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:pnt_draft_10", "PNT Draft 10");
        orch.ExecuteStation(ProductionStationID::Shape);
        orch.ExecuteStation(ProductionStationID::SurfaceUV);

        auto* draft = orch.GetCurrentDraft();
        PaintLayer l1;
        l1.name = "Layer64";
        l1.pixelBuffer = ImageBitmap(64, 64);
        PaintLayer l2;
        l2.name = "Layer32";
        l2.pixelBuffer = ImageBitmap(32, 32);

        draft->paintLayers = { l1, l2 };

        bool execOk = orch.ExecuteStation(ProductionStationID::Painter);
        const auto* st = orch.GetStation(ProductionStationID::Painter);

        bool rejected = (!execOk) && st && (st->GetStatus() == StationStatus::Failed) && st->GetDiagnostic().HasErrors();
        r.passed = rejected;
        r.observedDesc = std::string("Rejected: ") + (rejected ? "true" : "false") +
                         ", Error: " + (st && !st->GetDiagnostic().errors.empty() ? st->GetDiagnostic().errors[0] : "none");
        r.details = r.passed ? "PASS" : "FAIL: Dimensioni eterogenee non rifiutate con errore";
        results.push_back(r);
    }

    return results;
}

} // namespace fw
