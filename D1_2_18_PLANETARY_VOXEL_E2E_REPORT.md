# D1.2.18 — Planetary Voxel End-to-End Headless Verification

## Esito

**FAIL — il contratto end-to-end non è verificato.** Il runner ha prodotto evidenza reale per fixture, trasformazioni, ECS e TerrainSolver, ma `MapWorldGenerator` registra 53 chunk planetari invece dei 54 previsti. Inoltre il percorso di meshing CPU esistente non è invocabile in questo runner senza le dipendenze del renderer: non è stata osservata alcuna mesh CPU non vuota associata a un chunk ECS. Non è stato creato alcun dato mesh sintetico.

## Runner e dipendenze

Aggiunti il runner isolato `FairworldPlanetaryVoxelE2ETests` in `src/world/PlanetaryVoxelE2ETests.cpp` e la registrazione del target in `CMakeLists.txt`. Non è stato aggiunto a CTest: i test CTest del progetto usano la root sorgente come working directory, inappropriata per questo gate con fixture e scritture sandbox.

Il runner carica il JSON tramite `MapDocument::LoadJSON`, inizializza `BlockRegistry`, `MaterialRegistry`, `GameWorld` ed ECS, imposta il componente `PlanetGeometryComponent` sferico, esegue `MapWorldGenerator::Generate`, invoca `TerrainSolverSystem::Update` sui chunk raccolti e infine chiama `GameWorld::Update(0)` per osservare le richieste di meshing pendenti. Non crea finestra, contesto Vulkan o GUI e non richiede output GPU per le fasi che ha effettivamente completato. È linkato a `FairworldCore`; il link non equivale a inizializzare Vulkan.

Il mesher di produzione è incorporato nel percorso asincrono di `GameWorld::Update`: la dispatch richiede `SharedContext::jobSystem`; il worker dipende poi da allocator VRAM, RenderManager/engine e DMA Vulkan prima di associare `MeshComponent`. Il runner headless, lasciando tali dipendenze assenti, può osservare le richieste ma non certificare la mesh CPU di quel worker. Separare l'algoritmo CPU dal commit GPU richiede una modifica di produzione e non è stata fatta in questa fase.

## Fixture e comando

Fixture verificato: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json`.

- PlanetSize: `Tiny`, risoluzione faccia `N=3`.
- `isFlat=false`.
- Un template `default_terrain`, Tiny.
- Una istanza attiva `default_terrain`, faccia 0, `gridX=1`, `gridY=1`, grid-aligned.
- Uno spawn presente con coordinate finite e direzione planetaria normalizzata.
- SHA-256 fixture prima e dopo: `47AF4358E4BD663926E64A43ACF2B116F3A2939BA5605FC89B2983D32A9AD503` (5384 byte).

Build effettiva, dal progetto `D:\FAIRWORLD\FAIRWORLD`, riusando `build_r3z7` e la toolchain esistente:

```powershell
& 'D:\Visual Code\vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build '.\FAIRWORLD\build_r3z7' --config Debug --target FairworldPlanetaryVoxelE2ETests -- /m:1 /verbosity:minimal
```

**Exit code build: 0 (PASS).** È stato compilato il target diagnostico Debug, non l'intera applicazione Debug.

Durante lo sviluppo, una prima build della diagnostica aggiuntiva è fallita perché `resolution` era dichiarata dopo il nuovo controllo delle trasformazioni (MSVC C2065). Il solo runner è stato corretto spostando la dichiarazione prima del controllo; la build finale riportata sopra è quella successiva alla correzione. Non è stato necessario cambiare codice di produzione.

Esecuzione effettiva con working directory `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\runtime`:

```powershell
& 'D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldPlanetaryVoxelE2ETests.exe' --fixture 'D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json'
```

**Durata: 305 ms. Exit code: 1 (FAIL).** Log completo: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\voxel_e2e.log`. Log build: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\build_runner.log`.

## Risultati per fase

| Fase | Atteso | Osservato | Esito |
|---|---:|---:|---|
| Fixture | Tiny sferico, template e spawn validi | Tiny/N=3, `isFlat=false`, template e spawn validati | PASS |
| Template attivi risolti | 1 | 1 | PASS |
| Trasformazioni sferiche candidate | 54 | 54 valide | PASS |
| Chunk ECS con `VoxelChunkComponent`, `BiomeDataComponent`, `TerrainGenTag` | 54 | 53 | FAIL |
| `PlanetChunkCoord` validi e univoci | 54 | 53 | FAIL |
| Coordinate mancanti | nessuna | face 0, col 2, row 2; global `(2,0)`; lookup ChunkManager assente | FAIL |
| `TerrainGenTag` prima/dopo solver | tag consumati secondo contratto corrente | 53 prima, 53 dopo | il solver non rimuove il tag nel percorso osservato |
| TerrainSolver | 53 chunk processati, completati e non vuoti | 53 processati; coda 0; 53 generated; 53 richieste mesh | PASS sui 53 chunk presenti |
| Voxel solidi | almeno uno | 496379, tutti block ID 3 | PASS sui chunk presenti |
| Mesh CPU non vuota associata a ECS | almeno una | 0 `MeshComponent`, 0 mesh non vuote, 0 vertici, 0 triangoli | NOT VERIFIED / gate FAIL |
| Mesh GPU caricata | fuori capacità runner | non tentata | NOT VERIFIED |

Quando una mesh è presente, il runner controlla associazione con `VoxelChunkComponent`, material ID non zero e presente nella distribuzione di blocchi sorgente, UV finite nel dominio `[0,1]`, normali finite e unitarie, e coerenza tra winding dei triangoli e normali. In questa esecuzione non esistevano vertici su cui applicare tali controlli: il conteggio zero **non** è un PASS degli attributi.

## Primo collegamento non conforme e causa

Il primo scarto osservabile è all'uscita di `MapWorldGenerator::Generate`: tutte le 54 coordinate candidate hanno trasformazione sferica valida, ma il mondo contiene 53 coordinate planetarie valide e il `ChunkManager` non restituisce un'entità per `face=0,col=2,row=2` / global `(2,0)`. Il fixture ha `chunkOverrides=[]`; la causa interna dell'omissione non è dimostrata dal runner e non viene attribuita a una condizione specifica. Il difetto è quindi localizzato al collegamento **MapWorldGenerator → ECS**, ma la causa radice interna resta aperta.

Il secondo limite è nel collegamento **voxel ECS → mesher CPU**: il codice attuale rende la costruzione dei vertici parte del worker di `GameWorld::Update`, non esponendo un entry point CPU isolato. Nel contesto del runner `jobSystemAvailable=no`, `pendingMeshRequests=53`, `pendingJobs=0`, `MeshComponent=0`. Perciò non si può affermare che sia stata richiesta e completata una mesh CPU; né si può concludere se l'algoritmo di meshing produrrebbe geometria valida.

## Integrità e isolamento

Manifest pre-run: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\originals_before.csv`. Confronto post-run: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\originals_after.csv`.

Sono stati verificati 10 file: fixture, i sette file di salvataggio originali e le due divergenze già note (`assets/texture_cache.bin` e `assets/test_packaging_artifacts/pkg_roundtrip_03.json`). **10/10 SHA-256 e dimensioni invariati; zero mismatch.** Le due divergenze note non sono state ripristinate né re-baselined. Il processo è stato avviato dalla working directory sandbox e il runner ha letto il fixture sandbox.

`git diff --check` sul cambiamento tracciato in `CMakeLists.txt` è PASS; il nuovo file non tracciato è stato controllato separatamente per whitespace finale ed è PASS. Rimane il warning Git che normalizza LF/CRLF per `CMakeLists.txt` e il sorgente.

## Proposta minima successiva (non applicata)

1. Aggiungere strumentazione diagnostica temporanea solo in una revisione autorizzata attorno ai due return anticipati dentro la lambda `generateChunk`, registrando la coordinata candidate e quale condizione la esclude. Prima valutare/risolvere il caso 53/54.
2. Per certificare la mesh in headless, estrarre in una funzione CPU testabile l'attuale builder di vertici voxel e far sì che il worker di produzione continui a usare quella funzione prima della fase VRAM/DMA. Il runner dovrebbe poi allegare il risultato a un `MeshComponent` ECS solo attraverso il percorso CPU di test documentato, verificare attributi e sorgente e lasciare la GPU esplicitamente NOT VERIFIED. Non simulare JobSystem, allocator o upload Vulkan.
3. Ripetere il runner sul medesimo fixture e sandbox; accettare solo con 54 chunk, voxel non-Air e almeno una mesh CPU reale, non vuota e associata al chunk sorgente.

Queste correzioni non sono state applicate; non sono stati modificati sistemi di produzione, shader, serializer, materiali o salvataggi.

## Matrice finale

- Runner compilabile Debug: **PASS**.
- Fixture sandbox valido: **PASS**.
- Trasformazioni sferiche candidate: **PASS**.
- Generazione attesa dei 54 chunk ECS: **FAIL (53/54)**.
- Solver sui chunk generati: **PASS (53/53)**.
- Mesh CPU non vuota associata a chunk ECS: **NOT VERIFIED; accettazione complessiva FAIL**.
- Mesh caricata su GPU: **NOT VERIFIED**.
- Integrità dei 10 file del manifest: **PASS (10/10 invariati)**.
- D1 freeze: **non dichiarato**.
