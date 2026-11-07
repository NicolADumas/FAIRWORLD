# FAIRWORLD P03d.3 — Deterministic Mapping & Voxel Parity Report

**Esito: PASS CPU — Debug/Release voxel e mesh CPU identici.**

**GPU/GUI: NOT VERIFIED.** Non sono state avviate GUI o Vulkan.

## 1. Safety gate

- HEAD verificato: `f9c7a3047bf3c8ecaad70de4f1af39943933eaf1`, invariato rispetto alla baseline P01.
- Working tree già dirty e con molti file untracked; tutti i contenuti preesistenti sono stati preservati. Nessun reset, clean, commit, push o rigenerazione CMake.
- I progetti MSBuild usati per la prova sono copie diagnostiche in `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime`. Sono stati rimossi i custom build CMake e i ProjectReference che scrivevano nel build tree originale. OutDir, IntDir, PDB, librerie ed eseguibili prodotti puntano al sandbox.
- Fixture in sola lettura: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json`.
- Nessun asset, serializer o salvataggio originale è stato modificato da questa fase.

## 2. Root cause numerica

### 2.1 FaceUVToDirection

Nel progetto MSVC effettivo `FairworldCore` dichiara `FloatingPointModel=Fast` sia per Debug sia per Release. Debug usa `/Od /Ob0`; Release usa `/O2 /Ob2`; entrambi usano `/arch:AVX2`, MSVC 14.51.36231 e C++ latest. Il Release abilita inoltre LTCG al link.

Per il witness A (`face=3,col=0,row=2,layer=0`, voxel locale `(13,71,3)`), con UV `0x1.155556p-2, 0x1.755556p-1`, il vettore cubico e la lunghezza quadratica erano uguali tra build. Il risultato di `glm::normalize` differiva nell'ultima componente:

| Configurazione | Direzione mappata (x,y,z) |
| --- | --- |
| Debug | `(-0x1.ada3ccp-1, -0x1.89d628p-2, -0x1.89d624p-2)` |
| Release | `(-0x1.ada3ccp-1, -0x1.89d628p-2, -0x1.89d626p-2)` |

Il witness B (`face=5,col=2,row=1,layer=0`, voxel locale `(12,71,7)`) mostrava una differenza analoga. I test con input UV identici sulle sei facce e sei risoluzioni hanno confermato che le trasformazioni veloci potevano produrre anche `-0` contro `+0`.

Una prima normalizzazione in `double` rendeva i due build uguali, ma cambiava la geometria di riferimento Debug: il risultato passava da 99.252 a 99.256 triangoli e il contratto mesh falliva. Questa variante è stata scartata.

La correzione finale mantiene la normalizzazione float tramite inverse square root, delimita la funzione con floating-point preciso e canonicalizza gli zeri con segno. Non cambia facce, UV, topologia o tolleranze.

### 2.2 OpenSimplex2/FBm

Dopo aver reso stabile la direzione, la traccia del witness A mostrava ancora input identici a `FastNoiseLite::GetNoise` ma un output noise differente di pochi ULP sotto il modello Fast. Sperimentare `/fp:precise` sul solo translation unit `Noise.cpp` in Release ha prodotto buffer voxel identici al Debug; lo stesso esperimento in Debug non ha modificato il buffer di riferimento.

Il vincolo è stato quindi localizzato all’inclusione dell’implementazione FastNoiseLite in `Noise.cpp`, senza cambiare formule, seed o parametri del noise.

### 2.3 Normali mesh planetarie

Dopo la parità voxel restava una differenza CPU mesh. Il primo campo divergente era `normal.x` del vertice 0 del chunk `(face=5,col=2,row=0)`:

- Debug: `0xBE6D32A2` (`-0.2316385`)
- Release: `0xBE6D3273` (`-0.2316378`)

La compilazione di prova precisa sul solo `VoxelChunkMesher.cpp` non cambiava il risultato. La compilazione precisa sul solo `PlanetaryMeshGenerator.cpp` riportava invece sia il mesh digest legacy sia il full runner al riferimento Debug. La divergenza era nel calcolo geometrico delle normali dentro `ConvertToPlanetaryPositions` (`cross`, lunghezza e normalizzazione), non nel culling voxel del mesher.

È stato applicato il vincolo preciso soltanto a `ConvertToPlanetaryPositions`; `VoxelChunkMesher.cpp` è rimasto invariato.

## 3. Modifiche

- `src/world/CubeSphereMapping.cpp`: normalizzazione deterministica float in `FaceUVToDirection`, con pragma MSVC locali e canonicalizzazione di `-0`/`+0`.
- `src/world/noise/Noise.cpp`: pragma locali intorno all’implementazione FastNoiseLite.
- `src/world/PlanetaryMeshGenerator.cpp`: pragma locali intorno a `ConvertToPlanetaryPositions`.
- Runner `src/world/PlanetaryVoxelE2ETests.cpp` (diagnostica/test): digest UV/direzione su 16.980 campioni, trace numerica opt-in, dump CPU voxel/mesh e chiamata esplicita al test FWB v8/v7.

Le modifiche preesistenti in `CubeSphereMapping.cpp` (helper chart e tie-break `UVToGrid`) sono state preservate e non fanno parte della correzione P03d.3.

## 4. Build e test eseguiti

Build Debug e Release di `FairworldCore` e `FairworldPlanetaryVoxelE2ETests` con MSVC 14.51.36231, usando i progetti copiati nel sandbox e `BuildProjectReferences=false`:

```text
MSBuild FairworldCore.vcxproj /t:Build /p:Configuration=Debug  /p:Platform=x64 /p:BuildProjectReferences=false /m:1
MSBuild FairworldCore.vcxproj /t:Build /p:Configuration=Release /p:Platform=x64 /p:BuildProjectReferences=false /m:1
MSBuild FairworldPlanetaryVoxelE2ETests.vcxproj /t:Build /p:Configuration=Debug  /p:Platform=x64 /p:BuildProjectReferences=false /m:1
MSBuild FairworldPlanetaryVoxelE2ETests.vcxproj /t:Build /p:Configuration=Release /p:Platform=x64 /p:BuildProjectReferences=false /m:1
```

| Gate | Debug | Release |
| --- | --- | --- |
| FairworldCore build | PASS, exit 0 | PASS, exit 0 |
| E2E runner build | PASS, exit 0 | PASS, exit 0 |
| Full Tiny runner | PASS, exit 0 | PASS, exit 0 |
| P03d.1 FWB v8 parity / v7 compatibility / corruption rejection | PASS | PASS |
| P03d.3 mapping test, 16.980 campioni | PASS | PASS |
| Shared production CPU mesh contract | PASS | PASS |

`--validate-d12` sull'eseguibile Debug già disponibile (`D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldEditor.exe`, non ricostruito in questa fase), con working directory sandbox: PASS, exit 0. Il runner finale, compilato contro le librerie P03d.3, chiama inoltre direttamente il controllo P03d.1; questo passa in entrambe le configurazioni.

I runner includono e riportano PASS per P02a coordinate, P02b compatibilità legacy, P02c vicinato cross-face, P02d slab/DDA/raycast, P03 collision characterization, P03a Grounded/availability, e i test CPU di spawn, grounding, movimento e regressioni P03d.2.

## 5. Parità voxel e mesh

Fixture TinySphere: 54 chunk attesi e generati; TerrainSolver completato su 54; 504.877 voxel non-Air, tutti BlockID 3 nel fixture; 54 mesh CPU non vuote.

- Mapping digest (16.980 campioni, sei taglie e sei facce): Debug e Release `0xd1d27989d363c027`.
- SHA-256 dump voxel Debug = Release: `F3D0B98BBCEDDE8CC64D21DEA3588E866FA24F9AB2138658215FD68E601F3615`.
- Confronto per-chunk: **54/54 uguali**, zero differenze di metadata o byte voxel.
- Risultato mesh: 297.756 vertici, 297.756 indici sequenziali, 99.252 triangoli in entrambi i build.
- SHA-256 dump mesh CPU Debug = Release: `267B0E8D6CA28A5A07B5BEFB97AA48D7EE366DCFA1E74965F37E510B13B7B8C6`; confronto byte-per-byte PASS.
- Legacy flat mesh digest ripristinato: 135.820 triangoli, `0x20c0d276200e0e20` in Debug e Release.
- Indici, posizioni, UV, normali, winding, associazione chunk/materiale: zero errori nel runner.

Manifest per i 54 hash voxel per chunk: `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p03d3-final-per-chunk-voxel-hashes.csv` (SHA-256 `301F484425EC3C56F3842249F8B3D7683EB3189435DCBE9F7AA22DB9D300B85B`).

## 6. Regressioni, limiti e sicurezza

- Nessuna modifica a TerrainSolver, VoxelChunkMesher, PhysicsEngine, FWB/MapDocument, renderer o shader.
- La precisione locale può costare più delle trasformazioni Fast; non è stato eseguito un benchmark dedicato di performance.
- I runner confermano la mesh CPU. `MeshComponent` pubblicato dal worker, upload DMA, draw Vulkan e GPU restano **NOT VERIFIED**.
- Nessuna GUI/GPU avviata. Nessun commit o push.
- `git diff --check` restituisce exit 2 per trailing whitespace già presente nelle modifiche dirty di `src/states/BlockMakerState.cpp` (righe 4652–4669 e 11563–11830). Nessuna delle righe modificate da P03d.3 è segnalata. Questo residuo non è stato alterato.

## 7. Decisione

**P03d.3 CPU: PASS.** La root cause è stata affrontata in tre punti numerici distinti e i buffer voxel e mesh CPU sono ora identici tra Debug e Release sul fixture richiesto, senza modificare gli hash di accettazione.

**GO per revisione P03d.4**, limitatamente al prossimo gate previsto. Non è un PASS GPU, non è un freeze e non certifica altri fixture o piattaforme/compiler diversi da MSVC 14.51.36231.
