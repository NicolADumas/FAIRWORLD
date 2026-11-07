# FAIRWORLD P03c — Chunk Editor → Planet Mapper → GameWorld Audit

**Esito:** il flusso di progetto procedurale (ricetta → istanza → generazione runtime) è presente nel codice. Non esiste invece un importer che trasferisca nel Planet Mapper il buffer voxel prodotto dalla preview del Chunk Editor. La certificazione richiesta per un chunk voxel authored end-to-end è quindi **NOT VERIFIED**. La regressione Release resta **FAIL**. Decisione: **NO-GO** per dichiarare chiusa l'integrazione e per proseguire a P04.

## 1. Safety gate

- HEAD osservato: `f9c7a3047bf3c8ecaad70de4f1af39943933eaf1`, lo stesso HEAD riportato nella baseline P01/P03b.
- Il working tree era già dirty: 153 voci `git status --short` all'inizio dell'audit. Nessun reset, clean, checkout, commit, push o modifica di sorgenti è stato eseguito in P03c.
- È stato creato solo questo rapporto nel repository. I due full runner sono stati eseguiti da `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\runtime`, con fixture nel sandbox e log in `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime`.
- Nessuna GUI o GPU è stata avviata.

## 2. Chunk Editor: contratto effettivo

File principali: `src/states/ChunkEditorState.cpp`, `src/world/MapDocument.h/.cpp`, `src/world/WorldProjectManager.cpp`.

`ChunkEditorState::RebuildChunkPreview` prende il `TerrainTemplate` attivo, costruisce un `MapDocument` temporaneo con `planet.isFlat=true`, copia `baseRules` e `subRegions`, poi chiama `MapWorldGenerator::Generate` per mostrare una preview. Le modifiche all'editor sono sul modello `TerrainTemplate`: regole base, strati e subregioni, non modifiche al buffer voxel persistibile di una entità preview.

Il pulsante “SALVA LIBRO CHUNK (WORLD PROJECT)” chiama `WorldProjectManager::SaveProject()`. Il progetto viene serializzato in `world_map.json` e nel companion `.fwb`. La libreria contiene template e la mappa contiene istanze/riferimenti; il pulsante stesso avvisa che non è stato esportato alcun chunk voxel.

Contratti serializzati verificati nel codice:

- `TerrainTemplate`: `id`, `name`, `planetSize`, `baseType`, `seed`, `baseAngularRadius`, `baseRules`, `subRegions`.
- Regole layer: `blockName` stringa (non MaterialID/BlockID numerico), profondità e proprietà del layer.
- `PlanetChunkInstance`: `name`, `templateId`, `eulerAngles`, `angularRadius`, `isGridAligned`, `faceIndex`, `gridX`, `gridY`, `isActive`.
- `ChunkDataExport` non contiene voxel: contiene solo `ChunkCoord` e `ChunkMetadata` per override/metadata della mappa.
- Nessuna struttura di export del Chunk Editor contiene `VoxelChunkComponent::blocks`, luce o un riferimento a un asset `.bin`.

`MapDocument::SaveJSON/LoadJSON` e `SaveBinary/LoadBinary` persistono le ricette e le istanze. Il binario usa magic `FWB1`, versione 7. `LoadSmart` sceglie `.fwb` quando il suo timestamp è almeno quello del JSON e ricade sul JSON se il binario non è valido.

## 3. Planet Mapper: cosa importa davvero

File: `src/states/PlanetMapperState.cpp`, `src/states/components/PlanetMapperUI.cpp`, `src/states/components/PlanetMapperCamera.cpp`.

Il catalogo UI legge `doc.terrainLibrary` e filtra i template con `template.planetSize == currentPlanet.planetSize`. La tabella di collocamento crea `PlanetChunkInstance` con `templateId`, faccia e griglia. Salva tali riferimenti nel `MapDocument`; non legge file di chunk voxel né deserializza un payload `blocks`.

`PlanetMapperState::UpdateApp` converte `chunkInstances` attive in `MapRegion` e le consegna a `SphericalLODSystem::UpdateLODTree`. La superficie planetaria visualizzata segue il percorso LOD/procedurale. Il `m_previewWorld` non riceve da questa UI un chunk voxel importato dal Chunk Editor.

Il percorso effettivamente presente è dunque **ricetta template → istanza sulla mappa → regioni/LOD di preview**, non **asset voxel del Chunk Editor → chunk voxel planetario**.

## 4. GameWorld: generazione runtime distinta dall'import

File: `src/states/PlayState.cpp`, `src/world/MapWorldGenerator.cpp`, `src/systems/TerrainSolverSystem.cpp`, `src/world/GameWorld.cpp`, `src/world/WorldChunkManager.cpp`.

In PlayState, `WorldProjectManager` carica il map document. `MapWorldGenerator::Generate`:

1. risolve `PlanetChunkInstance.templateId` in `doc.terrainLibrary`;
2. combina `baseRules` e `subRegions` in `MapRegion`;
3. crea/riusa entità ECS e assegna `PlanetChunkCoord`, trasformazione e `BiomeDataComponent`;
4. aggiunge `TerrainGenTag`.

`GameWorld::Update` passa le entità al `TerrainSolverSystem`; `ResolveTerrainRules` risolve i `blockName` tramite `BlockRegistry` in block ID runtime e `TerrainSolver` scrive il buffer voxel. Il percorso di meshing chiama poi `VoxelChunkMesher::Build`, quindi DMA/upload e pubblicazione della mesh.

Questo produce nuovi voxel dal modello procedurale. Non carica il contenuto voxel della preview del Chunk Editor.

### Persistenza chunk runtime

`WorldChunkManager::SaveChunk/LoadChunk` usa `saves/world/chunk_<cx>_<cz>.bin`, header `FWCK`, versione 1, dimensioni, `cx/cz`, array `blocks` e `light`. Non salva `PlanetID`, faccia, colonna/riga canoniche, template ID o map ID. È una cache/runtime persistence con chiave cartesiana legacy, distinta dal formato progetto `.json/.fwb`.

`MapWorldGenerator` marca i chunk con `TerrainGenTag`; il solver li genera prima che il percorso mesh li consumi. Di conseguenza la presenza di `WorldChunkManager::LoadChunk` non dimostra un importer di chunk authored dal Chunk Editor. Non è stato verificato che la cache runtime venga usata per reidratare una mappa procedurale appena ricostruita.

## 5. Data contract e trasformazioni

| Dato | Chunk Editor / progetto | Planet Mapper | GameWorld/runtime |
|---|---|---|---|
| Voxel blocks | Non esportati dal preview | Non importati | Creati dal TerrainSolver in `VoxelChunkComponent::blocks` |
| Orientamento | subRegion angles; istanza: face/grid/euler | usa `faceIndex`, `gridX/Y`, chart e LOD | `PlanetChunkCoord(face,col,row,layer)` e trasformazione planetaria |
| Identità | template ID e istanza nella mappa | riferimento `templateId` | PlanetID/face/col/row più chiave storage legacy |
| Block/material | regole `blockName`; nessun MaterialID nel template | preserva ricetta | BlockRegistry risolve block ID; mesher/MaterialRegistry derivano il materiale |
| Air/solid/passable | non serializzati come voxel | non disponibili come stato voxel importato | classificati dai voxel runtime/query |
| PlanetSize | associato al template e al pianeta | UI filtra i template per size | usato da MapWorldGenerator/TerrainSolver |
| Persistenza | JSON `.json` e cache progetto `.fwb` v7 | stesso MapDocument | chunk cache `.bin` FWCK v1, `cx/cz` e voxel/light |

Una potenziale incoerenza resta: il catalogo Planet Mapper filtra per `PlanetSize`, ma `MapDocument::ValidateAndRepairDocument` ripara template mancanti e bounds/duplicati delle istanze, senza verificare un mismatch di size tra template e pianeta. `MapWorldGenerator` risolve il template per ID e non dimostra un rigetto di quel mismatch. Non è stato cambiato il comportamento.

## 6. End-to-end fixture e round-trip

**Nessun fixture raw voxel editor-origin è stato creato.** Il codice non offre il file o l'API di export/import necessari per partire dal buffer preview del Chunk Editor; creare un fixture parallelo avrebbe testato una nuova convenzione inventata e non il percorso reale.

Il fixture Tiny già usato dal runner (`D1_2_17_HEADLESS_SANDBOX/fixtures/TinySphere/world_map.json`) percorre MapDocument → MapWorldGenerator → TerrainSolver → ECS → mesher, ma i voxel vengono generati proceduralmente dalle regole. Non rappresenta dati voxel serializzati dal Chunk Editor.

Copertura esistente individuata:

- `TerrainValidation::TestD12SpawnPersistenceCompatibility` round-trippa spawn JSON e binario, non `TerrainTemplate` + `PlanetChunkInstance` + pattern voxel.
- Il full Tiny runner carica il documento e genera 54 chunk runtime; non salva e ricarica un template-origin voxel buffer.
- Non è stato trovato un test specifico di round-trip del template/editor attraverso JSON e FWB seguito da applicazione/import dei voxel.

Risultato: serializzazione dei documenti e generazione procedural Tiny hanno copertura separata; round-trip **Chunk Editor → voxel data → Planet Mapper importer** è **NOT VERIFIED / percorso raw assente**.

## 7. Release regression characterization

Full runner Debug e Release rieseguiti dai binari isolati disponibili, con lo stesso fixture e working directory sandbox.

| Configurazione | Exit code | Risultato |
|---|---:|---|
| Debug | 0 | PASS full runner; digest coordinate `0x8429a4e135752c13`; shared CPU mesh contract PASS; radial gravity/landing/jump/camera PASS; Tiny CPU E2E PASS, GPU publication NOT VERIFIED |
| Release | 1 | FAIL full runner; digest coordinate uguale e PASS; shared CPU mesh contract FAIL; test integrato radial gravity/landing/jump/camera FAIL |

### A. Mesher

Release riporta 99.252 triangoli prodotti dal builder condiviso contro 99.256 in Debug, pur con 54 mesh, attributi finiti/validi, zero winding mismatch e zero triangoli degeneri in entrambi i risultati. Il witness legacy flat nel test Release riporta 135.824 triangoli e hash `0x43bcceb03f81581c`; l'aspettativa Debug è 135.820 e `0x20c0d276200e0e20`. Gli hash mesh totali differiscono (`Debug 0xaa8de4f86721f55c`, `Release 0xa5bd4705d3cbf81a`).

Il runner pubblica conteggi e hash aggregati, non il primo voxel/faccia divergente né il primo triangolo divergente. Quindi la causa è **non isolata**; floating-point/ottimizzazione Release è un'ipotesi, non una root cause dimostrata. Aspettative e algoritmi non sono stati modificati.

### B. Grounded diagonale

Nel caso Release `diagonal`: `groundedTicks=2`, `groundedLosses=2`, `endedGrounded=0`, nessun TerrainPending, clearance radiale finale `2.91877747`. Il runner non stampa la trace per tick di questa sottoprova: il primo tick esatto e la query voxel/contatto che causano la perdita sono **NOT VERIFIED**. Nessuna modifica a PhysicsEngine.

### C. Terrain availability

La query autorevole `GameWorld::QueryBlockCartesian` collassa chunk assente e chunk registrato non generato in `TerrainPending`. Il percorso Raycast ha distinti `Missing` e `Pending`, ma non fornisce quella distinzione alla query fisica. La proposta P03b (geometria separata da availability) resta non implementata; nessuna API condivisa è stata cambiata.

## 8. Regression gate P02a–P03b

| Gate | Evidenza P03c | Stato |
|---|---|---|
| P02a coordinate / slab / query | full runner attuale, entrambi i config; coordinate digest PASS | PASS nei controlli eseguiti |
| P02b coordinate compatibility | full runner; identità/digest | PASS nei controlli eseguiti |
| P02c seam neighborhood | full runner, 72/72 directed edges; test size/topology | PASS nei controlli eseguiti |
| P02d planetary raycast | full runner, probe e boundary suite | PASS nei controlli eseguiti |
| P03 collision characterization/overlap | full runner; 14 scenari e overlap bounded | PASS nei controlli eseguiti |
| P03a support/availability/dispatch | matrice presente; overhead upper-body resta NOT VERIFIED | PARZIALE |
| P03b coordinate determinism | entrambi: 36/54, digest `0x8429a4e135752c13` | PASS |
| P03b full Debug | full runner exit 0 | PASS |
| P03b full Release | full runner exit 1: mesh + locomozione | FAIL |
| Build Debug/Release | target compilati nella fase P03b; exit 0. In P03c non sono stati ricompilati sorgenti; il runner utilizzato è l'artefatto isolato di quella build | PASS precedente / NOT REBUILT P03c |
| JSON/FWB template-origin voxel round-trip | non esiste un importer raw e non c'è un test dedicato | NOT VERIFIED |
| GUI/GPU | vietate/non eseguite | NOT RUN |

## 9. Problemi confermati e ipotesi

**Confermato:** Chunk Editor salva `TerrainTemplate` e regole procedurali nel progetto; non esporta i voxel del buffer di preview. Planet Mapper colloca riferimenti ai template e usa LOD/proiezione procedurale. PlayState costruisce voxel runtime dal `MapDocument` con MapWorldGenerator/TerrainSolver. Il formato chunk runtime è distinto e non contiene identità planetaria.

**Confermato:** il round-trip che parte da una mappa/progetto e genera voxel non equivale al round-trip di voxel creati/persistiti nel Chunk Editor.

**Confermato:** Release ha due FAIL nel full runner; manca però telemetria per la prima geometria divergente e per il tick Grounded.

**Non dimostrato:** causa floating point specifica del delta di quattro triangoli; causa specifica della perdita Grounded diagonale; comportamento d'uso della cache FWCK durante una mappa completa in ogni ordine di streaming.

## 10. Piano minimo, senza applicarlo

1. Decidere il contratto prodotto: il Chunk Editor è un editor di ricette procedural (`TerrainTemplate`) oppure deve esportare voxel authored. Il codice attuale implementa il primo.
2. Per il contratto ricetta, aggiungere test dedicato JSON/FWB del template e dell'istanza (stessi `templateId`, regole/string block names, size e face/grid), poi usare tale documento per MapWorldGenerator → TerrainSolver → hash voxel/materiali.
3. Se è richiesto voxel authored, progettare un asset/manifest distinto: magic/versione, dimensioni, BlockID/lighting, orientamento/local origin, PlanetSize e target PlanetID/Face/Col/Row. Un `ChunkDataExport` di soli metadata non è sufficiente; non riutilizzare `saves/world` come catalogo di import senza una chiave di provenienza.
4. Aggiungere validazione del mismatch PlanetSize template/pianeta prima di generare dati.
5. Isolare il primo voxel/faccia divergente nel builder Release con un diff deterministico dei risultati CPU; non aggiornare hash attesi.
6. Aggiungere trace opt-in per tick della prova diagonale con body/velocità, query voxel e ragione della transizione Grounded; poi ripetere Debug/Release senza cambiare PhysicsEngine.
7. Definire l'API `Missing/Pending` condivisa e la policy consumer solo dopo approvazione separata.

## 11. Decisione

**NO-GO per la certificazione dell'integrazione voxel Chunk Editor → Planet Mapper → GameWorld.** Il percorso procedural template → mappa → generazione runtime è presente, ma il chunk voxel authored non viene esportato/importato. Inoltre full runner Release resta FAIL e due first-divergence non sono osservabili nei log correnti.

Nessuna patch è stata applicata in P03c; nessun freeze o P04 è stato avviato.
