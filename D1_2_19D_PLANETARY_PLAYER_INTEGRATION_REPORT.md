# FAIRWORLD — D1.2.19d Planetary Player Integration Report

## Stato conclusivo

**PARTIAL PASS.** Tutti i gate automatici CPU/headless richiesti sono PASS. Il gate GUI/GPU è **NOT VERIFIED** perché l’istruzione esplicita vieta di avviare o controllare la GUI senza una nuova autorizzazione. Non è quindi certificata l’integrazione runtime completa e non viene dichiarato D1 FROZEN.

Nessun sorgente è stato modificato; nessun commit, push, reset o rebaseline è stato eseguito.

## Preflight

- Processi `FairworldEditor`/`FAIRWORLD` attivi al preflight: **0**. Non è stato terminato alcun processo.
- Fixture originale: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json`, 5.384 byte, SHA-256 `47AF4358E4BD663926E64A43ACF2B116F3A2939BA5605FC89B2983D32A9AD503`.
- Copia preparata: `D:\FAIRWORLD\D1_2_19D_SANDBOX\fixtures\TinySphere\world_map.json`; hash identico alla sorgente.
- Reparse point nel sandbox D1.2.19d: **0**.
- Manifest originale D1.2.8: **40** file. Stato corrente rispetto al manifest: **38 MATCH, 2 divergenze storiche, 0 mancanti**. Le divergenze sono le stesse note e documentate; non sono state ignorate né accettate aggiornando il manifest:

| File | SHA-256 manifest | SHA-256 corrente prima/dopo i test |
|---|---|---|
| `assets/texture_cache.bin` | `BFFDC3DD710108999E3251C349A27F2B27F27923AFBBE4570F21120EE4107851` | `D1D8655D0B929F0367D8B67473ED36B88E1D1DC5517A543643590A6DEC8E621D` |
| `assets/test_packaging_artifacts/pkg_roundtrip_03.json` | `0F0FE0DC7F2BEB69BF229092628D8C9159CAB7CD3730C7F97C26B9B839BE36C5` | `9C91BBA3D20DA85A2EA9D1C46291371F73F5E2B43C012DA2A0873BAE3EC7E089` |

Verifica SHA-256 dei 40 originali immediatamente prima/dopo la suite headless: **0 nuove differenze, 0 file mancanti**.

### Vulkan rilevato

`vulkaninfo --summary` è terminato con exit code 0 (2.048 ms). GPU: **NVIDIA GeForce GTX 1660 Ti**, driver `581.42.0.0`, API Vulkan `1.4.312`. Sono enumerati `VK_LAYER_KHRONOS_validation` 1.4.350 e `VK_LAYER_Twitch_Overlay` 1.1.0.

Il Debug di FAIRWORLD abilita nel codice `VK_LAYER_KHRONOS_validation`; il Debug Messenger registra severità warning/error. Questi fatti sono verificati staticamente. Non sono prova che un’istanza FAIRWORLD li abbia attivati o abbia ricevuto callback durante questa fase.

La synchronization validation **non risulta esplicitamente abilitata dal codice applicativo** tramite `VkValidationFeaturesEXT`. Il layer installato la supporta; l’attivazione va verificata in una sessione runtime autorizzata. `vulkaninfo` ha inoltre segnalato manifest di overlay di terze parti non apribili e la versione API più vecchia del layer Twitch; nessuna GUI o applicazione FAIRWORLD è stata avviata per riprodurre tali avvisi.

## Build e test headless

Eseguiti con `build_r3z7` Debug. Le suite sono state lanciate dalla working directory sandbox `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\runtime`; il runner Tiny ha caricato la copia dedicata D1.2.19d.

| Gate | Risultato | Exit code | Durata |
|---|---:|---:|---:|
| `ALL_BUILD` Debug | PASS | 0 | 22.031 ms |
| Tiny CPU E2E + Spawn + Camera + Movement | PASS | 0 | 5.656 ms |
| JobSystem | PASS 10/10 | 0 | 1.766 ms |
| DMA | PASS 14/14 | 0 | 179 ms |
| VRAM allocator | PASS 7/7 | 0 | 157 ms |
| Transfer/Graphics Sync | PASS 6/6 | 0 | 136 ms |
| Forge Vertex | PASS | 0 | 155 ms |
| `FairworldEditor.exe --validate-d12` | PASS, 4/4 gruppi | 0 | 833 ms |
| Hash originali pre/post | PASS, 40 verificati; nessuna nuova divergenza | 0 | — |

Il runner Tiny integra test spawn, camera e movimento, non tre eseguibili separati. Ha confermato 54 chunk, 504.877 voxel non-Air, 54 mesh CPU e 135.820 triangoli; gravità radiale, salto/atterraggio, input diagonale normalizzato, attraversamento di cella/faccia, collision frame stabile e camera post-fisica risultano PASS. Clearance misurata a riposo: `0,000137` unità; dopo il salto: `0,000092`.

Il runner dichiara esplicitamente **NOT VERIFIED** la pubblicazione MeshComponent del worker di produzione e l’upload DMA/GPU. Nessun risultato CPU è contato come draw o completamento GPU.

Log conservati in `D:\FAIRWORLD\D1_2_19D_SANDBOX\logs\`:

- `all_build.log`
- `TinyCpuSpawnCameraMovement.log`
- `JobSystem.log`
- `DMA.log`
- `VRAM.log`
- `TransferGraphicsSync.log`
- `ForgeVertex.log`
- `ValidateD12.log`
- `vulkaninfo_summary.log`

## Gate GUI/GPU

**NOT VERIFIED — GUI non avviata per vincolo esplicito.** Di conseguenza non ci sono exit code runtime, screenshot, input reale, messaggi Validation Layer prodotti dall’editor, draw, fence o retirement VRAM da riportare.

### Procedura per una futura sessione autorizzata

1. Creare/usare una working directory runtime D1.2.19d dedicata con gli asset necessari copiati in sola lettura e il fixture Tiny installato nella sola destinazione sandbox dei salvataggi. Non puntare il runtime a `D:\FAIRWORLD\FAIRWORLD\saves` o agli asset originali scrivibili.
2. Ricontrollare zero processi FAIRWORLD, nessun collegamento albero verso originali, hash pre-sessione dei 40 input e hash del fixture.
3. Avviare l’eseguibile Debug esistente dalla working directory sandbox con `--disable-twitch-layer`; raccogliere stdout/stderr completi. Confermare nel log `VK_LAYER_KHRONOS_validation` attivo e registrazione riuscita del Debug Messenger.
4. Se si abilita la synchronization validation, annotare la configurazione effettiva del layer (o la catena `VkValidationFeaturesEXT` se il progetto la supporta); non dedurla dalla sola presenza del layer.
5. Aprire PlayState e attendere il messaggio di spawn risolto sulla superficie voxel. Verificare visivamente piedi/collider rispetto al terreno, yaw/pitch, cursor capture, WASD, diagonale, salto e atterraggio; ripetere camminando attraverso una cella e un bordo Cube-Sphere e osservando il passaggio vicino ai poli.
6. Correlare per entity/chunk/request/handle le fasi job accettato → mesh CPU → DMA completato → MeshComponent pubblicata → draw → fence completata → retirement VRAM. Segnare ogni fase mancante come NOT VERIFIED.
7. Chiudere normalmente; registrare exit code, validation/sync messages, contatori ownership e confronto pre/post degli hash originali.

Questa procedura non è stata eseguita: richiede una nuova autorizzazione GUI.

## Classificazione

- **Risultati CPU:** PASS.
- **Input GUI e camera percepita:** NOT VERIFIED.
- **Fisica nel runtime GUI:** NOT VERIFIED; la fisica CPU sul fixture è PASS.
- **Vulkan Validation durante FAIRWORLD:** NOT VERIFIED.
- **Mesh GPU, draw, fence e retirement:** NOT VERIFIED.
- **Esito complessivo:** **PARTIAL PASS**, non PASS integrato.
