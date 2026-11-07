# D1.2.18g.2c — Transfer → Graphics Synchronization & Mesh Lifetime

## Esito

**CPU/headless contracts: PASS. GPU runtime validation: NOT VERIFIED.**

Il percorso attivo ora porta nella submit graphics una dipendenza Vulkan esplicita dal valore più recente della timeline DMA non ancora atteso dalla graphics queue. Il seriale graphics si aggiorna soltanto quando la submit primaria riesce; il retirement continua a richiedere la fence reale del frame. Non è stata avviata la GUI e non si dichiara PASS GPU.

## Audit del percorso e delle queue

### Queue family e buffer VRAM

- `VulkanCore::FindQueueFamilies` seleziona una famiglia con `GRAPHICS`, una famiglia present e una famiglia `TRANSFER` dedicata quando è disponibile una queue `TRANSFER` senza bit `GRAPHICS`; in mancanza usa la famiglia graphics. Il device richiede una queue per ciascuna famiglia unica. Se transfer e graphics coincidono, `vkGetDeviceQueue` restituisce la stessa queue di famiglia/index.
- `VulkanMemory::AddVramCompartment` crea buffer con uso `VERTEX_BUFFER | INDEX_BUFFER | STORAGE_BUFFER | TRANSFER_DST`. Quando graphics e transfer hanno famiglie distinte li crea `VK_SHARING_MODE_CONCURRENT` includendo entrambe le famiglie; quando coincidono usa `VK_SHARING_MODE_EXCLUSIVE`.
- Nel modello attivo non serve ownership release/acquire: famiglie distinte usano concurrent sharing; famiglia uguale significa stessa queue/index e sharing exclusive. La dipendenza di memoria resta necessaria in entrambi i casi ed è fornita dal signal/wait timeline.

### Upload e pubblicazione

`VulkanDmaManager::UploadMeshAsync` alloca/inizializza un command buffer dal pool transfer, copia staging → buffer VRAM e sottomette il command buffer alla transfer queue segnalando `m_transferTimeline` al target monotono successivo. La funzione attende poi il valore lato host: ritorna `Completed` soltanto quando `vkWaitSemaphores` riesce. Errori di wait e device lost non sono completamento.

I consumer `GameWorld` controllano `DmaUploadResult::completed()` prima di pubblicare o sostituire la `MeshComponent`; upload non confermati vengono quarantinati. Il target più recente è ora esposto come snapshot protetto dal mutex DMA.

### Dipendenza transfer → graphics

`RenderManager::RenderDesktop` confronta il valore transfer sottomesso con l’ultimo valore già atteso dalla graphics queue. Se è nuovo, la submit primaria attende:

- `imageAvailable[currentFrame]` con stage `COLOR_ATTACHMENT_OUTPUT` e valore timeline binario 0;
- la timeline DMA al valore esatto selezionato con stage `VERTEX_INPUT`.

La `VkTimelineSemaphoreSubmitInfo` fornisce valori `[0, target]` per i due wait semaphore e `[0]` per il binary render-finished signal. Il signal della transfer submission seguito dal wait device-side crea la dipendenza transfer-write → vertex-input read. La graphics submit aggiorna l’ultimo target atteso solo dopo `vkQueueSubmit == VK_SUCCESS`. Se la submit fallisce, incluso il percorso recovery vuoto, il target resta pendente per la successiva draw; device lost non avanza il target né il seriale graphics.

### Fence, seriale e retirement

- Ogni submit primaria riuscita associa un seriale al frame slot e alla relativa `m_inFlightFences[currentFrame]`.
- `m_completedGraphicsSerial` avanza soltanto quando `vkWaitForFences` sul frame slot ritorna `VK_SUCCESS`; allora `CollectRetired` può riciclare le suballocazioni fino a quel seriale.
- Sostituzione e distruzione delle mesh ritirano al seriale graphics più recente. Quando il renderer/device/timeline non sono utilizzabili, il handle è trattenuto al seriale `UINT64_MAX` e non viene riutilizzato.
- L’update ECS e la registrazione dei comandi avvengono sul percorso main-thread corrente; i worker mesh restituiscono payload differiti e non mutano direttamente la registry. Questo rende la sostituzione normale seriale rispetto alla registrazione del frame. Il test di stato allocator modella il caso frame-in-flight, ma non sostituisce una prova GPU concorrente reale.

### Command pool e accessi host

Il command pool transfer è condiviso tra il command buffer legacy persistente di `RenderManager` e i command buffer temporanei DMA. Il mutex interno DMA serializzava tra loro le operazioni DMA, ma non il reset legacy. È stato aggiunto `m_transferCommandPoolMutex`: `VulkanDmaManager` lo usa per allocate/free; `RenderManager::FlushTransferBatch` lo usa intorno a `vkResetCommandBuffer` e al successivo begin. Il mutex è distinto dal queue mutex e protegge solo le operazioni rilevanti del pool. I callsite di inizializzazione DMA in RuntimeManager, ForgeState, PlayState e BlockMaker passano lo stesso mutex.

## Percorsi alternativi

- Le draw di `MeshComponent` nei renderer applicativi sono registrate dentro `RenderDesktop` e usano la submit primaria sopra descritta. `RenderStereo` è ancora TODO e non costituisce un percorso draw attivo.
- `CopyBuffer` usa command buffer/pool graphics, sottomette sulla graphics queue e attende la propria fence prima di ritornare.
- Il percorso legacy `UploadChunkMesh` usa un command buffer transfer persistente, buffer `EXCLUSIVE` e fence host; il solo callsite trovato in `FAIRWORLD.cpp` è commentato. Se quel percorso venisse riattivato su device con famiglie diverse, richiederebbe ownership transfer o buffer concurrent e un gate di pubblicazione appropriato. Rimane **NOT VERIFIED / dormant**, non è usato per certificare il contratto attivo `MeshComponent`.
- Le submission ausiliarie texture/compute usano command buffer e semantiche distinte; non pubblicano handle `VramSlabAllocator` di `MeshComponent`.

## Diff funzionale

- `src/render/TransferGraphicsSync.h`: piano deterministico per target timeline/stage e classificazione ownership famiglia.
- `src/render/TransferGraphicsSyncTests.cpp` + target CMake: test CPU del target nuovo, target già consumato, retry dopo submit fallita, same/concurrent queue e caso exclusive con famiglie distinte.
- `src/render/VulkanDmaManager.h/.cpp`: snapshot del valore timeline sotto lock e mutex condiviso per allocate/free dal pool transfer.
- `src/render/RenderManager.h/.cpp`: wait timeline nella submit graphics primaria, stage `VERTEX_INPUT`, target consumato soltanto al successo; reset command buffer legacy protetto dal mutex pool.
- `src/core/app/RuntimeManager.cpp`, `src/states/ForgeState.cpp`, `src/states/PlayState.cpp`, `src/states/BlockMakerState.cpp`: passaggio del mutex del pool ai manager DMA.
- `src/core/app/FAIRWORLD.cpp`: se il runtime non conferma il drain/idle, conserva l’intero RenderManager invece di distruggere il command pool da cui il DMA manager può ancora possedere command buffer.
- `src/render/VramAllocatorContractTests.cpp`: scenario seriale retirement durante replacement con due frame in-flight e più draw riferite allo stesso handle.

Nessuna modifica alla geometria CPU, winding, shader, materiali, serializer, salvataggi o dimensione dei compartimenti.

## Validazione CPU/headless

Comandi eseguiti con working directory `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime`, binari Debug da `D:\FAIRWORLD\FAIRWORLD\bin\Debug` e fixture Tiny immutato `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json`.

| Verifica | Esito | Exit code | Log |
|---|---:|---:|---|
| Debug `ALL_BUILD`, MSBuild `/m:1` | PASS | 0 | `logs/d1_2_18g2c_all_build_final.log` |
| Transfer → Graphics Sync contract | 6/6 PASS | 0 | `logs/d1_2_18g2c_sync_final.log` |
| DMA fault/result tests | 14/14 PASS | 0 | `logs/d1_2_18g2c_dma_final.log` |
| VRAM allocator | 7/7 PASS | 0 | `logs/d1_2_18g2c_allocator_final.log` |
| JobSystem | 10/10 PASS | 0 | `logs/d1_2_18g2c_jobsystem_final.log` |
| Forge Vertex Contract | PASS | 0 | `logs/d1_2_18g2c_forge_vertex_final.log` |
| Tiny CPU E2E, run A e B | PASS in entrambe | 0 / 0 | `logs/d1_2_18g2c_tiny_final_a.log`, `logs/d1_2_18g2c_tiny_final_b.log` |
| `FairworldEditor.exe --validate-d12` | PASS | 0 | `logs/d1_2_18g2c_d12_final.log` |

Tiny ha mantenuto 54/54 chunk, 504.877 voxel non-Air, 54 mesh CPU, 135.820 triangoli, zero mismatch di winding e hash mesh stabile `0x20c0d276200e0e20`. MeshComponent pubblicata e upload GPU restano NOT VERIFIED dal runner CPU.

La prima build ha fallito perché `RenderManager.cpp` aveva solo il forward declaration di `VulkanDmaManager`; aggiunto l’header diretto e la build finale completa ha exit 0.

`git diff --check` sui file diversi da `src/states/BlockMakerState.cpp` passa. Il controllo del file BlockMaker segnala whitespace nel blocco callback e nelle righe diagnostiche UV delle patch precedenti, oltre alle due righe del callsite pool aggiunto qui; il controllo complessivo della patch che include quel file quindi non passa. Nessuna pulizia generale della working tree è stata eseguita.

## Integrità

Il manifest originale contiene 40 file: 38 coincidono. Le due sole divergenze restano quelle documentate in precedenza e non sono state rebaseline-ate:

| File | Atteso | Attuale |
|---|---|---|
| `assets/texture_cache.bin` | `BFFDC3DD710108999E3251C349A27F2B27F27923AFBBE4570F21120EE4107851` | `D1D8655D0B929F0367D8B67473ED36B88E1D1DC5517A543643590A6DEC8E621D` |
| `assets/test_packaging_artifacts/pkg_roundtrip_03.json` | `0F0FE0DC7F2BEB69BF229092628D8C9159CAB7CD3730C7F97C26B9B839BE36C5` | `9C91BBA3D20DA85A2EA9D1C46291371F73F5E2B43C012DA2A0873BAE3EC7E089` |

Log hash post-run: `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime\logs\d1_2_18g2c_original_hashes_after.log`.

## Piano di validazione GPU sandbox

Senza avviare ora la GUI, il gate GPU successivo deve usare sandbox e Vulkan Validation Layers e registrare:

1. Queue family e queue handle effettivi scelti dal device.
2. Un upload reale di mesh voxel Tiny; valore timeline segnalato e valore atteso dalla submit graphics, stage wait `VERTEX_INPUT`.
3. Draw della mesh, due frame in-flight e sostituzione della MeshComponent durante registrazione/submit.
4. Fence di ciascun frame e seriale ritirato/completato; verifica che l’offset non venga riassegnato prima della fence e venga riciclato dopo.
5. Assenza di errori di sync/ownership nel Validation Layer per entrambe le configurazioni queue disponibili. Se il device offre famiglie distinte, verificare concurrent sharing; se uguali, exclusive sharing.
6. Shutdown normale con device idle riuscito. Fault injection device-lost/submit failure solo in harness controllato, verificando retention e nessuna falsa conferma.
7. Verifica separata del dormant `UploadChunkMesh` solo se quel percorso viene riattivato.

**Conclusione:** criterio CPU PASS; criterio GPU **NOT VERIFIED**, necessita sessione Vulkan sandbox autorizzata. Nessun commit/push effettuato; nessun freeze D1 dichiarato.
