#include "PaintContractTests.h"
#include "TexturePaintingService.h"
#include <iostream>
#include <sstream>
#include <cmath>

namespace fw {

std::vector<PaintTestResult> PaintContractTests::RunContractTests() {
    std::vector<PaintTestResult> results;
    results.reserve(10);

    // =========================================================================
    // PT-1: Creazione e Cancellazione Layer
    // =========================================================================
    {
        PaintTestResult res;
        res.testId = "PT-1";
        res.testName = "PT-1 — Creazione e Cancellazione Layer";
        res.inputDesc = "Init canvas 64x64, aggiunta 2 layer, rimozione 1 layer intermedio, divieto rimozione ultimo layer";
        res.expectedDesc = "Layer count 1->3->2, ID stabili, rimozione ultimo layer fallisce";

        TexturePaintingService service;
        service.InitializeCanvas(64, 64, glm::vec4(0.0f));

        uint32_t id1 = service.GetLayers()[0].layerId;
        uint32_t id2 = service.AddLayer("Decals");
        uint32_t id3 = service.AddLayer("Details");
        size_t count3 = service.GetLayerCount();

        bool removeOk = service.RemoveLayer(id2);
        size_t count2 = service.GetLayerCount();
        const PaintLayer* remaining3 = service.GetLayer(id3);

        // Tentativo rimozione di tutti i layer
        service.RemoveLayer(id3);
        bool removeLastOk = service.RemoveLayer(id1); // Fallisce
        size_t finalCount = service.GetLayerCount();

        bool passed = (count3 == 3) && removeOk && (count2 == 2) && 
                      (remaining3 != nullptr && remaining3->layerId == id3) &&
                      (!removeLastOk) && (finalCount == 1);

        res.passed = passed;
        res.observedDesc = "Count3=" + std::to_string(count3) + ", RemoveMid=" + (removeOk ? "OK" : "ERR") + 
                           ", Count2=" + std::to_string(count2) + ", LastRemoveBlocked=" + (!removeLastOk ? "OK" : "ERR");
        res.details = passed ? "PASS: Ciclo di vita dei layer conforme e isolato" : "FAIL: Anomalia gestione layer";
        results.push_back(res);
    }

    // =========================================================================
    // PT-2: Ordinamento Layer e Duplicazione
    // =========================================================================
    {
        PaintTestResult res;
        res.testId = "PT-2";
        res.testName = "PT-2 — Ordinamento Layer e Duplicazione";
        res.inputDesc = "Riordinamento layer con MoveLayerUp/Down e duplicazione con buffer pixel indipendente";
        res.expectedDesc = "Ordine invertito, duplicato con nuovo ID univoco e pixel indipendenti";

        TexturePaintingService service;
        service.InitializeCanvas(64, 64, glm::vec4(0.0f));
        uint32_t l1 = service.GetLayers()[0].layerId;
        uint32_t l2 = service.AddLayer("Overlay");

        // Sposta l1 sopra l2
        bool movedUp = service.MoveLayerUp(l1);
        bool isL1Top = (service.GetLayers()[1].layerId == l1);

        // Duplica l2
        uint32_t dupId = service.DuplicateLayer(l2);
        PaintLayer* dupLayer = service.GetLayer(dupId);
        PaintLayer* origLayer = service.GetLayer(l2);

        bool deepCopyOk = false;
        if (dupLayer && origLayer) {
            dupLayer->pixelBuffer.SetPixel(10, 10, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
            glm::vec4 origPx = origLayer->pixelBuffer.GetPixel(10, 10);
            deepCopyOk = (origPx.r == 0.0f); // Non alterato
        }

        bool passed = movedUp && isL1Top && (dupId > 0) && deepCopyOk;
        res.passed = passed;
        res.observedDesc = "MovedUp=" + std::string(movedUp ? "OK" : "ERR") + 
                           ", L1Top=" + (isL1Top ? "YES" : "NO") + 
                           ", DupID=" + std::to_string(dupId) + 
                           ", DeepCopy=" + (deepCopyOk ? "OK" : "FAIL");
        res.details = passed ? "PASS: Reorder e Clone profondi deterministici" : "FAIL: Dipendenza buffer pixel clonati";
        results.push_back(res);
    }

    // =========================================================================
    // PT-3: Visibilità e Opacità
    // =========================================================================
    {
        PaintTestResult res;
        res.testId = "PT-3";
        res.testName = "PT-3 — Visibilità e Opacità";
        res.inputDesc = "Compositing con layer invisibile e layer con opacità 50%";
        res.expectedDesc = "Layer invisibile ignorato, opacità 0.5 produce mix lineare esatto (0.5, 0.5, 0.5)";

        TexturePaintingService service;
        service.InitializeCanvas(16, 16, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)); // Sfondo nero
        uint32_t whiteLayerId = service.AddLayer("White", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)); // Livello bianco
        PaintLayer* wl = service.GetLayer(whiteLayerId);

        // 1. Invisibile
        wl->isVisible = false;
        ImageBitmap compInvisible;
        service.CompositeLayers(compInvisible);
        glm::vec4 pxInv = compInvisible.GetPixel(8, 8);

        // 2. Visibile con opacità 50%
        wl->isVisible = true;
        wl->opacity = 0.5f;
        ImageBitmap compHalf;
        service.CompositeLayers(compHalf);
        glm::vec4 pxHalf = compHalf.GetPixel(8, 8);

        bool invOk = (pxInv.r < 1e-3f && pxInv.g < 1e-3f && pxInv.b < 1e-3f);
        bool halfOk = (std::abs(pxHalf.r - 0.5f) < 0.02f);

        bool passed = invOk && halfOk;
        res.passed = passed;
        res.observedDesc = "InvisibleCol=(" + std::to_string(pxInv.r) + "), HalfCol=(" + std::to_string(pxHalf.r) + ")";
        res.details = passed ? "PASS: Modulazione visibilità e opacità lineare conforme" : "FAIL: Compositing errato";
        results.push_back(res);
    }

    // =========================================================================
    // PT-4: Blend Mode Normal (Porter-Duff Over)
    // =========================================================================
    {
        PaintTestResult res;
        res.testId = "PT-4";
        res.testName = "PT-4 — Blend Mode Normal (Porter-Duff Over)";
        res.inputDesc = "Sorgente Rosso (1,0,0,1) su Destinazione Verde (0,1,0,1) e semitrasparente (1,0,0,0.5)";
        res.expectedDesc = "Opaco: puro Rosso. Semitrasparente: (0.5, 0.5, 0, 1.0)";

        glm::vec4 red(1.0f, 0.0f, 0.0f, 1.0f);
        glm::vec4 green(0.0f, 1.0f, 0.0f, 1.0f);

        glm::vec4 fullOver = TexturePaintingService::BlendPixelsLinear(red, 1.0f, green, 1.0f, LayerBlendMode::Normal);
        glm::vec4 halfOver = TexturePaintingService::BlendPixelsLinear(red, 0.5f, green, 1.0f, LayerBlendMode::Normal);

        bool fullOk = (std::abs(fullOver.r - 1.0f) < 1e-4f && std::abs(fullOver.g - 0.0f) < 1e-4f);
        bool halfOk = (std::abs(halfOver.r - 0.5f) < 1e-4f && std::abs(halfOver.g - 0.5f) < 1e-4f && std::abs(halfOver.a - 1.0f) < 1e-4f);

        bool passed = fullOk && halfOk;
        res.passed = passed;
        res.observedDesc = "FullOver=(" + std::to_string(fullOver.r) + "," + std::to_string(fullOver.g) + 
                           "), HalfOver=(" + std::to_string(halfOver.r) + "," + std::to_string(halfOver.g) + ")";
        res.details = passed ? "PASS: Equazione Porter-Duff Over rigorosamente verificata" : "FAIL: Formula Normal non conforme";
        results.push_back(res);
    }

    // =========================================================================
    // PT-5: Blend Modes Multiply, Add, Screen, Overlay
    // =========================================================================
    {
        PaintTestResult res;
        res.testId = "PT-5";
        res.testName = "PT-5 — Blend Modes Multiply / Add / Screen / Overlay";
        res.inputDesc = "Verifica formule in spazio lineare per Cs=0.4, Cd=0.6";
        res.expectedDesc = "Mul=0.24, Add=1.00, Screen=0.76, Overlay(Cd<0.5)=0.20";

        glm::vec4 src(0.4f, 0.4f, 0.4f, 1.0f);
        glm::vec4 dst(0.6f, 0.6f, 0.6f, 1.0f);

        glm::vec4 mul = TexturePaintingService::BlendPixelsLinear(src, 1.0f, dst, 1.0f, LayerBlendMode::Multiply);
        glm::vec4 add = TexturePaintingService::BlendPixelsLinear(src, 1.0f, dst, 1.0f, LayerBlendMode::Additive);
        glm::vec4 scr = TexturePaintingService::BlendPixelsLinear(src, 1.0f, dst, 1.0f, LayerBlendMode::Screen);

        // Overlay per Cd < 0.5
        glm::vec4 dstDark(0.25f, 0.25f, 0.25f, 1.0f);
        glm::vec4 ovlDark = TexturePaintingService::BlendPixelsLinear(src, 1.0f, dstDark, 1.0f, LayerBlendMode::Overlay);

        bool mulOk = (std::abs(mul.r - 0.24f) < 1e-4f);
        bool addOk = (std::abs(add.r - 1.00f) < 1e-4f);
        bool scrOk = (std::abs(scr.r - 0.76f) < 1e-4f);
        bool ovlOk = (std::abs(ovlDark.r - 0.20f) < 1e-4f);

        bool passed = mulOk && addOk && scrOk && ovlOk;
        res.passed = passed;
        res.observedDesc = "Mul=" + std::to_string(mul.r) + ", Add=" + std::to_string(add.r) + 
                           ", Scr=" + std::to_string(scr.r) + ", Ovl=" + std::to_string(ovlDark.r);
        res.details = passed ? "PASS: Formule matematiche PBR in spazio lineare verificate" : "FAIL: Discrepanza formule blend";
        results.push_back(res);
    }

    // =========================================================================
    // PT-6: Applicazione Maschera R8
    // =========================================================================
    {
        PaintTestResult res;
        res.testId = "PT-6";
        res.testName = "PT-6 — Applicazione Maschera R8";
        res.inputDesc = "Maschera con pixel a 0.0 (trasparente) e 0.5 (semitrasparente); verifica non distruttiva";
        res.expectedDesc = "Buffer pixel originale inalterato, compositing modulato da maschera";

        TexturePaintingService service;
        service.InitializeCanvas(16, 16, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)); // Sfondo nero
        uint32_t redLayerId = service.AddLayer("Red", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)); // Rosso pieno
        service.AddMaskToLayer(redLayerId, 1.0f);

        PaintLayer* rl = service.GetLayer(redLayerId);
        rl->mask->SetNormalizedValue(4, 4, 0.0f); // Completamente trasparente
        rl->mask->SetNormalizedValue(8, 8, 0.5f); // Semitrasparente

        // Verifica che il buffer pixel del layer contenga ancora rosso pieno
        glm::vec4 rawColor = rl->pixelBuffer.GetPixel(4, 4);
        bool nonDestructive = (rawColor.r == 1.0f && rawColor.a == 1.0f);

        // Compositing
        ImageBitmap comp;
        service.CompositeLayers(comp);
        glm::vec4 pZero = comp.GetPixel(4, 4); // Deve risultare nero
        glm::vec4 pHalf = comp.GetPixel(8, 8); // Deve risultare rosso a metà (0.5)

        bool maskZeroOk = (pZero.r < 1e-3f);
        bool maskHalfOk = (std::abs(pHalf.r - 0.5f) < 0.02f);

        bool passed = nonDestructive && maskZeroOk && maskHalfOk;
        res.passed = passed;
        res.observedDesc = "NonDestructive=" + std::string(nonDestructive ? "YES" : "NO") + 
                           ", MaskZeroPx=" + std::to_string(pZero.r) + 
                           ", MaskHalfPx=" + std::to_string(pHalf.r);
        res.details = passed ? "PASS: Maschera R8 modula l'alpha senza distruggere i dati pixel" : "FAIL: Errore masking";
        results.push_back(res);
    }

    // =========================================================================
    // PT-7: Stroke Deterministico
    // =========================================================================
    {
        PaintTestResult res;
        res.testId = "PT-7";
        res.testName = "PT-7 — Stroke Deterministico";
        res.inputDesc = "Due sessioni di pittura indipendenti con identici punti stroke e parametri brush";
        res.expectedDesc = "Hash dei pixel risultanti identico al 100% (bitwise determinism)";

        auto RunStrokeSession = []() -> uint64_t {
            TexturePaintingService s;
            s.InitializeCanvas(64, 64, glm::vec4(0.0f));
            uint32_t lid = s.AddLayer("Paint");
            PaintBrush brush;
            brush.radiusPixels = 8.0f;
            brush.hardness = 0.5f;
            brush.opacity = 0.8f;
            brush.color = glm::vec4(0.2f, 0.7f, 0.9f, 1.0f);

            s.BeginStroke(lid);
            s.UpdateStroke(glm::vec2(10.0f, 10.0f), glm::vec2(50.0f, 50.0f), brush);
            s.EndStroke();

            return s.GetLayer(lid)->pixelBuffer.ComputeContentHash();
        };

        uint64_t hashA = RunStrokeSession();
        uint64_t hashB = RunStrokeSession();

        bool passed = (hashA != 0 && hashA == hashB);
        res.passed = passed;
        res.observedDesc = "HashA=" + std::to_string(hashA) + ", HashB=" + std::to_string(hashB);
        res.details = passed ? "PASS: Ripetibilità matematica bitwise esatta" : "FAIL: Variazione non deterministica";
        results.push_back(res);
    }

    // =========================================================================
    // PT-8: CancelStroke e Ripristino Pixel
    // =========================================================================
    {
        PaintTestResult res;
        res.testId = "PT-8";
        res.testName = "PT-8 — CancelStroke e Ripristino Pixel";
        res.inputDesc = "Esecuzione di dab con brush e chiamata immediata a CancelStroke()";
        res.expectedDesc = "Pixel ripristinati esattamente al valore pre-stroke, dirty rect azzerata";

        TexturePaintingService service;
        service.InitializeCanvas(32, 32, glm::vec4(0.1f, 0.2f, 0.3f, 1.0f));
        uint32_t lid = service.GetLayers()[0].layerId;

        uint64_t hashBefore = service.GetLayer(lid)->pixelBuffer.ComputeContentHash();

        service.BeginStroke(lid);
        PaintBrush b;
        b.radiusPixels = 6.0f;
        b.color = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
        service.ApplyDab(glm::vec2(16.0f, 16.0f), b);

        uint64_t hashDuring = service.GetLayer(lid)->pixelBuffer.ComputeContentHash();
        bool changedDuring = (hashDuring != hashBefore);

        bool cancelOk = service.CancelStroke();
        uint64_t hashAfter = service.GetLayer(lid)->pixelBuffer.ComputeContentHash();

        bool passed = changedDuring && cancelOk && (hashAfter == hashBefore);
        res.passed = passed;
        res.observedDesc = "ChangedDuring=" + std::string(changedDuring ? "YES" : "NO") + 
                           ", CancelOk=" + (cancelOk ? "YES" : "NO") + 
                           ", HashMatch=" + (hashAfter == hashBefore ? "EXACT" : "MISMATCH");
        res.details = passed ? "PASS: Rollback transazionale atomico e bitwise conforme" : "FAIL: Residui dopo CancelStroke";
        results.push_back(res);
    }

    // =========================================================================
    // PT-9: PaintDirtyRect e Zero-Work
    // =========================================================================
    {
        PaintTestResult res;
        res.testId = "PT-9";
        res.testName = "PT-9 — PaintDirtyRect e Zero-Work";
        res.inputDesc = "Dab a (32, 32) con raggio 4 su canvas 128x128";
        res.expectedDesc = "DirtyRect delimita strettamente l'area toccata; compositing su dirty rect tocca solo l'area";

        TexturePaintingService service;
        service.InitializeCanvas(128, 128, glm::vec4(0.0f));
        uint32_t lid = service.AddLayer("Layer");

        service.BeginStroke(lid);
        PaintBrush b;
        b.radiusPixels = 4.0f;
        b.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        service.ApplyDab(glm::vec2(32.0f, 32.0f), b);
        PaintDirtyRect dr = service.EndStroke();

        bool boundsOk = (!dr.isEmpty) && 
                        (dr.xMin >= 27 && dr.xMax <= 37) && 
                        (dr.yMin >= 27 && dr.yMax <= 37);

        // Compositing incrementale su dirty rect
        ImageBitmap comp(128, 128, TexturePixelFormat::RGBA8, TextureColorSpace::Linear, glm::vec4(0.0f));
        service.CompositeLayers(comp, &dr);

        // Verifica che fuori dalla dirty rect sia rimasto nero
        glm::vec4 outside = comp.GetPixel(10, 10);
        glm::vec4 inside = comp.GetPixel(32, 32);

        bool zeroWorkOutside = (outside.a == 0.0f);
        bool renderedInside = (inside.r > 0.5f);

        bool passed = boundsOk && zeroWorkOutside && renderedInside;
        res.passed = passed;
        res.observedDesc = "Bounds=[" + std::to_string(dr.xMin) + ".." + std::to_string(dr.xMax) + 
                           "," + std::to_string(dr.yMin) + ".." + std::to_string(dr.yMax) + 
                           "], ZeroWorkOutside=" + (zeroWorkOutside ? "YES" : "NO");
        res.details = passed ? "PASS: Bounding box rigoroso e compositing selettivo Zero-Work" : "FAIL: Overflow area dirty";
        results.push_back(res);
    }

    // =========================================================================
    // PT-10: Serializzazione e Isolamento degli Asset
    // =========================================================================
    {
        PaintTestResult res;
        res.testId = "PT-10";
        res.testName = "PT-10 — Serializzazione e Isolamento degli Asset";
        res.inputDesc = "Roundtrip ToJson -> FromJson con più layer, blend mode, maschere e pixel dipinti";
        res.expectedDesc = "Deserializzazione perfetta, fingerprint identico, zero puntatori GPU";

        TexturePaintingService original;
        original.InitializeCanvas(32, 32, glm::vec4(0.1f, 0.1f, 0.1f, 1.0f));
        uint32_t l2 = original.AddLayer("Decal", glm::vec4(0.5f, 0.0f, 0.0f, 0.8f));
        original.GetLayer(l2)->blendMode = LayerBlendMode::Additive;
        original.AddMaskToLayer(l2, 0.7f);

        uint64_t hashOriginal = original.ComputeCombinedFingerprint();

        // Serializzazione JSON
        nlohmann::json j = original.ToJson();

        // Deserializzazione in nuova istanza
        TexturePaintingService restored;
        bool loaded = restored.FromJson(j);
        uint64_t hashRestored = restored.ComputeCombinedFingerprint();

        bool structureOk = (restored.GetCanvasWidth() == 32 && 
                            restored.GetCanvasHeight() == 32 && 
                            restored.GetLayerCount() == 2);

        const PaintLayer* restLayer2 = restored.GetLayer(l2);
        bool paramsOk = (restLayer2 != nullptr && 
                         restLayer2->blendMode == LayerBlendMode::Additive && 
                         restLayer2->mask.has_value());

        bool passed = loaded && structureOk && paramsOk && (hashOriginal == hashRestored);
        res.passed = passed;
        res.observedDesc = "Loaded=" + std::string(loaded ? "YES" : "NO") + 
                           ", Struct=" + (structureOk ? "OK" : "ERR") + 
                           ", FingerprintMatch=" + (hashOriginal == hashRestored ? "EXACT" : "DIFF");
        res.details = passed ? "PASS: Roundtrip JSON deterministico, completo e privo di dipendenze esterne" : "FAIL: Discrepanza serializzazione";
        results.push_back(res);
    }

    return results;
}

} // namespace fw
