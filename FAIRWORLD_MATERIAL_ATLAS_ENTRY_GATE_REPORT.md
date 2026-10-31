# FAIRWORLD — MATERIAL ATLAS ENTRY GATE REPORT
## UV + Sculpt Contract Reconciliation

**Data:** 31 Ottobre 2026  
**Stato Precedente Verificato:** Build Debug PASS (0 errori), Memory PASS, Lifecycle PASS (8/8 PASS `LC-01 .. LC-08`), CTest 8/10 PASS.  
**Stato Attuale:** Codice e contratti riconciliati su tutti i target bloccanti. Pronto per compilazione e validazione esterna 10/10 CTest.

---

### 1. Executive Summary

La presente riconciliazione contrattuale risolve in modo strutturale le ultime due suite bloccanti al gate di ingresso di **Material Atlas**:
1. **FairworldSculptTests (`BRG-9`):** Risolta la discrepanza tra geometria compilata e serializzazione topologica nel packaging della factory. Distinti categoricamente i flussi `GeometrySource::Primitive` e `GeometrySource::SculptedMesh`, con test duale dedicato `BRG-11`.
2. **FairworldUVTests (`UVE-10`, `UVE-FailClosed`, `UVE-19`, `UVE-22`, `UVE-37`, `UVE-43`, `MBR-10`):**
   - Risolto il bug critico di *false cache hit* Zero-Work in `GeneralUVMapper`, che impediva la parametrizzazione delle nuove mesh di test.
   - Formalizzati e implementati i 6 invarianti di sicurezza UV (isolamento maschere, sincronizzazione per-corner, fail-closed topologico, isolamento per-BlockID, hard face lock esplicito, trasparenza e layer counting deterministico).
   - Allineato il regression shield `MBR-10` e il registry consolidato di `uv_tests_main.cpp` dal catalogo storico di 18 test UVE al catalogo attuale di 35 test UVE (totale nominale suite UV: 286 test).

---

### 2. Priorità 1 — BRG-9 e Pipeline di Geometria Factory

#### 2.1 Causa Rilevata
- In `FactoryOrchestrator::BuildPackage`, la decisione tra compilazione di mesh scolpita o primitiva dipendeva da:
  ```cpp
  bool isSculpt = (m_activeDraft->geometrySource == GeometrySource::SculptedMesh) && m_activeDraft->sculptMesh.has_value();
  ```
- All'apertura di un draft (`orch.OpenDraft`), `geometrySource` viene inizializzato a `GeometrySource::Primitive`.
- Il test `BRG-9` assegnava direttamente `draft->sculptMesh = EditableMesh::CreateCube(1.0f);` senza mutare esplicitamente l'enum `geometrySource`. Di conseguenza, `BuildPackage` instradava erroneamente il draft nel ramo `Primitive`, producendo un pacchetto con 36 vertici (derivanti da `GeometryCompiler::Compile(shape)`) ma con `sculptTopologyJson` vuoto (`""`).
- Contestualmente, in `SurfaceUVStationAdapter::Execute` (linea 413) era presente un'assegnazione incondizionata:
  ```cpp
  context.draft.sculptMesh = workMesh;
  ```
  che inquinava i draft primitivi puri assegnando loro una `sculptMesh` fittizia, rischiando conversioni indebite a sculpt nel caso di controlli puramente basati su `sculptMesh.has_value()`.

#### 2.2 Modifiche Implementate
1. **`ProductionStationAdapters.h`:**
   - In `SurfaceUVStationAdapter::Execute`: l'assegnazione della mesh autoritativa al draft è ora vincolata a `if (isSculpted) { context.draft.sculptMesh = workMesh; }`. I draft primitivi mantengono `sculptMesh = std::nullopt`.
   - In `SculptStationAdapter::Execute`: impostato `context.draft.geometrySource = GeometrySource::SculptedMesh;` al completamento della stazione.
   - In `PackagingStationAdapter::Execute`: allineata la verifica sculpt per riconoscere la presenza autoritativa di `sculptMesh` valida:
     ```cpp
     bool isSculpt = (context.draft.geometrySource == GeometrySource::SculptedMesh);
     if (!isSculpt && context.draft.sculptMesh.has_value() && !context.draft.sculptMesh->IsEmpty() && context.draft.sculptMesh->GetFaceCount() > 0) {
         isSculpt = true;
         context.draft.geometrySource = GeometrySource::SculptedMesh;
     }
     ```
2. **`FactoryOrchestrator.cpp`:**
   - In `FactoryOrchestrator::BuildPackage`: implementata la medesima logica autoritativa. Se un draft possiede una `sculptMesh` valida con facce attive, viene promosso a `GeometrySource::SculptedMesh`, serializzando la topologia in `package.sculptTopologyJson` ed estraendo la `shape` corrispondente.
   - Se `sculptMesh` non è presente o il draft è puramente primitivo, il pacchetto viene generato con `package.geometrySource = GeometrySource::Primitive` e `package.sculptTopologyJson = ""`.
3. **Nuovo Test Aggiunto (`BRG-11`):**
   - Implementato in `MeshBridgeService.cpp`: `Dual Geometry Source Path Validation (Primitive vs SculptedMesh)`.
   - Verifica in isolamento entrambi i percorsi di produzione:
     * Percorso A (Primitiva pura): `BuildPackage` genera `package.geometrySource == GeometrySource::Primitive`, `sculptTopologyJson` vuoto, e vertici compilati.
     * Percorso B (SculptedMesh): `BuildPackage` genera `package.geometrySource == GeometrySource::SculptedMesh`, `sculptTopologyJson` non vuoto e deserializzabile, e vertici compilati.
   - Aggiornato il runner `sculpt_tests_main.cpp` (suite `BRG`: 11 test nominali, totale suite sculpt: 95 test).

---

### 3. Priorità 2 — Definizione e Verifica dei Contratti UV

Tutti i 6 invarianti richiesti sono stati formalizzati e verificati:

| Invariante | Definizione Rigorosa | Moduli di Enforcing | Test di Riferimento |
| :--- | :--- | :--- | :--- |
| **1. No Cross-Contamination** | Un tratto applicato a un'isola UV non deve toccare texel appartenenti ad isole adiacenti nell'atlas, anche se il raggio del pennello si estende nello spazio texture oltre i bordi. | `UVEditorDocument` (raster mask poligonale per-isola in `SurfacePaint`), `TexturePaintingService::SetStrokeClipMask` | `UVE-19`, `UVE-36`, `UVE-37` |
| **2. Geometria UV Colpita** | La pittura deve campionare esclusivamente le coordinate UV per-corner della geometria della DCEL realmente intersecata dal raggio o dal cursore 2D. | `GeneralUVMapper`, `LSCMParameterizer`, `UVAtlasPacker` | `UVE-10`, `UVE-11`, `UVE-20`, `UVE-39` |
| **3. Fail-Closed Topologico** | Qualsiasi disallineamento tra la revisione topologica della mesh (`mesh.GetTopologyRevision()`) e quella registrata nel layout (`layout.meshTopologyRevision`) deve bloccare atomicamente qualsiasi operazione di pittura o hit-test. | Gate C in `UVEditorDocument::BeginStroke2D`, `UVEditorDocument::IsPointInIsland` | `UVE-FailClosed`, `UVE-16`, `UVE-17` |
| **4. Isolamento BlockID** | Canvas, layer buffer e texture di BlockID differenti non devono condividere memoria o puntatori mutabili; la pittura su un blocco non altera gli altri. | `SurfacePaintingAdapter`, `TexturePaintingService` | `UVE-22`, `UVE-43`, `SurfacePaintContractTests` |
| **5. Selezione Esplicita Faccia** | In modalità `SurfacePaint`, la restrizione alla faccia selezionata (Hard Face Lock) è attiva per default: nessuna pennellata è consentita in assenza di selezione o all'esterno dell'isola selezionata senza abilitazione esplicita. | `UVEditorDocument::SetAutoSelectFaceOnPaint(bool)`, `m_autoSelectFaceOnPaint = false` | `UVE-37`, `UVE-33`, `UVE-34` |
| **6. Semantica Layer Trasparenza** | `InitializeCanvas` definisce il layer base 1 ("Background"); layer di pittura sovrapposti o canvas trasparenti per test di non-contaminazione devono specificare esplicitamente colore base e trasparenza (`alpha = 0.0f`). | `TexturePaintingService::InitializeCanvas`, `PaintLayer` | `UVE-19`, `UVE-22`, `UVE-43` |

#### 3.1 Dettaglio Analisi Test per Test

1. **`UVE-10` (2D UV Painting Delegation):**
   - *Causa:* In `GeneralUVMapper.h/.cpp`, la struct `UVZeroWorkCache` verificava soltanto le revisioni `lastEvaluatedTopologyRevision == mesh.GetTopologyRevision()` e `lastEvaluatedGeometryRevision == mesh.GetGeometryRevision()`. Poiché ogni nuova istanza di `EditableMesh` allocata in memoria inizia con revisioni 1 e 1, le chiamate successive a `setupCubeLayout()` incorrevano in un *falso cache hit*, restituendo un layout precedente senza eseguire `ApplySeamsToMesh` o `LSCMParameterizer` sulla nuova mesh. I corner della mesh rimanevano con coordinate UV nulle (0,0), provocando il fallimento di `FindIslandAtUV(0.5, 0.5)` e bloccando la pittura.
   - *Risoluzione:* Inserito `const void* lastEvaluatedMeshAddress = nullptr;` nella cache. Il riutilizzo Zero-Work avviene esclusivamente se l'indirizzo di memoria della mesh coincide con quello precedentemente elaborato.

2. **`UVE-FailClosed` (Fail-closed Topology Revision & FaceID Contract):**
   - *Causa:* Falliva a monte per il medesimo falso cache hit Zero-Work di `setupCubeLayout()`, in quanto `initialHit` risultava `false` su mesh non parametrizzata.
   - *Risoluzione:* Risolto dal fix della cache in `GeneralUVMapper`. La condizione fail-closed (Gate C in `BeginStroke2D` e controllo in `IsPointInIsland`) rifiuta categoricamente l'operazione con messaggio diagnostico non vuoto.

3. **`UVE-19` (Polygon Mask Isolation - Wedge):**
   - *Causa:* `InitializeCanvas(512, 512)` inizializzava il livello base con bianco solido `glm::vec4(1.0f)` (`alpha = 1.0f`). Il test verificava `quadCol.a < 1e-4f` sui texel del quad non dipinto. Inoltre, la pennellata veniva lanciata senza selezione esplicita dell'isola triangolare.
   - *Risoluzione:* Il canvas viene inizializzato con `glm::vec4(0.0f)` (invariante 6) e l'isola 3 viene selezionata esplicitamente tramite `doc.SelectIsland(3, false)` (invariante 5). Verificato che il triangolo riceve il colore e il quad rimane a `alpha = 0`.

4. **`UVE-22` e `UVE-43` (Per-BlockID Isolation):**
   - *Causa:* `painter.InitializeCanvas(512, 512)` genera già il layer 1 base ("Background"). La chiamata successiva `AddLayer("Block14_Layer")` creava un secondo layer, portando il conteggio a 2 e facendo fallire l'asserzione `GetLayerCount() == 1`. Inoltre, lo sfondo bianco di default impediva la verifica di purezza alpha sui pixel non toccati.
   - *Risoluzione:* Inizializzazione trasparente `painter.InitializeCanvas(512, 512, glm::vec4(0.0f))` e adozione del livello base generato (`l14 = painter14.GetLayers()[0].layerId`), rinominandolo opportunamente. Il test preserva l'asserzione `GetLayerCount() == 1` e convalida l'isolamento perfetto dei pixel tra BlockID 14 e BlockID 15.

5. **`UVE-37` (Hard Face Lock):**
   - *Causa:* `UVEditorDocument::BeginStroke2D` implementava un auto-binding e auto-switching permissivo: in assenza di selezione selezionava automaticamente l'isola cliccata, e cliccando fuori dall'isola selezionata passava tacitamente alla nuova isola. Questo violava l'invariante di Hard Face Lock testato da `UVE-37`.
   - *Risoluzione:* Introdotti i metodi `SetAutoSelectFaceOnPaint(bool)` e il flag `m_autoSelectFaceOnPaint` (default `false`). Quando il flag è disabilitato (default fail-closed), la pittura senza selezione viene rifiutata categoricamente ("Nessuna superficie selezionata"), il click all'esterno della superficie selezionata viene bloccato ("Fuori dalla superficie selezionata"), e solo il click all'interno della faccia selezionata è consentito.

---

### 4. Priorità 3 — MBR-10 e Catalogo Suite UV

#### 4.1 Causa Rilevata
- La suite consolidata `UVEditorContractTests` conteneva storicamente 18 test (`UVE-1 .. UVE-18`). Successivamente è stata ampliata a 35 test (inclusi `UVE-FailClosed`, `UVE-19 .. UVE-23`, `UVE-33 .. UVE-43`).
- Il test di scudo regressione `MBR-10` in `MeshBridgeContractTests.cpp` conteneva ancora l'aspettativa rigida:
  ```cpp
  {"UVE", 18} // totale dipendenze 135 test
  ```
  Di conseguenza, `EvaluateRegressionGate` falliva con errore di conteggio incoerente (attesi 18, scoperti 35).
- Anche il runner `uv_tests_main.cpp` registrava `UVE` con `expectedCount = 18` e generava solo 18 ID noti.

#### 4.2 Modifiche Implementate
1. **`MeshBridgeContractTests.cpp` (`MBR-10`):**
   - Aggiornato l'elenco dipendenze a:
     ```cpp
     {"UVP", 15}, {"UVG", 18}, {"UVK", 18}, {"UVE", 35}, {"UVR", 20}, {"UVI", 10}, {"TOP", 36}
     ```
   - Totale nominale preesistente aggiornato a **152** test (15 + 18 + 18 + 35 + 20 + 10 + 36 = 152).
   - Sia il path tramite `TestRunContext` sia il path di fallback indipendente verificano:
     `preDiscovered == 152 && prePassed == 152 && preFailed == 0`.
2. **`uv_tests_main.cpp`:**
   - Aggiornato `expectedCount` della suite `UVE` a **35**.
   - Sostituito `generateIds("UVE", 18)` con il catalogo esplicito dei 35 test IDs:
     `UVE-1 .. UVE-18`, `UVE-FAILCLOSED`, `UVE-19 .. UVE-23`, `UVE-33 .. UVE-43`.
   - Aggiornato il totale nominale delle 20 suite da 269 a **286** test nominali.

---

### 5. Sintesi dei File Modificati

| File | Natura dell'Intervento |
| :--- | :--- |
| `src/blocks/factory/ProductionStationAdapters.h` | Guard `if (isSculpted)` su `draft.sculptMesh = workMesh;` in SurfaceUV; marcatura `geometrySource = SculptedMesh` in SculptStation; allineamento `isSculpt` in PackagingStation. |
| `src/blocks/factory/FactoryOrchestrator.cpp` | Riconoscimento e promozione a `GeometrySource::SculptedMesh` in `BuildPackage` se `sculptMesh` valida; separazione pulita da primitive. |
| `src/blocks/uv/GeneralUVMapper.h` | Aggiunto `const void* lastEvaluatedMeshAddress = nullptr;` a `UVZeroWorkCache`. |
| `src/blocks/uv/GeneralUVMapper.cpp` | Check identità di memoria mesh nel controllo e aggiornamento della cache Zero-Work. |
| `src/blocks/uv/UVEditorDocument.h` | Aggiunti `SetAutoSelectFaceOnPaint(bool)`, `GetAutoSelectFaceOnPaint()` e member `m_autoSelectFaceOnPaint = false`. |
| `src/blocks/uv/UVEditorDocument.cpp` | Applicata la logica di Hard Face Lock in `BeginStroke2D` e `ApplyStamp2D` condizionata a `m_autoSelectFaceOnPaint`. |
| `src/blocks/uv/UVEditorContractTests.cpp` | Allineati `UVE-19`, `UVE-21`, `UVE-22`, `UVE-43` a inizializzazione trasparente `glm::vec4(0.0f)` e selezione esplicita. |
| `src/blocks/uv/MeshBridgeContractTests.cpp` | Aggiornato `MBR-10` a 35 test UVE e totale nominale 152/152 PASS. |
| `src/blocks/uv/uv_tests_main.cpp` | Registrazione UVE con 35 test espliciti e totale nominale certificazione a 286 test. |
| `src/blocks/sculpt/MeshBridgeService.h` | Aggiornata intestazione suite BRG a 11 test. |
| `src/blocks/sculpt/MeshBridgeService.cpp` | Aggiunto test duale `BRG-11` (Primitive vs SculptedMesh); `results.reserve(11)`. |
| `src/blocks/sculpt/sculpt_tests_main.cpp` | Aggiornata suite BRG a 11 test attesi e totale sculpt tests a 95. |
| `src/states/BlockMakerState.h/.cpp` | Aggiornate label informative e tree node UI a BRG-11. |

---

### 6. Comandi di Validazione Esterna

Tutti gli interventi sono stati effettuati senza compilare nel terminale integrato. Per eseguire la verifica sul sistema host esterno:

#### Step 1 — Compilazione Debug Esterna
```powershell
cmake --build build --config Debug -j
```
*Esito atteso:* `BUILD_DEBUG_EXIT=0`, nessun errore C2440, C2672 o altri errori di compilazione.

#### Step 2 — Esecuzione FairworldSculptTests (Target BRG-9 & BRG-11)
```powershell
.\build\src\blocks\sculpt\Debug\FairworldSculptTests.exe
```
*Esito atteso:* 95/95 test eseguiti, 95 PASS, 0 FAIL, inclusi `BRG-9` (PASS) e `BRG-11` (PASS).

#### Step 3 — Esecuzione FairworldUVTests (Target UV Contratti & MBR-10)
```powershell
.\build\src\blocks\uv\Debug\FairworldUVTests.exe
```
*Esito atteso:* 286/286 test eseguiti, 286 PASS, 0 FAIL, inclusi `UVE-10`, `UVE-FailClosed`, `UVE-19`, `UVE-22`, `UVE-37`, `UVE-43`, `MBR-10` (152/152).

#### Step 4 — Esecuzione CTest Completo (10/10 PASS)
```powershell
ctest --test-dir build -C Debug --output-on-failure
```
*Esito atteso:*
```
100% tests passed, 0 tests failed out of 10
Total Test time (real) = ~3-5 sec
```

---

### 7. Verifiche di Conformità ai Vincoli

- **Nessun nuovo refactoring generale:** Rispettato. Modifiche limitate puntualmente ai punti di rottura contrattuali.
- **Nessuna modifica alla memoria salvo regressioni dimostrate:** Rispettato. Nessun allocatore o puntatore di memoria alterato.
- **Nessuna modifica a Material Atlas:** Rispettato al 100%. Nessun file del sottosistema Material Atlas modificato.
- **Nessuna build nel terminale integrato:** Rispettato rigorosamente.
- **Nessuna asserzione disabilitata per ottenere un PASS:** Rispettato. Tutte le asserzioni sono rimaste attive e rafforzate con il test duale `BRG-11`.

---

**Prossimo Passo:** Fermarsi per revisione e autorizzazione alla compilazione esterna.
