# D1.2.14b — LOD Vertex Contract Audit

**Esito:** **STOP prima della patch**, come richiesto dal gate B. Il contratto Forge è incompatibile anche con producer di mesh voxel: un cambiamento soltanto in `SphericalLOD` non preserverebbe né correggerebbe in modo coerente gli altri generatori che usano la pipeline condivisa.

Nessuna modifica a sorgenti, shader, asset, cache o salvataggi è stata applicata in questa fase. Il solo file prodotto è questo rapporto.

## A — Layout effettivo della pipeline Forge

La struttura effettivamente usata dalla pipeline Forge è `fw::Vertex` in `src/components/ForgeComponents.h`, non il `Vertex` globale dell'omonimo header `src/core/utils/World.h`. `RenderManager.h` include entrambi tramite dipendenze; il commento vicino al layout in `RenderManager.cpp` che parla genericamente di `Vertex` non distingue i due tipi. La pipeline Forge usa `sizeof(fw::Vertex)` e `offsetof(fw::Vertex, ...)` in `RenderManager.cpp:3786-3802`.

| Vulkan location | Offset CPU (`fw::Vertex`) | Formato Vulkan / tipo shader | Dato scritto dai producer | Significato effettivo Forge | Compatibilità |
|---:|---:|---|---|---|---|
| 0 | 0, `position` | `R32G32B32_SFLOAT`, `vec3` | Posizione locale mesh | `inPosition`; posizione trasformata e passata come world position | Compatibile |
| 1 | 12, `color` | `R32G32B32A32_SFLOAT`, `vec4` | Colore vertice | `inColor` → `outColor` → `inVertexColor`; usato per glow/alpha | Compatibile |
| 2 | 28, `roughMetal` | `R32G32_SFLOAT`, `vec2` | Varia: UV, roughness/metallic, oppure coppie costanti | `inRoughMetal` viene trattato come UV e inoltrato a `inUV`; se una componente è sotto −0.5, shader applica UV planari | **Incompatibile per più generatori, inclusi voxel e LOD** |
| 3 | 36, `materialID` | `R32_UINT`, `uint` | ID blocco/materiale | `flat inMaterialID`, selezione layer albedo/normal/ORM | Compatibile quando l'ID coincide con l'indice layer |
| 4 | 40, `normal` | `R32G32B32_SFLOAT`, `vec3` | Normale geometrica/radiale | `inNormal` → `outNormal` → normale geometrica fragment | Compatibile |
| 5 | 52, `ao` | `R32_SFLOAT`, `float` | Ambient occlusion | `inAO` dichiarato nel vertex shader ma non inoltrato né usato dal fragment | Dato non consumato nel percorso Forge attuale |
| 6 | 56, `light` | `R32_SFLOAT`, `float` | Illuminazione vertex | `inLight` dichiarato ma non inoltrato né usato dal fragment | Dato non consumato nel percorso Forge attuale |
| 7 | 60, `emissive` | `R32_SFLOAT`, `float` | Intensità emissiva | `inEmissive` → fragment; contribuisce al glow | Compatibile |

`sizeof(fw::Vertex) == 64` e gli offset sono verificati da `static_assert` in `ForgeComponents.h:79-87`. I tipi e le assegnazioni pipeline corrispondono alle dichiarazioni in `forge.vert:13-20`.

### Shader e parametri PBR

In `forge.vert:45-61`, location 2 viene assegnata a `uv` e usata come coordinata di campionamento. `forge.frag:123-145` usa `inUV` per campionare i tre array albedo, normal e ORM. Roughness e metallic effettivamente usati per BRDF vengono estratti da `orm.g` e `orm.b` (`forge.frag:140-166`); non vengono letti dal valore CPU `roughMetal`. Il fragment contiene inoltre `BlockPropertiesGPU`, ma il percorso osservato lo consulta per flag/emissive/alpha, non usa la coppia location 2 come parametri roughness/metallic.

Quindi, quando un producer scrive `{roughness, metallic}` in `roughMetal`, Forge non riceve quei due parametri PBR da tale attributo: li interpreta come UV. Il comportamento può generare campionamento costante o concentrato su una piccola regione della texture e può rendere degeneri le derivate UV usate dal calcolo TBN.

## Producer e consumer verificati

| Producer / percorso | Valore scritto in location 2 | Material ID | Consumer / esito |
|---|---|---|---|
| `SphericalLODSystem::RequestMeshGeneration`, `SphericalLOD.cpp:427-442` | `{matDef.roughnessFallback, matDef.metallicFallback}`; per aree non dipinte l'ID è 0 | `materials[idx]`, inclusi ID bioma o 0 | Il mesh LOD è `fw::MeshComponent` di tipo Chunk. I renderer che lo disegnano usano Forge: UV invalide semanticamente. **Bug target confermato.** |
| Mesher voxel `GameWorld`, `GameWorld.cpp:417-577` | `{rough, metal}` per ciascuna faccia voxel | ID del blocco (`block`) | `PlayRenderer` usa la pipeline Forge e disegna le mesh del mondo. L'attributo diventa UV anziché proprietà PBR. **Incompatibilità voxel confermata.** |
| `MeshGenerators::MakeVoxelPreview`, `MeshGenerators.cpp:53-76` | `{rough, metal}` | ID del blocco in preview | Usato nelle preview/editor che disegnano la mesh tramite Forge. **Incompatibilità confermata.** |
| `MeshGenerators::MakeCube`, `MakeSphere`, `MakeSuperSphere`, `MeshGenerators.cpp:21-207` | Coppie costanti con commenti/valori roughness-metallic (ad es. `{0.5,0}`) | spesso 0 | UV costanti o degenerate; la pipeline non applica fallback procedurale perché non sono valori negativi. **Incompatibilità/rischio confermati.** |
| `BlockMakerState` ambiente, `BlockMakerState.cpp:4695-4707` | `{0.9,0.0}` indicato come rough/non-metallic | ID ambiente | Il piano viene disegnato con Forge; campionamento con UV costanti. **Incompatibilità confermata.** |
| `GeometryCompiler`, `GeometryCompiler.cpp:156-170, 2671-2687` | UV finali o `corner.uv` in `[0,1]` | ID materiale | È coerente con il contratto shader UV; include mapping e trasformazioni UV del compilatore. |
| `EditableMesh` runtime bridge, `EditableMesh.cpp:1274` | `faceCorners[i].uv` | ID materiale | Coerente; il commento esplicita il bridge UV in `roughMetal`. |
| `MeshGenerators::MakeObelisk`, `MeshGenerators.cpp:250-289` | UV esplicite sui vertici | ID 0 | Coerente come coordinate; il layer 0 può essere intenzionalmente neutro. |

Sono presenti inoltre il tipo globale `Vertex` in `src/core/utils/World.h` e una pipeline/renderer legacy con layout simile. Il presente gate si fonda sulla pipeline attiva Forge (`fw::Vertex` + `forge.vert/.frag`); non si propone di unificare tali tipi o pipeline.

## Render path condiviso

`RenderManager::CreateForgePipeline` carica `forge_vert.spv` e `forge_frag.spv` (`RenderManager.cpp:3766`), costruisce il layout `fw::Vertex` e crea `m_forgePipeline`. La lambda `initSubRenderer` assegna la stessa pipeline/layout ai renderer, tra cui Block Maker, Map, Forge, Play, Chunk Editor e Planet Mapper (`RenderManager.cpp:3913-3938`).

- `ChunkEditorRenderer::Draw` associa la pipeline ricevuta e disegna mesh Chunk/Prefab (`ChunkEditorRenderer.cpp:30-99`).
- `PlanetMapperRenderer::Draw` associa `m_pipeline` e disegna mesh Forge non-voxel, compresi i nodi LOD (`PlanetMapperRenderer.cpp:69,80`).
- `PlayRenderer::Draw` associa `m_pipeline` e disegna le `MeshComponent` del ForgeWorld (`PlayRenderer.cpp:33,42`); il mesher voxel produce le stesse `fw::Vertex`.

Pertanto non esiste una pipeline LOD indipendente che permetta di cambiare location 2 soltanto per il pianeta senza definire cosa deve avvenire ai mesh voxel della stessa pipeline.

## B — Gate di arresto e correzione non applicata

Il vincolo dell'utente è esplicito: se il layout Forge risulta incompatibile anche con i generatori voxel, fermarsi prima di ampliare la patch. L'incompatibilità è dimostrata da `GameWorld.cpp` e dal consumo della stessa `m_forgePipeline` nel PlayState.

Non ho modificato `SphericalLOD.cpp`. Scrivere UV corrette soltanto lì correggerebbe quel producer, ma lascerebbe GameWorld voxel, preview voxel e primitive con roughness/metallic in location 2 ancora interpretati come UV. Non è quindi una patch coerente del contratto condiviso.

### Direzione matematica da validare dopo la decisione sul contratto

Per il solo LOD, il dato corretto in location 2 dovrebbe essere UV finite nel dominio `[0,1]`. Le coordinate andrebbero derivate dalla direzione normalizzata di ciascun vertice attraverso gli helper canonici D1.2 `CubeSphereMapping::DirectionToFaceUV` (o una conversione equivalente già approvata), con tie-break delle facce agli spigoli deterministico. Ogni faccia avrebbe una chart locale orientata secondo la convenzione D1.2; i bordi di una singola faccia includerebbero 0 e 1. La continuità della posizione può restare geometrica, mentre il reset a UV di faccia e la discontinuità di texture agli spigoli sarebbero intenzionali salvo una futura topologia di atlas cross-face.

L'ID materiale resterebbe `materials[idx]` e sarebbe flat per triangolo/mesh come già richiesto da `flat inMaterialID`. UV e attributi continui (colore, normale, emissive, eventuale AO/light se resi consumabili) sarebbero interpolati; ID materiale non dovrebbe essere interpolato. Roughness/metallic devono provenire da un canale esplicito e coerente (attualmente ORM/material data), non riutilizzando location 2 per entrambi i significati.

Questa direzione **non è una patch proposta per approvazione immediata**: prima serve una decisione sul layout comune e sui producer voxel. In particolare bisogna scegliere se formalizzare location 2 come UV per tutti i producer, portare le proprietà PBR in un attributo/SSBO distinto, o mantenere pipeline/layout separati. La scelta coinvolge più generatori e supera il perimetro consentito per il solo LOD.

## C — Test di regressione da introdurre dopo il contratto condiviso

1. Test CPU puro del generatore LOD: per tutte e sei le facce, vertici UV finite e dentro `[0,1]`; almeno due valori distinti per ogni patch non degenere.
2. Test degli orientamenti UV usando centri e corner +Z, −Z, +X, −X, +Y, −Y confrontati con `CubeSphereMapping`.
3. Test dei bordi faccia: posizione/direzione coincidente sui lati adiacenti; UV locali 0/1; discontinuità texture ai bordi dichiarata e riproducibile se il contratto resta face-local.
4. Test layout Forge: `sizeof`, `offsetof`, formati pipeline e significato degli attributi verificati con una tabella/contract test.
5. Test ID: layer materiale preservato e flat; campionamento ORM selezionato dall'ID corretto.
6. Test generatori voxel CPU: ogni faccia voxel ha UV non degeneri nel dominio atteso, ID blocco preservato e roughness/metallic ancora forniti dalla fonte PBR concordata.
7. Golden/differential test dei generatori GeometryCompiler ed EditableMesh per garantire che le UV già scritte non vengano reinterpretate o trasformate due volte.
8. Build/run senza Vulkan per i test sui dati CPU; successivo test GPU per l'interpolazione e il sampling effettivi.

## D — Validazione e limiti

- **Audit statico:** completato; layout/producer/consumer tracciati sopra.
- **`git diff --check`:** PASS, exit code **0** dopo la creazione del rapporto. Git ha mostrato solo avvisi LF→CRLF su file già modificati nel workspace; nessun errore whitespace.
- **Build Debug, test dedicati, `--validate-d12`, test cache D1.2.14a:** **SKIPPED** per il gate di arresto: non è stata applicata alcuna patch e il contratto condiviso è incompatibile con i generatori voxel.
- **GUI Planet Mapper, Chunk Editor, PlayState:** **NOT RUN**; nessun cambiamento renderizzato da validare e il percorso voxel è una dipendenza aperta.
- **Vulkan Layer/ownership:** non avviati in questa fase.
- **Hash dei 40 originali:** non ricalcolati in questa fase; non sono stati toccati asset o salvataggi.

## Piano di rollback

Nessun rollback di sorgenti è necessario perché nessuna patch è stata applicata. Per una futura implementazione, il rollback dovrà ripristinare insieme la decisione sul layout condiviso e i producer coinvolti; ripristinare soltanto `SphericalLOD` non è sufficiente se il contratto Forge resta condiviso.

## Decisione

**BLOCKED — dipendenza di contratto.** Prima di procedere occorre autorizzare una correzione esplicita e coordinata dei producer voxel e del significato di roughness/metallic rispetto alle UV Forge. Nessun commit/push; D1 non è dichiarato FROZEN.
