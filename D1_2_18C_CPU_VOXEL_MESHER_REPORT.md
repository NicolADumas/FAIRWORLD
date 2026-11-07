# D1.2.18c — CPU Voxel Mesher Extraction & Planetary E2E Verification

## Esito

**CPU planetary voxel E2E: PASS.** Il runner usa il builder CPU condiviso con il worker di produzione e produce 54 risultati mesh non vuoti, associati ai rispettivi chunk sorgente. Vertici, indici, UV, normali, material ID e winding superano i controlli. La pubblicazione `MeshComponent` dal worker asincrono e l'upload GPU restano **NOT VERIFIED** e non sono dichiarati PASS.

## Audit prima dell'estrazione

Il percorso precedente era in `GameWorld::Update` (`src/world/GameWorld.cpp`):

1. Per ogni entity dirty il worker copia il `VoxelChunkComponent` in uno snapshot.
2. Acquisisce gli otto vicini tramite `WorldChunkManager::GetChunkEntity(cx+dx, cz+dz)` e li cattura nel job.
3. La lambda `getBlock` legge il chunk centrale, poi il vicino appropriato ai bordi; Y fuori chunk e vicini assenti sono trattati come Air.
4. `shouldDrawFace` emette contro Air, sopprime facce interne dello stesso blocco e mantiene il comportamento speciale per i block ID 6, 8 e 13.
5. Per ogni voxel non-Air genera le sei facce e i sei vertici per faccia in triangle-list non indicizzata. Calcola AO e light, prende fallback colore/emissive da `MaterialRegistry`, e usa il block ID come `materialID`.
6. Gli UV erano già prodotti con `VoxelFaceUV`; normali e winding erano inseriti esplicitamente nei vertici.
7. Per chunk planetari `PlanetaryMeshGenerator::ConvertToPlanetaryPositions` deforma posizioni e aggiorna normali.
8. Solo dopo la produzione CPU, il worker controlla cancellazione, alloca VRAM, invia `UploadMeshAsync` e accoda la mesh differita. `GameWorld::Update` processa poi la coda e associa `MeshComponent`.

Il percorso di upload è non indicizzato: carica `vertices.data()` e il gate visuale preesistente registra `Indices: 0`. Non è stato cambiato il draw GPU. Il builder restituisce anche indici sequenziali validi come rappresentazione CPU esplicita della triangle list; il worker continua a usare la stessa lista di vertici non indicizzata.

## Modulo estratto e integrazione

- `src/world/VoxelChunkMesher.h/.cpp`: `VoxelChunkMesher::Build` riceve il chunk, l'array dei vicini, `MaterialRegistry`, stato sferico e PlanetSize. Restituisce vertici, indici CPU, conteggio dei solidi e identità sorgente (`cx/cz` e `PlanetChunkCoord`). Non include né inizializza Vulkan, DMA, allocatori o JobSystem.
- `src/world/GameWorld.cpp`: il worker compone la griglia di puntatori ai vicini e chiama il builder. Dopo il ritorno continua con gli stessi punti di cancellazione, VRAM, DMA, coda differita e pubblicazione `MeshComponent`.
- `src/world/PlanetaryVoxelE2ETests.cpp`: dopo `MapWorldGenerator` e `TerrainSolver`, costruisce mesh CPU reali con lo stesso entry point e gli stessi dati voxel/materiali. Non simula né inserisce un `MeshComponent` di produzione.

Il sorgente nuovo è incluso dal `GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS` esistente. È stato rigenerato il build tree `build_r3z7` con il CMake/MSVC già configurato; non è stata cambiata la configurazione del generatore.

## Equivalenza e winding

La logica di accesso ai vicini, visibilità facce, AO, light, fallback colore, emissive, material ID, chiamate a `VoxelFaceUV` e conversione planetaria è stata trasferita dal worker al builder. Nessuna ottimizzazione o modifica Cube-Sphere è stata introdotta.

La prima esecuzione post-estrazione ha misurato 407460 vertici e 135820 triangoli, ma il test ha trovato **88996 triangoli** con winding opposto alle normali. Posizioni, UV, normali unitarie e material ID erano validi. L'origine era il diverso ordine dei vertici di quattro orientamenti di faccia nel mesher preesistente; il winding del risultato non era coerente con le normali outward.

Per soddisfare il contratto richiesto, il builder ora verifica ogni triangolo dopo l'eventuale conversione sferica e, se il cross product geometrico è opposto alla somma delle normali, scambia i due vertici completi. Geometria e associazione dei relativi attributi restano invariate; cambia soltanto l'ordine del triangolo necessario a rendere coerente il winding. Il worker riceve la stessa lista corretta. Il test successivo rileva zero mismatch.

## Risultati del fixture Tiny

Fixture invariato: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json`; working directory `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\runtime`.

| Controllo | Run 1 | Run 2 |
|---|---:|---:|
| PlanetSize / sfericità | Tiny, N=3, `isFlat=false` | Tiny, N=3, `isFlat=false` |
| Candidati univoci / ID logici | 54/54 | 54/54 |
| Chunk ECS distinti | 54/54 | 54/54 |
| Lookup manager / chiavi registrate | 54/54 | 54 |
| `PlanetChunkCoord` / `cx,c z` coerenti | 54/54 | 54/54 |
| Ripetizione idempotente | PASS, 54 entità stabili | PASS, 54 entità stabili |
| TerrainSolver | 54/54 completati | 54/54 completati |
| Voxel non-Air | 504877, ID 3 | 504877, ID 3 |
| Mesh CPU non vuote associate al sorgente | 54 | 54 |
| Vertici / indici / triangoli | 407460 / 407460 / 135820 | 407460 / 407460 / 135820 |
| Indici non validi | 0 | 0 |
| Posizioni non finite | 0 | 0 |
| UV non finite o fuori `[0,1]` | 0 | 0 |
| Normali non finite/non unitarie | 0 | 0 |
| Winding incoerente con normali | 0 | 0 |
| ID 0 nei vertici / mismatch blocco sorgente | 0 / 0 | 0 / 0 |
| `MeshComponent` ECS pubblicati dal worker | 0 (NOT VERIFIED) | 0 (NOT VERIFIED) |
| Exit code runner | 0 | 0 |
| Durata runner | 870 ms | 795 ms |

I log delle due esecuzioni sono identici; SHA-256 `543F244B65AAB8CC1F231A85654103B9511234C29C97CA9802663CF731503C09`.

## Regressioni e validazione

- Build Debug `FairworldPlanetaryVoxelE2ETests`, `FairworldEditor`, `FairworldForgeVertexContractTests`: **PASS, exit code 0**.
- `FairworldEditor.exe --validate-d12`, eseguito dalla working directory sandbox: **PASS, exit code 0**. I quattro gruppi D1.2 passano.
- `FairworldForgeVertexContractTests.exe`: **PASS, exit code 0** (orientamenti voxel, Cube-Sphere e produttori mesh).
- Regressione API legacy: il runner verifica due posizioni cartesiane, inclusa la conversione negativa preesistente; **PASS** in entrambe le esecuzioni.
- Non è stato trovato un runner unitario distinto per il mesher chunk; la copertura CPU è aggiunta al runner Tiny. Nessuna suite CTest root, GUI o GPU è stata avviata.
- `git diff --check` sui sorgenti modificati: **PASS**.

Un primo link del runner aveva segnalato `VoxelChunkMesher::Build` non risolto perché il progetto Visual Studio non aveva ancora rigenerato la lista sorgenti dopo il nuovo file. La rigenerazione esplicita con `cmake -S . -B build_r3z7` ha incluso `VoxelChunkMesher.cpp`; i build Debug successivi dei tre target sono PASS.

## Integrità

Manifest baseline mantenuto intatto: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\originals_before.csv`. Controllati prima e dopo i 10 file registrati: fixture, sette file di salvataggio e le due divergenze note. **10/10 SHA-256 e dimensioni invariati, zero mismatch.**

- Prima: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\d1_2_18c_hashes_before.csv`
- Dopo: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\d1_2_18c_hashes_after.csv`
- Runner: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\d1_2_18c_runner_1.log` e `...\d1_2_18c_runner_2.log`
- D1.2: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\d1_2_18c_validate_d12.log`
- Forge Vertex: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\d1_2_18c_forge_vertex.log`

## Contratti finali

- Fixture → 54 chunk ECS e registrazioni: **PASS**.
- TerrainSolver → voxel solidi: **PASS**.
- Builder CPU condiviso con worker → mesh, indici, materiale, UV, normali, winding e associazione chunk: **PASS**.
- Regressione D1.2 e Forge Vertex: **PASS**.
- API legacy cartesiana: **PASS**.
- Pubblicazione asincrona `MeshComponent`: **NOT VERIFIED**.
- Allocazione VRAM/DMA e rendering GPU: **NOT VERIFIED**.

Nessuna modifica a salvataggi, serializer, shader o materiali; nessun commit/push. D1 non è dichiarato FROZEN.
