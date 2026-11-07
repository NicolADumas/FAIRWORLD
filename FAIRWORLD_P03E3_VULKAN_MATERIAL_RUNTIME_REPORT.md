# FAIRWORLD P03e.3 — Vulkan Material Runtime & GPU Layer Validation

## Esito sintetico

**Esito complessivo: PARTIAL PASS / INCOMPLETE.**

**PASS:** preflight Vulkan; upload Vulkan reale e readback completo dei tre array RGBA8 del witness P03e.2 in Debug e Release; VkShaderModule creati dai due SPIR-V Forge.

**NOT VERIFIED:** upload tramite RenderManager di produzione, CacheManager live sync, descriptor set del renderer, pipeline/draw Forge, fence graphics e pixel finale del fragment shader.

**Diagnosi grigio: NOT VERIFIED nel processo editor.** È stata riprodotta una condizione compatibile con fallback grigio quando la root del packer è la CWD runtime, ma l’editor non è stato avviato e la sua CWD effettiva non è stata osservata. Nessuna correzione di produzione applicata.

**Decisione patch: NO-GO.** Le evidenze non dimostrano che il renderer di produzione campioni una layer errata o stantia.

## 1. Safety, isolamento e artefatti

- HEAD prima/dopo: f9c7a3047bf3c8ecaad70de4f1af39943933eaf1.
- Working tree già dirty/untracked. Nessun reset, clean, commit, push o rigenerazione CMake.
- Nessuna sorgente di produzione, registry, shader, fixture storico o salvataggio è stato modificato.
- Nessuna GUI/editor è stata avviata.
- Harness, progetto MSBuild, output, buffer CPU, cache, readback e log: D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p03e3-vulkan\.
- CWD e TEMP/TMP dei processi di test puntavano a sottodirectory sandbox. La root runtime/assets è stata controllata per reparse point; non ne sono stati osservati.
- Gli SPIR-V sono stati letti dal repository e non sovrascritti.

### Build dell’harness

OutDir e IntDir sono stati impostati a bin/{Debug,Release} e obj/{Debug,Release} sotto la directory P03e3 sandbox. I primi link con PDB abilitato sono falliti con LNK1101: versione di MSPDB140.DLL non corretta. Per il solo harness diagnostico autonomo è stata disabilitata la generazione di simboli/PDB; toolset, compiler e generatore non sono stati cambiati.

- Build Debug finale: exit 0.
- Build Release finale: exit 0.
- Il PDB di link non è stato generato dalla build finale. Un PDB di compilazione prodotto dal tentativo precedente resta nel sandbox; non è stato cancellato.
- Log: build-debug-final.log, build-release-final.log; i tentativi iniziali sono conservati nei build-debug*.log e build-release*.log.

Questa build del test harness non certifica una build Debug dell’applicazione.

## 2. CWD e asset root

La CWD dell’editor di produzione è NOT VERIFIED: l’editor non è stato avviato. Sono verificati invece questi fatti:

- RuntimeManager.cpp:325 costruisce TexturePacker(512), quindi usa la root predefinita ".".
- TexturePacker legge il registry da assetRoot/assets/definitions/materials.json, ma risolve un path texture relativo come assetRoot / texturePath (TexturePacker.cpp:80, 90–123, 312–314).
- Il registry ID 18 dichiara textures/blocks/fairworld_carburotitanio_albedo.png.
- Nel runtime sandbox il file è presente in runtime/assets/textures/blocks/; runtime/textures/blocks/ non esiste.

Ho creato un root sandbox con il layout di runtime/assets e l’ho passato al packer. Il packer ha cercato <root>/textures/blocks/..., non ha decodificato ID 18 e ha scritto il fallback grigio (178,178,178,255). Il test restituisce exit 0 perché verifica il ramo fallback per un path non disponibile: non significa che la texture ID 18 sia stata caricata.

Questo prova il comportamento del packer sotto quella root, non la CWD effettiva dell’editor. Nessun path di produzione è stato modificato.

### Materiali witness

| ID / layer | Registry | Stato asset | Esito CPU |
|---:|---|---|---|
| 1 | fairworld:grass | albedo vuota | fallback verde RGBA8 (76,204,51,255) |
| 12 | fairworld:thesims | albedo dichiarata ma assente | fallback blu RGBA8 (0,6,255,255) |
| 18 | fairworld:carburotitanio | file in assets/textures/... | texture decodificata nel root corretto; fallback grigio nel test condizionale con assetRoot="." |

Nel root corretto di P03e.2, ID 18 è stata decodificata da 512×512 RGBA e ridimensionata CPU a 16×16. Buffer albedo SHA-256: d6235e0c244cbca2f88e8c35b197788bca7ad157985e9b1. Pixel: (0,0)=(174,174,174,255), centro=(12,11,11,255), ultimo=(159,159,159,255).

## 3. Vulkan device e configurazione

D:\VulkanSDK\Bin\vulkaninfoSDK.exe --summary: exit code 0.

- GPU: NVIDIA GeForce GTX 1660 Ti, vendor 0x10DE, device 0x2182.
- Driver: NVIDIA 581.42.
- API fisica: Vulkan 1.4.312; loader/instance 1.4.350.
- VK_LAYER_KHRONOS_validation disponibile 1.4.350 ed esplicitamente abilitato.
- Synchronization validation abilitata tramite VK_EXT_validation_features.
- Formato: VK_FORMAT_R8G8B8A8_UNORM; feature ottimali includono sampled image, transfer source e transfer destination.
- maxImageArrayLayers=2048; 256 layer supportate.
- Queue family 0: graphics e transfer; il harness ha usato una queue family e sharing exclusive.
- SPIR-V letti senza modifica: vertex SHA-256 301C4C22EE8DF9175A47B8A92B590FB14618D38CFEB23AAAC49CFCA341EE8F7C; fragment SHA-256 DDD9960ACF3CD0025FE3408E707F182C2D328CA56EDC775938B4D154BA8CAE8E.

### Loader diagnostics

Il loader ha segnalato tre errori per manifest esterni non trovati: due JSON Overwolf su E: e il JSON Vulkan di Rockstar Social Club. Ha inoltre segnalato warning di naming per Galaxy Overlay. Il callback ha distinto questi messaggi id=Loader Message dagli errori del Khronos Validation Layer. Per ciascun run finale:

- Errori Validation Layer/VUID: 0.
- Errori loader: 3.
- Warning: 12, inclusi naming e layer impliciti disabilitati nel processo harness.
- VK_LOADER_LAYERS_DISABLE=~implicit~ è stato impostato solo nell’ambiente del processo harness per disabilitare i layer impliciti; Khronos validation è rimasto esplicitamente abilitato. Nessuna impostazione permanente cambiata.

I log completi conservano errori e warning. Non li classifico come zero errori Vulkan complessivi: sono zero errori di validazione delle operazioni harness, con errori ambientali del loader ancora presenti.

## 4. Upload e readback GPU

Il harness autonomo ha usato Vulkan API reale. Ha caricato dal payload CPU cache v2 P03e.2 tre array completi da 256 layer, 16×16 ciascuno. Per ogni array ha creato VkImage optimal tiling RGBA8_UNORM con usage sampled/transfer source/transfer destination, copiato il buffer CPU via staging e vkCmdCopyBufferToImage, transizionato il layout, sottomesso il command buffer, atteso una fence finita, copiato l’immagine in un buffer readback host-visible e confrontato tutti i byte.

| Array | SHA-256 CPU | SHA-256 readback GPU | Byte uguali |
|---|---|---|---|
| Albedo, root corretto | 22543785A72EEC0819E9A7C1C52066E68FBF6AE69294AAF026FEE045594978C9 | 22543785A72EEC0819E9A7C1C52066E68FBF6AE69294AAF026FEE045594978C9 | sì |
| Normal | 2C887542DFFEC8D71834CE0B2C011291A6FD421F2F1F09CF691E795AD856627B | 2C887542DFFEC8D71834CE0B2C011291A6FD421F2F1F09CF691E795AD856627B | sì |
| ORM | 23B58DFDB60664DC0782E9B2CD935BAD16F4FE66DC4AFAE7D16D7E71E1D0D11F | 23B58DFDB60664DC0782E9B2CD935BAD16F4FE66DC4AFAE7D16D7E71E1D0D11F | sì |

Debug e Release: exit code 0, readback esatto per tutti e tre gli array. Campioni centrali albedo: ID1 76,204,51,255; ID12 0,6,255,255; ID18 12,11,11,255. Nessun swizzle/conversione necessario per RGBA8_UNORM.

Nel test condizionale con layout runtime, il readback GPU ha restituito il fallback grigio ID18: SHA-256 AE80583D6A9FEF7D54B6B70D21897DF07AFE7ACCB13B4E8500D99C2D3A14EAFD; pixel centro 178,178,178,255.

**Limite:** questo dimostra che i byte passati al harness sono stati trasferiti e restituiti dalla GPU. Non chiama RenderManager::CreatePBRTextures, UpdateTextureLayer né l’upload DMA del renderer di produzione; non certifica quindi il percorso upload del renderer.

## 5. SPIR-V, descriptor, draw e pixel

- vkCreateShaderModule è riuscito per forge_vert.spv (1.080 parole) e forge_frag.spv (4.167 parole).
- Il harness non ha creato pipeline grafica, image view, sampler, descriptor set, vertex buffer o framebuffer.
- Nessuna draw, fence graphics o readback di frame.
- Mapping shader/descriptor verificato solo staticamente in P03e.2.
- Pixel PBR finale: NOT VERIFIED.
- Contratto GPU usato dalla draw FAIRWORLD: NOT VERIFIED.

## 6. CacheManager live sync

Non sono state invocate CacheManager::SyncMaterialGpuCache o SyncAllMaterialsGpuCache: richiedono il RenderManager Vulkan di produzione e non sono state simulate.

Restano confermati staticamente i rischi P03e.2: i callsite ignorano LoadPBRTextureFromFile per path non vuoto e invocano UpdateMaterialFallback soltanto quando il path è vuoto. Il test sentinella GPU non è stato eseguito.

- Decode failure live: NOT VERIFIED runtime.
- Layer stantia dopo sync: NOT VERIFIED runtime.
- Fallback live omesso: NOT VERIFIED runtime.
- Contratto statico del callsite: rischio confermato da codice, non prova di layer GPU osservata.

## 7. Diagnosi dei blocchi grigi

**Root cause finale dell’editor: NOT VERIFIED.**

Catena condizionale riprodotta:

CWD=runtime + TexturePacker assetRoot="." + albedoPath="textures/..." + file presente solo in assets/textures/... → decode fallito → fallback ID18 (178,178,178,255) → buffer CPU grigio → stessi byte nella VkImage harness.

Questo spiega come il fallback possa arrivare a una VkImage se viene usata tale root. Non prova la CWD dell’editor, né che la draw di produzione abbia usato quella VkImage/layer. Illuminazione/PBR, descriptor errato e layer stantia non sono stati discriminati.

## 8. Regression gate

Ogni binario è stato eseguito in CWD e TEMP/TMP nuovi sotto il sandbox.

| Test | Config. | Exit | Esito |
|---|---|---:|---|
| Witness registry/voxel/mesh P03e.1c | Debug | 0 | PASS |
| Witness registry/voxel/mesh P03e.1c | Release | 0 | PASS |
| TexturePacker CPU cold/warm P03e.2 | Debug | 0 | PASS; ID18 decoded nel root di test corretto |
| TexturePacker CPU cold/warm P03e.2 | Release | 0 | PASS; buffer e hash coincidenti |
| Cache contract P03e.2 | Debug | 0 | PASS 10/10 |
| Cache contract P03e.2 | Release | 0 | PASS 10/10 |
| Tiny runner storico | Debug | 0 | PASS; 54 chunk, 504.877 voxel, 54 mesh CPU, 99.252 triangoli |
| Tiny runner storico | Release | 0 | PASS; stessi conteggi e digest |
| Upload/readback P03e.3 | Debug | 0 | PASS; 3/3 array, byte esatti, zero errori layer validation |
| Upload/readback P03e.3 | Release | 0 | PASS; 3/3 array, byte esatti, zero errori layer validation |
| Actual-runtime-layout witness GPU | Debug | 0 | PASS del trasferimento; ID18 contiene il fallback grigio |

I runner Tiny continuano a dichiarare la GPU publication del renderer NOT VERIFIED.

## 9. Evidenza persistente

- Device: p03e3-vulkan/preflight/vulkaninfo-summary.log.
- Build/progetto: build-*.log, project/P03e3GpuLayerHarness.vcxproj, src/P03e3GpuLayerHarness.cpp.
- Debug: run-debug/run-final.log e tre file *-gpu-readback.rgba8.
- Release: run-release/run-final.log e tre readback.
- Layout condizionale: run-actual-layout-packer/run.log, actual-runtime-layout/assets/texture_cache.bin, run-actual-layout-gpu/run.log e readback.
- Regressioni: directory cpu-regression (packer, cache, witness e Tiny Debug/Release).
- Buffer CPU: cpu-inputs/ e cpu-inputs-actual-layout/.

Cache e fixture generati sono in directory sandbox nuove; cache originale e asset sorgente non erano destinazioni di scrittura.

## 10. Integrità finale

| File controllato | SHA-256 |
|---|---|
| Originale e sandbox assets/definitions/shapes.json | 14F903D98029369D35BE9FA16F87069BDE21027023726006B85800135EA44328 |
| Originale e sandbox assets/definitions/blocks.json | 66922BF44F11FC19BD0E59B1A919364056507CC731EFBAF122152BB07BF34744 |
| Originale assets/definitions/materials.json | 33C1E113852B22C0ECAF51020B8E4B6B195CDE2918925EF1C98D9FEF58F406E6 |
| Originale texture ID18 | 5403D6BF79D4A491C12FA42181BA15BEA7DB285653D172A11362D394228034DD |
| Sandbox materials.json | 33C1E113852B22C0ECAF51020B8E4B6B195CDE2918925EF1C98D9FEF58F406E6 |
| Sandbox texture ID18 | 5403D6BF79D4A491C12FA42181BA15BEA7DB285653D172A11362D394228034DD |
| Fixture P03e | AD62FE45DAB43BF3C465F13D7832AB937E271549159A172BBF89D278F5D3C7B5 |
| Fixture Tiny storico | 47AF4358E4BD663926E64A43ACF2B116F3A2939BA5605FC89B2983D32A9AD503 |
| SPIR-V vertex | 301C4C22EE8DF9175A47B8A92B590FB14618D38CFEB23AAAC49CFCA341EE8F7C |
| SPIR-V fragment | DDD9960ACF3CD0025FE3408E707F182C2D328CA56EDC775938B4D154BA8CAE8E |

Non è stato eseguito un hash esaustivo di tutti i salvataggi originali: nessun processo editor è partito e tutte le destinazioni del harness erano nel sandbox. Manifest e baseline non sono stati modificati.

## 11. Prossimo gate

1. In una sessione isolata successivamente autorizzata, registrare CWD e path completi realmente aperti dal runtime per ID12/18; confrontarli con assets/textures senza modificare i path di produzione.
2. Nel renderer integrato, inizializzare una layer sentinella e osservare il contenuto post-live-sync e post-fence; non inferire l’esito dal solo booleano o dal log.
3. Solo se una layer stale o fallback omesso è riprodotto, proporre una patch localizzata.
4. Per il pixel end-to-end, usare le stesse immagini del renderer in descriptor/draw, attendere fence e fare readback del target; separare albedo raw da lighting PBR.

**GO limitato:** trasferimento/readback RGBA8 GPU reale verificato nel harness isolato.

**NO-GO per patch renderer/cache e chiusura P03e.3:** asset root effettiva dell’editor, live sync, descriptor/draw e pixel finale non osservati. Blocchi grigi ancora NOT VERIFIED.
