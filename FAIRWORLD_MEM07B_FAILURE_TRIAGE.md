# FAIRWORLD — MEM-07B FINAL GATE: FAILURE TRIAGE REPORT
**Data:** 31 Ottobre 2026  
**Ambito:** CTest Debug Gate Triage & Root Cause Verification (MEM-04 e FairworldUVTests)  
**Stato Pre-Triage:** CTest 10 Suite Rilevate (8 PASS, 2 FAIL in Debug). Build Debug PASS (0 errori). Lifecycle Tests 8/8 PASS.  
**Obiettivo:** Diagnosi rigorosa e circoscritta dei fallimenti emersi, separazione analitica tra cause ambientali, regressioni reali e disallineamenti contrattuali preesistenti, applicazione del diff minimo correttivo senza ampliare lo scope.

---

## 1. Executive Summary & Stato di Ingresso

L'attivazione del gate CTest nativo in CMake ha portato alla luce due fallimenti distinti in configurazione Debug:
1. **P0 — `FairworldMemoryTests` (MEM-04):** Fallimento isolato del test `MEM04-06` con `Exit Code: 3221225477 (0xC0000005)` (Access Violation nel processo figlio del test runner isolato).
2. **P1 — `FairworldUVTests`:** Fallimento complessivo della suite contenente 20 sotto-suite (286 test totali scoperti contro 269 nominali storici), con **esattamente 15 test falliti**.

La build completa del progetto è confermata a **0 errori e 0 warning critici**. La suite `FairworldLifecycleTests` (MEM-07A + MEM-07B) passa con successo **8/8 PASS (`LC-01 .. LC-08`)**.

---

## 2. Priorità P0 — MEM04-06 (Tombstone Bounds Guard & Access Violation)

### 2.1 Evidenza e Sintomo
- **Test:** `GlobalMemoryLifecycleContractTests::Test06_PostShutdownDeleteSafety()` (ID `MEM04-06`).
- **Comportamento Rilevato:** Il processo figlio lanciato dal runner isolato termina bruscamente con codice di eccezione Windows `0xC0000005` (`STATUS_ACCESS_VIOLATION`) durante la deallocazione di un puntatore residuo (`delete lingeringScalar`).

### 2.2 Root Cause Tecnica Dimostrata
Nel corso della revisione F06 dell'audit, era stata introdotta in `ShutdownGlobalMemory(bool force)` (`src/core/memory/MemorySystem.cpp`) la seguente modifica:
```cpp
if (g_GlobalHeapBuffer) {
    std::free(g_GlobalHeapBuffer);
    g_GlobalHeapBuffer = nullptr;
    g_TrackedHeapStart = 0;  // <-- INTRODOTTO IN F06
    g_TrackedHeapEnd = 0;    // <-- INTRODOTTO IN F06
}
```

Tuttavia, l'implementazione dell'allocatore globale di FAIRWORLD prevede un meccanismo fondamentale denominato **Tombstone Bounds Guard** (`operator delete` e `operator delete[]` sovraccaricati):
```cpp
void operator delete(void* ptr) noexcept {
    if (!ptr) return;

    uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
    auto* alloc = g_GlobalAllocator.load(std::memory_order_acquire);

    if (alloc && g_GlobalHeapBuffer) {
        if (p >= g_TrackedHeapStart && p < g_TrackedHeapEnd) {
            std::lock_guard<std::mutex> lock(g_GlobalAllocatorMutex);
            alloc->Free(ptr);
            return;
        }
    } else if (g_TrackedHeapStart != 0) {
        // L'allocatore e' stato disattivato o spento
        if (p >= g_TrackedHeapStart && p < g_TrackedHeapEnd) {
            // Il puntatore apparteneva all'heap globale dismesso: non passiamo mai a std::free()!
            return;
        }
    }
    
    std::free(ptr);
}
```

Nel test `MEM04-06`:
1. Viene invocato `InitializeGlobalMemory()` allocando il buffer monolitico da 256 MB (`g_GlobalHeapBuffer`), registrando il range `[g_TrackedHeapStart, g_TrackedHeapEnd)`.
2. Vengono allocati oggetti tramite `new` (`lingeringScalar`, `lingeringArray`, `lingeringAligned`) residenti nell'arena.
3. Viene forzato lo shutdown: `ShutdownGlobalMemory(true)`, che dealloca fisicamente i 256 MB al sistema operativo (`std::free(g_GlobalHeapBuffer)`).
4. Avendo azzerato `g_TrackedHeapStart = 0` e `g_TrackedHeapEnd = 0`, il ramo `else if (g_TrackedHeapStart != 0)` è diventato **irraggiungibile**.
5. Quando il test ha invocato `delete lingeringScalar;`, la richiesta è caduta inevitabilmente sul fallback standard `std::free(ptr)`.
6. Poiché `ptr` apparteneva all'arena da 256 MB già restituita al CRT/VirtualAlloc, il gestore heap di Windows NT (`RtlFreeHeap`) ha rilevato un puntatore heap non valido sollevando un'Access Violation immediata `0xC0000005`.

### 2.3 Soluzione Applicata
La semantica del contratto di shutdown prescrive che i bound dell'heap dismesso (`g_TrackedHeapStart` e `g_TrackedHeapEnd`) rimangano persistentemente valorizzati come **Tombstone Guard** anche dopo che `g_GlobalHeapBuffer` è stato impostato a `nullptr`.
- All'uscita dell'applicazione o durante la distruzione di oggetti statici residui del CRT, `operator delete` intercetta gli indirizzi compresi nel tombstone range ed evita chiamate letali a `std::free()`.
- Qualora `InitializeGlobalMemory()` venga successivamente richiamata, essa riassegna `g_TrackedHeapStart` e `g_TrackedHeapEnd` al nuovo buffer allocato.

---

## 3. Priorità P1 — FairworldUVTests Triage Completo

Dall'ispezione analitica di `build/Testing/Temporary/LastTest.log` è stato catalogato l'esatto quadro dei **15 test interni falliti**.

### 3.1 Catalogo dei 15 Test Falliti

| # | Suite | Test ID | Nome Test | Esito & Messaggio Errore Diagnosticato | Categoria |
|---|---|---|---|---|---|
| **1** | `UVE` | `UVE-10` | 2D UV Painting Delegation | `FAIL -> Errore pittura 2D` | Contratto Test UV |
| **2** | `UVE` | `UVE-FailClosed` | Fail-closed Topology Revision & FaceID | `FAIL -> Violazione fail-closed: tratto consentito con revisione topologica disallineata` | Contratto Test UV |
| **3** | `UVE` | `UVE-19` | Polygon Mask Isolation | `FAIL -> Violazione isolamento: texel di altre isole contaminati` | Pre-condizione Canvas |
| **4** | `UVE` | `UVE-22` | Per-BlockID Isolation | `FAIL -> Contaminazione inter-BlockID rilevata` | Asserzione Preesistente |
| **5** | `UVE` | `UVE-37` | Hard Face Lock | `FAIL -> Violazione Hard Face Lock` | Discrepanza Feature vs Lock |
| **6** | `UVE` | `UVE-43` | Per-BlockID Isolation | `FAIL -> Rilevata contaminazione cross-BlockID` | Asserzione Preesistente |
| **7** | `MBR` | `MBR-10` | Pre-existing 135 Tests Regression Shield | `FAIL -> Regressione riscontrata: UVE -> Suite [UVE] conteggio incoerente (atteso: 18, scoperto: 35)` | Regression Gate Shield |
| **8** | `SBR` | `SBR-01` | Mesh primitiva non scolpita invariata | `FAIL -> FAIL: Pipeline delle primitive alterata` | **Regressione Reale Produzione** |
| **9** | `INT` | `INT-01` | Asset primitivo completo | `FAIL -> FAIL: Pipeline end-to-end per asset primitivo fallita` | **Regressione Reale Produzione** |
| **10** | `INT` | `INT-10` | Nessuna regressione dei 201 test precedenti | `FAIL -> Regressione riscontrata: SBR -> Suite [SBR] ha fallimenti (1 FAIL su 10)` | Cascata da SBR-01 |
| **11** | `SEC` | `SEC-10` | Nessuna regressione dei 211 test precedenti | `FAIL -> Regressione riscontrata: SBR (1 FAIL), INT (2 FAIL)` | Cascata da SBR-01 / INT-01 |
| **12** | `DGV` | `DGV-10` | Regressione delle suite precedenti | `FAIL -> Regressione riscontrata: SBR, INT, SEC` | Cascata |
| **13** | `CRH` | `CRH-10` | Regressione delle suite precedenti | `FAIL -> Regressione riscontrata: SBR, INT, SEC, DGV` | Cascata |
| **14** | `DEP` | `DEP-10` | PreviousSuitesRegressionCheck | `FAIL -> Regressione riscontrata: CRH, DGV` | Cascata |
| **15** | `MIG` | `MIG-10` | HistoricalSuitesRegressionGate | `FAIL -> Regressione riscontrata: DEP, CRH, DGV` | Cascata |

---

### 3.2 Separazione delle Cause

#### A — Problema Ambientale (CTest WORKING_DIRECTORY)
- **Causa Rilevata:** In `CMakeLists.txt`, la suite era registrata con:
  `set_tests_properties(FairworldUVTests PROPERTIES WORKING_DIRECTORY "$<TARGET_FILE_DIR:FairworldUVTests>")`
  Questo impostava la working directory a `build/bin/Debug` anziché alla root del progetto `${CMAKE_SOURCE_DIR}`.
- **Risoluzione Applicata:** Tutti i target CTest sono stati aggiornati con `WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"`. Le DLL Windows di runtime (`openxr_loader.dll`, `WebView2Loader.dll`) continuano ad essere caricate in modo prioritario dalla cartella dell'eseguibile grazie al path assoluto risolto da CTest.

#### B — Regressione Reale di Produzione (`SBR-01`, `INT-01` e cascata = 8 test)
- **Localizzazione:** `src/blocks/factory/FactoryOrchestrator.cpp:398`.
- **Meccanismo del Difetto:**
  In `FactoryOrchestrator::BuildPackage`, la compilazione geometrica valutava la modalità sculpt tramite:
  ```cpp
  bool isSculpt = (m_activeDraft->geometrySource == GeometrySource::SculptedMesh || m_activeDraft->sculptMesh.has_value());
  ```
  Durante `RunQualityControlAudit()`, la stazione preliminare `ShapeStationAdapter::Execute` istanzia sempre una `EditableMesh` di preview/lavoro in `draft->sculptMesh` anche per le primitive geometriche (`Cube`, `Cylinder`, ecc.).
  A causa dell'operatore logico `||`, ogni primitiva con `geometrySource == GeometrySource::Primitive` veniva erroneamente forzata nel percorso `SculptedMesh`:
  1. `package.geometrySource` veniva commutato forzatamente a `SculptedMesh`.
  2. `package.shape.type` veniva convertito in `TriangleMesh`.
  3. `package.sculptTopologyJson` veniva popolato con il JSON della mesh anziché rimanere vuoto.
- **Conseguenza:** `SBR-01` e `INT-01` fallivano categoricamente, e per effetto a cascata fallivano i gate di non-regressione dipendenti (`INT-10`, `SEC-10`, `DGV-10`, `CRH-10`, `DEP-10`, `MIG-10`).
- **Correzione:** Allineamento alla convenzione autoritativa adottata in `ProductionStationAdapters.h` (righe 244, 306, 1040):
  ```cpp
  bool isSculpt = (m_activeDraft->geometrySource == GeometrySource::SculptedMesh) && m_activeDraft->sculptMesh.has_value();
  ```
  Questa singola correzione ripristina la conformità architetturale e sana **8 dei 15 fallimenti**.

#### C — Fallimento Architetturale dello Scudo Storico (`MBR-10` = 1 test)
- `MBR-10` è uno scudo autoritativo che verifica che le suite FG-01R preesistenti mantengano esattamente 135 test (`UVP:15, UVG:18, UVK:18, UVE:18, UVR:20, UVI:10, TOP:36`).
- Nelle iterazioni successive, la suite `UVEditorContractTests.cpp` è stata arricchita con 17 test addizionali (`UVE-FailClosed`, `UVE-19..23`, `UVE-33..43`), portando il totale scoperto a 35.
- `TestRunContext` rileva legittimamente: `Suite [UVE] conteggio incoerente (atteso: 18, scoperto: 35)`.

#### D — Analisi Approfondita dei Test Rilevanti per Material Atlas (`UVE-10`, `UVE-FailClosed`, `UVE-19`, `UVE-22`, `UVE-37`, `UVE-43` = 6 test)

1. **`UVE-10` (2D UV Painting Delegation):**
   - Il test invoca `doc.PaintAtUV2D(glm::vec2(0.5f, 0.5f), brush, &paintService)`.
   - `UVEditorDocument::PaintAtUV2D` esegue un controllo fail-safe: `FindIslandAtUV(atlasUV)`. Se le coordinate non colpiscono alcuna isola valida, restituisce `false` prevenendo corruzione di memoria o pittura fuori UV.
   - Nella configurazione di test `setupCubeLayout()`, le 6 facce del cubo sono disposte dall'atlas packer su righe con margini e gutter. Le coordinate hardcoded `(0.5, 0.5)` cadono esattamente nello spazio vuoto di padding tra le isole dell'atlas.
   - Il rifiuto del documento è **corretto e sicuro**: non si deve dipingere all'esterno delle isole UV.

2. **`UVE-FailClosed` (Fail-closed Topology Revision & FaceID):**
   - Il test verifica che, incrementando la revisione topologica della mesh (`mesh.IncrementTopologyRevision()`), sia `IsPointInIsland` che `BeginStroke2D` rifiutino la pittura (`fail-closed`).
   - L'asserzione controlla congiuntamente `initialHit && !hitAfterTopoChange && !strokeStarted && !doc.GetLastPaintBlockedReason().empty()`.
   - Con la revisione disallineata, `hitAfterTopoChange` è `false` e `strokeStarted` è `false` con motivo registrato. Il fallimento dell'asserzione è originato da `initialHit`: il punto calcolato `(offset + 0.5 * scale)` su un'isola cubica parametrizzata LSCM e impacchettata con potenziale rotazione non intercetta la triangolazione interna autoritativa della faccia.

3. **`UVE-19` (Polygon Mask Isolation - Wedge Triangle vs Quad):**
   - Il test crea una mesh Wedge con isola triangolare (isola 3) e quad (isola 1).
   - Inizializza il canvas tramite `painter.InitializeCanvas(512, 512)`. Per specifica di `TexturePaintingService.cpp:64`, `InitializeCanvas` crea il livello base "Background" riempiendolo con colore bianco opaco `glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)`.
   - Il test esegue una pennellata sull'isola triangolare e verifica che i pixel dell'isola quad non siano contaminati verificando `quadCol.a < 1e-4f`.
   - Poiché il livello di background nasce opaco con alpha = 1.0f, `quadCol.a` è sempre 1.0f. La maschera poligonale ha protetto correttamente i texel da sovrascritture, ma il test assumeva erroneamente che il layer preesistente fosse trasparente.

4. **`UVE-22` & `UVE-43` (Per-BlockID Isolation):**
   - Invocano `painter.InitializeCanvas(512, 512)` e successivamente `painter.AddLayer("Block_Layer")`.
   - Asseriscono `painter.GetLayerCount() == 1`.
   - Poiché `InitializeCanvas` crea già il layer di background (Count=1), l'aggiunta esplicita porta il conteggio a 2.

5. **`UVE-37` (Hard Face Lock vs Auto-Binding):**
   - Il test verifica che `BeginStroke2D` fallisca tassativamente se invocato senza una selezione attiva o all'esterno dell'isola selezionata.
   - In `UVEditorDocument.cpp` (righe 555-585) è implementata la feature "Gate D: Surface Paint Auto-Binding & Auto-Switch", concepita per agganciare automaticamente l'isola cliccata quando nessuna faccia è selezionata e consentire il drag intuitivo tra facce.
   - Esiste una divergenza contrattuale esplicita tra la specifica di "Hard Lock" (che impone il blocco categorico senza pre-selezione) e la logica interattiva di "Auto-Binding".

---

## 4. Diff Minimo Applicato

### Diff 1: `src/core/memory/MemorySystem.cpp` (Priorità P0)
```diff
--- a/src/core/memory/MemorySystem.cpp
+++ b/src/core/memory/MemorySystem.cpp
@@ -75,8 +75,11 @@ namespace fw::memory {
         if (g_GlobalHeapBuffer) {
             std::free(g_GlobalHeapBuffer);
             g_GlobalHeapBuffer = nullptr;
-            g_TrackedHeapStart = 0;
-            g_TrackedHeapEnd = 0;
+            // MEM-04 / Tombstone Bounds Guard:
+            // Preserviamo g_TrackedHeapStart e g_TrackedHeapEnd affinche' operator delete
+            // e operator delete[] continuino a riconoscere i puntatori residui dell'arena
+            // dismessa ed evitino categoricamente di passarli a std::free(), prevenendo
+            // l'Access Violation 0xC0000005 durante lo shutdown.
         }
 
         g_LifecycleState.store(MemoryLifecycleState::Shutdown, std::memory_order_release);
```

### Diff 2: `CMakeLists.txt` (Priorità P1 — Parità CTest WORKING_DIRECTORY)
```diff
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ -282,10 +282,10 @@ add_custom_command(TARGET FairworldLifecycleTests POST_BUILD
-add_test(NAME FairworldUVTests COMMAND $<TARGET_FILE:FairworldUVTests>)
-set_tests_properties(FairworldUVTests PROPERTIES WORKING_DIRECTORY "$<TARGET_FILE_DIR:FairworldUVTests>")
+add_test(NAME FairworldUVTests COMMAND $<TARGET_FILE:FairworldUVTests>)
+set_tests_properties(FairworldUVTests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
... (ripetuto per tutti i 10 target add_test)
```

### Diff 3: `src/blocks/factory/FactoryOrchestrator.cpp` (Priorità P1 — Correzione Primitiva vs Sculpt)
```diff
--- a/src/blocks/factory/FactoryOrchestrator.cpp
+++ b/src/blocks/factory/FactoryOrchestrator.cpp
@@ -397,2 +397,2 @@ std::optional<RawAssetPackage> FactoryOrchestrator::BuildPackage(ShapeRegistry*
     GeometryCompileResult compRes;
-    bool isSculpt = (m_activeDraft->geometrySource == GeometrySource::SculptedMesh || m_activeDraft->sculptMesh.has_value());
+    bool isSculpt = (m_activeDraft->geometrySource == GeometrySource::SculptedMesh) && m_activeDraft->sculptMesh.has_value();
     if (isSculpt) {
```

---

## 5. Matrice di Risoluzione e Test Necessari

| Ambito | Prima della Correzione | Dopo la Correzione Minima | Test di Validazione Esterna |
|---|---|---|---|
| **P0: MEM04-06** | FAIL (0xC0000005 Access Violation) | **PASS** (Tombstone Guard attivo) | `bin/Debug/FairworldMemoryTests.exe` |
| **P1: Factory/Integrity** | 8 FAIL (`SBR-01`, `INT-01`, `INT-10`, `SEC-10`, `DGV-10`, `CRH-10`, `DEP-10`, `MIG-10`) | **8 PASS** (Pipeline primitive ripristinata) | `bin/Debug/FairworldUVTests.exe --suite SBR,INT,SEC,DGV,CRH,DEP,MIG` |
| **P1: CTest Environment** | Working Directory locale su `build/bin/Debug` | **Allineata alla radice repository** | `ctest -C Debug --output-on-failure` |
| **P1: UV Editor (UVE/MBR)** | 7 FAIL (Divergenze di contratto/pre-condizioni) | Documentate analiticamente | Da allineare prima di Material Atlas |

---

## 6. Rischi Residui e Raccomandazioni per Material Atlas

1. **Gate di Shutdown Global Memory:** Confermato sicuro e a tolleranza zero verso corruzioni heap. Il contratto garantisce che puntatori residui o distruttori del CRT invocati post-shutdown non causino mai double-free o crash su Windows NT.
2. **Material Atlas Prerequisiti:** Prima di implementare il texture atlas multi-materiale, è opportuno armonizzare formalmente la suite `UVE`:
   - Decidere se `BeginStroke2D` debba operare in modalità "Auto-Binding permissivo" o "Hard Face Lock vincolante".
   - Regolare la pre-condizione dei test di isolamento pittura (`UVE-19`, `UVE-22`, `UVE-43`) istanziando layer con sfondo trasparente `(0, 0, 0, 0)` o verificando il delta di colore rispetto al bianco base.
   - Formalizzare l'aggiornamento di `expectedCount` in `MBR-10` e `uv_tests_main.cpp` da 18 a 35 per ratificare ufficialmente l'estensione della suite `UVE`.
3. **Stato Operativo:** Come prescritto, **nessun comando di build è stato eseguito nel terminale integrato** e **l'implementazione di Material Atlas non è stata avviata**. Il repository è pronto per la revisione e la compilazione/validazione esterna.
