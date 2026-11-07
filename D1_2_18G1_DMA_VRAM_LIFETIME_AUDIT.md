# D1.2.18g.1 — Vulkan DMA Error Propagation & VRAM Lifetime Audit

## Stato

**Audit statico: FAIL del contratto d’errore e di ownership DMA; nessuna patch applicata.**

I test CPU/headless di regressione passano. Le condizioni Vulkan reali (fallimenti di submit/wait, memory visibility sulla GPU, reuse di uno slot letto da frame in-flight) non sono state fault-injected né riprodotte su device in questa fase. La build Debug e i test CPU non certificano l’upload GPU.

## Perimetro e sicurezza

- Revisione READ-ONLY di `VulkanDmaManager`, `VramSlabAllocator`, `GameWorld`, queue setup, renderer e shutdown.
- Nessun sorgente o shader modificato; nessuna GUI avviata.
- Test avviati con working directory `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime`; il fixture Tiny proviene dalla copia `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json`.
- Il report e i log di test sono l’unico output documentale nuovo intenzionale; nessun salvataggio è stato aperto o scritto.

## A — Percorso DMA e matrice degli esiti

Il percorso principale è `GameWorld::Update` → snapshot mesh → `VramSlabAllocator::Allocate` → `GetAllocation` → indice compartimento → `VulkanDmaManager::UploadMeshAsync` → coda transfer → timeline wait → coda differita → pubblicazione ECS. `UploadMeshAsync` è sincrona rispetto al chiamante nel percorso nominale: mantiene `m_mutex` mentre copia, sottomette e attende il timeline con timeout `UINT64_MAX`.

| Passo | Precondizione/risultato osservato | Cleanup e stato consegnato | Rischio di falsa pubblicazione |
|---|---|---|---|
| `Initialize` / timeline semaphore | Riceve device, queue, pool, staging buffer/memoria/mapping e mutex. La creazione del timeline viene controllata e tracciata. | La funzione è `void`; in caso di errore logga, ma il manager rimane utilizzabile e `RuntimeManager::CommitGpuGlobalVRAM` restituisce comunque `true`. `IsAcceptingSubmissions()` controlla soltanto `m_device != nullptr`. | **Sì.** Il chiamante può procedere con timeline nullo o dipendenze parziali. |
| Staging offset + `memcpy` | Non sono controllati `meshData`, size non-zero, mapping, capacità dell’anello, overflow, né overlap con range ancora in uso. | `AllocateStagingSpace` aggiorna solo `m_headOffset`; al wrap riparte da zero e `m_tailOffset` non viene usato. Nessun risultato al chiamante. | **Sì.** Un input non valido può causare accesso host invalido o copia fuori dal buffer prima di Vulkan. |
| `vkFlushMappedMemoryRanges` | Chiamata solo se `VkDeviceMemory` non è nullo; flush da offset 0 con `VK_WHOLE_SIZE`. | Il `VkResult` è ignorato; non c’è un risultato `FlushFailed`. Il percorso usa memoria VMA `CPU_TO_GPU`, senza verificare la proprietà coherent. Il manager riceve il `VkDeviceMemory` sottostante, ma non l’offset/size della `VmaAllocation`. | **Sì.** Non si dimostra che i byte di staging siano visibili al device in caso di errore. L’intervallo ampio può includere altre allocazioni VMA; effetto concreto non misurato. |
| `vkAllocateCommandBuffers` | Result e handle nullo sono controllati. | Su errore ritorna 0; se l’allocazione fallisce Vulkan libera gli eventuali command buffer ottenuti dalla stessa chiamata. | In questo ramo specifico non viene sottomesso. Il chiamante però ignora il valore 0 e continua a trattare il job come completato. |
| `vkBeginCommandBuffer` | Result controllato. | Su errore ritorna 0 senza `vkFreeCommandBuffers` e senza decrementare `m_liveCommandBuffers`: command buffer allocato perso. | Non c’è upload valido, ma i callsite ignorano 0 e possono pubblicare. |
| Validazione buffer e `vkCmdCopyBuffer` | Solo `destBuffer` e staging buffer nullo sono controllati. I due casi stampano un warning ma non interrompono il percorso. Non si controllano `destination.valid`, handle/compartment, mapping, limiti `offset + size`, usage o allineamento della copy. | `vkCmdCopyBuffer` è `void`; il comando può non essere registrato nel ramo nullo, ma il buffer viene comunque chiuso/sottomesso. | **Sì, confermata staticamente.** Il submit può segnalare il timeline senza avere copiato i byte attesi. |
| `vkEndCommandBuffer` | Deve riuscire a trasformare il buffer in executable. | Il result è ignorato; si invoca comunque `vkQueueSubmit`. | **Sì.** Un buffer invalidato dalla registrazione fallita non viene escluso dal submit. |
| `vkQueueSubmit` | `m_transferQueue` e timeline devono essere validi; il mutex globale protegge l’accesso host alla queue. | Su errore viene solo loggato. Il codice non aspetta, ma inserisce comunque `{m_currentTimelineValue, cmd}` in `m_pendingTransfers` e restituisce quel timeline ID. Il valore software è incrementato prima del submit. | **Sì, difetto confermato.** `GameWorld.cpp` ignora il valore restituito e marca l’esecuzione come `Completed`, poi può pubblicare una mesh mai trasferita. |
| `vkWaitSemaphores` | Chiamata solo dopo submit `VK_SUCCESS`, con wait al valore esatto e timeout infinito. | Il risultato è ignorato. Anche `VK_ERROR_DEVICE_LOST` o altro errore ritorna al percorso normale; l’ID viene restituito e il buffer registrato pending. | **Sì, difetto confermato.** Timeline ID non prova il completamento. La spec richiede `VK_SUCCESS` per poter dichiarare il wait riuscito; un device lost durante wait infinito deve comunque ritornare in tempo finito. |
| `vkGetSemaphoreCounterValue` GC | Chiamata se il timeline non è nullo. | Il result è ignorato; il valore di uscita parte da 0. I command buffer con ID minore/equal vengono liberati se il valore supera la soglia. | In caso di query fallita il GC normalmente conserva gli ID positivi, ma non registra errore/device lost né rende esplicito che lo stato è ignoto. Solo `VK_SUCCESS` può convalidare il valore letto. |
| `Drain` / distruttore | RuntimeManager chiama `Drain` dopo lo stop/join del JobSystem nel percorso nominale. | `vkQueueWaitIdle` è protetto dal queue mutex ma il result è ignorato; poi ogni command buffer pending viene liberato e la lista svuotata. Il distruttore, se trova pending, logga soltanto e distrugge comunque il timeline. `Drain` non prende `m_mutex`. | **Sì, sul percorso di errore.** Un idle non dimostrato non autorizza a liberare command buffer ancora pending; un distruttore invocato senza Drain può distruggere un semaforo ancora referenziato. |

Riferimenti implementativi: `src/render/VulkanDmaManager.cpp:15–25,28–60,67–108,111–236`; dichiarazione/result legacy in `src/render/VulkanDmaManager.h:26–63`; chiamata di produzione in `src/world/GameWorld.cpp:559–572` e altri callsite `367–374,406–414,738–747`.

### Stato che arriva oggi ai producer

`UploadMeshAsync` restituisce `uint64_t`; 0 rappresenta soltanto alcuni errori precoci, mentre un valore positivo può essere restituito anche dopo submit o wait fallito. Tutti i callsite individuati ignorano il return. Il worker `GameWorld` restituisce `JobExecutionStatus::Completed` dopo la funzione senza verificare il risultato, e la coda differita considera completato un payload così marcato. I callsite principali che allocano e pubblicano sono `GameWorld.cpp:367–383`, `406–424`, `559–621` e `738–747`.

## B — Contratto di risultato proposto (non implementato)

Separare il risultato dell’allocatore dal risultato DMA:

```text
VramAllocationResult = { status, handle, allocationInfo }
DmaUploadResult = { status, vkResult, timelineValue, commandBufferOwnership }
```

Stati DMA minimi da distinguere:

- `Completed` — solo dopo `vkQueueSubmit == VK_SUCCESS` e wait/query di completamento verificato con `VK_SUCCESS` (oppure dopo una politica async che conserva la risorsa fino a una prova successiva di completamento).
- `SubmitFailed`.
- `WaitFailed`.
- `CommandBufferAllocationFailed` / `CommandBufferBeginFailed` / `CommandBufferEndFailed` (raggruppabili in `CommandBufferNotReady` se il codice diagnostico mantiene il sottocodice).
- `InvalidBufferOrAllocation`.
- `FlushFailed`.
- `CancelledBeforeSubmit`.
- `DeviceLost`.
- `AllocationFailed` sul risultato VRAM.

Un timeline value è un’identità di target, non un certificato: `vkGetSemaphoreCounterValue` va accettato solo con `VK_SUCCESS` e valore >= target; `vkWaitSemaphores` solo con `VK_SUCCESS`. Se submit è riuscito ma wait/query fallisce, command buffer, staging range e destinazione restano posseduti/quarantinati finché completion o device-lost teardown non stabilisce il rilascio. La mesh non può essere pubblicata prima della conferma richiesta dal contratto.

Per un submit fallito per host/device OOM Vulkan garantisce che stato e contenuto delle risorse/sincronizzazioni referenziate restino invariati; se l’implementazione non può garantirlo deve restituire `VK_ERROR_DEVICE_LOST`. Il codice deve quindi distinguere il fallimento normale dal device lost e non inferire submission dal timeline software.

## C — Ownership e lifetime VRAM

### Allocatore

- `Allocate` e `Free` prendono `m_mutex`, ma `GetAllocation`, `GetAllocatedBytes` e `GetWastedBytes` leggono vettori/statistiche senza lock (`VramSlabAllocator.cpp:104–166`). `Allocate` può fare `m_allocations.push_back`/riuso mentre RenderManager legge la stessa vector; il modello consente data race C++ e invalidazione della memoria interna. Non è stato riprodotto con un detector.
- `Free` mette subito lo slot in `Bucket::freeSlots`; il prossimo `Allocate` dello stesso bucket lo può riutilizzare immediatamente. Il mutex protegge i metadati solo durante quelle chiamate, non attende la GPU.
- Gli handle sono indici riusati da `m_freeHandles`; non portano una generation counter. Un consumer che conservi un handle stale può osservare un handle numericamente valido che ora indica un’altra allocazione (rischio ABA, dipendente dalla correttezza di tutte le ownership ECS).
- `Free` non aggiorna `m_statAllocated`/`m_statWasted`; le statistiche non sono un conteggio affidabile del live VRAM.
- `AllocateNewCompartment` incrementa il conteggio e chiama un callback `void`, poi aggiunge pagine logiche senza sapere se `VulkanMemory::AddVramCompartment` ha creato un buffer. Se `vmaCreateBuffer` fallisce, l’allocatore può restituire un handle `valid` per un compartimento che non esiste; `GameWorld` indicizza `GetVramCompartments()[compartmentIdx]` senza verificare la dimensione. **Difetto di propagazione confermato staticamente**, non fault-injected.

### Sostituzione/distruzione/frames

`GameWorld` libera l’handle della mesh sostituita al commit (`GameWorld.cpp:380,421`), libera in `DestroyEntity` (`978–985`) e durante teardown (`198–230`). `RenderManager` raccoglie gli handle, chiama `GetAllocation`, registra `vkCmdDraw` e sottomette frame che restano in volo fino alle fence per frame (`RenderManager.cpp:1187–1225,1509–1790`). La free VRAM non memorizza l’ultima fence/timeline di lettura e non accoda una retire-list. Una successiva allocazione può riusare lo stesso offset mentre un precedente frame graphics lo sta ancora leggendo. **Rischio d’uso-after-recycle dimostrato dal modello; manifestazione su GPU non riprodotta.** Le fence frame proteggono il riuso del frame/command buffer, ma non sono collegate agli handle liberati.

Il percorso di shutdown nominale fa stop/join dei job, `Drain` DMA, elimina il manager DMA e l’allocator, poi `RenderManager::Shutdown` attende `vkDeviceWaitIdle` prima di distruggere `VulkanMemory` e i compartimenti. È una barriera di shutdown, non risolve il riuso anticipato durante normale sostituzione/distruzione di entity. Se `Drain` non ottiene una prova d’idleness, il codice attuale procede comunque alla liberazione.

### Visibilità transfer → draw

`VulkanCore::FindQueueFamilies` seleziona una famiglia transfer dedicata quando disponibile; `CreateLogicalDevice` crea le queue di graphics e transfer (`VulkanCore.cpp:224–252,321–365`). I compartimenti VRAM diventano `VK_SHARING_MODE_CONCURRENT` quando le famiglie differiscono (`VulkanMemory.cpp:72–94`), ma il renderer non usa `GetTimelineSemaphore()` (l’unico match è la dichiarazione in `VulkanDmaManager.h:63`). `vkWaitSemaphores` è un’attesa host; non sottomette un semaphore wait nella queue graphics né inserisce una pipeline memory dependency per il successivo vertex input. La spec definisce la dipendenza device-to-device tramite signal/wait semaphore tra submit, e submission order da solo non è una memory dependency.

**Gap statico confermato nel contratto di sincronizzazione; esito visivo/device dipendente non riprodotto.** Il piano minimo deve collegare l’upload al draw tramite timeline semaphore wait sul submit graphics (con stage di attesa appropriato), o una soluzione di barriera equivalente; se le queue family sono diverse ed il buffer è exclusive, aggiungere anche ownership transfer. Il buffer dei compartimenti è attualmente concurrent quando necessario, perciò resta comunque da assicurare la memory dependency.

## D — Race e classificazione

| Interazione | Classificazione | Evidenza |
|---|---|---|
| Worker `GetAllocation` vs renderer `GetAllocation`/`Allocate`/`Free` | **RACE STATICAMENTE POSSIBILE; non riprodotta** | `GetAllocation` legge `m_allocations` senza `m_mutex`; worker alloca mentre il renderer legge gli handle nelle viste ECS. |
| Worker DMA vs `RenderManager::FlushTransferBatch` sul `VkCommandPool` transfer condiviso | **RISCHIO CONDIZIONALE; non riprodotto** | Lo stesso `m_transferCommandPool` è condiviso (`RenderManager.cpp:92–109`; passato a DMA dal RuntimeManager). Se `UploadChunkMesh` venisse eseguita mentre il worker alloca/inizia/libera cmd, il pool sarebbe usato tramite lock distinti; le specifiche richiedono host synchronization al command pool in Begin/End/Reset/Free. Il solo callsite di `UploadChunkMesh` trovato è attualmente commentato in `FAIRWORLD.cpp`; `FlushTransferBatch` è un no-op quando `m_currentOffset==0`. Non è quindi dimostrata una sovrapposizione nel percorso corrente. |
| Queue submit transfer vs graphics/present | **Protetta per l’accesso host; dipendenza GPU mancante** | Il mutex condiviso protegge le chiamate host alle queue; non sostituisce semaphore wait/device memory dependency. |
| Worker A/B DMA upload | **Serializzata internamente in `UploadMeshAsync`** | `m_mutex` copre l’intera funzione, inclusa la wait. Non risolve la concorrenza col command pool persistente del RenderManager né il lifetime degli handle VRAM. |
| `Drain` vs upload/GetPendingTransferCount | **API non thread-safe; barriera d’uso nominale** | `Drain` manipola `m_pendingTransfers` senza `m_mutex`; RuntimeManager chiama dopo `JobSystem::Shutdown`/join (`RuntimeManager.cpp:76–118`). Altro uso concorrente non è stato trovato nel flusso corrente, ma il metodo non lo protegge autonomamente. |
| `UpdateStagingBuffer` vs upload | **SUSPECTED** | Scrive handle/puntatori senza `m_mutex`; i callback di nuovi compartimenti lo invocano. Una variazione effettiva del buffer dopo l’inizializzazione non è stata osservata. |
| Free allocazione vs draw già sottomessa | **RISCHIO DI LIFETIME CONFERMATO STATICAMENTE; GPU non testata** | Free e riciclo slot immediati; nessuna last-use fence per slot. |
| Staging ring wrap dopo wait non riuscito | **RISCHIO CONDIZIONALE** | Nel caso normale la funzione aspetta il transfer prima del ritorno. Se il wait fallisce e viene ignorato, gli offset non tracciati possono avvolgersi e sovrascrivere uno staging range ancora in uso. |

## E — Test isolati e fault injection

Nessun device Vulkan/GUI è stato avviato. Non esistono risultati runtime per submit/wait failure né per il reuse dello slot mentre una draw è pending. Un mock non può certificare upload reale.

Per la fase correttiva successiva si propone un’interfaccia di dispatch iniettata nel solo `VulkanDmaManager` test harness, con backend normale che inoltra alle Vulkan API e backend deterministico che restituisce risultati configurati per allocate/begin/end/flush/submit/counter/wait/queue-idle. Il harness può certificare stato restituito, cleanup dei command buffer, ownership mantenuta e mancata pubblicazione CPU in ciascun failure path; deve etichettare sempre i risultati come **test di controllo**, non GPU upload.

Test obbligatori dopo autorizzazione:

1. fault injection individuale per allocate, begin, flush, end, submit OOM, submit device-lost, wait error/device-lost, counter-query error, queue-idle error;
2. per ogni errore: nessuna pubblicazione, nessun command buffer perso, nessun free di risorsa pending, stato device-lost corretto;
3. timeline successivo monotono e wait/counter value confrontati con l’effettivo target;
4. overflow/size e buffer/allocation non validi rifiutati prima di `memcpy`/recording;
5. test multithread CPU per `Allocate`/`Free`/`GetAllocation` e callback fallito del compartimento;
6. test GPU/integration separato: transfer→graphics semaphore/memory dependency, stall/error device, più frame in-flight, sostituzione/distruzione chunk, e verifica che lo slot non sia riciclato prima dell’ultima fence di draw;
7. shutdown success, device lost e wait/idle failure, controllando i command buffer, timeline e handle allocator ancora posseduti.

## F — Regressioni CPU/headless eseguite

Toolchain esistente: CMake `4.3.1-msvc1`, Visual Studio generator `Visual Studio 18 2026`, MSBuild `18.10.1`, configurazione `build_r3z7`, Debug, `/m:1`.

| Controllo | Risultato | Exit code |
|---|---:|---:|
| Build Debug `ALL_BUILD` | **PASS** (durata 34.53 s) | 0 |
| `FairworldJobSystemContractTests.exe` | **PASS 10/10** | 0 |
| `FairworldPlanetaryVoxelE2ETests.exe --fixture ...TinySphere/world_map.json` | **PASS CPU 54/54** | 0 |
| `FairworldForgeVertexContractTests.exe` | **PASS** | 0 |
| `FairworldEditor.exe --validate-d12` | **PASS 4/4 gruppi** | 0 |

Tiny conserva i risultati attesi: 54 chunk/lookup, 54 solver, 504.877 voxel non-Air, 54 mesh CPU, 407.460 vertici e indici, 135.820 triangoli, zero invalid UV/normali/indici e zero winding mismatch. `MeshComponent` async e GPU upload sono **NOT VERIFIED** dal runner, che esplicitamente non li simula.

Log in `D1_2_8_GUI_SANDBOX/runtime/logs/`:

- `d1_2_18g1_build_debug.log`
- `d1_2_18g1_jobsystem.log`
- `d1_2_18g1_tiny_cpu.log`
- `d1_2_18g1_forge.log`
- `d1_2_18g1_d12.log`
- `d1_2_18g1_original_hashes_after.log`

Non è stato eseguito CTest completo, GUI, Vulkan Validation Layer o fault injection Vulkan.

## G — SHA-256 originali

Manifest letto: `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\original_input_manifest.csv` (40 file; verificatore: `Verify-OriginalInputs.ps1`). Il controllo prima e dopo i runner dà lo stesso risultato: **38/40 hash coincidono; due file restano divergenti dal manifest con gli hash correnti già documentati in D1.2.14b.2**. Nessun aggiornamento/rebaseline è stato eseguito.

| File | SHA-256 manifest | SHA-256 corrente prima e dopo | Stato |
|---|---|---|---|
| `assets/texture_cache.bin` | `BFFDC3DD710108999E3251C349A27F2B27F27923AFBBE4570F21120EE4107851` | `D1D8655D0B929F0367D8B67473ED36B88E1D1DC5517A543643590A6DEC8E621D` | Divergenza preesistente, nessun cambiamento in questo run |
| `assets/test_packaging_artifacts/pkg_roundtrip_03.json` | `0F0FE0DC7F2BEB69BF229092628D8C9159CAB7CD3730C7F97C26B9B839BE36C5` | `9C91BBA3D20DA85A2EA9D1C46291371F73F5E2B43C012DA2A0873BAE3EC7E089` | Divergenza preesistente, nessun cambiamento in questo run |

Verificatore prima e dopo: exit 1 per queste due divergenze note, non per nuove variazioni. Gli altri 38 input hanno match SHA-256. La verifica non implica che i due file siano conformi al manifest: restano dichiaratamente divergenti.

## H — Piano minimo ordinato per dipendenza

1. Definire il risultato tipizzato di allocazione/upload e la politica di ownership per `SubmittedButNotConfirmed`/`DeviceLost`; mantenere la mesh non pubblicabile finché lo stato non autorizza il commit.
2. Correggere solo la propagazione dei risultati e il cleanup command buffer del singolo percorso DMA; controllare precondizioni/range e ogni `VkResult`. Non riutilizzare staging/VRAM dopo wait non riuscito.
3. Separare/sincronizzare il command pool del DMA dal pool/buffer persistente del RenderManager, o applicare un lock di pool condiviso a ogni accesso host richiesto.
4. Collegare le scritture transfer all’uso graphics con timeline device wait/memory dependency. Registrare quale queue family e ownership mode si sta usando.
5. Aggiungere protezione dei metadati `VramSlabAllocator` per letture e callback di compartment con esito, e gestione di allocazione fallita. Questo non sostituisce il lifetime GPU.
6. Aggiungere retirement delle suballocazioni sulla prova di ultima lettura GPU (fence per frame o timeline graphics); soltanto dopo rimettere lo slot nel free list. Proteggere i handle da riuso ABA.
7. Testare infine shutdown e device lost, quindi eseguire integrazione Vulkan reale nel sandbox. Nessuna di queste correzioni è stata applicata in questa revisione.

Fonti API Vulkan primarie usate per i contratti: [vkQueueSubmit](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueueSubmit.html), [vkWaitSemaphores](https://docs.vulkan.org/refpages/latest/refpages/source/vkWaitSemaphores.html), [vkGetSemaphoreCounterValue](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSemaphoreCounterValue.html), [vkFlushMappedMemoryRanges](https://docs.vulkan.org/refpages/latest/refpages/source/vkFlushMappedMemoryRanges.html), [vkEndCommandBuffer](https://docs.vulkan.org/refpages/latest/refpages/source/vkEndCommandBuffer.html), [vkFreeCommandBuffers](https://docs.vulkan.org/refpages/latest/refpages/source/vkFreeCommandBuffers.html), [vkQueueWaitIdle](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueueWaitIdle.html), [Synchronization and Cache Control](https://docs.vulkan.org/spec/latest/chapters/synchronization.html), [Command Buffers](https://docs.vulkan.org/spec/latest/chapters/cmdbuffers.html).

**Conclusione:** CPU regressions PASS; DMA result propagation e VRAM GPU lifetime FAIL statico; GPU runtime **NOT VERIFIED**. Nessun commit/push. D1 non è dichiarato FROZEN.

