# FAIRWORLD P03e.1b — Real Registry Bootstrap & CPU Material Witness

**Stato:** BLOCKED per il witness C++ con registry reali; regressione storica Tiny eseguita e PASS in Debug/Release. Nessuna prova GPU.

## 1. Esito e gate

| Gate | Esito |
|---|---|
| Audit del bootstrap di produzione | PASS statico |
| Copie sandbox dei registry, hash e contenuti | PASS per ispezione JSON; caricamento tramite classi C++ NOT RUN |
| Build di harness CPU Debug/Release | BLOCKED: MSVC v145/MSBuild non disponibile nell'ambiente |
| BlockName → ID → TerrainSolver → voxel → mesh CPU col registry JSON | NOT VERIFIED |
| Full runner storico sul fixture Tiny originale | PASS Debug e Release, exit code 0 |
| P03e.2 Vulkan | NO-GO finché non passa il witness CPU reale |

Il blocco non è stato aggirato usando PowerShell o il full runner come sostituti del caricamento C++: sarebbero prove diverse da quella richiesta.

## 2. Safety e baseline

- HEAD invariato: `f9c7a3047bf3c8ecaad70de4f1af39943933eaf1`.
- Il working tree era già dirty/untracked; nessun reset, clean, commit, push o CMake configure è stato eseguito.
- Nessun sorgente, registry o asset originale è stato modificato.
- Le copie sandbox `assets/definitions/blocks.json` e `materials.json` sono state lette e gli hash confrontati con le copie source: corrispondono.
- `blocks.json` sandbox: 21.438 byte, SHA-256 `66922BF44F11FC19BD0E59B1A919364056507CC731EFBAF122152BB07BF34744`.
- `materials.json` sandbox: 13.353 byte, SHA-256 `33C1E113852B22C0ECAF51020B8E4B6B195CDE2918925EF1C98D9FEF58F406E6`.
- Fixture witness P03e invariato: 5.974 byte, SHA-256 `AD62FE45DAB43BF3C465F13D7832AB937E271549159A172BBF89D278F5D3C7B5`.
- Nessuna build è partita; non sono stati creati object, PDB, DLL o eseguibili. I due runner storici hanno scritto log e directory temporanee solo nelle rispettive directory nuove sotto `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime`; `TEMP/TMP` sono stati reindirizzati lì.
- Il fixture originale P03d4 ha mantenuto SHA-256 `47AF4358E4BD663926E64A43ACF2B116F3A2939BA5605FC89B2983D32A9AD503` prima e dopo i runner.

## 3. Bootstrap reale individuato

Il bootstrap in `src/core/app/main.cpp` procede nell'ordine:

1. `shapeRegistry.Initialize()` e `LoadFromJson("assets/definitions/shapes.json")`.
2. `blockRegistry.Initialize()` e `LoadFromJson("assets/definitions/blocks.json")`.
3. `materialRegistry.Initialize()` e `LoadFromJson("assets/definitions/materials.json")`.
4. `MaterialAppearanceRegistry::BootstrapLegacyBlockAppearances(&materialRegistry)`.
5. Caricamento di `ScientificMaterialRegistry`.

Queste chiamate avvengono nel bootstrap dell'editor reale dopo l'inizializzazione del motore. I `bool` di `BlockRegistry::LoadFromJson` e `MaterialRegistry::LoadFromJson` non vengono controllati in questo callsite.

Entrambi i loader restituiscono `false` su file mancante o errore di parsing e conservano almeno i valori inizializzati/default. `BlockRegistry::GetBlock(name)` restituisce `m_fallbackBlock` per un nome sconosciuto; nei dati del fallback esaminati è l'ID 255 (`fairworld:error`). Questo spiega come un bootstrap incompleto possa proseguire con ID di errore, ma non prova che sia successo nell'editor o che i registry JSON sandbox non siano leggibili.

Il runner storico `PlanetaryVoxelE2ETests` non replica il bootstrap completo: il suo `main` chiama soltanto `BlockRegistry::Initialize()` e `MaterialRegistry::Initialize()` e non chiama i due `LoadFromJson`. Pertanto è deliberatamente adatto alla fixture Tiny standard, ma non al witness P03e con blocchi aggiunti al JSON.

`TexturePacker` risolve un path texture assoluto invariato e uno relativo come `assetRoot / path`; l'asset root predefinito è `.`. Il cache file è sotto `assetRoot/assets/texture_cache.bin`. Il runtime deve quindi essere avviato con working directory coerente; il harness CPU non richiede TexturePacker né scrive cache.

## 4. Registry sandbox: dati osservati

Il parsing JSON PowerShell ha verificato i valori seguenti. È una verifica dei dati nel file, **non** una chiamata ai loader C++.

| BlockName | BlockID nel blocks.json | `target_block_id` | `appearanceId` blocco | `appearanceId` materiale | `baseColorFallback` | Albedo | Asset sandbox |
|---|---:|---:|---:|---|---|---|---|
| `fairworld:Grass` | 1 | 1 | 0 | chiave assente | `(0.30000001,0.80000001,0.20000000)` | vuoto | fallback senza file |
| `fairworld:thesims` | 12 | 12 | 0 | chiave assente | `(0.0,0.02476811,1.0)` | `textures/blocks/fairworld_thesims_albedo.png` | assente |
| `fairworld:carburotitanio` | 18 | 18 | 0 | chiave assente | `(0.7,0.7,0.7)` | `textures/blocks/fairworld_carburotitanio_albedo.png` | presente e leggibile |

Il campo `PBRMaterialDef::appearanceId` esiste nella struct ma non è serializzato/deserializzato da `MaterialRegistry.cpp`; nel file è assente e resta al default 0 nel caricamento. Questo comportamento è coerente con la correzione documentale del rapporto P03e.1.

## 5. Harness: preflight e blocco toolchain

Prima di compilare sono stati verificati gli strumenti e gli output esistenti:

- `Get-Command` / `where.exe`: `cl.exe`, `MSBuild.exe`, CMake, Ninja e clang non sono reperibili nel PATH; è disponibile `dotnet.exe` soltanto.
- `vswhere` restituisce `D:\Visual Code\vs`, ma quella directory è vuota. Il percorso standard `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe` non esiste.
- Il progetto generato esistente dichiara `PlatformToolset=v145`; il solo MSBuild .NET non fornisce i task/target C++ di Visual Studio né il compilatore v145. Non è stata tentata una toolchain diversa.
- Sono presenti librerie statiche precompilate P03d3 in `runtime\p03d3-core-bin\Debug` e `Release`. Il vecchio progetto runner punta a OutDir/IntDir/PDB del runner storico e, usato senza modifiche, sovrascriverebbe artefatti esistenti. Non è stato riutilizzato per la compilazione.
- Un nuovo progetto diagnostico avrebbe dovuto usare directory nuove, per esempio `runtime\p03e1b-harness-bin\Debug|Release`, `runtime\p03e1b-harness-obj\Debug|Release` e PDB/import library nelle rispettive OutDir. Poiché la toolchain C++ non è disponibile, non sono stati creati progetto o eseguibile incompleti e nessun output è stato scritto.

Non è quindi possibile eseguire `BlockRegistry::LoadFromJson`, `MaterialRegistry::LoadFromJson`, generazione MapWorldGenerator/TerrainSolver o `VoxelChunkMesher::Build` in un nuovo harness Debug/Release senza ripristinare prima la toolchain MSVC esistente. Non è stata modificata alcuna API di produzione.

## 6. Regressione storica Tiny

È stato eseguito il full runner precompilato, con la fixture Tiny originale P03d4 e il v7 fixture sandbox atteso. Le directory di lavoro erano nuove e isolate.

| Configurazione | Eseguibile | Log | Exit | Risultato |
|---|---|---|---:|---|
| Debug | `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p03d2-debug-runner-bin\FairworldPlanetaryVoxelE2ETests.exe` | [log Debug](</D:/FAIRWORLD/D1_2_19D2_SANDBOX/runtime/p03e1b-historical-debug/run.log>) | 0 | full runner PASS; 54 chunk, 504.877 voxel non-Air, tutti ID 3 nel fixture standard |
| Release | `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p03d2-release-runner-bin\FairworldPlanetaryVoxelE2ETests.exe` | [log Release](</D:/FAIRWORLD/D1_2_19D2_SANDBOX/runtime/p03e1b-historical-release/run.log>) | 0 | full runner PASS; stessi conteggi e ID |

Entrambi riportano: `Planetary voxel CPU end-to-end contract verified; GPU publication NOT VERIFIED`. Hash log Debug: `8745F9B11106A9E3206A8B3D89DE9529BAC1D5C120DED4EBB153F009A09D0794`; Release: `990409DE4EF20D74D51C03411890B3C8BE1590BFB815CA6746CC03272DA6CBBE`.

I run precedenti sul fixture P03e (P03e.1) non valgono come witness: i binari hanno usato registry inizializzati ma non caricati, producendo ID 1 e 255 invece di 1/12/18. Non sono stati ripetuti in questa fase; il loro exit 1 includeva il FAIL P02D e non è stato mascherato.

## 7. Risultati richiesti non disponibili

Non si riportano coordinate campione o conteggi per ID 1/12/18 del witness P03e perché il percorso C++ con i JSON reali non è stato eseguito. Analogamente non si dichiarano superfici, `Vertex.materialID` o `Vertex.color` verificati. Correggere il fixture ora sarebbe prematuro: il FAIL precedente proveniva dal loader assente nel runner, non da una prova che le regole procedurali non producano i layer.

## 8. Piano minimo per sbloccare P03e.1b

1. Ripristinare o rendere disponibile la stessa installazione MSVC v145/MSBuild già usata per i build storici; non cambiare generatore, preset o versione compiler.
2. Clonare il progetto runner in un nuovo progetto diagnostico sandbox, con sorgente e tutte le destinazioni OutDir/IntDir/PDB/import library sotto nuove directory del runtime sandbox. Collegare le librerie P03d3 esistenti solo dopo aver verificato compatibilità ABI/header.
3. Il programma deve ricevere path espliciti sandbox, controllare tutti i return dei loader e gli ID 1/12/18, poi caricare il fixture e generare voxel/mesh con il percorso condiviso di produzione. Se i risultati non contengono i tre ID, distinguere la regola/layer che non li genera; modificare solo una copia del fixture dopo tale prova.
4. Eseguire lo stesso harness in Debug e Release; confrontare conteggi voxel e record materiali dei vertici con un contratto numerico esplicito. Poi rieseguire la regressione storica Tiny già PASS.
5. Soltanto dopo il PASS CPU chiedere una sessione Vulkan separata; non avviare GPU da questa fase.

## Decisione

**NO-GO per P03e.2 Vulkan.** I registry JSON sono coerenti per ispezione e la regressione Tiny storica è PASS Debug/Release, ma manca la prova C++ dei loader reali e del witness CPU. Lo stato è **BLOCKED dalla toolchain**, non FAIL del codice di produzione. Nessun codice, registry, asset, serializer o fixture è stato corretto in questa fase; nessun freeze e nessun P04.
