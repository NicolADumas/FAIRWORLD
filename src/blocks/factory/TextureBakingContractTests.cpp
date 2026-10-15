#include "pch.h"
#include "TextureBakingContractTests.h"
#include "TextureBakingService.h"
#include "FactoryOrchestrator.h"
#include "ProductionStationAdapters.h"
#include "PainterStationContractTests.h"
#include "blocks/uv/CompiledTopologyTests.h"
#include <fstream>
#include <filesystem>
#include <iostream>

namespace fw {

std::vector<TextureBakingTestResult> TextureBakingContractTests::RunAllTests() {
    std::vector<TextureBakingTestResult> results;
    results.reserve(10);

    const std::string testDir = "assets/textures/blocks/test_txb";
    std::error_code ec;
    std::filesystem::create_directories(testDir, ec);

    // -------------------------------------------------------------------------
    // TXB-01: CompositeAlbedo valido -> PNG valido
    // -------------------------------------------------------------------------
    {
        TextureBakingTestResult r;
        r.testId = "TXB-01";
        r.testName = "CompositeAlbedo valido -> PNG valido";
        r.inputDesc = "ImageBitmap 64x64 RGBA8 con colore solido sRGB";
        r.expectedDesc = "Bake produce successo, file su disco con header PNG valido (0x89504E47)";

        TextureBakingService service;
        service.SetBaseDirectory(testDir);

        ImageBitmap bmp(64, 64, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.8f, 0.2f, 0.1f, 1.0f));
        auto bakeRes = service.BakeAlbedoAtlas(bmp, "txb_asset_01", 1, true);

        bool success = bakeRes.success && bakeRes.artifact.isValid;
        bool fileExists = std::filesystem::exists(bakeRes.artifact.physicalPath);

        bool headerValid = false;
        if (fileExists) {
            std::ifstream f(bakeRes.artifact.physicalPath, std::ios::binary);
            uint8_t magic[4] = {0};
            f.read(reinterpret_cast<char*>(magic), 4);
            headerValid = (magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G');
        }

        r.passed = success && fileExists && headerValid;
        r.observedDesc = "Success: " + std::string(success ? "true" : "false") +
                         ", FileExists: " + std::string(fileExists ? "true" : "false") +
                         ", PNGHeader: " + std::string(headerValid ? "VALID" : "INVALID");
        r.details = r.passed ? "PASS" : "FAIL: " + bakeRes.errorMessage;
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // TXB-02: PNG decode round-trip
    // -------------------------------------------------------------------------
    {
        TextureBakingTestResult r;
        r.testId = "TXB-02";
        r.testName = "PNG decode round-trip";
        r.inputDesc = "Scrittura PNG su disco e rilettura/decodifica con confronto pixel lossless";
        r.expectedDesc = "Tutti i pixel decodificati corrispondono bit a bit (delta < 0.005f) al buffer sorgente";

        TextureBakingService service;
        service.SetBaseDirectory(testDir);

        ImageBitmap bmp(32, 32, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB);
        for (uint32_t y = 0; y < 32; ++y) {
            for (uint32_t x = 0; x < 32; ++x) {
                bmp.SetPixel(x, y, glm::vec4(x / 31.0f, y / 31.0f, (x + y) / 62.0f, 1.0f));
            }
        }

        auto bakeRes = service.BakeAlbedoAtlas(bmp, "txb_asset_02", 1, true);
        std::string roundTripErr;
        bool roundTripOk = bakeRes.success && TextureBakingService::VerifyArtifactRoundTrip(
            bakeRes.artifact.physicalPath, bmp, &roundTripErr
        );

        r.passed = roundTripOk;
        r.observedDesc = roundTripOk ? "Round-trip verificato al 100%: 32x32 pixel identici." : ("Errore: " + roundTripErr);
        r.details = r.passed ? "PASS" : "FAIL: " + roundTripErr;
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // TXB-03: Alpha preservato
    // -------------------------------------------------------------------------
    {
        TextureBakingTestResult r;
        r.testId = "TXB-03";
        r.testName = "Alpha preservato";
        r.inputDesc = "Bitmap con pixel a trasparenza graduata (alpha = 0.0, 0.25, 0.5, 0.75, 1.0)";
        r.expectedDesc = "Canale alpha decodificato intatto, nessuna perdita o clamp a 1.0";

        TextureBakingService service;
        service.SetBaseDirectory(testDir);

        ImageBitmap bmp(16, 16, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB);
        bmp.SetPixel(0, 0, glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        bmp.SetPixel(1, 0, glm::vec4(0.0f, 1.0f, 0.0f, 0.25f));
        bmp.SetPixel(2, 0, glm::vec4(0.0f, 0.0f, 1.0f, 0.5f));
        bmp.SetPixel(3, 0, glm::vec4(1.0f, 1.0f, 0.0f, 0.75f));
        bmp.SetPixel(4, 0, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

        auto bakeRes = service.BakeAlbedoAtlas(bmp, "txb_asset_03", 1, true);

        ImageBitmap readBack;
        bool readOk = bakeRes.success && readBack.LoadFromFile(bakeRes.artifact.physicalPath, TextureColorSpace::sRGB);

        bool alpha0 = readOk && (std::abs(readBack.GetPixel(0, 0).a - 0.0f) < 0.01f);
        bool alpha25 = readOk && (std::abs(readBack.GetPixel(1, 0).a - 0.25f) < 0.01f);
        bool alpha50 = readOk && (std::abs(readBack.GetPixel(2, 0).a - 0.5f) < 0.01f);
        bool alpha75 = readOk && (std::abs(readBack.GetPixel(3, 0).a - 0.75f) < 0.01f);
        bool alpha100 = readOk && (std::abs(readBack.GetPixel(4, 0).a - 1.0f) < 0.01f);

        bool allAlphaOk = alpha0 && alpha25 && alpha50 && alpha75 && alpha100;
        r.passed = allAlphaOk;
        r.observedDesc = "Alpha test: [0.0:" + std::to_string(alpha0) +
                         ", 0.25:" + std::to_string(alpha25) +
                         ", 0.50:" + std::to_string(alpha50) +
                         ", 0.75:" + std::to_string(alpha75) +
                         ", 1.00:" + std::to_string(alpha100) + "]";
        r.details = r.passed ? "PASS" : "FAIL: Canale alpha corrotto o alterato durante il baking";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // TXB-04: Dimensioni preservate
    // -------------------------------------------------------------------------
    {
        TextureBakingTestResult r;
        r.testId = "TXB-04";
        r.testName = "Dimensioni preservate";
        r.inputDesc = "Baking di bitmap con aspect ratio differenti (64x32 e 128x128)";
        r.expectedDesc = "Dimensioni preservate esattamente nel file PNG codificato e decodificato";

        TextureBakingService service;
        service.SetBaseDirectory(testDir);

        ImageBitmap nonSquare(64, 32, TexturePixelFormat::RGBA8);
        ImageBitmap square(128, 128, TexturePixelFormat::RGBA8);

        auto res1 = service.BakeAlbedoAtlas(nonSquare, "txb_asset_04_nonsquare", 1, true);
        auto res2 = service.BakeAlbedoAtlas(square, "txb_asset_04_square", 1, true);

        ImageBitmap rb1, rb2;
        bool ok1 = res1.success && rb1.LoadFromFile(res1.artifact.physicalPath);
        bool ok2 = res2.success && rb2.LoadFromFile(res2.artifact.physicalPath);

        bool dims1 = ok1 && (rb1.GetWidth() == 64 && rb1.GetHeight() == 32);
        bool dims2 = ok2 && (rb2.GetWidth() == 128 && rb2.GetHeight() == 128);

        r.passed = dims1 && dims2;
        r.observedDesc = "NonSquare: " + std::to_string(rb1.GetWidth()) + "x" + std::to_string(rb1.GetHeight()) +
                         " (Expected 64x32), Square: " + std::to_string(rb2.GetWidth()) + "x" + std::to_string(rb2.GetHeight()) + " (Expected 128x128)";
        r.details = r.passed ? "PASS" : "FAIL: Discrepanza dimensioni dopo decodifica PNG";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // TXB-05: Input invariato -> nessuna riscrittura (Zero-Work disk hit)
    // -------------------------------------------------------------------------
    {
        TextureBakingTestResult r;
        r.testId = "TXB-05";
        r.testName = "Input invariato -> nessuna riscrittura";
        r.inputDesc = "Esecuzione ripetuta di BakeAlbedoAtlas con input identico";
        r.expectedDesc = "Seconda chiamata rileva cache hit, diskWriteCount invariato, wasCacheHitNoDiskWrite == true";

        TextureBakingService service;
        service.SetBaseDirectory(testDir);
        service.ResetTelemetry();

        ImageBitmap bmp(64, 64, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.3f, 0.4f, 0.5f, 1.0f));

        // Prima esecuzione (scrittura iniziale su disco)
        auto res1 = service.BakeAlbedoAtlas(bmp, "txb_asset_05", 1, false);
        uint32_t writesAfterFirst = service.GetTelemetry().diskWriteCount;
        uint32_t encAfterFirst = service.GetTelemetry().encodingCount;

        // Seconda esecuzione con input invariato
        auto res2 = service.BakeAlbedoAtlas(bmp, "txb_asset_05", 1, false);
        uint32_t writesAfterSecond = service.GetTelemetry().diskWriteCount;
        uint32_t encAfterSecond = service.GetTelemetry().encodingCount;

        bool zeroDiskWrites = (writesAfterFirst == 1) && (writesAfterSecond == 1);
        bool zeroEncodes = (encAfterFirst == 1) && (encAfterSecond == 1);
        bool flagsOk = res2.wasCacheHitNoEncoding && res2.wasCacheHitNoDiskWrite;

        r.passed = res1.success && res2.success && zeroDiskWrites && zeroEncodes && flagsOk;
        r.observedDesc = "Writes: " + std::to_string(writesAfterFirst) + " -> " + std::to_string(writesAfterSecond) +
                         ", Encodes: " + std::to_string(encAfterFirst) + " -> " + std::to_string(encAfterSecond) +
                         ", CacheHitNoDiskWrite: " + (res2.wasCacheHitNoDiskWrite ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: Riscrittura disco o codifica non necessaria non evitata";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // TXB-06: Modifica pixel -> nuovo artefatto o aggiornamento coerente
    // -------------------------------------------------------------------------
    {
        TextureBakingTestResult r;
        r.testId = "TXB-06";
        r.testName = "Modifica pixel -> nuovo artefatto o aggiornamento coerente";
        r.inputDesc = "Modifica di un singolo pixel in ImageBitmap tra due chiamate";
        r.expectedDesc = "Rilevamento nuovo contentHash, riesecuzione codifica e scrittura atomica";

        TextureBakingService service;
        service.SetBaseDirectory(testDir);
        service.ResetTelemetry();

        ImageBitmap bmp(32, 32, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        auto res1 = service.BakeAlbedoAtlas(bmp, "txb_asset_06", 1, false);

        // Modifica singolo pixel
        bmp.SetPixel(0, 0, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
        auto res2 = service.BakeAlbedoAtlas(bmp, "txb_asset_06", 2, false);

        bool hashDifferent = (res1.artifact.contentHash != res2.artifact.contentHash);
        bool rewriteOccurred = (service.GetTelemetry().diskWriteCount == 2);
        bool flagsCorrect = !res2.wasCacheHitNoDiskWrite;

        r.passed = res1.success && res2.success && hashDifferent && rewriteOccurred && flagsCorrect;
        r.observedDesc = "Hash1: " + std::to_string(res1.artifact.contentHash) +
                         " vs Hash2: " + std::to_string(res2.artifact.contentHash) +
                         ", TotalWrites: " + std::to_string(service.GetTelemetry().diskWriteCount);
        r.details = r.passed ? "PASS" : "FAIL: Modifica pixel non ha prodotto un aggiornamento coerente";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // TXB-07: Errore di scrittura -> nessuna corruzione dell'artefatto precedente
    // -------------------------------------------------------------------------
    {
        TextureBakingTestResult r;
        r.testId = "TXB-07";
        r.testName = "Errore di scrittura -> nessuna corruzione dell'artefatto precedente";
        r.inputDesc = "Generazione artefatto valido seguito da tentativo di bake con immagine invalida/vuota";
        r.expectedDesc = "Bake fallisce con codice diagnostico esplicito; l'artefatto precedente e il Draft rimangono integri";

        TextureBakingService service;
        service.SetBaseDirectory(testDir);

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:txb_draft_07", "TXB Draft 07");
        orch.ExecuteStation(ProductionStationID::Shape);
        orch.ExecuteStation(ProductionStationID::SurfaceUV);

        auto* draft = orch.GetCurrentDraft();
        PaintLayer l1;
        l1.name = "BaseLayer";
        l1.pixelBuffer = ImageBitmap(32, 32, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
        draft->paintLayers = { l1 };

        orch.ExecuteStation(ProductionStationID::Painter);
        const auto* painterStation = dynamic_cast<const PainterStationAdapter*>(orch.GetStation(ProductionStationID::Painter));

        // 1. Bake valido iniziale
        auto bakeOk = service.BakeDraftAlbedo(*draft, *painterStation, false);
        std::string initialPath = draft->albedoPath;
        uint64_t initialFileSize = std::filesystem::file_size(bakeOk.artifact.physicalPath);

        // 2. Tentativo di bake con sorgente corrotta/invalida
        ImageBitmap invalidBmp; // Bitmap vuota
        auto bakeFail = service.BakeAlbedoAtlas(invalidBmp, "txb_draft_07", 2, true);

        // Verifica che l'artefatto originale su disco sia ancora integro
        bool fileStillExists = std::filesystem::exists(bakeOk.artifact.physicalPath);
        uint64_t currentFileSize = fileStillExists ? std::filesystem::file_size(bakeOk.artifact.physicalPath) : 0;
        bool draftPathPreserved = (draft->albedoPath == initialPath);
        bool failHandled = (!bakeFail.success) && (bakeFail.errorCode == BakingErrorCode::InvalidSourceImage);

        r.passed = bakeOk.success && failHandled && fileStillExists && (initialFileSize == currentFileSize) && draftPathPreserved;
        r.observedDesc = "InitialSize: " + std::to_string(initialFileSize) +
                         ", CurrentSize: " + std::to_string(currentFileSize) +
                         ", DraftPathPreserved: " + (draftPathPreserved ? "true" : "false") +
                         ", ErrorCode: " + std::to_string(static_cast<int>(bakeFail.errorCode));
        r.details = r.passed ? "PASS" : "FAIL: Corruzione artefatto o draft in seguito a errore di bake";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // TXB-08: Percorsi relativi e portabili
    // -------------------------------------------------------------------------
    {
        TextureBakingTestResult r;
        r.testId = "TXB-08";
        r.testName = "Percorsi relativi e portabili";
        r.inputDesc = "Validazione URI prodotto per asset con caratteri speciali e verifica vincoli di portabilita";
        r.expectedDesc = "URI relativo inizia per 'textures/blocks/', niente lettere drive (C:), backslash o traversal";

        std::string testAssetId = "custom:stone.brick 01";
        std::string canonicalUri = TextureBakingService::GenerateCanonicalRelativeUri(testAssetId, "albedo");

        std::string err;
        bool canonicalValid = TextureBakingService::ValidatePortableUri(canonicalUri, &err);

        // Test di rifiuto percorsi assoluti illegittimi
        std::string errAbsWin, errAbsUnix, errTrav;
        bool rejWin = !TextureBakingService::ValidatePortableUri("C:/Windows/System32/albedo.png", &errAbsWin);
        bool rejUnix = !TextureBakingService::ValidatePortableUri("/var/fairworld/textures/albedo.png", &errAbsUnix);
        bool rejTrav = !TextureBakingService::ValidatePortableUri("textures/../../secret/albedo.png", &errTrav);
        bool rejBackslash = !TextureBakingService::ValidatePortableUri("textures\\blocks\\albedo.png", &err);

        bool allPortabilityChecksPass = canonicalValid && rejWin && rejUnix && rejTrav && rejBackslash;

        r.passed = allPortabilityChecksPass;
        r.observedDesc = "Canonical: '" + canonicalUri + "', Rejections: [Win:" + std::to_string(rejWin) +
                         ", Unix:" + std::to_string(rejUnix) + ", Trav:" + std::to_string(rejTrav) +
                         ", Backslash:" + std::to_string(rejBackslash) + "]";
        r.details = r.passed ? "PASS" : "FAIL: Violazione regole di portabilita URI";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // TXB-09: Nessuna perdita dei PaintLayers originali
    // -------------------------------------------------------------------------
    {
        TextureBakingTestResult r;
        r.testId = "TXB-09";
        r.testName = "Nessuna perdita dei PaintLayers originali";
        r.inputDesc = "Verifica integrita di 2 PaintLayers nel Draft prima e dopo il baking";
        r.expectedDesc = "I layer del Draft rimangono esattamente identici per conteggio, nomi, blendMode e pixel";

        FactoryOrchestrator orch;
        orch.OpenDraft("fairworld:txb_draft_09", "TXB Draft 09");
        orch.ExecuteStation(ProductionStationID::Shape);
        orch.ExecuteStation(ProductionStationID::SurfaceUV);

        auto* draft = orch.GetCurrentDraft();
        PaintLayer l1;
        l1.name = "LayerA";
        l1.blendMode = PaintBlendMode::Normal;
        l1.opacity = 0.8f;
        l1.pixelBuffer = ImageBitmap(32, 32, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

        PaintLayer l2;
        l2.name = "LayerB";
        l2.blendMode = PaintBlendMode::Multiply;
        l2.opacity = 0.5f;
        l2.pixelBuffer = ImageBitmap(32, 32, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));

        draft->paintLayers = { l1, l2 };

        uint64_t hashL1_pre = draft->paintLayers[0].pixelBuffer.ComputeContentHash();
        uint64_t hashL2_pre = draft->paintLayers[1].pixelBuffer.ComputeContentHash();

        orch.ExecuteStation(ProductionStationID::Painter);
        const auto* painterStation = dynamic_cast<const PainterStationAdapter*>(orch.GetStation(ProductionStationID::Painter));

        TextureBakingService service;
        service.SetBaseDirectory(testDir);
        auto bakeRes = service.BakeDraftAlbedo(*draft, *painterStation, true);

        bool layersCountOk = (draft->paintLayers.size() == 2);
        bool name1Ok = (draft->paintLayers[0].name == "LayerA");
        bool name2Ok = (draft->paintLayers[1].name == "LayerB");
        bool blend1Ok = (draft->paintLayers[0].blendMode == PaintBlendMode::Normal);
        bool blend2Ok = (draft->paintLayers[1].blendMode == PaintBlendMode::Multiply);
        bool hash1Ok = (draft->paintLayers[0].pixelBuffer.ComputeContentHash() == hashL1_pre);
        bool hash2Ok = (draft->paintLayers[1].pixelBuffer.ComputeContentHash() == hashL2_pre);

        bool allPreserved = bakeRes.success && layersCountOk && name1Ok && name2Ok && blend1Ok && blend2Ok && hash1Ok && hash2Ok;
        r.passed = allPreserved;
        r.observedDesc = "LayersPreserved: " + std::string(allPreserved ? "true" : "false") +
                         ", HashesMatch: " + std::string((hash1Ok && hash2Ok) ? "true" : "false");
        r.details = r.passed ? "PASS" : "FAIL: I layer originali del Draft sono stati alterati o persi durante il baking";
        results.push_back(r);
    }

    // -------------------------------------------------------------------------
    // TXB-10: Nessuna regressione FG-01R e Painter
    // -------------------------------------------------------------------------
    {
        TextureBakingTestResult r;
        r.testId = "TXB-10";
        r.testName = "Nessuna regressione FG-01R e Painter";
        r.inputDesc = "Esecuzione delle suite autorevoli CompiledTopologyTests (36) e PainterStationContractTests (10)";
        r.expectedDesc = "Tutti i test delle suite superati senza regressioni (46/46 PASS)";

        auto topResults = CompiledTopologyTests::RunAllTests();
        auto pntResults = PainterStationContractTests::RunAllTests();

        size_t topPass = 0;
        for (const auto& tr : topResults) {
            if (tr.passed) topPass++;
        }

        size_t pntPass = 0;
        for (const auto& pr : pntResults) {
            if (pr.passed) pntPass++;
        }

        bool allTopPass = (topPass == topResults.size() && topResults.size() == 36);
        bool allPntPass = (pntPass == pntResults.size() && pntResults.size() == 10);

        r.passed = allTopPass && allPntPass;
        r.observedDesc = "CompiledTopology: " + std::to_string(topPass) + "/" + std::to_string(topResults.size()) +
                         ", PainterStation: " + std::to_string(pntPass) + "/" + std::to_string(pntResults.size());
        r.details = r.passed ? "PASS" : "FAIL: Rilevata regressione in FG-01R o Painter";
        results.push_back(r);
    }

    return results;
}

} // namespace fw
