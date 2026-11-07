# FAIRWORLD — D1.2.17 Planetary Runtime, Input & Material Contract Verification

**Modalità:** audit statico e test automatici isolati CPU/headless. Nessuna GUI o Computer Use. Nessun sorgente, shader, serializer o asset di produzione è stato modificato. Nessun salvataggio originale è stato ripristinato o scritto. Nessun commit/push.

## Esito sintetico

I test headless pertinenti disponibili sono PASS, ma non verificano l'intero percorso richiesto. Il contratto geometrico D1.2, i producer UV/vertex e la cache PBR hanno copertura automatica; non esiste nei target disponibili un test end-to-end Tiny che attraversi `MapWorldGenerator → TerrainSolver → ECS → GameWorld voxel mesh`, né un test headless del polling fisico Action Map, della camera di gameplay o dell'upload/rendering GPU.

| Area | Stato | Evidenza principale |
| --- | --- | --- |
| 1. Geometria planetaria | **PARTIAL PASS** | D1.2 PASS; gravità e frame radiale verificati staticamente; continuità del frame camera ai poli non testata. |
| 2. Action Map e device | **PARTIAL / NOT VERIFIED** | Wiring statico verificato; suite AsyncInput PASS ma non esercita polling Win32, binding fisici o `WantCapture`. |
| 3. Generazione voxel planetaria | **NOT VERIFIED** | Fixture Tiny preparato; nessun runner headless integra il fixture con MapWorldGenerator e mesh ECS. |
| 4. Materiali e texture | **PARTIAL PASS** | Forge vertex e cache PBR PASS; ID 0/1/17 verificati nel codice/configurazione, non nel rendering GPU effettivo. |
| 5. Safety e integrità | **PASS per i test eseguiti** | Tutti i runner eseguiti da cwd isolata; hash dei due file divergenti invariati; fixture e log separati. |
| 6. Piano patch | **COMPLETATO, patch non applicate** | Piano indipendente A–D in §8. |

Nessun requisito non eseguito è marcato PASS per inferenza. **D1 non è dichiarato FROZEN.**

## Ambiente, comandi e scritture

**Build usata:** eseguibili Debug già presenti in `D:\FAIRWORLD\FAIRWORLD\bin\Debug`. Nessuna build è stata avviata.

**Sandbox di questa validazione:**

- Working directory per tutti i runner: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\runtime`
- Log: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs`
- Fixture: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json`

| Comando eseguito | CWD | Exit code | Durata osservata | Esito / scritture |
| --- | --- | ---: | ---: | --- |
| `D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldEditor.exe --validate-d12` | `...\D1_2_17_HEADLESS_SANDBOX\runtime` | 0 | 102 ms | 4 gruppi D1.2 PASS. Il gruppo di persistenza crea JSON e `.fwb` unici sotto `%TEMP%` e li rimuove via cleanup RAII (`TerrainValidation.cpp:1011-1070`). Log `logs\d12.log`. |
| `D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldForgeVertexContractTests.exe` | stessa cwd | 0 | 45 ms | 3 gruppi PASS; test in memoria. Log `logs\forge_vertex.log`. |
| `D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldTextureCacheTests.exe` | stessa cwd | 0 | 390 ms | 10/10 PASS. Il runner usa root univoche `%TEMP%\fairworld-pbr-cache-tests-*`, include `remove_all` del solo root temporaneo nel distruttore e non usa la cache di progetto (`TextureCacheContractTests.cpp:15-25`). Nessuna directory di test è rimasta. Log `logs\texture_cache.log`. |
| `D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldAsyncInputTests.exe` | stessa cwd | 0 | 263.97 ms (durata stampata dal runner) | 7/7 PASS. Esercita lifecycle/ownership/double-buffering di `AsyncInput`, non tastiera fisica, Action Map o ImGui. Log `logs\async_input.log`. |

La suite CTest completa non è stata invocata. I test CMake sono registrati con working directory pari alla root sorgente (`CMakeLists.txt:342-396`), quindi non sono stati lanciati indiscriminatamente da lì.

### Integrità dei file divergenti già noti

| File originale | Prima dei test | Dopo i test | Risultato |
| --- | --- | --- | --- |
| `assets/texture_cache.bin` | 805,306,460 byte; SHA-256 `D1D8655D0B929F0367D8B67473ED36B88E1D1DC5517A543643590A6DEC8E621D` | stessi byte e stesso SHA-256 | invariato |
| `assets/test_packaging_artifacts/pkg_roundtrip_03.json` | 28,358 byte; SHA-256 `9C91BBA3D20DA85A2EA9D1C46291371F73F5E2B43C012DA2A0873BAE3EC7E089` | stessi byte e stesso SHA-256 | invariato |

Gli hash sono stati misurati sul checkout in `D:\FAIRWORLD\FAIRWORLD`. Non sono stati confrontati con un nuovo manifest e non sono state effettuate operazioni di ripristino.

### Fixture Tiny

È stata copiata una mappa già presente nel sandbox D1.2.8 in una nuova directory isolata e modificata solo la copia:

- `planetSize = 0` (`PlanetSize::Tiny`, risoluzione 3×3 per faccia).
- `isFlat = false`.
- template `default_terrain`, con `planetSize = 0`.
- una sola istanza attiva al centro della faccia 0, coordinate documentali `gridX=1`, `gridY=1`, `isGridAligned=true`.
- spawn di prova al centro della faccia 0.
- SHA-256 fixture: `47AF4358E4BD663926E64A43ACF2B116F3A2939BA5605FC89B2983D32A9AD503`; dimensione 5,384 byte.

Il fixture è **preparato ma non consumato da un test**: non viene dichiarato verificato né usato per simulare un risultato runtime. Rimane nel sandbox per una futura integrazione headless autorizzata.

## 1 — Planetary Geometry Contract

### Expected

Il mondo giocabile è sempre una sfera logica con sistema Cube-Sphere; `PlanetSize` determina risoluzione/raggio, `PlanetChunkCoord` usa coordinate canoniche. Movimento e camera sono locali alla superficie: up radiale, WASD tangenziale, salto lungo up, gravità verso il centro. Le rappresentazioni planari servono a editor/proiezioni. Non deve essere introdotta una modalità gameplay flat.

### Observed

- `PlanetMath` dichiara Tiny/Micro/Small/Medium/Large/Huge con risoluzioni 3/5/7/11/21/41 (`src/core/utils/PlanetMath.h:71-105`).
- D1.2 eseguito: round-trip UV/chart/direction, sei facce, bordi/angoli/tie, sei dimensioni e righe, row flow fino al mapping TerrainSolver/mesh e compatibilità spawn JSON/binaria tutti PASS (`logs\d12.log`). Exit code 0.
- `MapWorldGenerator` converte la row legacy in row canonica all'assegnazione di `PlanetChunkCoord` (`src/world/MapWorldGenerator.cpp:69-110`). I test D1.2 coprono l'inversione singola nel flusso TerrainSolver/mesh.
- PlayState calcola lo spawn custom da face/chart e lo colloca radialmente a `R + heightOffset` (`src/states/PlayState.cpp:162-176`).
- `PlayerMovementSystem` usa `up = normalize(playerPosition - planetCenter)` quando `isLogicalSphere`; yaw ruota intorno a up, forward/right sono tangenti, WASD produce velocità tangenziale e salto usa `jumpForce` lungo up (`src/systems/Systems.cpp:127-222`). `CameraSystem` usa lo stesso frame (`:15-73`).
- `PhysicsEngine::ApplyGravity` sceglie forza verso il centro se il pianeta è logico-sferico e gravità -Y nel ramo non sferico (`src/systems/PhysicsEngine.cpp:60-99`). PlayState assegna il flag geometrico secondo la dimensione documento (`PlayState.cpp:267-275`) e, dopo generazione, lo forza a `true` (`:321-326`).
- Il riferimento tangente in camera/movimento cambia formula quando `abs(dot(up, north))` raggiunge 0.99 (`Systems.cpp:54-60,171-179`). D1.2 testa la continuità Cube-Sphere, non la continuità della base tangente dinamica della camera attorno a questa soglia.

### Stato

**PARTIAL PASS.** Mapping canonico, bounds e conversione row PASS; collegamento sorgente di spawn, movimento e gravità coerente con il contratto sferico. **NOT VERIFIED** per esecuzione reale di movimento/salto/gravità e continuità della camera vicino ai poli: i test disponibili non eseguono questi consumer.

### `isFlat` legacy

La scrittura finale `geom.isLogicalSphere = true` in PlayState è **confermata**. Alla luce del contratto architetturale D1.2.17 è coerente con il requisito che il gameplay sia sempre planetario. La scrittura precedente derivata da `originalFlat` è sovrascritta e non ha effetto sul flag runtime: questo è un possibile debito di chiarezza/compatibilità fra il formato documentale, le proiezioni editor e il mondo giocabile. Non è una richiesta di rendere flat il gameplay e non è stata proposta la rimozione del flag o una modifica ai serializer.

## 2 — Action Map & Device Contract

### Expected

WASD e Space devono arrivare al consumer di movimento; il mouse deve distinguere UI/look; focus, capture e cursor lock devono produrre gate comprensibili. Input ricevuto, input bloccato dal routing e movimento impedito da fisica devono restare distinguibili.

### Observed

- `ActionMap` è un dizionario globale action→binding, senza contesti per stato (`src/hardware/InputTypes.h:46-59`). I default sono inizializzati se il binding map è vuoto (`DeviceManager.cpp:530-619`).
- Il loop principale chiama `PollHardwareEvents()` e poi `DeviceManager::Update()` (`src/core/app/main.cpp:418-422`). `DeviceManager` polla Win32 `GetKeyboardState` e XInput, risolve binding, compone `InputState` e registra click mouse per place/destroy (`DeviceManager.cpp:342-428,515-527`).
- `DeviceManager` calcola il gate da `WantCaptureKeyboard/Mouse` combinato con `requireFreeCursor` (`:351-355`). Il flag è aggiornato dall'engine nei rami UI/editor (`FAIRWORLD.cpp:497-500`); l'ordine main mostra che il valore aggiornato dall'engine segue l'update input nello stesso ciclo. La possibile latenza di un frame è quindi un rischio statico, non un risultato osservato.
- Lock usa click sinistro raw, Esc e gamepad; `ShowCursor` nasconde/mostra il puntatore, `SetCursorPos` lo ricentra, il delta diventa `lookYaw/lookPitch` (`DeviceManager.cpp:430-513`). `PlayerMovementSystem` non aggiorna il player se `requireFreeCursor` è true; inoltre non consuma movimento quando lo stato combat lo impedisce (`Systems.cpp:146-158,185-213`). Sono gate separati: uno sull'input/consumer, uno sulla capacità di muoversi.
- Planet Mapper e Chunk Editor leggono direttamente `ImGuiIO`/`ImGui::IsMouseDragging`; non usano `DeviceManager` Action Map per muovere le proprie camere (`PlanetMapperCamera.cpp:133-180`; `ChunkEditorState.cpp:240-289`).
- Altri comandi applicativi sono raw `GetAsyncKeyState` (ad esempio F1/B in PlayState e F5/hotbar in FAIRWORLD), quindi non condividono tutti lo stesso routing (`PlayState.cpp:397,485`; `FAIRWORLD.cpp:477-530`).
- AsyncInput lifecycle tests: **7/7 PASS**, exit 0, ma coprono ownership, re-binding, stati e double-buffer queue. Non simulano `GetKeyboardState`, XInput, i default `ActionMap`, ImGui capture, focus della finestra, cursor lock, mouse delta o movimento del rigidbody.

### Stato

**STATIC WIRING PASS / RUNTIME INPUT NOT VERIFIED.** È dimostrato il percorso WASD/Space verso `PlayerMovementSystem`; non è dimostrato che l'OS consegni quegli input nella finestra, che ImGui non li blocchi o che la fisica consenta il movimento in un caso reale. Nessun test automatico disponibile isola questi stati.

## 3 — Planetary Voxel Generation Contract

### Expected

Per il fixture Tiny sferico: template e istanza devono passare dal documento ai chunk ECS con face/col/row valide; il solver deve produrre voxel non-Air; il mesher deve creare mesh associate ai chunk e conservare il material ID.

### Observed

- Chunk Editor legge il template da `terrainLibrary`, costruisce un documento temporaneo per la preview e invoca `MapWorldGenerator`; la preview è distinta dal mondo runtime (`src/states/ChunkEditorState.cpp:71-190`).
- Planet Mapper cerca i `templateId` delle istanze e proietta base/subregioni su coordinate di griglia; la mappa persistita mantiene template e istanze, non payload di voxel già meshed (`src/states/PlanetMapperState.cpp:169-221`; serializzazione `src/world/MapDocument.cpp:450-548,587-747`).
- PlayState carica il documento e chiama `MapWorldGenerator::Generate` per creare i chunk attorno allo spawn (`src/states/PlayState.cpp:254-304`). Il generator espande template/istanze, crea/riusa entità chunk, assegna `PlanetChunkCoord`, `BiomeDataComponent` e `TerrainGenTag` (`src/world/MapWorldGenerator.cpp:35-63,69-127,175-...`).
- `TerrainSolverSystem` legge coordinate planetarie canoniche, risolve base/regional rules, genera il contenuto voxel e marca mesh dirty se i voxel cambiano (`src/systems/TerrainSolverSystem.cpp:32-78,97-165`). `TerrainSolver::ClassifyVoxels` assegna block IDs e usa ID0 come aria/cave (`src/world/TerrainSolver.cpp:289-328`).
- `GameWorld` esclude `block==0`, legge `MaterialRegistry`, produce facce con UV e assegna `materialID=block` (`src/world/GameWorld.cpp:503-576`).
- `FairworldForgeVertexContractTests` verifica orientamenti UV voxel, mapping Cube-Sphere e producer `MeshGenerators`, ma non crea `MapDocument`→chunk ECS né controlla `TerrainGenTag`, voxel non-Air o associazione `MeshComponent` al chunk.
- Il fixture Tiny è stato creato e hashato (§ Ambiente), ma nessun eseguibile test esistente lo consuma. `--validate-d12` verifica le convenzioni spaziali/row, non `MapWorldGenerator` end-to-end. Non è stata eseguita `--validate-terrain` perché la fase CLI carica definizioni relative alla cwd e non costituisce il test richiesto sul fixture; non è stato invocato un runtime grafico.

### Stato

**NOT VERIFIED.** Il percorso è ricostruito staticamente e i sotto-contratti matematici/UV sono coperti, ma nessuna evidenza runtime/headless prova conteggio di chunk, tag, voxel non-Air, material IDs, mesh GPU o associazione mesh/chunk per Tiny.

## 4 — Material & Texture Contract

### Expected

Un block ID voxel non-Air deve mantenere lo stesso ID nel vertex; Forge deve trattare location 2 come UV, location 3 come layer ID materiale; la cache deve mettere il materiale nello slot corrispondente. ID0 è aria nel mesher. I casi 1 e 17 devono arrivare al fallback/texture attesi.

### Observed

- `BlockRegistry::GetBlock(blockName)` restituisce ID del blocco o fallback `fairworld:error` ID255 per nomi sconosciuti (`src/blocks/BlockRegistry.cpp:576-586`). La funzione separata `GetBlockIdByString` restituisce zero su nome sconosciuto (`:589-599`), ma `ResolveTerrainRules` usa `GetBlock(...).id` (`src/world/MapDocument.cpp:1346-1358`). Non è stata trovata in questo percorso una conversione involontaria a zero.
- ID0: il solver lo usa come Air; il voxel mesher salta il blocco 0 (`TerrainSolver.cpp:290-328`; `GameWorld.cpp:503`). Quindi un chunk voxel Air non produce vertici di layer 0. Una preview geometrica esplicita può avere ID0, ma non equivale al rendering dell'aria nel mondo.
- ID1: il registro predefinito è verde (`MaterialRegistry.cpp:23-30`); il record `materials.json` ID1 ha fallback verde e `albedoPath` vuoto (`assets/definitions/materials.json:24-41`). `TexturePacker` genera fallback dinamico da `baseColorFallback` per path vuoto (`TexturePacker.cpp:306-314`), mentre `CacheManager` chiama `UpdateMaterialFallback` nel ramo senza albedo (`CacheManager.cpp:50-62`).
- ID17: `materials.json` associa l'ID17 a `textures/blocks/fairworld_carburotitanio_albedo.png`; il file esiste. `CacheManager` carica i path nella layer indicizzata dall'ID (`CacheManager.cpp:50-69`); `TexturePacker` itera la lista materiale e produce gli array nello stesso ordine (`TexturePacker.cpp:144-...`). L'esistenza non prova upload o campionamento GPU riuscito.
- `fw::Vertex` e i test Forge confermano il layout UV/material ID; `forge.vert` espone location 2 `inUV`, location 3 `inMaterialID`, e passa UV/material ID al fragment (`src/render/forge.vert:15-16,47-62`). `forge.frag` clampa material ID a layer count e campiona albedo/normal/ORM con coordinate `(UV, layer)` (`src/render/forge.frag:122-145`).
- `FairworldForgeVertexContractTests`: voxel orientations, Cube-Sphere UV e mesh producers **PASS**, exit 0 (3/3 gruppi). `FairworldTextureCacheTests`: **10/10 PASS**, exit 0, inclusi cache invalidation, versioning, mapping ID→layer e fallback ID12; non copre specificamente l'effettiva scena voxel ID0/1/17 e non avvia Vulkan.
- I test e l'audit sono CPU-only. Nessuna lettura dei buffer GPU o immagine renderizzata è avvenuta.

### Stato

**PARTIAL PASS.** Mapping CPU vertex→shader e cache v2 sono coperti dai test eseguiti; i percorsi statici ID0/ID1/ID17 sono coerenti col contratto. **NOT VERIFIED** per voxel reale del fixture, texture ID17 caricata nella sessione renderer, campionamento GPU, colore/pixel e comportamento PBR visuale.

## 5 — Safety & Integrity Gate

### Expected

Test solo headless, cwd e destinazioni di scrittura isolate, nessuna scrittura sui salvataggi originali o file divergenti.

### Observed / Stato

**PASS per il perimetro eseguito.** Tutti e quattro i processi hanno avuto cwd `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\runtime`. I test che scrivono usano file temporanei unici in `%TEMP%` con cleanup nel codice; non hanno usato `assets/texture_cache.bin` del progetto. Hash dei due file divergenti invariati. Il fixture è in `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere`; log in `...\logs`. Nessuna GUI, restore, manifest update o modifica ai file originali.

## 6 — Evidenze automatiche e copertura mancante

| Binario | Gruppi/test | Risultato | Exit |
| --- | ---: | --- | ---: |
| `FairworldEditor.exe --validate-d12` | 4 gruppi D1.2 | 4 PASS | 0 |
| `FairworldForgeVertexContractTests.exe` | 3 gruppi (orientamento voxel, Cube-Sphere, producer mesh) | 3 PASS | 0 |
| `FairworldTextureCacheTests.exe` | 10 test cache | 10 PASS | 0 |
| `FairworldAsyncInputTests.exe` | ASYNC-01…07 | 7 PASS | 0 |

Non esistono evidenze in questo report per:

- Action Map fisica da tastiera/mouse + gate ImGui/focus/cursor lock, inclusi input ricevuto vs bloccato.
- CameraSystem/PlayerMovementSystem/PhysicsEngine eseguiti su un player in movimento, salto grounded e gravità presso i poli.
- Generazione integrata del fixture Tiny fino a ECS, voxel non-Air e mesh associata.
- Material ID0/1/17 verificati nel buffer di una mesh runtime e layer corrispondenti verificati in GPU.
- Vulkan, texture sampling o risultati visivi.

Questi punti restano **NOT VERIFIED**, non FAIL e non PASS.

## 7 — Causa confermata o sospetta

### Confermate da sorgente

1. Il gameplay PlayState imposta `isLogicalSphere=true`; questo è coerente con il requisito che il mondo giocabile sia sempre planetario. Il precedente valore derivato da `isFlat` è sovrascritto: possibile debito di compatibilità/semantica del documento, non richiesta di una modalità flat.
2. I binding Action Map sono globali e non contestuali; input polling e raw Win32 convivono.
3. ImGui/cursor gates e la condizione fisica combat/grounded sono separati dal ricevimento input.
4. Chunk Editor e Planet Mapper producono dati documentali/preview; PlayState genera nuovi chunk voxel dal documento.
5. Il mesher voxel mantiene il block ID nel `materialID` per blocchi non-Air; ID0 viene omesso come aria.
6. La cache e shader usano ID come layer; path texture ID17 è presente sul filesystem; il rendering GPU non è stato osservato.

### Sospette / da misurare

1. Un frame di disallineamento `requireFreeCursor` può derivare dall'ordine di aggiornamento tra `DeviceManager` e engine.
2. Il cambio branch tangent frame a dot 0.99 può produrre discontinuità near-pole.
3. La differenza tra asset selezionato in Block Maker e record globale `materials.json` può produrre una preview diversa dal runtime se la registry non è aggiornata/persistita.

Non è confermata una causa unica per i sintomi utente senza una traccia/runtime isolata.

## 8 — Piano patch minimo, indipendente e non applicato

### A. Input routing

1. Aggiungere prima test/strumentazione CPU per verificare ActionBinding→`IsActionActive`→`InputState`, gate `WantCapture`/`requireFreeCursor`, transizioni di focus e lock. Separare i contatori `received`, `filtered`, `consumer-skipped`.
2. Usare i risultati per consolidare in modo mirato i raw input duplicati o il momento di aggiornamento del gate. Mantenere i binding correnti; nessun nuovo action context in questa fase.

### B. Camera e movimento planetario

1. Testare frame up/right/front e angoli camera per sei facce, equatore e campioni ai lati della soglia polare, includendo round-trip camera pose→direzione.
2. Se la discontinuità è dimostrata, cambiare solo la costruzione della base tangente in una routine stabile/continua condivisa tra CameraSystem e PlayerMovementSystem; mantenere CubeSphereMapping invariato.
3. Documentare esplicitamente che il gameplay forza la sfera e mantenere `isFlat` come metadato/proiezione legacy; non rimuovere né migrare il campo.

### C. Generazione voxel

1. Aggiungere un runner CPU headless che carichi il fixture Tiny dalla directory passata esplicitamente, inizializzi BlockRegistry/JobSystem/World e chiami `MapWorldGenerator` + `TerrainSolverSystem` + builder mesh senza creare renderer/Vulkan.
2. Asserire: un template ID risolto, istanza attiva, tutte le coordinate valide, chunk ECS con `TerrainGenTag`, voxel non-Air, ID voxel previsto, `MeshComponent` non vuota collegata allo stesso chunk.
3. Mantenere distinti preview Chunk Editor, LOD e authoritative voxel runtime.

### D. Materiali e rendering

1. Estendere i test CPU per ID0 (nessun voxel-face), ID1 (fallback/array layer) e ID17 (hash/input asset, pack layer e ID invariato). Includere il reale bridge `blockName→blockID→Vertex.materialID`.
2. In una fase GPU separata verificare layer count, caricamento ID17 e campioni/pixel; non cambiare shader Forge, TexturePacker v2, ID o materiali durante questa patch CPU.

## Conclusione

I test automatici isolati eseguiti sono tutti PASS. Il gate di integrità è PASS per i file controllati. Tuttavia il contratto end-to-end di generazione voxel Tiny, input fisico, movimento/camera reale e resa GPU è incompleto: **D1.2.17 = INCOMPLETE, con sottosistemi automatici PASS e verifiche runtime NOT VERIFIED**. Nessuna correzione è stata applicata e D1 non è dichiarato FROZEN.
