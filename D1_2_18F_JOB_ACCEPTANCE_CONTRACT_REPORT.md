# D1.2.18f — Job Acceptance, Cancellation & Mesh Publication Contract

## Esito

La verifica CPU/headless è **PASS per il contratto di accettazione, completamento, cancellazione, retry e scarto dei risultati obsoleti**. Non è una certificazione della pubblicazione GPU: DMA, upload e disponibilità del `MeshComponent` nel renderer non sono stati modificati né simulati.

## Modifiche

- `src/systems/JobSystem.h/.cpp`: aggiunto `ExecuteTracked`, con ID monotono e stati separati di accettazione (`Accepted`, `RejectedShutdown`, `RejectedInvalid`) ed esecuzione (`Completed`, `Cancelled`, `Failed`). Il callback terminale riceve anche `exception_ptr`. `Initialize(workerCount=0)` mantiene il dimensionamento automatico esistente e permette pool controllati nei test. Le eccezioni dei job tracciati vengono convertite in `Failed`; il worker continua a servire la coda. `Dispatch` usa l’API tracciata, così un enqueue rifiutato distrugge il packaged task e il future diventa `broken_promise`, anziché restare pendente.
- `src/components/ForgeComponents.h`: aggiunti `VoxelChunkComponent::meshRevision` e `ChunkDirtyComponent::activeRequestId`, entrambi runtime-only.
- `src/world/GameWorld.h/.cpp`: il worker mesh usa `ExecuteTracked`; un rifiuto ripristina subito `pendingJob=false` e conserva il chunk dirty per un successivo tentativo. Il callback registra l’esito nella coda differita. Il consumo sul thread principale controlla entity, request ID, chiave logica `(cx,cz)`, `PlanetChunkCoord`, revisione e `needsRebuild` prima di accettare il risultato. Gli esiti fallito/cancellato puliscono `pendingJob` e `activeRequestId`; una nuova richiesta viene pianificata solo se era stata richiesta una ricostruzione durante il job. Teardown disabilita l’accettazione mesh e cancella il token prima di attendere i job.
- `src/systems/JobSystemContractTests.cpp` e `CMakeLists.txt`: aggiunto il runner headless `FairworldJobSystemContractTests` e la registrazione CTest.

La API legacy `Execute(void)` resta invariata per i callsite fire-and-forget esistenti. I callsite `WaitAll`, `CancelJobs` e i percorsi di teardown non richiedono cambi di firma; il mesher di `GameWorld` è il callsite aggiornato al contratto tracciato. `Enqueue` rimane un dettaglio privato.

## Identità e stato dirty

Ogni richiesta mesh conserva entity sorgente, chiave logica, coordinata planetaria, ID JobSystem, revisione mesh al momento dello snapshot e stato terminale. Un risultato non viene dichiarato pronto per il commit ECS se l’entity/request/coordinate/revisione non corrispondono più.

`meshRevision` è un token runtime incrementato da `GameWorld::MarkMeshDirty`; non è serializzato e non sostituisce i dati voxel. I mutatori che invalidano la mesh devono passare da `MarkMeshDirty` (direttamente o tramite il completamento del `TerrainSolver`). Una scrittura diretta ai voxel che ometta l’invalidazione non può essere rilevata dal token: resta un vincolo da mantenere nei futuri producer.

## Test e build

Toolchain: CMake `4.3.1-msvc1`, generatore esistente `Visual Studio 18 2026`, MSBuild `18.10.1`; configurazione `build_r3z7`, Debug, `/m:1`.

| Controllo | Esito | Exit code |
|---|---:|---:|
| `cmake --build build_r3z7 --config Debug --target ALL_BUILD -- /m:1` | PASS | 0 |
| `FairworldJobSystemContractTests.exe` — 10 scenari | PASS, 10/10 | 0 |
| `FairworldPlanetaryVoxelE2ETests.exe --fixture ...TinySphere/world_map.json` | PASS CPU | 0 |
| `FairworldForgeVertexContractTests.exe` | PASS | 0 |
| `FairworldEditor.exe --validate-d12` | PASS, 4/4 gruppi | 0 |
| `git diff --check` mirato sui file versionati del task | PASS | 0 |
| Controllo whitespace sul nuovo runner non versionato | PASS | 0 |

Nel runner Tiny sono stati mantenuti i risultati di baseline: 54/54 chunk e lookup, 54/54 TerrainSolver completati, 504.877 voxel non-Air, 54 mesh CPU, 407.460 vertici/indici, 135.820 triangoli, zero winding mismatch, UV/normali/indici invalidi o mismatch di associazione. La disponibilità del `MeshComponent` prodotto dall’async worker e il caricamento GPU risultano **NOT VERIFIED** da quel runner.

Log prodotti:

- `D1_2_17_HEADLESS_SANDBOX/logs/d1_2_18f_build_debug.log`
- `D1_2_17_HEADLESS_SANDBOX/logs/d1_2_18f_jobsystem_contract.log`
- `D1_2_17_HEADLESS_SANDBOX/logs/d1_2_18f_tiny_cpu.log`
- `D1_2_17_HEADLESS_SANDBOX/logs/d1_2_18f_forge_contract.log`
- `D1_2_17_HEADLESS_SANDBOX/logs/d1_2_18f_validate_d12.log`

Il `git diff --check` sull’intera working tree segnala inoltre whitespace preesistente in `src/states/BlockMakerState.cpp` (righe diagnostiche estranee a questa patch). I file modificati da questo task superano il controllo mirato; quelle righe non sono state toccate.

## Test JobSystem (10/10)

1. Accettazione e completamento.
2. Rifiuto per shutdown e retry dopo riavvio del pool.
3. Job cancellato prima dell’avvio.
4. Job cancellato durante l’esecuzione.
5. Eccezione worker, stato `Failed` e sopravvivenza del pool.
6. Rebuild durante job attivo: nessun concorrente duplicato, risultato stale scartato e nuova richiesta.
7. Cancellazione mesh: `pendingJob` rilasciato e retry possibile.
8. Enqueue rifiutato: dirty mantenuto e retry possibile dopo riapertura del JobSystem.
9. Fallimento terminale d’allocazione CPU: `pendingJob` rilasciato senza loop di retry automatico.
10. Teardown con job in corso: attesa del worker e rifiuto di nuovo lavoro del mondo.

Il caso 9 usa il `VramSlabAllocator` CPU esistente con un bucket volutamente inferiore al buffer richiesto; l’allocazione viene rifiutata prima del percorso DMA. Non certifica né simula un upload GPU.

## Integrità e ambito

I runner sono stati avviati dalla working directory sandbox `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\runtime`; il fixture Tiny è stato letto dal sandbox. Non sono state avviate GUI né operazioni di salvataggio. La build ha scritto gli output previsti nelle directory `build_r3z7` e `bin\Debug`. Nessun commit/push è stato eseguito.

Non è stato creato un nuovo manifest SHA-256 degli originali in questo task; quindi il report non rivendica una verifica hash prima/dopo. Il runner E2E non pubblica mesh GPU, come indicato esplicitamente nel suo output.

## Rischi residui

- Il token di revisione rileva le modifiche che percorrono `MarkMeshDirty`; una mutazione voxel che salti tale invalidazione può rendere non rilevabile uno snapshot obsoleto.
- Il contratto `ExecuteTracked` è usato dal mesher. I restanti callsite legacy di `Execute` rimangono fire-and-forget e non espongono stato terminale ai propri consumer.
- GUI, Vulkan, DMA, upload e presenza finale del `MeshComponent` nel renderer restano fuori da questa validazione.

**Conclusione:** PASS per il gate CPU/headless D1.2.18f nei casi esercitati; pubblicazione GPU NOT VERIFIED. D1 non è dichiarato FROZEN.
