# D1.2.19d.5 — Unified Planetary Radial Mapping

## Esito

La convenzione verificata è quella già usata da `TerrainSolver`, `PlanetRadialMapping` e dal mesher planetario: l’origine radiale è il **centro del voxel locale 64** (`VoxelsY / 2`), i voxel sono centrati su coordinate `localY` intere e le facce sono a mezzo voxel dal centro.

La conversione collisione/spawn era rimasta su offset `25`, mentre i vertici planetari usavano `64`. Sul voxel Tiny di superficie `y=82`, il limite collisionale risultava a raggio `82`, ma la faccia superiore mesh CPU a `42.5`. Dopo l’allineamento, entrambi risultano a `42.5` (differenza `0` nel test, tolleranza `1e-4`).

È stata inoltre corretta la risoluzione del contatto nel solver fisico: la sweep collision fermava il body sull’ultimo passo libero, fino a `0.1` unità sopra la superficie; il tick successivo perdeva Grounded prima che la gravità lo riportasse in contatto. La correzione affina il segmento libero/solido e impedisce che la correzione geometrica step-up venga convertita in velocità outward. Gravità e impulso di salto non sono stati modificati.

**Esito CPU:** build target, D1.2 test e due run Tiny PASS. Il test idle mantiene Grounded 120/120 tick; caduta, salto e ritorno PASS. Nel tracciato walk si osserva un tick con Grounded falso durante la variazione locale del terreno, senza `TerrainPending`; il giocatore torna Grounded e non compare impulso radiale outward da collisione. La continuità Grounded in ogni singolo tick durante il movimento non è dichiarata PASS.

**GUI/GPU:** NOT VERIFIED, nessuna GUI avviata.

## Contratto matematico verificato

Per `S = ChunkDimensions::VoxelWorldSize`, `H = ChunkDimensions::VoxelsY`, `O = H/2 = 64`, layer `L`, coordinata di centro cella `y` e raggio nominale pianeta `R`:

```text
g(L, localY) = L*H + localY - O
r(L, localY) = R + g(L, localY)*S
```

Il centro del voxel `y` è `r(L,y)`; il suo limite radiale esterno è `r(L,y+0.5)`. Le celle occupano l’intervallo semiaperto `[y-0.5, y+0.5)`. La coordinata continua passata alla discretizzazione `floor()` è:

```text
cellCoord(r) = (r-R)/S + O + 0.5
voxelY = floor(cellCoord(r))
```

Al confine superiore esatto la coordinata appartiene alla cella successiva; il float immediatamente sotto resta nella cella corrente. Questa regola è verificata con `std::nextafter` su tutte le 128 celle.

### Prima

- `MapWorldGenerator::WorldToVoxelCoord`: `localY = (r-R)+25`, poi `floor(localY)`.
- `GameWorld::TryResolvePlanetarySpawn`: limite esterno `R + (y+1-25)*S`.
- Mesh/TerrainSolver: `g = L*128 + localY - 64`, con vertice superiore del voxel a `localY=y+0.5`.

### Dopo

- Query/collisione usa `PlanetRadialMapping::GetVoxelCellCoordinateFromDistance()` e `floor()`; il valore deriva da offset `64` e dall’intervallo centrato sul voxel.
- Spawn usa `GetCellOuterRadius(layer, y, R)`.
- `MapWorldGenerator::GetTrueSphericalPosition` delega alle conversioni radiali condivise.
- `TerrainSolver` e `PlanetaryMeshGenerator` continuano a usare il mapping condiviso `GetGlobalRadialOffset`/`GetRadialDistance`; non sono state cambiate facce, coordinate orizzontali o topologia.

Per Tiny (`R=24`) e il voxel solido `y=82`:

```text
centro cella: 24 + (82 - 64)       = 42.0
faccia superiore: 24 + (82.5 - 64) = 42.5
limite collisione/spawn:            42.5
differenza collider-mesh CPU:        0.0
```

## Modifiche

| File | Modifica |
| --- | --- |
| `src/world/PlanetRadialMapping.h/.cpp` | Nuovi helper condivisi per coordinata di cella da raggio, raggio del centro voxel e faccia superiore. |
| `src/world/MapWorldGenerator.cpp` | Rimosso l’offset radiale divergente `25` dalla query world→voxel e dal percorso spherical legacy di vertex positioning. |
| `src/world/GameWorld.cpp` | Spawn risolto usando la faccia superiore del voxel attraverso `PlanetRadialMapping`. |
| `src/systems/PhysicsEngine.cpp` | Raffinamento binario (8 iterazioni) sul segmento dell’impatto radiale verso un voxel solido; rimozione della sola velocità radiale entrante quando Grounded; step-up non converte l’altezza correttiva in impulso radiale. Nessuna modifica alla gravità. |
| `src/world/PlanetaryVoxelE2ETests.cpp` | Round-trip/celle/bordi `nextafter`, limiti del mapping, assertion collider/mesh, idle 120 tick, walk con rilevamento impulso, caduta e salto. Aggiornato il test OOB al dominio centrato. |

La rifinitura di contatto è una risposta alla sweep già bloccata da `SolidVoxel`, non un clamp del raggio pianeta: cerca il punto libero sul medesimo segmento prima della posizione che interseca il voxel. Lo step-up resta un aggiustamento di posizione; non diventa una spinta fisica.

## Risultati di validazione

Configurazione: MSVC / Visual Studio 18 2026, `build_r3z7`, Debug. Runner eseguito dal working directory sandbox `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime`, fixture `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\saves\map\world_map.json`. Tutti i log sono nel `D1_2_19D2_SANDBOX\logs`.

| Gate | Risultato | Evidenza |
| --- | --- | --- |
| Build Debug `FairworldPlanetaryVoxelE2ETests` | **PASS**, exit 0 | `cmake --build build_r3z7 --config Debug --target FairworldPlanetaryVoxelE2ETests -- /m:1` |
| Build Debug `FairworldEditor` | **PASS**, exit 0 | `cmake --build build_r3z7 --config Debug --target FairworldEditor -- /m:1` |
| Mapping radiale | **PASS** | 128/128 celle, round-trip massimo `0`; facce di cella e `nextafter` verificati |
| Tiny run 1 | **PASS**, exit 0 | `d19d5_tiny_run1.log` |
| Tiny run 2 | **PASS**, exit 0 | `d19d5_tiny_run2.log`; metriche e stable mesh hash identici al run 1 |
| `--validate-d12` | **PASS**, exit 0 | `d19d5_validate_d12.log`; tutti i quattro gruppi PASS |
| Collisione e mesh CPU | **PASS** | `collisionSurfaceRadius=42.5`, `voxelMeshTopRadius=42.5`, delta 0 |
| Grounded idle | **PASS** | 120 tick; `firstGroundedLossTick=-1`; `contactStable=1`; penetrazione massima `3.8147e-6` |
| Caduta radiale | **PASS** | 181 tick; gravità inward ogni tick; landing Grounded; clearance finale circa `7.25e-5` |
| Salto e ritorno | **PASS** | input consumato, apice `1.56016`, relanding true, clearance finale `7.63e-6` |
| Walk | **PASS limitato** | nessun outward velocity da collisione (massimo positivo `1.99e-7`, rumore float); re-grounded finale e zero pending. Grounded è falso al tick 1 mentre cambia la colonna di terreno: continuità tick-per-tick durante il walk non certificata. |
| Chunk/voxel/mesh Tiny | **PASS** | 54/54 chunk; TerrainSolver 54/54; 504.877 voxel non-Air; 54 mesh CPU; 135.820 triangoli; mesh hash stabile `0x20c0d276200e0e20` in entrambi i run |
| `git diff --check` sui file D1.2.19d.5 | **PASS**, exit 0 | Restano solo avvisi di conversione LF/CRLF |
| Integrità originali | **PASS** | 40/40 SHA-256 invariati rispetto al baseline immediatamente precedente. Le due divergenze storiche rispetto al manifest originario restano quelle già documentate; nessun rebaseline. |
| GUI / GPU | **NOT VERIFIED** | Nessuna GUI avviata; i risultati certificano CPU e mesh CPU soltanto. |

Il walk log registra `firstGroundedLossTick=1`, poi `lastGrounded=1`, `terrainPending=0`; non viene emesso alcun evento `WALK_OUTWARD_VELOCITY`. È quindi verificata l’assenza del lancio radiale della collisione, ma non una camminata con Grounded continuo su ogni campione della superficie procedurale.

## Riproducibilità e sicurezza

Comandi di test eseguiti:

```powershell
& 'D:\Visual Code\vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build_r3z7 --config Debug --target FairworldPlanetaryVoxelE2ETests -- /m:1
& 'D:\Visual Code\vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build_r3z7 --config Debug --target FairworldEditor -- /m:1
& 'D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldPlanetaryVoxelE2ETests.exe' --fixture 'D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\saves\map\world_map.json'
& 'D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldEditor.exe' --validate-d12
```

I due runner sono stati lanciati dalla working directory sandbox sopra indicata. Nessuna GUI, nessun asset o salvataggio originale è stato scritto; hash dopo i run: 40/40 invariati. Nessun commit o push.

## Valutazione

La discrepanza radiale è risolta nel contratto CPU: generazione/terrain, query collisione, spawn e mesh usano ora lo stesso centro voxel e la stessa faccia. Grounded è stabile in riposo su superficie solida; caduta, salto e ritorno sono ripetibili. Il fix del collisione impedisce che la discretizzazione dei passi lasci il body sospeso sopra il contatto o che lo step-up aggiunga velocità outward. Il walk reale sul fixture include un breve campione senza Grounded dovuto a una transizione di terreno e termina nuovamente grounded; non è una prova di camminata GPU né di continuità su ogni cella.

**D1.2.19d.5 CPU: PASS con la limitazione walk sopra documentata. GUI/GPU: NOT VERIFIED. D1 non è dichiarato FROZEN.**
