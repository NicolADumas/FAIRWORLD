# D1.2.14b.1 — Shared Forge Vertex Contract Report

**Esito complessivo: INCOMPLETE — CPU/build PASS, GUI/GPU BLOCKED.**

La location 2 della pipeline Forge è stata formalizzata come UV, mantenendo ABI e dati persistenti. La build Debug completa, il test di contratto, `--validate-d12` e il test della cache PBR v2 sono PASS. CTest termina con 15/17 test PASS; restano EXT-10 e due prove RLC della suite Lifecycle in FAIL. La GUI non è stata avviata perché il manifest di sicurezza segnala due originali modificati; non è stato eseguito alcun ripristino.

## 1. Contratto CPU/GPU e inventario

### Layout Forge verificato

| Location | Campo `fw::Vertex` | Offset | Formato Vulkan | Semantica Forge |
|---:|---|---:|---|---|
| 0 | `position` | 0 | `R32G32B32_SFLOAT` | posizione |
| 1 | `color` | 12 | `R32G32B32A32_SFLOAT` | colore |
| 2 | `roughMetal` (nome ABI legacy) | 28 | `R32G32_SFLOAT` | UV |
| 3 | `materialID` | 36 | `R32_UINT` | layer/material ID |
| 4 | `normal` | 40 | `R32G32B32_SFLOAT` | normale |
| 5 | `ao` | 52 | `R32_SFLOAT` | AO |
| 6 | `light` | 56 | `R32_SFLOAT` | luce |
| 7 | `emissive` | 60 | `R32_SFLOAT` | emissive |

Stride: 64 byte, offset e formato invariati. `RenderManager::CreateForgePipeline` lega location 2 all’offset 28; `forge.vert` riceve ora `inUV` e passa le coordinate interpolate a `forge.frag::inUV`. Il fallback planare continua ad attivarsi soltanto per coordinate negative non assegnate.

### Riferimenti individuati

| Percorso | Uso prima della patch | Stato dopo la patch |
|---|---|---|
| `src/components/ForgeComponents.h` | `fw::Vertex::roughMetal`, offset 28 | Nome e layout conservati; commento chiarisce il significato UV |
| `src/render/RenderManager.cpp` | Attributo Forge location 2 | Offset e formato invariati; commento allineato al contratto UV |
| `src/render/forge.vert` | `inRoughMetal` già inoltrato come UV | input rinominato `inUV`; semantica GPU invariata |
| `src/render/forge.frag` | UV per albedo/normal/ORM; roughness da `orm.g`, metallic da `orm.b` | invariato |
| `src/world/SphericalLOD.cpp` | Scriveva roughness/metallic fallback nel campo vertex | Scrive UV da `CubeSphereMapping::DirectionToFaceUV`; ID materiale invariato |
| `src/world/GameWorld.cpp` | Il mesher voxel scriveva roughness/metallic nel campo vertex | Ogni faccia genera UV per vertice; ID e dati PBR del registro restano separati |
| `src/world/MeshGenerators.cpp` | Cube, voxel preview, Sphere e SuperSphere usavano coppie costanti/PBR | UV per faccia o coordinate sferiche; ID preview preservato |
| `src/states/BlockMakerState.cpp` | Il floor sovrascriveva UV con `{0.9,0}`; diagnostica chiamava il campo roughMetal | sovrascrittura rimossa; diagnosi descrive UV e nome legacy |
| `src/states/PhysicsLabState.cpp` | Quad di pavimento con UV implicite tutte zero | quattro vertici UV esplicite |
| `src/core/utils/World.cpp` / `World.h` | Tipo `Vertex` legacy distinto; `AddFace` assegnava valore costante | `AddFace` usa la tabella UV per-corner già presente; nessun formato di mondo toccato |
| `src/world/GeometryCompiler.cpp` | Scrive coordinate UV nel campo legacy | invariato |
| `src/blocks/sculpt/EditableMesh.cpp` | Scrive UV per-corner | invariato |
| `src/blocks/sculpt/MeshBridgeService.cpp` | Legge il campo come UV per `corner.uv` | invariato |
| `src/blocks/factory/RawAssetContract.h` | Serializza e hasha la chiave `roughMetal` | invariato: nessuna migrazione o riscrittura dei dati |

`shader_instanced.vert` contiene ancora `inRoughMetal` e un varying `fragRoughMetal`, ma la ricerca nel C++ non trova un caricamento o una pipeline che lo utilizzi; il file di shader attivo Forge è `forge.vert`/`forge.frag`. La pipeline legacy principale usa un tipo `Vertex` distinto (`src/core/utils/World.h`) e `shader.vert` legge location 2 come `inTexCoord`; il fragment legacy prende roughness/metallic da `props.roughness`/`props.metallic`, non dal campo vertex. GLB usa un layout proprio e `GLBVertex::uv` a location 2.

**Conclusione preflight:** non è stato trovato un consumer della location 2 Forge che interpreti effettivamente i due float come roughness/metallic. Il campo è un ponte grafico legacy; le proprietà persistenti di materiale e simulazione non vengono rinominate né rimosse.

## 2. PBR e compatibilità

- Forge usa `materialID` per selezionare il layer e campiona roughness/metallic da ORM (`orm.g` e `orm.b`); le mappe PBR non dipendono dalla location 2.
- `RenderManager::UpdateMaterialFallback` costruisce il fallback ORM dal roughness/metallic del materiale.
- I valori `roughnessFallback` e `metallicFallback` rimangono nei registri e nelle definizioni dei materiali; `GameWorld` conserva base color, emissive e material ID nel mesh.
- Nessun material ID è stato rinumerato.
- Nessuna modifica a `TexturePacker.cpp/.h`, serializer dei mondi, `CubeSphereMapping` o dati salvati.
- Il campo e la chiave RawAsset restano `roughMetal`, così i vecchi documenti mantengono i byte e i numeri esistenti. Il bridge RawAsset → mesh continua a trattarli come UV, come già faceva.

## 3. Modifiche applicate

- Nuovo helper [VoxelFaceUV.h](src/world/VoxelFaceUV.h): proiezione riproducibile su assi per le sei normali voxel, dominio `[0,1]`.
- `GameWorld.cpp`: tutte le 36 emissioni di vertici nelle sei facce usano la proiezione per-faccia, senza cambiare winding, posizione, normale, AO o material ID.
- `MeshGenerators.cpp`: UV di faccia per cube/voxel preview; UV longitude/latitude per Sphere/SuperSphere.
- `SphericalLOD.cpp`: UV canoniche da `CubeSphereMapping::DirectionToFaceUV`, senza cambiare topologia, altezza, indici o sistema LOD.
- `World.cpp`: il mesher legacy assegna le UV per-corner già definite invece della coppia costante.
- `PhysicsLabState.cpp`: UV esplicite per il quad di pavimento.
- `forge.vert`, `ForgeComponents.h`, `RenderManager.cpp`, `BlockMakerState.cpp`: contratto/documentazione del campo chiariti senza cambiare ABI.
- Nuovo target e test CPU-only `FairworldForgeVertexContractTests`.
- File di build aggiornato: `CMakeLists.txt`.

## 4. Evidenze di build e test

Toolchain: CMake e MSBuild già configurati in `build_r3z7`, Visual Studio 18 / MSVC.

| Verifica | Risultato | Exit code |
|---|---:|---:|
| `git diff --check` | Exit 2: segnala i carriage return delle righe CRLF modificate come trailing whitespace | 2 |
| `git -c core.whitespace=cr-at-eol diff --check` | PASS: controlla gli spazi finali tollerando i CRLF già usati dai file Windows | 0 |
| Build Debug completa `ALL_BUILD`, `/m:1` | PASS | 0 |
| `FairworldForgeVertexContractTests` | PASS: layout, orientamenti voxel, Cube-Sphere, producer, ID e area UV | 0 |
| CTest mirato Forge dopo l’ultimo aggiornamento del test | PASS, 1/1 | 0 |
| `FairworldEditor.exe --validate-d12` | PASS, quattro gruppi D1.2 | 0 |
| `FairworldTextureCacheTests.exe` | PASS, 10/10; inclusi ID/layer e fallback ID 12 | 0 |
| CTest completo | FAIL: 15/17 test PASS | 8 |

Fallimenti CTest osservati:

- `FairworldScientificTests`: EXT-10 deterministico, 37/38; `okSave1=1`, `okLoad=1`, `okSave2=1`, `identical=0` (causa già isolata in `catalogGeneration`, fuori da questo perimetro).
- `FairworldLifecycleTests`: 11/13. RLC-01 watchdog oltre 30 s con `PBR=NotReady`, worker attivo; RLC-03 shutdown completato ma `PBRNotReady=YES`. LC-01–LC-08 e RLC-02/RLC-04/RLC-05 PASS.

Altre 15 suite CTest: PASS, inclusi Sculpt, UV, Texture Cache, Forge contract, Event, Noise, Async Input, Memory, Gameplay Stress, Diagnostics, Appearance, Custom Scientific, Binding, Authoring e Vulkan Recovery.

Il log CTest disponibile in `build_r3z7/Testing/Temporary/LastTest.log` corrisponde all’esecuzione filtrata successiva (solo Forge contract), non alla run completa. La run completa è stata acquisita nel risultato del comando e riassunta sopra; non è stato conservato il suo stdout integrale su file.

La sorgente GLSL `forge.vert` cambia il nome della variabile d’ingresso, ma la build non compila GLSL/SPIR-V: `CMakeLists.txt` copia il preesistente `forge_vert.spv`. Il nome non cambia la location o le istruzioni shader; il percorso GPU non è stato eseguito in questa sessione.

Il primo build tentativo aveva avuto un errore di tipo nel test, corretto prima della build completa finale. Un errore linker COFF su `TexturePacker.obj` apparso in quella prima build non si è riprodotto: la build completa successiva è terminata con exit code 0.

## 5. GPU, GUI e integrità sandbox

Le verifiche automatiche sono CPU/build; non equivalgono a una verifica dei pixel GPU. Non è stato acquisito alcuno screenshot e non si dichiara un confronto visivo.

Il preflight `Verify-OriginalInputs.ps1` ha restituito exit code 1 e segnala due hash diversi dal manifest:

| File originale | Hash atteso | Hash osservato |
|---|---|---|
| `assets/texture_cache.bin` | `BFFDC3DD710108999E3251C349A27F2B27F27923AFBBE4570F21120EE4107851` | `D1D8655D0B929F0367D8B67473ED36B88E1D1DC5517A543643590A6DEC8E621D` |
| `assets/test_packaging_artifacts/pkg_roundtrip_03.json` | `0F0FE0DC7F2BEB69BF229092628D8C9159CAB7CD3730C7F97C26B9B839BE36C5` | `9C91BBA3D20DA85A2EA9D1C46291371F73F5E2B43C012DA2A0873BAE3EC7E089` |

Il JSON risultava già modificato nel workspace prima di questa attività, ma per la cache non era stato registrato un hash iniziale specifico in questo turno. Non è quindi possibile attribuire causalmente le differenze ai comandi eseguiti ora o a modifiche precedenti. La sessione GUI sandbox è stata bloccata; nessun file è stato ripristinato, rimosso o sovrascritto da noi durante il controllo.

Stato Vulkan/ownership runtime: **NOT VERIFIED**, perché la GUI non è partita. `FairworldVulkanRecoveryTests` è PASS, ma non certifica l’ownership della sessione grafica.

## 6. Diff e rischio residuo

Diff del task limitato a layout/documentazione location 2, producer UV voxel/planetari/primitivi, nuovo helper e test. I diff preesistenti del workspace non sono stati ripuliti né inclusi intenzionalmente.

Rischi aperti:

1. Mancano verifica GPU, confronto visivo, layer Vulkan e gate ownership; il sandbox è bloccato dagli hash del manifest.
2. CTest completo resta non verde per EXT-10 e due timeout PBR Lifecycle.
3. Le UV canoniche LOD sono calcolate per direzione: ai confini delle facce la selezione deterministica della faccia può produrre discontinuità UV attese dal mapping cube-face; non è stata valutata visivamente.
4. Il test CPU verifica i mapping e i helper realmente invocati, ma non istanzia una scena Vulkan né esegue il mesher asincrono su un chunk caricato.

Non sono state eseguite correzioni a EXT-10/Lifecycle né modifiche fuori dal contratto vertex.

## 7. Stato

**INCOMPLETE / non pronto per certificazione visiva.** Le modifiche CPU e la build Debug sono verificate; il gate automatico complessivo resta FAIL e il gate GUI/GPU è BLOCKED dall’integrità degli originali. Nessun commit/push e nessuna dichiarazione di freeze D1.
