# FAIRWORLD P03e.1 — Material Witness & GPU Cache Validation

**Modalità:** verifica CPU parziale; piano GPU soltanto. Nessuna sessione Vulkan è stata avviata.

## Esito

- **Registry JSON sandbox:** contenuti e hash verificati; i tre nomi hanno gli ID attesi nei JSON.
- **Caricamento tramite classi C++ reali:** NOT VERIFIED. Il runner E2E disponibile non carica questi file: inizializza registry predefiniti. Non esiste nel runner un'opzione per passare i JSON.
- **Voxel witness con i tre ID:** FAIL per i binari E2E eseguiti: sono comparsi soltanto ID 1 e 255; ID 12 e 18 non sono stati generati. Il risultato è contaminato dalla risoluzione fallback del runner preconfigurato e non dimostra un difetto del fixture o del runtime editor.
- **Vertex.materialID/Vertex.color sul witness reale:** NOT VERIFIED. La mesh del test non è stata costruita con i `MaterialRegistry` JSON effettivi.
- **Vulkan/cache GPU/rendering:** NOT RUN per vincolo esplicito.
- **Decisione GPU:** **NO-GO per ora**: il preflight CPU non ha dimostrato il witness con i registry effettivi. Occorre prima un runner CPU isolato che li carichi realmente e produca i tre ID in voxel/mesh.

## 0. Safety e preflight

- HEAD: `f9c7a3047bf3c8ecaad70de4f1af39943933eaf1`.
- Working tree già dirty/untracked; nessun reset, clean, commit, push o riconfigurazione CMake.
- Registry usati per l'ispezione: copie sandbox in `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\assets\definitions`.
- SHA-256 `blocks.json` sandbox: `66922BF44F11FC19BD0E59B1A919364056507CC731EFBAF122152BB07BF34744`; coincide con la copia source letta.
- SHA-256 `materials.json` sandbox: `33C1E113852B22C0ECAF51020B8E4B6B195CDE2918925EF1C98D9FEF58F406E6`; coincide con la copia source letta.
- Fixture P03e: `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p03e-material-witness\world_map.json`, 5.974 byte, SHA-256 `AD62FE45DAB43BF3C465F13D7832AB937E271549159A172BBF89D278F5D3C7B5`. JSON leggibile, Tiny (enum 0), `isCompiled=false`, template `default_terrain`.
- Fixture P03d4 di origine: SHA-256 invariato `47AF4358E4BD663926E64A43ACF2B116F3A2939BA5605FC89B2983D32A9AD503`.
- Nessuna build è stata eseguita; pertanto non sono stati prodotti `.obj`, PDB o binari nuovi. I runner hanno scritto solo in directory di run nuove sotto il runtime sandbox. `TEMP` e `TMP` del processo sono stati diretti alle rispettive directory.

## 1. Validazione registry JSON

> I valori seguenti sono stati estratti e confrontati da copie JSON sandbox tramite parsing PowerShell. Questo verifica contenuto e riferimenti asset, ma **non equivale** a eseguire `BlockRegistry::LoadFromJson` e `MaterialRegistry::LoadFromJson`.

| Nome | BlockID JSON | `target_block_id` | `appearanceId` BlockRegistry | `appearanceId` MaterialRegistry JSON | `baseColorFallback` | Albedo | Presenza/leggibilità |
|---|---:|---:|---:|---|---|---|---|
| `fairworld:Grass` | 1 | 1 | 0 | chiave assente | `(0.30000001, 0.80000001, 0.20000000)` | vuoto | fallback; non richiede file |
| `fairworld:thesims` | 12 | 12 | 0 | chiave assente | `(0.0, 0.02476811, 1.0)` | `textures/blocks/fairworld_thesims_albedo.png` | assente |
| `fairworld:carburotitanio` | 18 | 18 | 0 | chiave assente | `(0.7, 0.7, 0.7)` | `textures/blocks/fairworld_carburotitanio_albedo.png` | presente e apribile in lettura |

**Nota di contratto:** il membro `PBRMaterialDef::appearanceId` esiste nella struct, ma `MaterialRegistry.cpp` non lo legge né lo scrive nel JSON esaminato; le tre righe `materials.json` non contengono `appearanceId`. Al load, il campo rimane quindi al default (0), salvo successive modifiche runtime. La precedente formulazione nel rapporto P03e che poteva far intendere che questo campo fosse persistito dal `MaterialRegistry` va letta corretta in questo senso.

## 2. Fixture e risultati voxel/mesh

Il fixture contiene due layer nominati `fairworld:Grass` e `fairworld:thesims`, e `coreBlockName=fairworld:carburotitanio`. Il documento passa la validazione Tiny del runner.

Sono stati eseguiti i seguenti binari preesistenti con il fixture esplicito:

| Configurazione | Eseguibile | Working directory / log | Exit code | Esito pertinente |
|---|---|---|---:|---|
| Debug | `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p03d2-debug-runner-bin\FairworldPlanetaryVoxelE2ETests.exe` | `...\p03e1-debug-run\`; [log](</D:/FAIRWORLD/D1_2_19D2_SANDBOX/runtime/p03e1-debug-run/run.log>) | 1 | Tiny caricato; `nonAir=504877`; distribuzione `1=13824, 255=491053`, senza ID12/18 |
| Release | `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p03d2-release-runner-bin\FairworldPlanetaryVoxelE2ETests.exe` | `...\p03e1-release-run\`; [log](</D:/FAIRWORLD/D1_2_19D2_SANDBOX/runtime/p03e1-release-run/run.log>) | 1 | stessi conteggi ID `1=13824, 255=491053`, senza ID12/18 |

Entrambi i log riportano anche `[P02D][FAIL] authoritative Tiny solid probes and planetary CastVoxel disagree`, perciò il full runner non è PASS. Non è una regressione attribuibile a P03e.1.

Il sorgente del runner (`src/world/PlanetaryVoxelE2ETests.cpp`, blocco di inizializzazione registry in `main`) chiama `BlockRegistry::Initialize()` e `MaterialRegistry::Initialize()`, senza `LoadFromJson`. La distribuzione osservata, quasi tutta ID255, è coerente con i nomi non presenti nel registry predefinito del runner che risolvono al blocco fallback. Questi run **non sono** prova che il runtime FAIRWORLD, che carica i JSON in bootstrap, perda i materiali.

Non sono stati registrati `Vertex.materialID` o `Vertex.color` per i tre materiali da una mesh con registry effettivi. Di conseguenza la richiesta di correggere il fixture sandbox per forzare i tre ID non viene soddisfatta artificialmente: l'ostacolo è il loader del runner, non una differenza già dimostrata nella ricetta. Il fixture è rimasto invariato.

## 3. Audit statico complementare

- `ResolveTerrainRules` e `PlanetMapperCompiler` risolvono nomi via `BlockRegistry::GetBlock(name).id`; la lookup sconosciuta restituisce il fallback ID255, senza esito tipizzato alla callsite.
- `VoxelChunkMesher::Build` per voxel non-Air legge `MaterialRegistry.GetMaterial(block)`, mette `baseColorFallback` in `Vertex.color` e assegna `Vertex.materialID=block`.
- Il vertex layout Forge trasporta la location 1 colore e location 3 materialID; il fragment shader sceglie il layer texture con materialID. Per il normale PBR opaco il colore vertex non sostituisce il campionamento albedo.
- `TexturePacker` costruisce il fallback layer dal `baseColorFallback`, incluso quando l'albedo mancante non viene caricata.
- Il rischio P03e rimane: `CacheManager::SyncMaterialGpuCache` ignora il `false` di `LoadPBRTextureFromFile`; sul ramo albedo path non vuoto non esegue `UpdateMaterialFallback` in caso di load fallito. Impatto GPU non riprodotto.
- Capacità, clamp e shader sono stati esaminati staticamente: lo shader clampa il materiale al numero di layer (`textureSize(albedoArray,0).z-1`) e anche il BlockProperties index a 255. Non è una prova che descriptor e immagine caricati siano quelli usati dalla draw.

## 4. Piano GPU — non eseguito

### A. Packing iniziale

Preparare un test sandbox che acquisisca pixel/hash delle layer 1, 12 e 18 dopo packing.

- Layer 1: albedo path vuoto → tutti i pixel dovrebbero essere il fallback RGB convertito a byte (accettazione: hash/pixel uniforme e colore entro quantizzazione 8-bit).
- Layer 12: file mancante → warning di load, ma layer fallback blu uniforme (stesso criterio).
- Layer 18: texture presente → contenuto campionato dalla texture resized/packed; non deve coincidere col fallback uniforme se la texture sorgente non è uniforme.
- PASS ulteriore: `materialID` dei vertici corrisponde al layer verificato.

### B. Sync live

1. Salvare hash/pixel del layer12 iniziale.
2. Assicurarsi che il layer12 GPU contenga dati non blu noti.
3. Invocare la sync live con l'albedo mancante.
4. Registrare `LoadPBRTextureFromFile` result e hash/pixel successivi.

PASS solo se la layer post-sync riflette il fallback blu registrato e nessun layer diverso viene mutato. FAIL se load fallisce e la layer resta stantia o non corrisponde al fallback. Questo test richiede una sessione Vulkan, dunque non eseguito.

### C. Descriptor e risultato rendering

Registrare per ogni triangolo campione materialID, safeLayer, immagine/view e descriptor set; verificare che l'array descriptor punti all'immagine campionata e che il pixel in readback corrisponda al sample albedo dopo PBR/illuminazione (con fixture luce, UV e normal controllati).

PASS solo con mesh pubblicata, draw sottomessa, frame/fence completata e readback verificato. Il solo vertex CPU o upload DMA non basta.

## 5. Gate e raccomandazioni

| Gate | Stato |
|---|---|
| Hash e contenuto registry JSON sandbox | PASS (ispezione JSON; hash allineati alle copie lette) |
| Load via classi C++ BlockRegistry/MaterialRegistry | NOT VERIFIED |
| Tutti e tre i BlockID presenti nei voxel del fixture con registry reali | NOT VERIFIED; il runner disponibile li sostituisce nel fallback |
| ID/materialID/Vertex.color per tre campioni reali | NOT VERIFIED |
| Debug/Release fixture run eseguiti | FAIL, exit 1; runner ha default registry e fallisce anche il gate P02D |
| Parità CPU P03d storica | non ripetuta da questa fase |
| Packing pixel/cache GPU | NOT RUN |
| Sync live/cache GPU | NOT RUN |
| Descriptor, draw, readback | NOT RUN |

Correzione minima proposta, **non applicata**: aggiungere un runner CPU diagnostico isolato che carichi effettivamente le copie sandbox di `blocks.json` e `materials.json`, validi ID e lookup, carichi il fixture, quindi riporti il conteggio ID e `materialID`/`color` della mesh CPU. Il runner deve fallire se trova 255 per i tre nomi noti o se un ID campione non ha vertici. Non toccare il full runner attuale o il codice di produzione finché la revisione non approva il nuovo harness.

**NO-GO per la sessione GPU immediata.** Prima serve il witness CPU reale e verificabile con i registry sandbox; in seguito va autorizzata separatamente la prova Vulkan. Nessun fix a CacheManager è applicato, nessuna root cause del blocco grigio è dichiarata e nessun freeze è assegnato.
