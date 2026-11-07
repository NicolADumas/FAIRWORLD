# D1.2.18g.2b — VRAM Allocator Safety & Deferred Retirement

## Esito

**CPU/headless: PASS. Sicurezza del retirement Vulkan reale: NOT VERIFIED.**

La patch protegge gli handle stale, sincronizza i metadati, rifiuta la pubblicazione di compartimenti la cui callback fallisce e lega il riuso delle suballocazioni a un seriale graphics completato. Il teardown ora richiede anche una conferma di `vkDeviceWaitIdle` prima di eliminare allocator e manager DMA quando esistono allocazioni VRAM. Nessuna sessione GUI/GPU è stata eseguita; quindi non si dichiara certificato il comportamento del driver o della fence reale.

## Audit e modifiche

- `src/render/VramSlabAllocator.h/.cpp`: handle runtime a 64 bit (32 bit slot + 32 bit generation), stati `Active`, `Retired` e `Quarantined`, accessi metadata protetti da mutex, `GetAllocation` restituisce una copia. `Free` non libera handle stale o non attivi. Le statistiche includono richieste ritirate/quarantinate finché la suballocazione non è effettivamente rilasciata.
- La callback di compartimento restituisce `bool`. Un fallimento o un’eccezione lascia invariato il numero di compartimenti e non produce un handle valido.
- `src/render/vulkan/VulkanMemory.h/.cpp`, `src/render/RenderManager.h/.cpp`, `src/core/app/RuntimeManager.cpp`: creazione dei buffer propagata al chiamante; il seriale graphics viene associato alla submission principale protetta dalla fence del frame. Il seriale completed avanza solo dopo `vkWaitForFences == VK_SUCCESS`; in shutdown le allocazioni ancora conteggiate richiedono inoltre `WaitForGraphicsIdle() == VK_SUCCESS`. Errori e device lost impediscono la liberazione dell’ownership allocator/DMA.
- `src/world/GameWorld.cpp/.h`: sostituzione mesh, distruzione entity e teardown ritirano le allocazioni pubblicate al seriale dell’ultima submission graphics. Senza timeline/renderer utilizzabile o con device lost, il handle viene ritirato a seriale `UINT64_MAX`, perciò non può essere riutilizzato. Gli upload DMA dall’esito incerto restano quarantinati; una query timeline riuscita deve confermare il target prima del rilascio.
- `src/render/VramAllocatorContractTests.cpp`: nuovi test CPU per handle generazionali, fallimento callback, retirement, quarantena e accessi concorrenti.
- `src/render/DmaResultContractTests.cpp`: la verifica che un timeline sotto target non rilasci la risorsa è stata incorporata nello scenario wait-failure per mantenere il conteggio DMA previsto di 14.
- `CMakeLists.txt`: target `FairworldVramAllocatorContractTests`.

I callback di compartimento già presenti nei percorsi RuntimeManager, ForgeState, PlayState e BlockMakerState verificano esito e indice del buffer prima di aggiornare la staging DMA. I layout vertex, il draw, il modello a compartimenti, gli shader, i materiali, i serializer e i salvataggi non sono stati cambiati in questa fase.

## Contratti di handle e ownership

L’handle è composto da slot (32 bit bassi) e generation (32 bit alti). A ogni rilascio riuscito la generation aumenta; il riuso del medesimo slot produce un handle differente. Gli handle sono runtime-only e non vengono serializzati. Le API metadata serializzano l’accesso tramite mutex e non espongono riferimenti interni.

La transizione di una mesh pubblicata è:

`Active → Retired(serial ultima submission graphics) → riutilizzabile dopo fence completata`

Un upload DMA non confermato segue invece:

`Active → Quarantined → rilascio solo dopo query timeline riuscita e target raggiunto`

Il completamento DMA non è usato come prova che la graphics queue abbia terminato le letture. Durante shutdown una conferma device-idle riuscita copre il completamento globale; se fallisce, le ownership restano trattenute. Il device lost viene trattato come completamento non confermato.

## Validazione eseguita

Tutti i comandi sono stati lanciati da `D:\FAIRWORLD\D1_2_8_GUI_SANDBOX\runtime`; i log sono nella stessa directory `logs`. Build eseguita con il preset/toolchain già esistenti, `build_r3z7`, Debug, MSBuild seriale `/m:1`.

| Verifica | Risultato | Exit code | Log |
|---|---:|---:|---|
| Debug `ALL_BUILD` finale | PASS | 0 | `logs/d1_2_18g2b_all_build_final2.log` |
| DMA fault/result contract | 14/14 PASS | 0 | `logs/d1_2_18g2b_dma_final3.log` |
| VRAM allocator contract | 6/6 PASS | 0 | `logs/d1_2_18g2b_allocator_final3.log` |
| JobSystem contract | 10/10 PASS | 0 | `logs/d1_2_18g2b_jobs_final3.log` |
| Forge Vertex Contract | PASS | 0 | `logs/d1_2_18g2b_forge_final3.log` |
| Tiny CPU E2E, due esecuzioni | PASS, 54/54 in entrambe | 0 / 0 | `logs/d1_2_18g2b_tiny_final3a.log`, `logs/d1_2_18g2b_tiny_final3b.log` |
| `FairworldEditor.exe --validate-d12` | PASS | 0 | `logs/d1_2_18g2b_d12_final3.log` |
| `git diff --check` sui file di questa patch | PASS | 0 | Output nel log di sessione |

Il runner Tiny ha riprodotto due volte: 54 chunk ECS, 54 lookup/registrazioni, 54 completamenti TerrainSolver, 504.877 voxel non-Air, 54 mesh CPU, 135.820 triangoli, zero mismatch di winding/material association/UV/normali, hash mesh stabile `0x20c0d276200e0e20`. Il runner dichiara correttamente `MeshComponent` pubblicata e upload GPU **NOT VERIFIED**.

Il `git diff --check` globale della working tree non è pulito: segnala whitespace su righe già presenti/modificate in `src/states/BlockMakerState.cpp`, incluse altre righe del precedente lavoro Forge UV. Non sono state corrette per non alterare modifiche fuori dal perimetro allocator di questo intervento. Il controllo ristretto ai file di questa patch passa.

## Integrità

Manifest originale: 40 file. Dopo build e test: 38 hash corrispondono; le stesse due divergenze storiche sono rimaste identiche e non è stato aggiornato il manifest:

| File | SHA-256 atteso | SHA-256 osservato |
|---|---|---|
| `assets/texture_cache.bin` | `BFFDC3DD710108999E3251C349A27F2B27F27923AFBBE4570F21120EE4107851` | `D1D8655D0B929F0367D8B67473ED36B88E1D1DC5517A543643590A6DEC8E621D` |
| `assets/test_packaging_artifacts/pkg_roundtrip_03.json` | `0F0FE0DC7F2BEB69BF229092628D8C9159CAB7CD3730C7F97C26B9B839BE36C5` | `9C91BBA3D20DA85A2EA9D1C46291371F73F5E2B43C012DA2A0873BAE3EC7E089` |

Verifica finale: `logs/d1_2_18g2b_original_hashes_after_final.log`. I dati originali non sono stati ripristinati o sovrascritti.

## Matrice criteri e limiti

| Criterio | Stato | Evidenza/limite |
|---|---|---|
| Allocate/Free, statistiche, generazioni stale | PASS | Allocator contract 6/6 |
| Accessi metadata multithread | PASS | 8 thread × 1.000 cicli nel test allocator |
| Callback compartimento fallita non pubblica handle | PASS | Test allocator |
| Retirement non riusa prima del seriale completato | PASS CPU | Test stato allocator; non usa una fence Vulkan reale |
| Quarantena DMA finché il timeline target non è confermato | PASS CPU/fault injection | DMA 14/14 e allocator contract |
| Sostituzione MeshComponent durante draw realmente in-flight | NOT VERIFIED | Il runner testa l’invariante allocator per seriale, ma non avvia renderer/submit GPU |
| `WaitForGraphicsIdle`/device lost di shutdown su device Vulkan reale | NOT VERIFIED | Nuovo gate compilato; nessuna GUI o device-session secondo i vincoli |
| Rilascio/reuse dopo fence reale e shutdown ordinario GPU | NOT VERIFIED | Richiede successiva validazione GPU controllata |

## Rischi residui e raccomandazione

1. La prova dei seriali allocator è CPU-only; manca la conferma runtime che ogni percorso di draw della build finale usa il submit/fence coperto dal seriale, specialmente eventuali percorsi di rendering alternativi.
2. La nuova attesa `WaitForGraphicsIdle()` è compilata ma non esercitata contro Vulkan reale, inclusi errore generico e `VK_ERROR_DEVICE_LOST`.
3. Il teardown trattiene allocator e DMA manager se non può dimostrare il completamento; la gestione della vita del processo in quel caso resta una retention intenzionale e non una distruzione completata.
4. Non è stato aggiunto un test d’integrazione con sostituzione di `MeshComponent` e draw in-flight reale; l’asserzione disponibile prova il contratto allocator, non il percorso GPU end-to-end.

**Raccomandazione:** patch CPU/headless accettabile; gate di sicurezza Vulkan reale **NOT VERIFIED**. Non dichiarare GPU safety PASS finché una sessione GPU approvata non verifica la fence graphics, device-idle di shutdown, sostituzione mesh in-flight e ownership dopo device lost. Nessun commit/push effettuato; D1 non è dichiarato FROZEN.
