# FAIRWORLD P03a — Collision Contract Report

## Esito

**P03a: NO-GO per P04.** Il dispatch fra mondo logico sferico e legacy cartesiano è stato corretto e i gate CPU Debug passano. Restano però due limiti che impediscono di chiudere il contratto: il fixture sintetico upper-body-only non è riuscito a isolare un contatto senza supporto dei piedi, e `QueryBlockCartesian` non distingue chunk Missing da chunk Pending. Inoltre la suite Release completa fallisce un controllo di compatibilità delle coordinate prima di raggiungere P03a.

Non sono state eseguite GUI o prove GPU. Non sono state modificate le semantiche di Grounded o delle query di streaming.

## Safety gate e baseline

- HEAD verificato: `f9c7a3047bf3c8ecaad70de4f1af39943933eaf1`, uguale al commit riportato dalla baseline P01.
- Working tree già dirty e con molti file untracked prima di P03a; le modifiche preesistenti sono state preservate. Nessun reset, clean, commit o push.
- Build usata: `build_r3z7`, Visual Studio/MSVC già installati. Le compilazioni P03a hanno sovrascritto solo output temporanei sotto `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p03a-*`; nessuna riconfigurazione CMake.
- Fixture Tiny letto: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json`, SHA-256 `47AF4358E4BD663926E64A43AC2FB116F3A2939BA5605FC89B2983D32A9AD503`, uguale all'hash registrato in precedenza.
- `git diff --check` sui tre file interessati: PASS, exit code 0. Git ha avvertito solo della normalizzazione LF→CRLF per `PhysicsEngine.cpp`.

## Contratto Grounded osservato

Nel percorso sferico `ResolveSphericalCollisions` considera il corpo supportato quando una sweep radiale verso il centro incontra `SolidVoxel`; quindi imposta `isGrounded=true` e usa `localUp` come normale di contatto. La query del voxel non fornisce una normale geometrica della faccia. La condizione non verifica direttamente che il contatto sostenga la parte inferiore del collider.

Il contratto raccomandato è: Grounded richiede un contatto con voxel solido nel footprint sotto i piedi e una normale di supporto con componente positiva sufficiente lungo l'Up radiale locale. Contatti laterali e al soffitto non sono supporto; Air, OutOfBounds, Missing, Pending e input invalido non confermano Grounded. La tolleranza e la soglia angolare vanno definite insieme a un test che eserciti davvero il contatto obliquo.

Non ho cambiato la regola Grounded: la ricerca P03 precedente non aveva trovato falsi positivi in 8.181 candidati e il fixture upper-body-only di P03a si è rivelato non valido. Il risultato è NOT VERIFIED, non una dimostrazione dell'assenza di difetto.

## Terrain availability

`WorldVoxelQueryStatus` distingue `SolidVoxel`, `Air`, `PassableVoxel`, `OutOfBounds`, `TerrainPending` e `InvalidInput`. Nei rami sferico e cartesiano di `GameWorld::QueryBlockCartesian`, sia un chunk non presente nel `ChunkManager` sia un chunk presente ma non generato producono `TerrainPending`. Non esiste uno stato Missing osservabile dal consumer.

La collisione sferica tratta Pending come movimento non libero: la posizione del passo non viene avanzata e Pending non imposta Grounded. L'API non espone però se il chunk è assente o in caricamento; inoltre l'integrazione fisica può aver già aggiornato la velocità. Perciò una politica deterministica e distinta per Missing/Pending richiede una nuova informazione o API condivisa. In P03a non è stata cambiata `QueryBlockCartesian` né la semantica di streaming.

| Stato / fixture | Evidenza | Esito |
| --- | --- | --- |
| Air, PassableVoxel, OutOfBounds, InvalidInput | Query sintetiche: status rispettivamente `1, 2, 3, 5` | PASS |
| Chunk non generato | Query `TerrainPending`; collisione segnala pending e non Grounded | PASS |
| Chunk rimosso | Query produce lo stesso `TerrainPending` del chunk non generato | Comportamento osservato; distinzione Missing NOT AVAILABLE |

## Fixture e risultati

I test sintetici sono nel runner CPU `FairworldPlanetaryVoxelE2ETests`; i fixture creano dati in memoria e non scrivono mappe o salvataggi.

| Caso | Misura | Esito |
| --- | --- | --- |
| Pavimento radiale uniforme | Grounded 1, supporto sotto i piedi 1, contatti voxel 1 | PASS |
| Bordo del supporto | Rimossi i voxel solidi sotto il footprint; Grounded 0, supporto 0 | PASS, distacco consentito |
| Spigolo / corner planetario | Scenario `cube_corner` della matrice P03, nessuna penetrazione densa oltre tolleranza | PASS CPU |
| Parete cartesiana | Contatto osservato durante il tick; massimo X `11.71`, prima del muro X=12 | PASS |
| Gradino voxel | Posizione finita e corpo non oltre il blocco, Grounded 1 | PASS per non penetrazione; step-up automatico non è richiesto né attestato |
| Soffitto cartesiano | Massimo Y `1.21` sotto la geometria a Y=3; nessuna penetrazione | PASS |
| Upper-body-only/overhang sferico | Fixture effettivo ha supporto ai piedi e termina Grounded a raggio `44.5`; non isola il caso previsto | NOT VERIFIED |
| Seam Cube-Sphere | Scenario `cross_face` e P02c: vicinato topologico consegnato 72/72 | PASS CPU |
| Penetrazione iniziale | Test P03: overlap radiale iniziale di 1 m recuperato; unresolved 0 | PASS CPU |
| Supporto rimosso | Quattro voxel del footprint rimossi; Grounded passa da 1 a 0 | PASS |
| Pending | Grounded 0 e contatore Pending attivo | PASS |

## Collision dispatch

**Difetto confermato:** `StepSimulation` e `ResolveCollisions` selezionavano il percorso sferico dalla sola presenza di `PlanetGeometryComponent`; `ResolveCollisions` ignorava `isLogicalSphere`. Ciò mandava un mondo legacy cartesiano sul solver radiale.

Fixture prima della correzione: con `isLogicalSphere=false`, body da `(10.5, 1.1, 10.5)` terminava a `(11.7737, 1.20038, 11.7737)`, Grounded 0. Dopo la modifica, termina a `(10.5, 0.99, 10.5)`, Grounded 1.

Correzione minima in [PhysicsEngine.cpp](/D:/FAIRWORLD/FAIRWORLD/src/systems/PhysicsEngine.cpp:24): entrambi i dispatch usano ora `PlanetGeometryComponent::isLogicalSphere`. Gravità e drag già usavano questo flag. La regressione cartesiana è PASS.

## Benchmark CPU

Runner diagnostico con contatori attivi, 54 chunk Tiny sintetici e la matrice di 14 workload P03. I tempi query sono misurati dentro `QueryBlockCartesian`; `solverNsApprox` è il tempo del tick meno il tempo query. Il timing per query introduce overhead, soprattutto Debug: i valori sono baseline caratterizzative, non benchmark di produzione senza strumentazione.

| Scenario | Tick | Debug totale / query / solver approx | Release totale / query / solver approx |
| --- | ---: | ---: | ---: |
| Idle | 30 | 20.464 / 19.257 / 1.208 ms | 0.457 / 0.300 / 0.158 ms |
| Caduta radiale | 30 | 23.758 / 22.619 / 1.139 ms | 0.505 / 0.387 / 0.118 ms |
| Camminata tangenziale | 30 | 50.205 / 48.204 / 2.001 ms | 0.997 / 0.743 / 0.255 ms |
| Diagonale | 30 | 58.265 / 56.040 / 2.225 ms | 1.142 / 0.865 / 0.277 ms |
| Bordo cella | 20 | 35.875 / 34.491 / 1.385 ms | 0.708 / 0.533 / 0.175 ms |
| Seam | 30 | 48.836 / 46.915 / 1.921 ms | 0.950 / 0.717 / 0.233 ms |
| Corner | 20 | 34.288 / 32.970 / 1.317 ms | 0.697 / 0.528 / 0.170 ms |
| Velocità 60 m/s | 1 | 4.626 / 4.482 / 0.144 ms | 0.091 / 0.072 / 0.019 ms |
| Velocità 200 m/s | 1 | 13.689 / 13.357 / 0.331 ms | 0.269 / 0.209 / 0.060 ms |

Per il tick di recovery da overlap iniziale, la diagnostica P03 registra 162 probe nel runner Tiny e nessuna penetrazione residua. I conteggi per fase sono stampati in `[P03][CASE]`: `recoveryProbes` comprende la verifica iniziale della collisione a ogni tick e i passi di recupero; `refinementProbes` conta le query del raffinamento del contatto radiale. Il Debug full-suite ha raggiunto massimo 2.448 probe/tick e 14.326 ms nel workload high-speed, con contatori temporali attivi. Release sullo stesso workload sintetico: 2.448 probe e 0.269 ms. Questi numeri non giustificano ancora un'ottimizzazione, ma il costo è dominato dalle query voxel.

## Modifiche

- [PhysicsEngine.cpp](/D:/FAIRWORLD/FAIRWORLD/src/systems/PhysicsEngine.cpp:24): dispatch cartesiano/sferico ora coerente con `isLogicalSphere`.
- [PhysicsEngine.cpp](/D:/FAIRWORLD/FAIRWORLD/src/systems/PhysicsEngine.cpp:788) e [PhysicsEngine.h](/D:/FAIRWORLD/FAIRWORLD/src/systems/PhysicsEngine.h:73): misurazione query e conteggi recovery/refinement opt-in tramite `dbg_countCollisionProbes`; inattivi nel normale percorso.
- [PlanetaryVoxelE2ETests.cpp](/D:/FAIRWORLD/FAIRWORLD/src/world/PlanetaryVoxelE2ETests.cpp:1699): fixture Grounded/availability/dispatch e modalità `--p03a-characterize`.

I tre file avevano modifiche precedenti P02b–P03 già presenti nella working tree. Questa patch si limita alle nuove righe P03a descritte sopra e non riscrive quelle modifiche.

## Build e regressioni

| Comando / target | Esito | Exit code |
| --- | --- | ---: |
| MSBuild `FairworldCore.vcxproj`, Debug, output sandbox | PASS | 0 |
| MSBuild runner CPU Debug | PASS | 0 |
| Full `FairworldPlanetaryVoxelE2ETests.exe --fixture ...`, Debug, prima dei test finali PassableVoxel | PASS | 0 |
| Full runner Debug dopo l'ultima aggiunta P03a | PASS | 0 |
| `--p03a-characterize`, Debug | PASS | 0 |
| MSBuild `FairworldCore.vcxproj`, Release, output sandbox | PASS | 0 |
| MSBuild Jolt Release, output sandbox | PASS | 0 |
| MSBuild runner CPU Release | PASS | 0 |
| `--p03a-characterize`, Release | PASS | 0 |
| Full runner Tiny, Release | **FAIL** al gate P02a `GetChunkCoordFromPosition legacy output digest preserved` | 1 |
| `git diff --check` sui file P03a | PASS | 0 |

Il gate Release osserva 24/54 agreement e digest `0x6b920240dd9b4801`; Debug osserva 36/54 e digest `0x8429a4e135752c13`, quello atteso dal test. Le sei probe centrali QueryBlock e CastVoxel restano solide/hit in Release. La causa numerica precisa della divergenza Release non è stata isolata in P03a; il controllo non è stato aggirato e il Release full runner non ha raggiunto i test successivi.

La suite Debug completa certifica P02a coordinate, P02c seam 72/72, P02d ray/slab, P03 14 scenari, overlap d1m, Tiny 54/54, radial mapping e locomozione d5/d6. Il runner riporta GPU publication NOT VERIFIED. Non è stato eseguito CTest completo né un build ALL_BUILD.

## Rischi residui e piano minimo

1. Correggere o caratterizzare separatamente il test/algoritmo della divergenza di mapping Debug/Release. Non cambiare il suo digest atteso senza spiegare i risultati.
2. Preparare un fixture sferico upper-body-only verificabile prima di decidere se Grounded richiede un filtro per normale/supporto; nessuna correzione Grounded è giustificata finora.
3. Formalizzare Missing rispetto a Pending con il proprietario dello streaming, quindi definire cosa accade alla velocità mentre la query non è risolta. Non cambiare ora `QueryBlockCartesian`.
4. Ripetere benchmark Release dopo la risoluzione del gate Release e con una build non strumentata per una misura prestazionale rappresentativa.

**Decisione: NO-GO P04** finché i punti 1–3 non sono verificati o formalmente accettati come limitazioni. Nessuna valutazione GUI/GPU è stata fatta e nessun sistema è dichiarato FROZEN.
