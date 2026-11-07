# D1.2.18e — Async Mesh Publication & GPU Upload Contract Audit

**Esito complessivo:** il builder e la richiesta CPU Tiny sono PASS; la catena asincrona, il completamento DMA e la disponibilità renderer sono NOT VERIFIED senza JobSystem/Vulkan. Sono presenti lacune di correttezza nel codice di errore DMA e nella convalida dell’identità/versione del risultato; non sono state applicate patch.

## Ambito e controlli eseguiti

Audit statico dei callsite in `GameWorld::Update`, `JobSystem`, `VoxelChunkMesher`, `VramSlabAllocator`, `VulkanDmaManager`, coda differita e renderer. Eseguiti i soli runner CPU esistenti dalla working directory sandbox:

`D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\runtime`

- `FairworldPlanetaryVoxelE2ETests.exe --fixture D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json` — **PASS, exit 0**.
- `FairworldForgeVertexContractTests.exe` — **PASS, exit 0**.

Il runner stampa `jobSystemAvailable=no`, `pendingMeshRequests=54`, `pendingJobs=0`, `MeshComponent=0`. Questo è intenzionale per il runner headless: non crea un JobSystem né simula VRAM, DMA, pubblicazione ECS o disegno. Nessuna GUI o Vulkan è stata avviata. Nessun sorgente o dato persistente è stato modificato in questa attività.

Log:

- `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\d1_2_18e_tiny_cpu.log`
- `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\d1_2_18e_forge_contract.log`

## A — Ordine effettivo del percorso

1. `GameWorld::Update` consuma la coda differita lasciata dal frame precedente.
2. Il solver marca chunk dirty tramite `MarkMeshDirty`.
3. Se `SharedContext::jobSystem` è nullo, il dispatch non viene eseguito e i chunk restano richieste dirty.
4. Se presente, per ogni chunk valido il dispatcher applica `maxJobsPerFrame=15`, verifica `pendingJob`, e in modalità PlanetMapper rimuove il dirty senza generare mesh.
5. In modalità di rendering imposta `ChunkDirtyComponent.pendingJob=true`, copia il chunk e fino a otto vicini, cattura l’entity ECS, `GameWorld*`, `SharedContext*` e token di cancellazione, quindi chiama `JobSystem::Execute`.
6. Il worker controlla il token, genera/carica i dati eventualmente mancanti, controlla di nuovo il token, legge la geometria planetaria dal registry attivo, invoca `VoxelChunkMesher::Build` e sposta i vertici CPU.
7. Mesh vuota: accoda una voce differita senza allocazione. Mesh non vuota: controlla il token, alloca VRAM, controlla ancora il token e libera l’allocazione se è cancellato.
8. Il worker ottiene i metadati allocazione, seleziona il buffer compartimento, chiama `UploadMeshAsync`, costruisce `MeshComponent` e infine accoda il risultato in `m_deferredMeshes` con mutex.
9. Al successivo `GameWorld::Update`, il thread ECS consuma la coda. Scarta e libera l’allocazione solo se l’entity target non è più valida. Se è valida, aggiorna eventualmente blocks/light per `isNewlyGenerated`, sostituisce il `MeshComponent` e rimuove o riattiva lo stato dirty.
10. Un render pass successivo visita `MeshComponent + TransformComponent`, applica policy di visibilità, verifica l’allocazione e registra `vkCmdDraw`.

Riferimenti principali: `src/world/GameWorld.cpp:219-340`, `348-501`; `src/world/GameWorld.h:155-177`; `src/systems/JobSystem.cpp:65-100`; `src/render/VulkanDmaManager.cpp:111-236`; `src/render/apps/PlayRenderer.cpp:48-109`.

## B — Identità, dati e lifetime

| Identità/dato | Stato nel percorso | Valutazione |
|---|---|---|
| Entity ECS sorgente | Catturata nel job e memorizzata in `DeferredMeshSpawn::targetEntity`; al commit si verifica `registry.valid(entity)` | **Parziale PASS**: protegge da entity distrutte/handle generazionali non validi |
| Chiave logica chunk | Copiata nel `chunkData` snapshot (`cx/cz`, `PlanetChunkCoord`); usata nel builder | **Parziale**: non viene confrontata con la coordinata attuale al commit |
| `PlanetChunkCoord` | Presente nello snapshot; il builder la consuma per la mesh; non è un campo identificativo separato del risultato | **NOT VERIFIED** nella mesh pubblicata; nessun controllo di corrispondenza al commit |
| Versione contenuto voxel | Esiste `voxelHash` nel componente, ma non viene confrontato dal worker/commit; non è una generation ID del job | **FAIL di contratto**: il risultato non porta una versione verificata |
| Stato job | Solo `pendingJob`/`needsRebuild`; nessun job ID, timestamp o esito persistente | **Parziale**: nessuna distinzione affidabile tra queued, started, cancelled, failed e completed |
| Identità risultato CPU | `MeshComponent` contiene nome, vertici e handle VRAM; non contiene job ID/hash sorgente | **NOT VERIFIED** per correlazione univoca |
| Identità mesh pubblicata | ECS entity + nome derivato da `cx/cz`; il nome può ripetersi tra facce planetarie | **Parziale**: entity distingue l’oggetto, nome da solo no |

Il controllo entity valida solo la sopravvivenza del target. Se il chunk viene modificato mentre il job usa lo snapshot, `MarkMeshDirty` imposta `needsRebuild`, ma il risultato dello snapshot può comunque essere pubblicato prima che il rebuild successivo lo sostituisca. È quindi confermato un intervallo in cui la mesh ECS può essere obsoleta; l’aggiornamento successivo è richiesto solo se ogni mutazione passa da `MarkMeshDirty`. Non c’è un test runtime di questa race.

`activeRegistry` viene dereferenziato dal worker per leggere `PlanetGeometryComponent` (`GameWorld.cpp:420-425`). Il puntatore/registry è condiviso e non è protetto da mutex. Le transizioni Chunk Editor e Planet Mapper cancellano e attendono i job in alcuni percorsi; durante gli update Play, invece, main thread e worker possono accedere allo stesso registry. Rischio di accesso concorrente confermato dal codice, manifestazione runtime non provata.

`VramSlabAllocator::Allocate` e `Free` usano mutex, ma `GetAllocation` legge `m_allocations` senza lock (`VramSlabAllocator.cpp:154-159`). I worker possono allocare in parallelo e il renderer può interrogare la stessa struttura; una riallocazione della vector durante la lettura costituisce un rischio di data race concreto. Nessun test concorrente è stato eseguito in questa fase.

### Cancellazione e teardown

- Il job verifica il token in quattro punti, ma i return anticipati per token cancellato, VRAM allocation fallita, o submit rifiutato non pubblicano un esito che azzeri `pendingJob`.
- `JobSystem::Execute` restituisce `void`; `Enqueue` scarta silenziosamente il job quando `m_shutdown` è true. `GameWorld` ha già marcato `pendingJob=true`, senza acknowledgement dell’accettazione.
- `needsRebuild` permette un secondo dispatch quando il primo risultato differito viene committato, ma non impedisce la pubblicazione temporanea del risultato vecchio.
- `TeardownResources` fa `WaitAll` prima di svuotare queue/ECS. `RuntimeManager::ShutdownGpuRuntime` ferma e unisce il pool, poi drena DMA e infine distrugge allocator: ordine nominale utile a proteggere `self` e i raw pointer catturati, a condizione che il join termini.
- Il worker può rimanere bloccato in `vkWaitSemaphores(..., UINT64_MAX)`; in tal caso `WaitAll`/join non termina. L’ordine nominale non risolve un wait GPU infinito.
- `CancelJobs()` da solo non attende. I percorsi osservati di cambio template/modalità che lo usano chiamano anche `WaitAll` prima di riusare/distruggere il mondo; non è stato provato un altro consumer che cancelli e poi mantenga il mondo senza reset.

## C — Risultato DMA e primo gate non verificato

`UploadMeshAsync` non è realmente non-bloccante nel percorso attuale: serializza con `m_mutex`, registra una copy, sottomette e, su submit riuscito, attende il timeline con timeout `UINT64_MAX` prima di tornare. Solo dopo il ritorno `GameWorld` accoda il risultato.

L’esito non è però un contratto attendibile per il chiamante:

- Il chiamante ignora il valore di ritorno di `UploadMeshAsync` e incrementa `g_d74f6l_dma_completed` subito dopo la chiamata (`GameWorld.cpp:466-472`). Il contatore significa quindi “ritorno della funzione”, non completamento Vulkan verificato.
- Se `vkBeginCommandBuffer` fallisce, la funzione ritorna `0` senza liberare il command buffer già allocato e contato.
- Se `vkQueueSubmit` fallisce, salta il wait ma aggiunge comunque il command buffer alla pending list e ritorna il timeline ID incrementato. `GameWorld` prosegue e può pubblicare una mesh la cui copy non è stata sottomessa.
- Il `VkResult` di `vkWaitSemaphores` è ignorato; un risultato diverso da `VK_SUCCESS` non impedisce la pubblicazione.
- `vkEndCommandBuffer` e il flush della memoria mappata non propagano l’esito.
- Buffer destinazione o staging nulli generano un log, ma il codice continua verso `vkEndCommandBuffer`/submit e può restituire un ID come se il trasferimento fosse valido.

Questi sono **FAIL statici del contratto di errore**; non provano che i 13 errori o una specifica immagine errata siano stati causati da questi casi. Un submit fallito e un wait infinito sono rischi runtime non riprodotti senza Vulkan.

Il **primo gate non verificabile** dopo la richiesta CPU è l’accettazione del job: `Execute` non fornisce acknowledgement e il runner non crea il JobSystem. Il primo gate successivo osservabile solo in una sessione GPU è l’allocazione VRAM riuscita.

## D — Matrice delle fasi

| Fase | Risultato | Evidenza/limite |
|---|---|---|
| 1. Mesh richiesta | **PASS** (headless) | 54 `meshRequests` dal TerrainSolver; restano dirty nel runner |
| 2. Job accettato | **NOT VERIFIED** | `Execute` è fire-and-forget; nessun ack. Runner: `jobSystemAvailable=no` |
| 3. Job avviato | **NOT VERIFIED** | Nessun worker eseguito nel runner |
| 4. Builder CPU completato | **PASS** per runner CPU; **NOT VERIFIED** nel worker di produzione | 54 mesh CPU, 407.460 vertici, 135.820 triangoli nel runner. Callsite worker con builder condiviso verificato staticamente |
| 5. VRAM allocata | **NOT VERIFIED** | Dipende da `VramSlabAllocator`, callback buffer compartimento e Vulkan runtime |
| 6. DMA richiesto | **NOT VERIFIED** | Nessuna chiamata runtime headless |
| 7. Upload completato | **NOT VERIFIED** | Serve timeline Vulkan; risultati wait/submit sono attualmente ignorati dal percorso di pubblicazione |
| 8. Mesh differita accodata | **NOT VERIFIED** | Il runner non avvia il worker |
| 9. `MeshComponent` pubblicato | **NOT VERIFIED** | Headless osserva zero componenti; non viene simulato un commit ECS |
| 10. Mesh vista dal renderer | **NOT VERIFIED** | Richiede frame Vulkan e passaggio delle condizioni di visibilità/allocazione/buffer |

Il runner E2E e Forge contract sono PASS, ma non sono prove di pubblicazione o di rendering GPU.

## E — Problemi confermati e rischi

1. **FAIL — Esiti DMA persi.** Il chiamante pubblica anche senza un esito Vulkan positivo attendibile.
2. **FAIL — Command buffer leak su begin failure.** Percorso certo nel codice, non fault-injected.
3. **FAIL — Nessun acknowledgement di enqueue/job state.** La coda può scartare il task senza informare `GameWorld`, lasciando `pendingJob` impostato.
4. **FAIL — Nessuna convalida versione/key al commit.** Entity valida non garantisce che lo snapshot sia ancora la versione corrente.
5. **SUSPECTED — Data race su `activeRegistry` e `VramSlabAllocator::GetAllocation`.** Accessi concorrenti sono possibili secondo il modello multi-worker; non riprodotti.
6. **SUSPECTED — Retiro anticipato della VRAM.** Le sostituzioni/distruzioni liberano handle allocatore immediatamente (`GameWorld.cpp:281-282`, `322-323`, `DestroyEntity`), senza evidenza qui di un fence per le draw già in volo. La GPU può ancora leggere lo slot se il frame precedente non è terminato; serve verifica con sync/frame ownership.
7. **NOT VERIFIED — Upload DMA e disegno reali.** Nessun dispositivo, layer o frame è stato avviato in questa fase.

## F — Diagnostica minima proposta per la prossima sessione

Non installata in questa revisione. La proposta è logging opt-in (`FAIRWORLD_MESH_TRACE=1`) senza cambiare la logica di rendering, con timestamp monotono e un correlation key composto da job sequence + entity generazionale + `PlanetChunkCoord` + snapshot `voxelHash`:

- `request`, `enqueue accepted/rejected`, `worker start/cancel/exception`;
- `builder complete` con count/hash CPU e durata;
- `vram allocate` con handle, offset, bucket, compartment ed esito;
- `dma submit` con command buffer, timeline target e `VkResult` di begin/end/submit;
- `timeline wait begin/end`, risultato e durata (nessuna attesa dichiarata completata senza `VK_SUCCESS`);
- `deferred enqueue`, `ECS commit/discard` con motivo e confronto di entity/key/versione;
- `renderer eligible/draw recorded` con visibility, allocation e buffer validi.

Per distinguere in modo verificabile “job accettato” serve un acknowledgement esplicito dal JobSystem (oggi `Execute` è `void`). Per distinguere completamento DMA serve esporre e controllare l’esito del submit/wait; la strumentazione deve registrare l’errore, non trasformarlo in successo. Le eccezioni devono essere catturate al confine del worker e registrate come errori, non come cancellazioni.

## G — Prossima validazione GPU sandbox

Il CPU-only non può creare Vulkan device/queue, allocate le slab buffer, inviare `vkCmdCopyBuffer`, attendere il timeline, entrare nel successivo `GameWorld::Update`, né osservare un draw in un frame. Perciò la fase completa richiede eseguibile Debug con Vulkan funzionante e una scena che lasci attivo il percorso GameWorld/Play renderer. La prossima sessione deve essere sandbox-only e raccogliere almeno un chunk per ciascun correlation key fino a un draw registrato; poi testare un errore/failure injection solo in harness controllato, non alterando a mano i risultati.

Ordine minimo per la revisione successiva:

1. Aggiungere diagnostica opt-in e ack di enqueue, preservando API/callsite secondo un diff revisionabile.
2. Aggiungere un esito DMA controllabile e logging di tutti i VkResult senza dichiarare publish se upload fallisce (patch funzionale separata, da autorizzare dopo revisione).
3. Verificare lifetime e sincronizzazione dell’allocazione con draw in volo.
4. Eseguire sessione Vulkan in sandbox e confrontare ogni stage log; mantenere gli stage non osservati NOT VERIFIED.

Nessuna modifica correttiva applicata. Nessun commit/push. Nessun D1 FROZEN.
