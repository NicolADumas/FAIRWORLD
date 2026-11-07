# FAIRWORLD P03e.2 — CPU Material Packing & Cache Contract Validation

## Esito

**CPU packing/cache: PASS, Debug e Release.**

**Regressioni richieste: PASS, Debug e Release.**

**CacheManager/GPU live sync: rischio statico confermato; comportamento Vulkan NOT VERIFIED.**

**P03e.3: GO condizionato** a un test Vulkan sandbox che prima registri e convalidi la working directory e la risoluzione degli asset. La causa dei blocchi grigi non è dimostrata da questa fase.

## 1. Safety e ambiente

- HEAD prima e dopo: `f9c7a3047bf3c8ecaad70de4f1af39943933eaf1`.
- Stato Git letto prima del report: 167 voci dirty/untracked; nessuna di esse è stata ripristinata o ripulita. I sorgenti di produzione non sono stati modificati in P03e.2.
- Toolchain esistente: MSVC v145 recuperato nelle fasi precedenti; i progetti diagnostici sandbox Debug e Release erano già compilati con questa toolchain. Nessuna rigenerazione CMake.
- Esecuzioni CPU/headless soltanto: nessuna GUI, inizializzazione Vulkan, commit o push.
- I progetti, gli eseguibili, i PDB, gli oggetti, i log, le directory temporanee e le cache di packing sono in `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\`. Gli output del progetto di packing sono distinti per configurazione e root.
- `git diff --check` è stato eseguito: segnala whitespace preesistente in `src/states/BlockMakerState.cpp` (righe 4652–4669 e 11563–11830 nel diff corrente). Il report stesso non contiene whitespace finale. Il file non è stato modificato in questa fase e il problema non è stato corretto.

### Integrità degli input

Gli hash correnti coincidono con quelli registrati in P03e.1c per i tre registry sandbox e per il fixture P03e. La texture sorgente e il fixture Tiny storico sono rimasti leggibili e invariati nei controlli eseguiti.

| Input | SHA-256 |
|---|---|
| `assets/definitions/shapes.json` (originale e copia sandbox) | `14F903D98029369D35BE9FA16F87069BDE21027023726006B85800135EA44328` |
| `assets/definitions/blocks.json` (originale e copia sandbox) | `66922BF44F11FC19BD0E59B1A919364056507CC731EFBAF122152BB07BF34744` |
| `assets/definitions/materials.json` (originale e copia sandbox) | `33C1E113852B22C0ECAF51020B8E4B6B195CDE2918925EF1C98D9FEF58F406E6` |
| Texture sorgente ID 18 `fairworld_carburotitanio_albedo.png` | `5403D6BF79D4A491C12FA42181BA15BEA7DB285653D172A11362D394228034DD` |
| Fixture P03e `p03e-material-witness/world_map.json` | `AD62FE45DAB43BF3C465F13D7832AB937E271549159A172BBF89D278F5D3C7B5` |
| Fixture Tiny storico `p03d4-fixture/world_map.json` | `47AF4358E4BD663926E64A43ACF2B116F3A2939BA5605FC89B2983D32A9AD503` |

## 2. Pipeline CPU e mapping

Il percorso esaminato è:

`MaterialRegistry::PBRMaterialDef → TexturePacker::PackMaterials → ResolveAssetPath → stbi_load → resize/fallback RGBA → array CPU → cache PBR v2`.

Il costruttore runtime in `src/core/app/RuntimeManager.cpp:325` crea `TexturePacker(512)` con asset root predefinita `.`. Il packer carica la definizione da `assetRoot/assets/definitions/materials.json`; i percorsi relativi delle texture sono risolti invece come `assetRoot / texturePath` (`src/render/TexturePacker.cpp:80, 90–123, 312–314`). L’ordine dei vettori di `MaterialRegistry` è usato come indice di layer: il log del witness mostra indici 1, 12 e 18 coerenti con `target_block_id` e BlockID; l’array viene mantenuto a 256 layer.

La cache v2 registra formato, algoritmo, risoluzione, canali, numero layer, fingerprint degli input e digest del payload. Il test CPU legge/rivalida la cache fredda e calda senza Vulkan. Il packing usa RGBA8, `stbi_load(...,4)` e `stbir_resize_uint8`; i fallback RGB sono quantizzati con conversione float ×255 a `uint8_t`, alpha 255. Il packer non applica una conversione esplicita di spazio colore prima del resize.

## 3. CPU Packing Witness

Harness sandbox: `p03e2-packer-harness`; eseguibili `bin/Debug/P03e2CpuPacking.exe` e `bin/Release/P03e2CpuPacking.exe`. Entrambe le configurazioni hanno usato resolution 16 per il test diagnostico, registry reali copiati nel sandbox e root indipendenti `pack-debug-final-root` / `pack-release-final-root`. Ogni root ha prodotto e riutilizzato una propria cache.

| Materiale | BlockID / target / layer | Percorso risolto nel witness | Origine / decode | Campioni albedo RGBA8 | SHA-256 buffer albedo |
|---|---:|---|---|---|---|
| `fairworld:grass` | 1 / 1 / 1 | nessuno | Fallback verde | `(0,0)=centro=ultimo=(76,204,51,255)` | `94b866bcf58c4ecb29f3fbf53e5f153b32c2524feda0660d6cd1979ceb3dd2f2` |
| `fairworld:thesims` | 12 / 12 / 12 | `<witness-root>/textures/blocks/fairworld_thesims_albedo.png` | File assente; fallback blu | `(0,0)=centro=ultimo=(0,6,255,255)` | `bbf57461b0dce1b2c2cfcaa73ea7934a65cf3a293a4e91b77d0b4923f0c6d535` |
| `fairworld:carburotitanio` | 18 / 18 / 18 | `<witness-root>/textures/blocks/fairworld_carburotitanio_albedo.png` | Decodificata, 512×512, 4 canali; ridimensionata a 16×16 | `(0,0)=(174,174,174,255)`, centro `(12,11,11,255)`, ultimo `(159,159,159,255)` | `d6235e0c244cbca2f88e8c35b197788bca7ad157985e9b1` |

Il witness confronta l’output ridimensionato ID 18 con un decode/resize indipendente e ottiene corrispondenza esatta. Per ID 1 e 12 il buffer corrisponde byte per byte al fallback atteso. I relativi buffer normal e ORM dei tre layer hanno, in entrambi i build:

- Normal fallback `(128,128,255,255)`, SHA-256 `b0be2f825d253ab3be9d7cf34f5e8a04acd702b0f748572563340af7e7b733e5`.
- ORM fallback `(255,128,0,255)`, SHA-256 `4198aab074678c74f886b7425f24a105d59ecc6d2a194c2d4acccd4afcabedab`.

Debug e Release concordano su mapping ID/layer, decode, dimensioni, campioni, hash dei buffer e confronto cold/warm. Il payload cache è di 786.432 byte e ha digest `162D03B714383111C3572E6850CC26246E77EF12380334A971AF2839323C1E15`; i byte payload Debug/Release coincidono. I file cache completi hanno hash diversi (`FBB8F390BCE8B6112CB2559C776A98EA754A581FB9F3791B5D021D1D265113C7` Debug, `61B39B06DCC56DBEEF901113AB12C9E2D72F8F5E0B92955C3C1EB466AADED880` Release) perché l’input fingerprint include il percorso della definizione materiali sotto le due root separate; il digest del payload nel loro header è uguale e verificato ricalcolando SHA-256.

### Limite di rappresentatività del percorso

Per far risolvere la texture nel witness, il file ID 18 è stato copiato nella root diagnostica come `<witness-root>/textures/...`. Nel runtime sandbox verificato, invece, il file presente è `runtime/assets/textures/...`; `runtime/textures/...` non esiste. Poiché RuntimeManager usa il packer con asset root `.`, e i `albedoPath` sono `textures/blocks/...`, sotto quella working directory la risoluzione relativa punta a `runtime/textures/...`. La definizione materiali viene invece cercata correttamente sotto `runtime/assets/definitions/...`.

Questo è un **mismatch di layout/percorso riproducibile e un blocco da chiarire per la prova runtime**, non una dimostrazione di ciò che il processo editor effettivamente carica con qualunque working directory o packaging. Il test non ha avviato l’editor e non ha risolto la root runtime effettiva. Le righe di log ID 18 del witness dimostrano il decode solo nella root preparata appositamente; non dimostrano il decode con il layout reale `assets/textures`.

## 4. Cache contract tests

Binario di test CPU `FairworldTextureCacheTests.exe`, compilato da progetto sandbox in Debug e Release. Working directory e `TEMP/TMP` separati sotto `p03e2-cache-tests/run-debug` e `run-release`. Entrambi gli eseguibili hanno exit code 0; log completi in `run.log`.

Entrambe le configurazioni: **10/10 PASS**.

1. Cache invariata riutilizzata.
2. Modifica del contenuto di `materials.json` invalida.
3. Modifica byte albedo con timestamp invariato invalida.
4. Modifica byte normal con timestamp invariato invalida.
5. Modifica byte ORM con timestamp invariato invalida.
6. Texture assente → presente → rimossa invalida.
7. Cache troncata e payload corrotto rigenerati.
8. Versione incompatibile rigenerata.
9. Mapping ID→layer e pixel fallback ID 12.
10. Modifica risoluzione e numero layer invalida.

I fixture e le cache create da questi test sono temporanei e confinati alle directory di run nel sandbox.

## 5. CacheManager e confini di errore

**Difetto statico confermato:** in `src/core/memory/CacheManager.cpp:29–45, 50–73`, sia `SyncMaterialGpuCache` sia `SyncAllMaterialsGpuCache` ignorano il valore booleano restituito da `LoadPBRTextureFromFile` per i percorsi non vuoti. Il fallback (`UpdateMaterialFallback`) viene chiamato soltanto quando `albedoPath` è vuoto. Quindi una texture non vuota che non si carica non fa applicare il fallback in questo ramo e può lasciare la layer precedente invariata.

**Limite di propagazione statico:** `RenderManager::LoadPBRTextureFromFile` (`src/render/RenderManager.cpp:3328–3337`) restituisce true se `stbi_load` riesce e chiama `UpdateTextureLayer`, che è `void` (`:3219`); `UpdateTextureLayer` può interrompersi per layer/dimensioni/immagine non validi. Il booleano quindi certifica il decode, non necessariamente l’aggiornamento Vulkan completato. Non è stata verificata né simulata una layer GPU sentinella: il risultato concreto in memoria Vulkan richiede Vulkan reale.

**Differenza packing/cache vs live sync:** il TexturePacker produce in CPU un fallback albedo quando un path è configurato ma non decodificabile (`TexturePacker.cpp:385+`). CacheManager live-sync segue invece un altro percorso; su decode failure ignora il risultato e non invoca il fallback. Una cache fredda da packing e una sincronizzazione live successiva non hanno quindi lo stesso contratto di errore.

**ORM:** il fallback CPU TexturePacker è staticamente `(AO=255, roughness=128, metallic=0)` (`TexturePacker.cpp:295`), roughness circa `0,502`. I fallback registry osservati sono 0,9 per ID 1, 1,0 per ID 12 e 0,8 per ID 18; metallic 0. Lo shader usa `orm.g` e `orm.b` per roughness/metallic (`forge.frag:137–141`), mentre l’SSBO block-properties riceve i valori registry. Questa divergenza è un contratto PBR non uniforme; è un rischio visivo/speculare da valutare, non prova della causa del grigio. Nessuna correzione è stata applicata.

## 6. Shader e layout CPU/GPU: verifica statica

- `Vertex.materialID` è un `uint32_t`; `RenderManager.cpp:586` configura location 3 come `VK_FORMAT_R32_UINT`. Location 2 usa UV (`Vertex::roughMetal`) come `VK_FORMAT_R32G32_SFLOAT` (`:579–588`). `forge.vert:15–16, 62` riceve e passa materiale come `flat uint` e UV.
- Il fragment shader clampa il layer al numero effettivo di layer della texture array (`forge.frag:122–125`), poi campiona albedo, normal e ORM allo stesso `uvLayer` (`:137`). Il descriptor layout Forge in `RenderManager.cpp:406–430` ha binding 0 albedo, 1 normal, 2 ORM, 3 SSBO. L’ID per il buffer proprietà viene clampato a 0..255 (`forge.frag:178–179`). Nessuna incoerenza statica di binding/format è emersa nel percorso esaminato.
- La base albedo proviene da `albedoArray`; `Vertex.color` entra nei rami emissive/color override/alpha, non sostituisce normalmente la texture base (`forge.frag:178–185, 214–215`). Perciò il witness dei colori dei vertici P03e.1c non era prova sufficiente del colore pixel, e questo report usa buffer CPU del packer per il lato texture.
- L’ID 255 rientra nel limite di 256 layer e nell’indice massimo del SSBO, ma non è stato usato come sostituto per i witness 1/12/18.

## 7. Comandi e risultati

I comandi sono stati invocati dai runner esclusivamente con i binari già compilati e con working directory/TEMP/TMP per-run nel sandbox. Argomenti esatti:

```powershell
P03e1cMaterialWitness.exe --shapes <runtime>\assets\definitions\shapes.json --blocks <runtime>\assets\definitions\blocks.json --materials <runtime>\assets\definitions\materials.json --fixture <runtime>\p03e-material-witness\world_map.json
P03e2CpuPacking.exe --blocks <runtime>\assets\definitions\blocks.json --asset-root <p03e2-packer-harness>\pack-{debug,release}-final-root --resolution 16
FairworldTextureCacheTests.exe
FairworldPlanetaryVoxelE2ETests.exe --fixture <runtime>\p03d4-fixture\world_map.json
```

Per entrambi i runner Tiny è stato impostato `FAIRWORLD_P03D1_V7_FIXTURE` al fixture sandbox esistente `p03d1_fwb_test_10541779950400\legacy_v7.fwb`; gli eseguibili hanno richiesto l’opzione `--fixture`.

| Gate | Config. | Exit code | Risultato |
|---|---|---:|---|
| Packing CPU cold/warm | Debug | 0 | PASS; layer 1/12/18 conformi ai valori sopra |
| Packing CPU cold/warm | Release | 0 | PASS; buffer/payload identici a Debug |
| Cache contract (10 casi) | Debug | 0 | 10 PASS, 0 FAIL |
| Cache contract (10 casi) | Release | 0 | 10 PASS, 0 FAIL |
| Witness registry/voxel/mesh P03e.1c | Debug | 0 | PASS; 54 chunk, 504.877 voxel non-Air, witness per ID 1/12/18 |
| Witness registry/voxel/mesh P03e.1c | Release | 0 | PASS; output di controllo coincidente con Debug |
| Tiny runner storico | Debug | 0 | PASS; 54/54 chunk, 504.877 voxel ID 3, 54 mesh CPU, 99.252 triangoli |
| Tiny runner storico | Release | 0 | PASS; stessi risultati/digest attesi del fixture storico |

Il primo tentativo del runner Tiny è stato respinto con exit code 1 perché mancava l’argomento obbligatorio `--fixture`; il messaggio d’uso è stato letto e il comando corretto è stato rieseguito in directory separate. Non è un FAIL del test. Un controllo `Get-CimInstance` intermedio non era disponibile nell’ambiente; non ha coinvolto i runner e non ha impedito di registrarne i completamenti/exit code.

I log sono conservati nel sandbox:

- Packing: `p03e2-packer-harness/run-debug-final/run.log` e `run-release-final/run.log`.
- Cache: `p03e2-cache-tests/run-debug/run.log` e `run-release/run.log`.
- Witness: `p03e2-packer-harness/regression-witness-debug/run.log` e `regression-witness-release/run.log`.
- Tiny: `p03e2-packer-harness/regression-tiny-debug/run-corrected.log` e `regression-tiny-release/run-corrected.log`.

Il runner Tiny dichiara esplicitamente la pubblicazione GPU, MeshComponent di produzione e upload VRAM/DMA come **NOT VERIFIED**: il suo PASS riguarda il contratto CPU.

## 8. Cause e ipotesi

### Confermato

1. Il TexturePacker CPU applica deterministicamente il fallback albedo per texture mancanti o non decodificabili e decodifica/resize correttamente ID 18 quando il percorso risolto esiste.
2. Nel layout del sandbox, il path `textures/...` non corrisponde al percorso fisico `assets/textures/...` quando la root di packing è la directory runtime stessa. Questo rende plausibile il fallback anziché la texture sotto tale root.
3. CacheManager non reagisce al fallimento del load per path non vuoti; il fallback non viene applicato in quel ramo. Il metodo di load può inoltre riferire un decode riuscito senza confermare l’update layer.
4. Cache v2 e invalidazione sono deterministiche nei 10 casi CPU testati.

### Non dimostrato

- Quale CWD/root usa la sessione editor che mostrava i blocchi grigi.
- Quali pixel/layer sono realmente residenti e campionati dalla GPU in quella sessione.
- Se il colore grigio deriva dalla texture ID 18, da layer stale, da descriptor, da illuminazione/PBR o da un’altra fase.
- Qualunque errore di sincronizzazione GPU/cache live.

## 9. Piano minimo per P03e.3

In un successivo gate Vulkan autorizzato e isolato:

1. Registrare CWD effettiva, asset root, path completo dei registry e path completo risolto per ID 12/18 prima del packing; fallire il preflight se l’asset previsto non è leggibile.
2. Acquisire o leggere in modo verificabile la layer dopo upload (o introdurre un test controllato con layer sentinella nel solo harness GPU) e confrontarla con il buffer CPU; non inferire l’esito dal log “texture caricata”.
3. Per la sincronizzazione live, inizializzare una layer sandbox con pattern sentinella, provocare un path mancante, registrare return code e verificare la layer risultante prima/dopo; misurare anche esito `UpdateTextureLayer`/upload, non soltanto `stbi_load`.
4. Tracciare `Vertex.materialID → safeLayer → descriptor binding 0 → pixel` su una draw osservata, mantenendo Vulkan Validation Layer attivo e senza toccare shader in questa fase.
5. Separare la prova del fallback colorato dal test della texture ID 18. Conservare screenshot/log e un criterio pixel atteso che tenga conto di lighting/tone mapping.

## 10. Decisione

**P03e.2 CPU: PASS.** I test dimostrano mapping, fallback, decode/resize condizionale, cache cold/warm e invalidazione Debug/Release; le regressioni richieste sono PASS.

**P03e.3: GO condizionato al preflight asset-root.** Il mismatch tra `textures/...` dichiarato e `assets/textures/...` presente deve essere chiarito nel runtime sandbox prima di interpretare l’immagine GPU. Non è autorizzata una patch di produzione da questa fase. Non si dichiara una root cause GPU né un freeze.
