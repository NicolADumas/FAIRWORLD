# D1.2.18g.2a — DMA Result Safety Report

## Esito

**Patch e verifiche CPU/headless: PASS.** Ogni upload è trattato come completato soltanto dopo `VK_SUCCESS` da `vkWaitSemaphores`. Un trasferimento sottomesso ma non confermato resta pending e non può riutilizzare il buffer staging né pubblicare la mesh.

**Validazione GPU reale: NOT VERIFIED.** I test fault-injection usano callback Vulkan simulate; non attestano l'esecuzione di una copia su GPU, la visibilità tra transfer e graphics queue o il rendering. Nessuna GUI è stata avviata.

## Modifiche

- [VulkanDmaManager.h](D:/FAIRWORLD/FAIRWORLD/src/render/VulkanDmaManager.h) e [VulkanDmaManager.cpp](D:/FAIRWORLD/FAIRWORLD/src/render/VulkanDmaManager.cpp): introdotti `DmaUploadResult`, `DmaUploadStatus` e `DmaSubmissionState`. Il risultato conserva `VkResult` e timeline target. Sono controllati input, staging, intervallo destinazione, compartment buffer passato dal chiamante, timeline, command pool e queue. Sono propagati gli esiti di allocate, begin, flush, end, submit, wait, counter query e queue idle.
- [GameWorld.cpp](D:/FAIRWORLD/FAIRWORLD/src/world/GameWorld.cpp) e [GameWorld.h](D:/FAIRWORLD/FAIRWORLD/src/world/GameWorld.h): i percorsi differiti, worker e `UploadMeshToVram` ora controllano il risultato. Non si pubblica una mesh dopo upload fallito e il contatore DMA sale solo su `Completed`. Gli handle collegati a submission non confermate restano esplicitamente riservati in `m_unconfirmedDmaAllocations`; il job fallisce e `pendingJob` segue il percorso di completamento fallito previsto dal contratto 18f.
- [RuntimeManager.cpp](D:/FAIRWORLD/FAIRWORLD/src/core/app/RuntimeManager.cpp): controlla `Drain()` e i conteggi reali prima di dichiarare il cleanup completato o distruggere DMA manager e allocator. Se rimangono trasferimenti o command buffer non confermati, registra cleanup incompleto e conserva i proprietari invece di cancellarli.
- [DmaResultContractTests.cpp](D:/FAIRWORLD/FAIRWORLD/src/render/DmaResultContractTests.cpp) e [CMakeLists.txt](D:/FAIRWORLD/FAIRWORLD/CMakeLists.txt): aggiunto il runner CPU fault-injection `FairworldDmaResultContractTests`.

Le modifiche sono state sovrapposte alla working tree già modificata: non ho ripristinato o riorganizzato le modifiche precedenti.

## Contratto e cleanup

| Percorso | Risultato | Command buffer / staging | Mesh e allocazione |
| --- | --- | --- | --- |
| Input o intervallo non valido | `InvalidInput`, `NotSubmitted` | Nessuna scrittura/submission | Non pubblicata |
| Allocate, begin, flush o end falliscono | Stato specifico, `NotSubmitted` | Il command buffer allocato viene liberato; nessuna submission | Non pubblicata; allocazione liberabile |
| Submit fallisce normalmente | `SubmitFailed`, `SubmitFailed` | Non entra tra i pending e viene liberato | Non pubblicata; allocazione liberabile |
| Submit segnala device lost | `DeviceLost`, `DeviceLost` | Trattenuto conservativamente, non riciclato | Non pubblicata; allocazione riservata |
| Submit riuscito e wait riuscito | `Completed`, `Completed` | Liberato dopo conferma; staging riutilizzabile | Pubblicazione consentita |
| Submit riuscito e wait fallisce | `WaitFailed`, `SubmittedButNotConfirmed` | Pending mantenuto; gli upload successivi sono respinti finché non si conferma il completamento | Non pubblicata; allocazione mantenuta riservata |
| Counter query fallisce | `CounterQueryFailed`; `DeviceLost` se applicabile | Nessuna nuova scrittura o submission | Non pubblicata |
| `vkQueueWaitIdle` fallisce in Drain | `Drain()` restituisce il `VkResult`; device lost distinto | Non libera pending né command buffer | Runtime shutdown resta incompleto e i proprietari non vengono eliminati |
| Drain riuscito | `VK_SUCCESS` | Pending liberati, conteggio riportato a zero | Cleanup dei proprietari consentito |

Non sono state aggiunte attese oltre al wait già previsto (`UINT64_MAX`). Se il wait fallisce, l'upload successivo non sovrascrive lo staging range posseduto dal pending. `vkGetSemaphoreCounterValue` viene considerato valido soltanto con `VK_SUCCESS`.

## Fault-injection CPU

Runner: `FairworldDmaResultContractTests.exe`; output completo in [d1_2_18g2a_dma_fault_tests_final.log](D:/FAIRWORLD/D1_2_8_GUI_SANDBOX/runtime/logs/d1_2_18g2a_dma_fault_tests_final.log).

- **14/14 PASS, exit code 0**: input invalido; command allocation, begin, flush ed end failure; submit failure; device lost su submit, wait e queue idle; wait failure e staging non riutilizzabile; counter query failure; Drain success dopo pending; nessuna liberazione prematura dopo queue-idle failure; completamento solo dopo wait riuscito.
- La fault injection è una verifica di propagazione/ownership sul boundary CPU, non un upload Vulkan reale.

## Regressioni eseguite

Tutti i comandi sono stati lanciati dalla working directory sandbox `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime`; il fixture Tiny è letto da `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json`.

| Controllo | Risultato | Exit code | Log |
| --- | --- | ---: | --- |
| Debug `ALL_BUILD`, `build_r3z7`, MSBuild `/m:1` | PASS | 0 | [d1_2_18g2a_all_build_pass.log](D:/FAIRWORLD/D1_2_8_GUI_SANDBOX/runtime/logs/d1_2_18g2a_all_build_pass.log) |
| DMA fault contract | PASS 14/14 | 0 | [d1_2_18g2a_dma_fault_tests_final.log](D:/FAIRWORLD/D1_2_8_GUI_SANDBOX/runtime/logs/d1_2_18g2a_dma_fault_tests_final.log) |
| JobSystem Contract | PASS 10/10 | 0 | [d1_2_18g2a_jobsystem_final.log](D:/FAIRWORLD/D1_2_8_GUI_SANDBOX/runtime/logs/d1_2_18g2a_jobsystem_final.log) |
| Tiny CPU E2E | PASS | 0 | [d1_2_18g2a_tiny_cpu_final.log](D:/FAIRWORLD/D1_2_8_GUI_SANDBOX/runtime/logs/d1_2_18g2a_tiny_cpu_final.log) |
| Forge Vertex Contract | PASS | 0 | [d1_2_18g2a_forge_vertex_final.log](D:/FAIRWORLD/D1_2_8_GUI_SANDBOX/runtime/logs/d1_2_18g2a_forge_vertex_final.log) |
| `FairworldEditor.exe --validate-d12` | PASS 4/4 | 0 | [d1_2_18g2a_validate_d12_final.log](D:/FAIRWORLD/D1_2_8_GUI_SANDBOX/runtime/logs/d1_2_18g2a_validate_d12_final.log) |

Tiny E2E ha mantenuto **54/54 chunk**, **504.877 voxel non-Air**, **54 mesh CPU**, **135.820 triangoli**, zero winding mismatch e zero attributi mesh invalidi. Il runner marca esplicitamente la pubblicazione ECS/GPU come NOT VERIFIED.

Due tentativi intermedi di build hanno fallito per invocazioni MSBuild concorrenti che si sono contese gli stessi artefatti (`ForgeState.obj` / `FairworldCore.lib`). Ho interrotto il tentativo sovrapposto; la build seriale successiva e la build finale registrata sopra sono terminate con exit code 0.

## Diff check

- `git diff --check` sui file di questo intervento: **exit 0** (solo avvisi di conversione LF/CRLF).
- `git diff --check` sull'intera working tree: **exit 2** per whitespace già presente nelle modifiche preesistenti a `src/states/BlockMakerState.cpp` alle righe 11565, 11572, 11573, 11808, 11830 e 11832. Non ho modificato quel file per non ampliare la patch.

## Integrità degli originali

Il controllo post-run del manifest esistente ha riportato gli stessi due scostamenti già presenti nel baseline 18g.1, senza nuove divergenze tra i 40 file:

- `assets/texture_cache.bin`: manifest `BFFDC3DD710108999E3251C349A27F2B27F27923AFBBE4570F21120EE4107851`; hash rilevato prima e dopo `D1D8655D0B929F0367D8B67473ED36B88E1D1DC5517A543643590A6DEC8E621D`.
- `assets/test_packaging_artifacts/pkg_roundtrip_03.json`: manifest `0F0FE0DC7F2BEB69BF229092628D8C9159CAB7CD3730C7F97C26B9B839BE36C5`; hash rilevato prima e dopo `9C91BBA3D20DA85A2EA9D1C46291371F73F5E2B43C012DA2A0873BAE3EC7E089`.

Baseline e post-run: 38/40 corrispondenti al manifest; i due file sopra sono divergenze storiche invariate. Manifest non aggiornato e originali non ripristinati.

## Rischi residui

1. Un trasferimento non confermato trattiene la propria allocazione fino al cleanup del relativo allocator; non viene riciclata prematuramente, ma il recupero selettivo dell'allocazione dopo una successiva conferma timeline non è implementato.
2. Se `Drain()` fallisce, il runtime mantiene manager e allocator e marca il cleanup incompleto. La successiva distruzione del device/renderer e la riconciliazione di tali risorse non sono state esercitate; questo caso resta un gate di shutdown esplicitamente non completato, non un PASS.
3. Non è stato verificato il memory dependency/ownership transfer tra transfer queue e graphics queue. Tale sincronizzazione è fuori perimetro di questa patch e rimane un gate GPU separato.
4. L'audit precedente segnala un command pool transfer condiviso con il renderer. Il mutex DMA serializza i callsite DMA, ma non dimostra da solo la sincronizzazione host con eventuali operazioni renderer sullo stesso pool; il percorso renderer resta fuori perimetro.
5. Non è stata eseguita GUI, Vulkan Validation Layer su upload reale o rendering GPU.

**Conclusione:** il contratto CPU di risultato e i percorsi ordinari di cleanup sono verificati; fault injection e regressioni richieste sono PASS. La correttezza dell'upload/rendering GPU reale, il recupero completo dopo device lost e la sincronizzazione transfer→graphics restano NOT VERIFIED/aperti.
