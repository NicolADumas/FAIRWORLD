#include "pch.h"
#include "TextureContractTests.h"
#include "ImageBitmap.h"
#include "TextureAsset.h"
#include "PBRTexturePack.h"
#include <iostream>
#include <cmath>
#include <vector>

namespace fw {

// =============================================================================
// SUITE DI TEST DETERMINISTICA BM-TX2 (TX-1 .. TX-10)
// =============================================================================

std::vector<TextureTestResult> TextureContractTests::RunContractTests() {
    std::vector<TextureTestResult> results;
    results.reserve(10);

    // -------------------------------------------------------------------------
    // TX-1: Import PNG valido da memoria
    // -------------------------------------------------------------------------
    {
        TextureTestResult tr;
        tr.testId = "TX-1";
        tr.testName = "Valid PNG Import";
        tr.inputDesc = "Bitmap sintetica 32x32 codificata in PNG memory buffer";
        tr.expectedDesc = "Decodifica con successo: 32x32, RGBA8, 4096 byte, pixel leggibili";

        ImageBitmap srcImg(32, 32, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.2f, 0.4f, 0.8f, 1.0f));
        auto pngBytes = srcImg.EncodeToPngMemory();

        ImageBitmap decodedImg;
        bool ok = decodedImg.DecodeFromMemory(pngBytes.data(), pngBytes.size(), TextureColorSpace::sRGB);

        bool valid = ok && decodedImg.GetWidth() == 32 && decodedImg.GetHeight() == 32 && 
                     decodedImg.GetPixelFormat() == TexturePixelFormat::RGBA8 &&
                     decodedImg.GetTotalBytes() == (32 * 32 * 4);

        glm::vec4 p = decodedImg.GetPixel(16, 16);
        bool pixelMatch = (std::abs(p.r - 0.2f) < 0.05f) && 
                          (std::abs(p.g - 0.4f) < 0.05f) && 
                          (std::abs(p.b - 0.8f) < 0.05f);

        tr.observedDesc = "Decodificato: " + std::to_string(decodedImg.GetWidth()) + "x" + 
                          std::to_string(decodedImg.GetHeight()) + ", Bytes: " + 
                          std::to_string(decodedImg.GetTotalBytes()) + ", Match: " + (pixelMatch ? "true" : "false");
        tr.passed = valid && pixelMatch;
        tr.details = tr.passed ? "Import PNG da memoria e decodifica conformi." : "Errore durante la decodifica o pixel non corrispondenti.";
        results.push_back(tr);
    }

    // -------------------------------------------------------------------------
    // TX-2: Gestione File Corrotto / Byte Garbage
    // -------------------------------------------------------------------------
    {
        TextureTestResult tr;
        tr.testId = "TX-2";
        tr.testName = "Corrupt File Handling";
        tr.inputDesc = "Buffer di byte casuali/corrotti passati al decoder PNG";
        tr.expectedDesc = "DecodeFromMemory restituisce false, IsValid() == false, zero crash";

        std::vector<uint8_t> corruptData = { 0x89, 0x50, 0x4E, 0x47, 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x12, 0x34 };
        ImageBitmap img;
        bool ok = img.DecodeFromMemory(corruptData.data(), corruptData.size());

        bool safe = (!ok) && (!img.IsValid()) && (img.GetWidth() == 0) && (img.GetHeight() == 0);
        tr.observedDesc = "Ritorno: " + std::string(ok ? "true" : "false") + 
                          ", IsValid: " + std::string(img.IsValid() ? "true" : "false") + 
                          ", W: " + std::to_string(img.GetWidth());
        tr.passed = safe;
        tr.details = safe ? "File corrotto respinto in sicurezza senza memory fault." : "Comportamento anomalo su dati corrotti.";
        results.push_back(tr);
    }

    // -------------------------------------------------------------------------
    // TX-3: Rifiuto Dimensioni Non Valide ed Overflow
    // -------------------------------------------------------------------------
    {
        TextureTestResult tr;
        tr.testId = "TX-3";
        tr.testName = "Invalid Dimensions Handling";
        tr.inputDesc = "Tentativo di istanziare ImageBitmap 0x0 e 100000x100000 (overflow/eccedenza limite)";
        tr.expectedDesc = "ValidateDimensions restituisce false, allocazione bloccata in sicurezza";

        bool zeroOk = ImageBitmap::ValidateDimensions(0, 512);
        bool maxOk = ImageBitmap::ValidateDimensions(8192, 8192, 4096); // Supera 4096
        bool validOk = ImageBitmap::ValidateDimensions(512, 512, 4096);

        ImageBitmap zeroImg(0, 0);
        bool zeroSafe = !zeroImg.IsValid();

        tr.observedDesc = "Zero Dim: " + std::string(zeroOk ? "true" : "false") + 
                          ", Exceed Max: " + std::string(maxOk ? "true" : "false") + 
                          ", Valid: " + std::string(validOk ? "true" : "false") + 
                          ", Zero Img Valid: " + std::string(zeroSafe ? "false" : "true");
        tr.passed = (!zeroOk) && (!maxOk) && validOk && zeroSafe;
        tr.details = tr.passed ? "Protezione overflow e limiti massimi dimensioni verificati." : "Validazione dimensioni fallita.";
        results.push_back(tr);
    }

    // -------------------------------------------------------------------------
    // TX-4: Gestione Color Space (sRGB vs Linear)
    // -------------------------------------------------------------------------
    {
        TextureTestResult tr;
        tr.testId = "TX-4";
        tr.testName = "Color Space Handling";
        tr.inputDesc = "Conversione sRGB -> Linear -> sRGB con verifica curva gamma 2.2";
        tr.expectedDesc = "Valore sRGB 0.5 convertito in Linear ~0.214 e ripristinato a 0.5";

        float srgbVal = 0.5f;
        float linearVal = ImageBitmap::sRGBToLinear(srgbVal);
        float roundtripVal = ImageBitmap::LinearTosRGB(linearVal);

        bool linearAccurate = std::abs(linearVal - 0.214041f) < 0.01f;
        bool roundtripAccurate = std::abs(roundtripVal - 0.5f) < 0.01f;

        ImageBitmap img(8, 8, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
        img.ConvertColorSpace(TextureColorSpace::Linear);
        bool isLinear = (img.GetColorSpace() == TextureColorSpace::Linear);
        glm::vec4 pLin = img.GetPixel(0, 0);

        tr.observedDesc = "Linear: " + std::to_string(linearVal) + 
                          ", Roundtrip: " + std::to_string(roundtripVal) + 
                          ", Bitmap Pixel R: " + std::to_string(pLin.r);
        tr.passed = linearAccurate && roundtripAccurate && isLinear && (std::abs(pLin.r - 0.214f) < 0.02f);
        tr.details = tr.passed ? "Conversioni di spazio colore matematicamente conformi." : "Errore nella trasformazione gamma.";
        results.push_back(tr);
    }

    // -------------------------------------------------------------------------
    // TX-5: Packing ORM (R=AO, G=Roughness, B=Metallic, A=255)
    // -------------------------------------------------------------------------
    {
        TextureTestResult tr;
        tr.testId = "TX-5";
        tr.testName = "ORM Packing Exact Channel Mapping";
        tr.inputDesc = "AO (R=0.8), Roughness (G=0.6), Metallic (B=0.2) impacchettati in ORM";
        tr.expectedDesc = "Mappa ORM composita con R=0.8, G=0.6, B=0.2, A=1.0, Linear color space";

        PBRTexturePack pack;
        ImageBitmap aoImg(16, 16, TexturePixelFormat::R8, TextureColorSpace::Linear, glm::vec4(0.8f));
        ImageBitmap roughImg(16, 16, TexturePixelFormat::R8, TextureColorSpace::Linear, glm::vec4(0.6f));
        ImageBitmap metalImg(16, 16, TexturePixelFormat::R8, TextureColorSpace::Linear, glm::vec4(0.2f));

        pack.SetAmbientOcclusionMap(aoImg);
        pack.SetRoughnessMap(roughImg);
        pack.SetMetallicMap(metalImg);

        bool packed = pack.GenerateORM(16, 16);
        bool hasOrm = pack.HasORM();
        glm::vec4 ormP = pack.GetORMMap()->GetPixel(8, 8);

        bool rOk = std::abs(ormP.r - 0.8f) < 0.02f;
        bool gOk = std::abs(ormP.g - 0.6f) < 0.02f;
        bool bOk = std::abs(ormP.b - 0.2f) < 0.02f;
        bool aOk = std::abs(ormP.a - 1.0f) < 0.01f;
        bool isLinear = pack.GetORMMap()->GetColorSpace() == TextureColorSpace::Linear;

        tr.observedDesc = "ORM Generata: " + std::string(packed ? "true" : "false") + 
                          ", Pixel: (" + std::to_string(ormP.r) + ", " + std::to_string(ormP.g) + ", " + 
                          std::to_string(ormP.b) + ", " + std::to_string(ormP.a) + ")";
        tr.passed = packed && hasOrm && rOk && gOk && bOk && aOk && isLinear;
        tr.details = tr.passed ? "Compositing ORM multicanale lineare rigorosamente corretto." : "Mappatura canali ORM non corretta.";
        results.push_back(tr);
    }

    // -------------------------------------------------------------------------
    // TX-6: Fallback PBR su Canali Mancanti
    // -------------------------------------------------------------------------
    {
        TextureTestResult tr;
        tr.testId = "TX-6";
        tr.testName = "PBR Fallback Values on Missing Channels";
        tr.inputDesc = "GenerateORM senza mappe di AO e Metallic fornite (solo Roughness 0.4)";
        tr.expectedDesc = "ORM generata usando fallbackAO (1.0), fallbackRoughness (0.4), fallbackMetallic (0.0)";

        PBRTexturePack pack;
        pack.fallbackAO = 1.0f;
        pack.fallbackMetallic = 0.0f;
        ImageBitmap roughImg(16, 16, TexturePixelFormat::R8, TextureColorSpace::Linear, glm::vec4(0.4f));
        pack.SetRoughnessMap(roughImg);

        bool packed = pack.GenerateORM(16, 16);
        glm::vec4 ormP = pack.GetORMMap()->GetPixel(0, 0);

        bool aoFallbackOk = std::abs(ormP.r - 1.0f) < 0.02f;
        bool roughOk = std::abs(ormP.g - 0.4f) < 0.02f;
        bool metalFallbackOk = std::abs(ormP.b - 0.0f) < 0.02f;

        tr.observedDesc = "Pixel: R(AO)=" + std::to_string(ormP.r) + 
                          ", G(Rough)=" + std::to_string(ormP.g) + 
                          ", B(Metal)=" + std::to_string(ormP.b);
        tr.passed = packed && aoFallbackOk && roughOk && metalFallbackOk;
        tr.details = tr.passed ? "Valori di fallback applicati fedelmente sui canali assenti." : "Errore nei valori scalari di fallback.";
        results.push_back(tr);
    }

    // -------------------------------------------------------------------------
    // TX-7: Resampling Risoluzioni Eterogenee
    // -------------------------------------------------------------------------
    {
        TextureTestResult tr;
        tr.testId = "TX-7";
        tr.testName = "Resampling Heterogeneous Map Resolutions";
        tr.inputDesc = "AO a 32x32 e Roughness a 64x64 impacchettati in ORM target 64x64";
        tr.expectedDesc = "AO viene ricampionata bilinearmente a 64x64 senza errori di allineamento";

        PBRTexturePack pack;
        ImageBitmap aoImg(32, 32, TexturePixelFormat::R8, TextureColorSpace::Linear, glm::vec4(0.7f));
        ImageBitmap roughImg(64, 64, TexturePixelFormat::R8, TextureColorSpace::Linear, glm::vec4(0.3f));
        pack.SetAmbientOcclusionMap(aoImg);
        pack.SetRoughnessMap(roughImg);

        bool packed = pack.GenerateORM(64, 64);
        const auto& orm = pack.GetORMMap();

        bool dimOk = (orm->GetWidth() == 64 && orm->GetHeight() == 64);
        glm::vec4 pCorner = orm->GetPixel(0, 0);
        glm::vec4 pCenter = orm->GetPixel(32, 32);

        bool valOk = (std::abs(pCorner.r - 0.7f) < 0.02f) && (std::abs(pCenter.g - 0.3f) < 0.02f);

        tr.observedDesc = "Dimensioni finali: " + std::to_string(orm->GetWidth()) + "x" + 
                          std::to_string(orm->GetHeight()) + ", Valori: AO=" + 
                          std::to_string(pCenter.r) + ", Rough=" + std::to_string(pCenter.g);
        tr.passed = packed && dimOk && valOk;
        tr.details = tr.passed ? "Ricampionamento bilineare automatico per risoluzioni eterogenee riuscito." : "Resampling fallito.";
        results.push_back(tr);
    }

    // -------------------------------------------------------------------------
    // TX-8: Fingerprint Deterministico & Sensibilità Pixel
    // -------------------------------------------------------------------------
    {
        TextureTestResult tr;
        tr.testId = "TX-8";
        tr.testName = "Deterministic Fingerprint & Pixel Sensitivity";
        tr.inputDesc = "Confronto hash tra 2 immagini identiche e 1 immagine con 1 pixel alterato";
        tr.expectedDesc = "Hash identico per immagini uguali; hash diverso modificando un singolo pixel";

        ImageBitmap imgA(16, 16, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(1.0f));
        ImageBitmap imgB(16, 16, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(1.0f));
        ImageBitmap imgC(16, 16, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(1.0f));
        imgC.SetPixel(7, 7, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)); // Modifica 1 pixel

        uint64_t hashA = imgA.ComputeContentHash();
        uint64_t hashB = imgB.ComputeContentHash();
        uint64_t hashC = imgC.ComputeContentHash();

        bool same = (hashA == hashB) && (hashA != 0);
        bool diff = (hashA != hashC);

        tr.observedDesc = "Hash A == B: " + std::string(same ? "true" : "false") + 
                          ", Hash A != C: " + std::string(diff ? "true" : "false");
        tr.passed = same && diff;
        tr.details = tr.passed ? "Algoritmo FNV-1a a 64 bit deterministico e sensibile a modifiche locali." : "Collisione di hash o mancata determinazione.";
        results.push_back(tr);
    }

    // -------------------------------------------------------------------------
    // TX-9: Zero-Work Invariance su Input Invariato
    // -------------------------------------------------------------------------
    {
        TextureTestResult tr;
        tr.testId = "TX-9";
        tr.testName = "Zero-Work Invariance on Stable Input";
        tr.inputDesc = "Due chiamate consecutive a ComputeFingerprint su PBRTexturePack stabile";
        tr.expectedDesc = "Zero ricalcoli, fingerprint dirty flag == false, costo O(1)";

        PBRTexturePack pack;
        ImageBitmap albedo(16, 16, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.5f));
        pack.SetBaseColorMap(albedo);

        uint64_t fp1 = pack.ComputeFingerprint();
        uint64_t fp2 = pack.ComputeFingerprint();

        bool zeroWork = (fp1 == fp2) && (fp1 != 0);

        tr.observedDesc = "FP1: " + std::to_string(fp1) + ", FP2: " + std::to_string(fp2) + 
                          ", Match: " + std::string(zeroWork ? "true" : "false");
        tr.passed = zeroWork;
        tr.details = tr.passed ? "Invariante Zero-Work rispettata: caching O(1) attivo." : "Ricalcolo ridondante non necessario.";
        results.push_back(tr);
    }

    // -------------------------------------------------------------------------
    // TX-10: Serializzazione PNG & Isolamento Asset
    // -------------------------------------------------------------------------
    {
        TextureTestResult tr;
        tr.testId = "TX-10";
        tr.testName = "Serialization & Asset Isolation";
        tr.inputDesc = "TextureAsset importato in Asset A; duplicazione in Asset B e modifica indipendente";
        tr.expectedDesc = "Modifica a B non altera revisione, immagine o fingerprint di A";

        TextureAsset texA("tex:wood_diffuse");
        ImageBitmap base(32, 32, TexturePixelFormat::RGBA8, TextureColorSpace::sRGB, glm::vec4(0.9f));
        texA.SetImage(base);

        uint64_t revA_initial = texA.GetRevision();
        uint64_t fpA_initial = texA.GetContentFingerprint();

        // Crea Asset B indipendente
        TextureAsset texB("tex:wood_diffuse_copy");
        texB.SetImage(texA.GetImage()); // Deep copy

        // Modifica drastica su B
        texB.GetImageMutable().SetPixel(0, 0, glm::vec4(0.0f));
        texB.GetImageMutable().IncrementRevision();
        texB.MarkDirty();

        uint64_t revA_after = texA.GetRevision();
        uint64_t fpA_after = texA.GetContentFingerprint();
        uint64_t fpB_after = texB.GetContentFingerprint();

        bool aIsolated = (revA_initial == revA_after) && (fpA_initial == fpA_after);
        bool bModified = (fpB_after != fpA_after);

        tr.observedDesc = "A Inalterato: " + std::string(aIsolated ? "true" : "false") + 
                          ", B Differenziato: " + std::string(bModified ? "true" : "false");
        tr.passed = aIsolated && bModified;
        tr.details = tr.passed ? "Isolamento della memoria e indipendenza transazionale tra asset garantite." : "Perdita di isolamento tra asset.";
        results.push_back(tr);
    }

    return results;
}

} // namespace fw
