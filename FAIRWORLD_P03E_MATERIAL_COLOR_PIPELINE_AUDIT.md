# FAIRWORLD P03e — Block Material & Color Pipeline Audit

**Modalità:** audit statico e preparazione fixture isolato. Nessuna build, GUI o prova GPU eseguita in questa fase.

## Esito sintetico

- **Root cause del blocco grigio osservato:** non dimostrata. Non è stata osservata una sessione GPU e quindi non è possibile attribuire il sintomo a un ID, a una texture, a un descriptor o allo shader.
- **Contratti confermati:** MapDocument persiste nomi di blocco e regole procedurali; `MaterialRegistry` persiste separatamente fallback PBR e percorsi texture; `TerrainSolver` risolve i nomi in BlockID; il mesher voxel mantiene il BlockID come `materialID` e preleva `baseColorFallback` dal registro per il colore CPU del vertice.
- **Rischio concreto nel percorso live:** `CacheManager::SyncMaterialGpuCache` ignora il `bool` restituito da `LoadPBRTextureFromFile`. Se un albedo con percorso non vuoto manca o non è caricabile, il ramo non applica `baseColorFallback`; la layer GPU può quindi conservare dati precedenti. È un rischio statico confermato, ma non prova che si sia verificato nel caso visuale segnalato.
- **Differenza fra viste:** Chunk Editor 2D visualizza colori di tipo/bioma; la preview 3D del Chunk Editor usa generazione voxel; Planet Mapper usa anche il percorso LOD sferico. Queste viste non sono prove equivalenti del medesimo materiale GPU.
- **Decisione:** **GO condizionato** per un successivo test Vulkan isolato con fixture diagnostico e logging di layer/materiale. Nessuna causa GPU va dichiarata prima di quella prova.

## Safety e baseline

- Repository: `D:\FAIRWORLD\FAIRWORLD`.
- HEAD osservato: `f9c7a3047bf3c8ecaad70de4f1af39943933eaf1`.
- Working tree già dirty e con numerosi file untracked al momento dell'audit. Nessun file di produzione è stato modificato da questa fase; non sono stati eseguiti reset, clean, commit, push o CMake.
- È stato creato un solo nuovo file di prova nel sandbox: `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p03e-material-witness\world_map.json`.
- SHA-256 fixture P03e: `AD62FE45DAB43BF3C465F13D7832AB937E271549159A172BBF89D278F5D3C7B5`.
- SHA-256 fixture sorgente P03d4, rimasto separato: `47AF4358E4BD663926E64A43ACF2B116F3A2939BA5605FC89B2983D32A9AD503`.
- Il fixture P03e è una copia sandbox del documento Tiny P03d4, con i nomi di layer impostati a `fairworld:Grass`, `fairworld:thesims` e `fairworld:carburotitanio`. Non è stato caricato dal runtime, né usato per produrre voxel o mesh in questa fase.

## 1. Pipeline reale

```text
Chunk Editor layer UI
  └─ TerrainTemplate / TerrainLayer.blockName
       └─ MapDocument JSON o FWB v8 (nomi e regole; nessun colore PBR)
            ├─ Planet Mapper compiler: templateId → MapRegion → surface/subsurface BlockID
            └─ PlayState: MapWorldGenerator → TerrainSolver
                  └─ BlockRegistry.GetBlock(BlockName).id → voxel BlockID
                       └─ VoxelChunkMesher
                            ├─ MaterialRegistry[BlockID].baseColorFallback → Vertex.color
                            └─ materialID = BlockID
                                 └─ RenderManager vertex input → Forge shader
                                      ├─ materialID → layer albedo/normal/ORM
                                      └─ BlockProperties SSBO → PBR/behaviors
```

Percorso Planet Mapper aggiuntivo:

```text
PlanetMapperCompiler → PlanetChunkInstance / MapRegion e ID superficie
  → SphericalLOD: materiale per vertice + UV Cube-Sphere + colore debug/bioma
  → PlanetaryMeshGenerator → mesh LOD
  → Forge shader e layer PBR indicizzato dal materialID
```

Il percorso LOD non è il mesher voxel di PlayState: un test della preview planetaria non certifica l'identità/mesh dei voxel.

## 2. Block identity e risoluzione

`TerrainLayer` serializza `blockName`, profondità, noise e blend mode (`src/world/MapDocument.cpp`, `to_json(TerrainLayer)`); il documento JSON scrive `terrainLibrary` e le istanze planetarie. Il binario FWB v8 serializza le regole come JSON incorporato nel record. Il materiale visivo non è incluso in questi dati: è ricostruito dal registry dei materiali, caricato separatamente.

`ResolveTerrainRules` converte `coreBlockName`, ogni `layer.blockName` e i nomi dei blocchi acqua tramite `BlockRegistry::GetBlock(name).id` (`src/world/MapDocument.cpp`, circa righe 1278 e 1431–1440). La lookup non restituisce uno stato di errore: un nome sconosciuto restituisce `m_fallbackBlock` (ID 255, `fairworld:error`) in `src/blocks/BlockRegistry.cpp`, `GetBlock(const std::string&)`.

Questo dimostra un possibile percorso di sostituzione silenziosa per un nome non risolto. Non dimostra che il fixture o il mondo visualizzato contengano un nome irrisolto. L'UI del Chunk Editor risolve il nome per mostrare il blocco selezionato e, quando si sceglie una voce, salva `def.stringId`; non è stato provato che il caricamento di ogni documento validi tutti i nomi.

### Matrice campione

| BlockName | BlockID / material slot Forge | Colore fallback persistito | Albedo dichiarato | Stato asset osservato | Significato |
|---|---:|---|---|---|---|
| `fairworld:Grass` | 1 | `(0.30000001, 0.80000001, 0.20000000)` | vuoto | nessun albedo referenziato; esiste un file `fairworld_Grass_albedo.png`, ma l'ID 1 non lo referenzia | caso fallback uniforme |
| `fairworld:thesims` | 12 | `(0.0, 0.02476811, 1.0)` | `textures/blocks/fairworld_thesims_albedo.png` | file assente in asset source e sandbox | caso texture mancante; colore blu atteso come fallback al packing iniziale |
| `fairworld:carburotitanio` | 18 | `(0.7, 0.7, 0.7)` | `textures/blocks/fairworld_carburotitanio_albedo.png` | file presente in asset source e sandbox | caso albedo referenziato e disponibile |

I valori sono stati letti dai registry JSON di source e sandbox. ID, material slot, `appearanceId` e `scientificMaterialId` sono campi/concetti distinti; nel percorso Forge voxel attuale l'indice texture è esplicitamente il BlockID (`materialID = block`).

## 3. Materiali e persistenza

`PBRMaterialDef` (`src/blocks/MaterialRegistry.h`) include `target_block_id`, `appearanceId`, percorsi albedo/normal/ORM, `baseColorFallback`, roughness, metallic, emissive, alpha, behaviors e geometria. `MaterialRegistry::LoadFromJson` e `SaveToJson` persistono separatamente i percorsi, fallback PBR, behaviors e geometria (`src/blocks/MaterialRegistry.cpp` circa 97–177); **il campo `appearanceId` della struct non viene letto né scritto da questi metodi** e non compare nelle righe `materials.json` esaminate. `BlockRegistry` persiste identità e proprietà del blocco, incluso `appearanceId`, ma non contiene il fallback colore PBR.

Chunk Editor salva ricette (`TerrainTemplate`, layer, nomi), non buffer voxel. La preview 3D richiama la generazione di un `GameWorld` dalla ricetta; la canvas 2D colora le regioni secondo `MapRegionType` (Forest/Ocean/Desert ecc.), non secondo il materiale del blocco. Pertanto il colore della canvas 2D è una rappresentazione di biome/region e non prova il colore materiale persistito.

Block Maker può aggiornare `MaterialRegistry` dal draft e salvare il fallback/percorso nel registry. La preview Block Maker aggiunge il fallback ai vertici candidati e assegna il material ID selezionato (`BlockMakerState.cpp`, `UpdatePreviewMesh` circa 5023–5160); può inoltre usare il Painter e le sue texture preview. Quella preview non dimostra da sola che il documento planetario abbia referenziato lo stesso ID o che la layer GPU del mondo sia corretta.

`appearanceId` è presente nei registri e nei test, ma nell'inventario di questo percorso non è emerso un consumer che lo risolva durante il meshing voxel o nel lookup Forge: la pipeline runtime descritta usa la tabella `MaterialRegistry` indicizzata per BlockID. È una separazione architetturale da tenere presente, non una prova che appearance sia la causa del colore perso.

## 4. CPU mesh e dati trasmessi

In `src/world/VoxelChunkMesher.cpp`:

1. ID 0 (Air) non produce superfici.
2. Per un voxel non Air, `materials.GetMaterial(block)` recupera la definizione dal medesimo ID.
3. `Vertex.color` viene impostato da `baseColorFallback`; emissive dal materiale.
4. `Vertex.materialID` viene impostato a `block`.
5. Le facce usano `VoxelFaceUV` e portano lo stesso `materialID`.

Questo conferma che sul percorso CPU il mesher non sostituisce l'identità del blocco con zero, salvo che il voxel input sia già ID 0. Non certifica la pubblicazione ECS/GPU.

`src/states/components/PlanetMapperCompiler.cpp` compila i template attivi nelle regioni e risolve gli ID superficie/sottosuperficie tramite BlockRegistry. `src/world/SphericalLOD.cpp` produce un percorso diverso: attribuisce material IDs per bioma/elevazione e crea colori di debug per sabbia/acqua/neve/pietra e marker equatoriali/polari. I colori non sono necessariamente il fallback del materiale; shader e albedo layer determinano il risultato base del Forge.

## 5. GPU pipeline statica e fallback

- `RenderManager` configura location 0 posizione, 1 colore, 2 UV, 3 materialID (`R32_UINT`), 4 normale, 5 AO, 6 luce, 7 emissive. La sorgente `forge.vert` riceve gli stessi location e trasferisce materialID in uscita flat.
- `forge.frag` converte `inMaterialID` nel layer albedo clampato al numero di layer, poi campiona albedo, normal e ORM. Il colore vertex non viene moltiplicato nella normale uscita PBR albedo. Viene usato nel ramo emissive/glow (e dal codice circostante per gli effetti/alpha). Quindi il colore `Vertex.color` del mesher non è la fonte colore base per superfici opache nel contratto Forge corrente.
- `TexturePacker::PackMaterials` costruisce `fallbackAlbedo` da `baseColorFallback` e lo passa a `LoadAndAppendTexture`. Se il path è vuoto o `stbi_load` fallisce, `LoadAndAppendTexture` riempie la layer col fallback fornito. Perciò il percorso di packing iniziale è progettato per rendere blu il layer 12 anche con texture mancante; il file mancante produce un warning, non automaticamente layer bianca.
- `CacheManager::SyncMaterialGpuCache` sceglie `LoadPBRTextureFromFile` quando il percorso albedo è non vuoto; altrimenti chiama `UpdateMaterialFallback`. `LoadPBRTextureFromFile` restituisce `false` se `stbi_load` fallisce, ma il risultato è ignorato dal chiamante. Nel ramo fallito non viene chiamato `UpdateMaterialFallback`. `UpdateMaterialFallback` sovrascrive la layer albedo con il fallback.

**Rischio statico confermato:** una sincronizzazione live di materiale con path albedo non vuoto ma non caricabile può lasciare una layer GPU precedente senza applicare il fallback corrente. In un layer riutilizzato ciò può mostrare colore/texture stantii. L'asset ID12 è effettivamente dichiarato ma mancante nei percorsi esaminati; non abbiamo dimostrato che `SyncMaterialGpuCache` sia stato chiamato per quel materiale nella sessione che ha mostrato i blocchi grigi. Inoltre l'iniziale packing usa un percorso distinto e applica il fallback.

La cache PBR e lo shader statico non dimostrano quali byte della cache siano stati caricati, quali descriptor siano legati, né cosa sia stato disegnato. Nessuna conclusione sul colore GPU effettivo è quindi possibile in questo audit.

## 6. Confronto dei tre strumenti

| Vista | Dati/mesher | Origine colore base attesa | Limite dell'evidenza |
|---|---|---|---|
| Chunk Editor 2D | `MapRegion` e overlay editor | colore hardcoded del tipo regione | non rappresenta BlockID/materiale |
| Chunk Editor preview 3D | ricetta del `TerrainTemplate` → generatore → GameWorld/voxel mesher | albedo layer PBR per BlockID; fallback dal registry usato dal packer | preview separata e pianare; non prova persistenza GPU nel PlayState |
| Planet Mapper | compiler regioni e SphericalLOD | material ID LOD + layer PBR; alcuni vertex colors debug/emissive | percorso LOD differente dai voxel; non prova il percorso voxel del PlayState |
| PlayState | documento caricato → MapWorldGenerator/TerrainSolver → GameWorld/VoxelChunkMesher | albedo layer PBR per BlockID | mesh CPU coerente non certifica upload, descriptor o draw |

## 7. Diagnostic witness

È pronto il documento sandbox `p03e-material-witness/world_map.json`, Tiny e non compilato, con ricetta basata sui tre nomi della matrice (fallback verde, texture mancante con fallback blu, texture valida). SHA-256 è riportato nel gate safety.

Il test non è stato eseguito: il runner CPU già disponibile non carica automaticamente i registry esterni necessari a verificare le layer reali, e un risultato sintetico senza MaterialRegistry caricato non sarebbe prova valida. Non è stato inventato un voxel buffer né un esito del renderer. Prossima prova dovrebbe registrare, per voxel campione: `BlockName → BlockID → MaterialRegistry[target_block_id] → materialID vertex → cache layer hash/pixel → shader layer → draw`.

## 8. Prima divergenza e classificazione

| Osservazione | Classificazione | Evidenza |
|---|---|---|
| MapDocument non porta con sé colore PBR/texture; porta BlockName e regole | CONFIRMED | serializer `TerrainLayer` e schema MapDocument |
| Runtime risolve BlockName a ID tramite BlockRegistry | CONFIRMED | `ResolveTerrainRules`, `BiomeSystems`, `PlanetMapperCompiler` |
| Nome sconosciuto può diventare ID 255/error block senza errore esplicito alla callsite | CONFIRMED come comportamento condizionale | `GetBlock(string)` restituisce fallback; nessuna prova che il witness lo attivi |
| Voxel mesher mantiene l'ID in materialID e copia il fallback nei vertici | CONFIRMED staticamente | `VoxelChunkMesher::Build` |
| Shader usa materialID per campionare layer PBR e non usa vertex color come albedo diffuso standard | CONFIRMED staticamente | `forge.vert`/`forge.frag` |
| Pack iniziale della texture mancante applica il fallback baseColor | CONFIRMED staticamente | `PackMaterials` + `LoadAndAppendTexture` |
| Sync live può ignorare il fallimento texture e non applicare il fallback | CONFIRMED staticamente; impatto runtime non verificato | `CacheManager` ignora il bool di `LoadPBRTextureFromFile` |
| Questo sia la causa dei blocchi grigi osservati | NOT VERIFIED | nessun replay Vulkan e nessuna cattura della layer/descriptor/draw |

Non è emersa una prima divergenza BlockID/CPU mesh dimostrata per il caso visivo attuale. Il primo rischio statico concreto è nel sync GPU live, dopo il registry e il mesher; la presenza di tale rischio non identifica il guasto della sessione segnalata.

## 9. Regression gate

P03e non ha modificato codice, test o asset di produzione e non ha eseguito build/test. I risultati riportati sotto sono soltanto evidenze precedenti, non rieseguite:

| Gate storico | Evidenza precedente | P03e |
|---|---|---|
| FWB v8 semantic parity | P03d.1 documentato PASS | NON RIESEGUITO |
| BlockID round-trip | P03d.1 documentato PASS | NON RIESEGUITO |
| Tiny voxel Debug/Release parity | P03d.3–d.5 documentata 54/54 byte-identici | NON RIESEGUITO |
| Mesh CPU parity | P03d.3–d.5 documentata identica | NON RIESEGUITO |
| GPU mesh publication/render | non verificato in baseline P03e | NOT VERIFIED |

I log P03d.5 riportavano 54 chunk, 504877 voxel non-Air e 99252 triangoli; hash voxel `F3D0B98BBCEDDE8CC64A43ACF2B116F3A2939BA5605FC89B2983D32A9AD503` e hash mesh CPU `267B0E8D6CA28A5A07B5BEFB97AA48D7EE366DCFA1E74965F37E510B13B7B8C6`, Debug/Release uguali. Sono citati come baseline storica e non come test P03e.

## 10. Proposta minima successiva — non applicata

1. Nel sandbox, caricare esplicitamente copie sandbox di `blocks.json` e `materials.json`; risolvere i tre BlockName e rifiutare il witness se uno non risolve all'ID atteso.
2. Eseguire prima un test CPU che verifichi per i voxel campione BlockID, materialID vertex e fallback vertex del mesher.
3. In una sessione Vulkan autorizzata e isolata, ispezionare pixel/hash delle layer 1, 12 e 18 dopo packing e dopo sync live; registrare `LoadPBRTextureFromFile` result e layer effettiva, i descriptor usati e gli ID delle draw.
4. Ripetere con albedo ID12 assente al bootstrap e poi con layer GPU già popolata prima della sync live. Questo separa fallback iniziale da stale layer da sync.
5. Solo se il fallimento viene riprodotto, proporre il fallback live per texture non caricabili o una validazione esplicita dei BlockName. Non cambiare shader o renderer in assenza della prova.

## Decisione

**GO condizionato** per una successiva validazione Vulkan del fixture nel runtime sandbox: esiste un witness con tre casi distinguibili e una pipeline statica coerente abbastanza da rendere il test informativo. La sessione GPU non è autorizzata né eseguita in P03e; prima di avviarla va verificato che eseguibile, asset, cache e scritture restino nel sandbox. Nessuna modifica è proposta come già applicata e nessun freeze è dichiarato.
