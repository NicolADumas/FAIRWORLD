# FAIRWORLD P02b — Coordinate Canonicalization & Legacy Compatibility

**Data:** 2026-11-05  
**Esito:** PASS per centralizzazione compatibile; NO-GO per dichiarare risolte le divergenze Raycast/mesher, che restano fuori ambito.

## 1. Safety gate e provenienza

- HEAD invariato: `f9c7a3047bf3c8ecaad70de4f1af39943933eaf1`.
- La baseline P01 era già dirty; il working tree è stato preservato, senza reset, clean, checkout, commit o push.
- I 18 hash critici registrati in P01 sono stati ricontrollati: 17/18 coincidono. `src/world/MapWorldGenerator.cpp` differisce perché è il file modificato nel lavoro; questo file era già dirty alla baseline P01. Gli altri 17 hash coincidono.
- Il fixture Tiny è stato soltanto letto; SHA-256 post-run: `47AF4358E4BD663926E64A43ACF2B116F3A2939BA5605FC89B2983D32A9AD503`, uguale all'hash registrato nei report P02a e D1.2.18.
- Build e log del runner sono stati scritti sotto `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime`. Nessuna GUI, GPU, cache CMake rigenerata o salvataggio originale usato.
- `CMakeCache.txt` di `build_r3z7` conserva la data `2026-11-03 17:36:04`; non è stata eseguita una riconfigurazione.

## 2. File interessati

| File | Intervento |
|---|---|
| `src/world/MapWorldGenerator.cpp` | Sostituita la proiezione sferica duplicata con helper Cube-Sphere e adattatori del net legacy in `GetSphericalChunkTransform`, `GetTrueSphericalPosition`, `WorldToVoxelCoord` e `GetChunkCoordFromPosition`. |
| `src/world/PlanetGridCompatibility.h` | Estesi gli adattatori tra chiave net 3×2, `PlanetChunkCoord`, chart continuo e UV. Il file era già non tracciato prima di P02b; non è stato sostituito né rinominato. |
| `src/world/PlanetaryVoxelE2ETests.cpp` | Estesi i test deterministici di coordinate per validare gli adattatori, le sei taglie e la compatibilità radiale/legacy. Il runner era già presente nel working tree. |

La generazione di entità, le API d'identità, il renderer, raycast, collisioni e mesher non sono stati modificati in questa fase. In `Generate`, la conversione legacy→riga canonica e la creazione con chiave esplicita erano già presenti nel baseline dirty e sono state conservate.

## 3. Contratto centralizzato

Per risoluzione `N`, il net legacy mantiene la disposizione 3×2 e le sue chiavi:

```text
faceColumn = globalX / N
faceRow    = globalZ / N
face       = faceColumn + 3 * faceRow
globalX    = faceColumn * N + col
globalZ    = faceRow * N + (N - 1 - canonicalRow)
```

La riga canonica è `canonicalRow = N - 1 - legacyRow`; la conversione rifiuta risoluzioni non positive, facce fuori intervallo e coordinate net fuori dominio. L'adattatore UV interpreta le coordinate locali legacy come frazioni della cella: `u=(col+localX)/N`, `v=(canonicalRow+1-localZ)/N`.

La chart continua è convertita nelle unità legacy dei centri cella con:

```text
globalX = faceColumn*N + ((chartX+1)*N/2 - 0.5)
globalZ = faceRow*N    + ((chartY+1)*N/2 - 0.5)
```

La direzione selezione faccia e la proiezione faccia/UV/chart delegano a `CubeSphereMapping`. Le coordinate net persistenti e le chiavi del `ChunkManager` restano quelle legacy; non è stata introdotta una migrazione.

### `GetChunkCoordFromPosition`

Ricerca dei consumer: definizione/dichiarazione e il test E2E; nessun consumer produttivo aggiuntivo in `src`. Il metodo conserva il suo discretizzatore faccia-specifico legacy (`floor`, `ceil`, clamp e ordine delle coordinate); è stata sostituita solo la matematica duplicata che proietta la direzione in chart. Il digest dei 54 centri Tiny è rimasto `0x8429a4e135752c13`.

`WorldToVoxelCoord` mantiene conversione flat, offset dei centri e coordinata radiale; la proiezione sferica ora usa Face/UV/chart canonici. La differenza già misurata fra i due metodi legacy (36/54 d'accordo, 18/54 discordanti) rimane intenzionalmente visibile e non è stata mascherata cambiando la semantica pubblica.

### Riferimento radiale legacy

`PlanetRadialMapping` usa `ReferenceVoxelOffset=64`, mentre `GetTrueSphericalPosition` riceveva storicamente coordinate con `localY=25` sul guscio di riferimento (`R + localY - 25`). Una sostituzione diretta avrebbe spostato la geometria di 39 unità. Il percorso usa ora l'helper radiale dopo un adattamento esplicito dell'origine:

```text
canonicalLocalY = localY + (ReferenceVoxelOffset - 25)
g = GetGlobalRadialOffset(0, canonicalLocalY) = localY - 25
radius = GetRadialDistance(g, R)
```

È stata aggiunta una regressione sui valori `localY=25` e `localY=0`; il mapping radiale canonico non altera il risultato numerico legacy di questa API.

## 4. Test e risultati

Fixture: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json`  
Working directory: `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime`.

| Gate | Comando/configurazione | Risultato |
|---|---|---|
| Build `FairworldCore` Debug isolata | `& 'D:\Visual Code\vs\MSBuild\Current\Bin\MSBuild.exe' 'D:\FAIRWORLD\FAIRWORLD\build_r3z7\FairworldCore.vcxproj' /m:1 /v:minimal /p:Configuration=Debug /p:OutDir='D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p02b-core-bin\' /p:IntDir='D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p02b-core-obj\' /p:BuildProjectReferences=false` | PASS, exit 0. `MapWorldGenerator.cpp` ricompilato. |
| Build runner Debug isolata | `& 'D:\Visual Code\vs\MSBuild\Current\Bin\MSBuild.exe' 'D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p02b-test-project-diag2\FairworldPlanetaryVoxelE2ETests.vcxproj' /m:1 /v:minimal /p:Configuration=Debug /p:OutDir='D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p02b-linked-diag2-bin\' /p:IntDir='D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p02b-linked-diag2-obj\' /p:BuildProjectReferences=false` | PASS, exit 0. |
| E2E Tiny | `FairworldPlanetaryVoxelE2ETests.exe --fixture <fixture>` dal runtime sandbox | PASS, exit 0; ripetuto dopo la build finale. |
| Coordinate e topologia P02a | Incluso nello stesso runner | PASS: 13.956 round-trip Face/UV/direction; 13.956 conversioni net; 13.956 campioni generator; 6/6 invarianti `2*extent+1=N`; 2.112 relazioni edge reciproche. |
| Adattatori P02b | Incluso nel controllo deterministico su tutte le celle di tutte le taglie | PASS: round-trip net↔`PlanetChunkCoord`, identità PlanetID/layer, UV-centro e chart-centro coerenti. |
| `GetChunkCoordFromPosition` | Digest dei 54 centri Tiny | PASS compatibilità: `0x8429a4e135752c13`, identico al baseline pre-patch. |
| Tiny generation/mesh CPU | Incluso nello stesso runner | PASS: 54 chunk, 504.877 voxel non-Air, 54 risultati mesh CPU, 135.820 triangoli, hash mesh `0x20c0d276200e0e20` invariato, zero indici/UV/normali invalidi e zero mismatch winding. GPU NOT VERIFIED. |
| Regressioni radiali e locomozione d5/d6 | Incluse nel runner | PASS CPU secondo i gruppi del runner; non costituisce verifica GUI/GPU. |
| Diff whitespace | `git diff --check -- src/world/MapWorldGenerator.cpp` + scansione helper/test | PASS per i file P02b. Il controllo completo del repository fallisce su spazi finali preesistenti in `src/states/BlockMakerState.cpp` (righe 4652–4669, 11563–11571, 11806, 11828, 11830); quel file non è stato modificato da P02b. |

Gli output dettagliati sono nel sandbox, fra cui `p02b-core-final-build_20261105.log`, `p02b-runner-final-build_20261105.log` e `p02b-compat-final_20261105.log`.

MSBuild ha riportato warning `MSB8028` sulle directory intermedie isolate condivise dal progetto e `GLM_GTX_hash requires C++11 standard library support`; non hanno impedito la build (exit 0). Gli artefatti di build sono rimasti nel sandbox.

## 5. Regressioni e problemi aperti

- Le 18/54 divergenze fra `WorldToVoxelCoord` e la discretizzazione pubblica legacy `GetChunkCoordFromPosition` restano documentate; i due metodi hanno contratti di discretizzazione diversi e non sono stati resi semanticamente intercambiabili.
- Il runner P02a continua a osservare discrepanza tra le query Raycast e la query voxel autorevole (solo 1/6 hit `CastVoxel` nei probe riportati); P02b non modifica raycast.
- Il test P02a riporta 70/72 vicinati topologici cross-face non consegnati correttamente al mesher; P02b non modifica vicinato o mesher.
- La build isolata verifica CPU e link del runner, non rendering GPU, fisica GUI o salvataggi serializzati.
- Il working tree rimane sporco per modifiche preesistenti. Questo rapporto non riassegna la provenienza dei diff P01 né certifica altre integrazioni.

## 6. GO / NO-GO per P02c

**GO limitato per iniziare P02c sul contratto cross-face del mesher**, partendo dall'evidenza misurata di 70/72 relazioni mancanti. La conversione P02b conserva le chiavi legacy e produce coordinate canoniche validate per tutte le taglie/celle; P02c può quindi concentrarsi sull'adattamento topologico e sui dati consegnati al mesher. Questo non autorizza P02d né risolve i difetti Raycast.

Non avviare automaticamente P02c/P02d. Nessun sistema è dichiarato FROZEN.
