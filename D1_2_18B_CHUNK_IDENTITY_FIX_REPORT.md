# D1.2.18b — Planetary Chunk Identity & Registration Fix

## Esito

**Contratto chunk planetario Tiny: PASS.** Entrambe le esecuzioni generano 54 candidati, 54 entità ECS distinte e recuperabili tramite 54 chiavi logiche coerenti. I componenti ECS contengono le coordinate globali corrette e i `PlanetChunkCoord` attesi. La seconda chiamata a `MapWorldGenerator::Generate` riusa le stesse entità senza duplicazioni o sovrascritture incrociate. Il solver completa tutti i 54 chunk e produce voxel non-Air.

Il runner termina con **exit code 2** in entrambe le esecuzioni perché la mesh CPU non è prodotta dal percorso headless senza JobSystem/renderer/Vulkan DMA. Questo resta NOT VERIFIED e non è dichiarato PASS. I gate di identità e generazione richiesti da D1.2.18b sono PASS.

## Diff implementato

- `src/world/GameWorld.h`: aggiunto overload `CreateChunkEntity(name, position, chunkX, chunkZ)` e helper privato comune.
- `src/world/GameWorld.cpp`: l'API legacy conserva la conversione cartesiana esistente (`static_cast<int>(position.x/z) / 16`) e delega all'helper. Il nuovo overload passa invece la chiave esplicita al `VoxelChunkComponent.cx/cz` e al `WorldChunkManager::RegisterChunkEntity`; non usa `Transform.position` per derivare la chiave.
- `src/world/MapWorldGenerator.cpp`: il percorso sferico chiama il nuovo overload con `(global_cx, global_cz)`. Il percorso flat conserva la vecchia API. La posizione e la rotazione sferiche continuano a essere passate separatamente.
- `src/world/PlanetaryVoxelE2ETests.cpp`: estesi i controlli Tiny per unicità candidati/ID, lookup manager, identità ECS, coerenza `cx/cz` e `PlanetChunkCoord`, ripetizione idempotente e comportamento legacy cartesiano.

Le modifiche preesistenti della working tree sono state preservate. In particolare `GameWorld.cpp` contiene già modifiche UV voxel non appartenenti a questa patch; `MapWorldGenerator.cpp` contiene già la conversione D1.2 legacy/canonica della riga. Non sono state alterate.

## Callsite e compatibilità legacy

Inventario effettuato con ricerca globale:

| Callsite | API | Coordinate/semantica |
|---|---|---|
| `MapWorldGenerator::Generate`, ramo sferico | Nuovo overload esplicito | Chiave globale logica `global_cx/global_cz` |
| `MapWorldGenerator::Generate`, ramo non sferico | API legacy | Posizione cartesiana come prima |
| `GameWorld::SetBlockFlat` / percorso di creazione chunk | API legacy | Posizione `(cx*16, cz*16)` |
| `GameWorld::SetBlock` / percorso di creazione chunk | API legacy | Posizione `(cx*16, cz*16)` |
| `ForgeState` workspace | API legacy | Workspace cartesiano `(0,0,0)` |
| `playstate_old.cpp` | API legacy | Coordinate cartesiane moltiplicate per 16 |
| Runner regressione legacy | API legacy | Verifica `x=32,z=-17 → cx=2,cz=-1` e `x=48,z=-17 → cx=3,cz=-1`, inclusa la troncatura preesistente dei negativi |

La firma legacy a due argomenti resta disponibile e conserva il comportamento verificato. Nessun callsite cartesiano è stato migrato.

## Risultati delle due esecuzioni Tiny

Fixture invariato: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json`, PlanetSize Tiny, N=3, `isFlat=false`, template `default_terrain`, istanza attiva face 0/grid (1,1), spawn valido.

| Controllo | Run 1 | Run 2 |
|---|---:|---:|
| Build Debug target runner | PASS, exit 0 | stessa build |
| Candidate trasform sferiche | 54/54 | 54/54 |
| Chiavi globali candidate univoche | 54/54 | 54/54 |
| ID logici `WorldChunk_x_z` univoci | 54/54 | 54/54 |
| Chunk ECS distinti | 54/54 | 54/54 |
| `PlanetChunkCoord` valide e univoche | 54/54 | 54/54 |
| Lookup logici risolti | 54/54 | 54/54 |
| Entità distinte dal lookup manager | 54/54 | 54/54 |
| Chiavi registrate nel manager | 54 | 54 |
| Corrispondenza `VoxelChunkComponent.cx/cz` | 54/54 | 54/54 |
| Corrispondenza face/col/row canoniche | 54/54 | 54/54 |
| Riuso incrociato | 0 | 0 |
| Duplicati o coordinate mancanti | 0 | 0 |
| Idempotenza seconda generazione | PASS, 54 entità stabili | PASS, 54 entità stabili |
| Solver | 54/54 completati | 54/54 completati |
| Voxel non-Air | 504877, block ID 3 | 504877, block ID 3 |
| Test API legacy | PASS | PASS |
| Mesh CPU non vuota associata | NOT VERIFIED | NOT VERIFIED |
| Exit code runner | 2 (gate mesh non verificato) | 2 (gate mesh non verificato) |
| Durata runner | 463 ms | 394 ms |

Log completi:

- `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\d1_2_18b_run_1.log`
- `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\d1_2_18b_run_2.log`

I due log sono byte-identici: SHA-256 `B0C8A1F8C8A2EB238402DCB49C0EDBEC301D4356DE9FA9D771511526EC09C395`.

## Build e test pertinenti

Build runner:

    cmake --build build_r3z7 --config Debug --target FairworldPlanetaryVoxelE2ETests -- /m:1 /verbosity:minimal

**PASS, exit code 0.**

Build applicazione Debug per aggiornare il binario usato dal test D1.2:

    cmake --build build_r3z7 --config Debug --target FairworldEditor -- /m:1 /verbosity:minimal

**PASS, exit code 0.**

Validazione D1.2 eseguita dalla working directory sandbox con l'eseguibile aggiornato:

    FairworldEditor.exe --validate-d12

**PASS, exit code 0, 146 ms.** Tutti e quattro i gruppi riportano PASS: round-trip UV/chart/direction, dimensioni/righe/bordi, singola conversione legacy/canonica e compatibilità spawn JSON/binaria. Log: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\d1_2_18b_validate_d12.log`.

`git diff --check` sui file tracciati della patch: PASS. È presente il solo warning Git di normalizzazione LF/CRLF. Nessuna suite CTest dalla root è stata avviata.

## Integrità

Il confronto è stato eseguito rispetto al manifest originale `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\originals_before.csv`, senza aggiornarlo. **10/10 file** (fixture, sette file di salvataggio e due divergenze note) conservano dimensioni e SHA-256. Snapshot D1.2.18b:

- Prima: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\d1_2_18b_hashes_before.csv`
- Dopo: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\d1_2_18b_hashes_after.csv`

Mismatch: 0. Nessun dato persistente è stato scritto o ripristinato.

## Problemi residui

- La produzione e l'associazione di una mesh CPU reale restano NOT VERIFIED; il runner non crea una mesh sintetica.
- Il runner termina con exit 2 proprio per rendere esplicito questo gate mesh aperto. La correzione dell'identità/registrazione dei chunk è comunque provata dai controlli 54/54.
- Non sono stati eseguiti GUI, Vulkan o test GPU.

Nessuna modifica ai serializer, shader, texture, materiali o salvataggi. Nessun commit/push. D1 non è dichiarato FROZEN.
