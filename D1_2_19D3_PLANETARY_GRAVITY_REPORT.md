# D1.2.19d.3 — Planetary Gravity & Player Anchoring

**Esito:** CPU/headless **PASS** dopo correzioni localizzate. Verifica visuale GUI/GPU **NOT VERIFIED**: non è stata avviata la GUI. Nessun commit o push.

## Audit del percorso

```text
MapDocument / fallback
  → PlayState::Init: body e Transform iniziali; spawn planetario pending
  → PlayState::Update: sistemi giocatore, poi GameWorld::Update
  → TryResolvePlanetarySpawn: posizione verificata sul terreno voxel
  → PlayerMovementSystem: input tangenziale e impulso Space
  → PhysicsSystem / PhysicsEngine::StepSimulation
       ApplyGravity → drag → integrazione velocità → ResolveSphericalCollisions
       → Transform = body.position + radialUp * eyeOffset
  → CameraSystem: orientamento e view dal Transform
  → PlayState::Render: usa la camera aggiornata
```

- `PlayState::Init` crea il body e il Transform del player nello stesso registry dello stato. Il mondo dei chunk ha un registry distinto.
- `PlayState::Update` esegue i sistemi prima di `GameWorld::Update`; dopo l'update tenta la risoluzione dello spawn. Con `planetarySpawnPending=true`, movimento e fisica saltano il player fino alla disponibilità dei voxel. Alla risoluzione vengono assegnati body position e velocità zero.
- `PhysicsEngine::StepSimulation` applica, nell'ordine, gravità, drag, integrazione e collisioni. `ApplyGravity` usa un'accelerazione radiale centripeta inverse-square per la sfera logica. `ResolveSphericalCollisions` azzera Grounded a inizio tick e lo riattiva solo su un blocco solido durante una discesa.
- `PhysicsSystem::Update` è l'autorevole writer runtime del Transform: dopo la fisica lo aggiorna con la posizione del body più l'offset radiale degli occhi. `CameraSystem` gira dopo la fisica e aggiorna frame/orientamento. Il test CPU verifica la relazione body/Transform a ogni tick.
- I writer della posizione body nel percorso PlayState sono: inizializzazione, risoluzione dello spawn, teletrasporto UI; durante il gioco ordinario il writer è la fisica. Il teletrasporto assegna anche il Transform, che la fisica riallinea al successivo tick. Non è emerso un writer concorrente del movimento ordinario.

Riferimenti callsite: `PlayState::Init` e setup fallback in `src/states/PlayState.cpp:160-234`; ordine e risoluzione pending in `src/states/PlayState.cpp:367-372, 471-519`; teletrasporto UI in `src/states/PlayState.cpp:670-699`; `PlayerMovementSystem` e modalità Creative/Space in `src/systems/Systems.cpp:127-219`; `PhysicsSystem` e sync Transform in `src/systems/Systems.cpp:222-259`; gravità/integratore in `src/systems/PhysicsEngine.cpp:12-140`; collisione sferica e Grounded su `SolidVoxel` in `src/systems/PhysicsEngine.cpp:737-876`; policy fallback in `src/world/PlanetarySpawnPolicy.h:8-20`; regressione integrata in `src/world/PlanetaryVoxelE2ETests.cpp:785-902`.

## Cause dimostrate e correzioni

### Spawn temporaneo non ancorato

Il fallback precedente partiva da `(0, 150, 0)` e non marcava lo spawn pending. La posizione non era calcolata rispetto al raggio del pianeta; inoltre, una mappa sferica senza spawn esplicito usava `R + 150` ma iniziava a simulare subito, senza verificare il terreno. Su Tiny la gravità inverse-square è debole a quella distanza e l'aggiornamento della camera poteva rendere visibile il player lontano dal terreno.

È stato introdotto `PlanetarySpawnPolicy::MakePlanetaryFallbackSpawn`: produce un punto temporaneo esterno `direction * (R + safeAltitude)` e lo marca pending. PlayState applica il piano sia al fallback generale sia alla mappa senza spawn valido; il player resta fermo finché `TryResolvePlanetarySpawn` non trova la superficie reale. Gli spawn espliciti restano risolti dal percorso voxel esistente. Non sono stati aggiunti teletrasporti per tick.

### Salto su Tiny e gravità locale

La formula esistente rimane invariata: `g(r) = 9.81 * (R / max(r, 0.1R))²`, diretta verso il centro. Nel fixture Tiny il raggio Cube-Sphere nominale è 24, mentre la superficie voxel misurata dal resolver è circa 82. Quindi l'accelerazione locale è circa `0.84 m/s²` sulla superficie. Applicare direttamente `jumpForce=5.5 m/s` produceva un'apice misurato di `21.95` unità e, nei primi 10 secondi simulati, nessun rientro.

Il salto in Survival ora scala solo l'impulso verticale con `sqrt(localGravity / 9.81)`, usando l'accelerazione dell'ultimo tick. Ciò preserva l'altezza di salto prevista dal valore `jumpForce` a gravità terrestre senza alterare la gravità, il drag, il solver delle collisioni o la modalità Creative. La formula di gravità costante provata durante l'isolamento è stata scartata: ha fatto fallire la regressione di attraversamento del terreno e non è rimasta nel codice.

### Modalità Creative

`PlayState` alterna `isCreativeMode` con F1; `PlayerMovementSystem` copia tale stato in `RigidBody::isFlying`. In Creative la gravità è intenzionalmente esclusa e Space aggiunge velocità lungo l'Up locale: tenere Space può allontanare il player dalla superficie. Il default del modello Player è Survival (`isCreativeMode=false`), ma lo stato effettivo nella sessione GUI segnalata non è disponibile e resta **NOT VERIFIED**.

## Modifiche

- `src/world/PlanetarySpawnPolicy.h`: policy condivisa del fallback sicuro e pending.
- `src/states/PlayState.cpp`: inizializza fallback e mappe senza spawn valido come pending; attende la risoluzione sul terreno voxel.
- `src/systems/Systems.cpp`: normalizza l'altezza di salto alla gravità radiale locale; a `9.81 m/s²` il comportamento resta quello del valore `jumpForce` esistente.
- `src/world/PlanetaryVoxelE2ETests.cpp`: test integrato di caduta, gravity inward a ogni tick, contatto Grounded, Space attraverso `InputState`, salto, rientro e sincronizzazione body/Transform. La finestra massima del test è 40 s per coprire le traiettorie Tiny; il test esce prima al landing.
- `src/systems/PhysicsEngine.cpp`: nessuna modifica funzionale finale.

## Risultati effettivi

| Verifica | Esito | Exit code / osservazione |
| --- | --- | ---: |
| Debug `ALL_BUILD` MSVC x64 (`build_r3z7`) | **PASS** | 0 |
| Tiny CPU E2E, fixture sandbox | **PASS** | 0, 2.421 s |
| JobSystem contract | **PASS** | 10/10, 0 |
| DMA result contract | **PASS** | 14/14, 0 |
| VRAM allocator contract | **PASS** | 7/7, 0 |
| Transfer/Graphics Sync contract | **PASS** | 6/6, 0 |
| Forge Vertex contract | **PASS** | 0 |
| `FairworldEditor.exe --validate-d12` | **PASS** | 0 |
| `git diff --check` sui file modificati in questo intervento | **PASS** | 0; solo avviso Git di conversione LF/CRLF |
| GUI, camera visuale, Vulkan GPU | **NOT VERIFIED** | GUI non avviata |

Osservazioni Tiny nel log `D1_2_19D2_SANDBOX/logs/tiny_gravity_anchoring_final.log`:

- 54 chunk generati; TerrainSolver completato; 504.877 voxel non-Air.
- Caduta: 324 tick; accelerazione centripeta a ogni tick; Grounded raggiunto; clearance `0.039917`.
- Space consumato; apice `1.53961`; rientro confermato; clearance finale `0.00437927`.
- Sincronizzazione body → Transform → posizione camera: PASS a ogni tick.

Le altre suite e i log completi sono in `D1_2_19D2_SANDBOX/logs/d19d3_*.log`. La prima prova diagnostica a 10 secondi è conservata in `tiny_gravity_anchoring.log`: mostrava apice `21.9515` e mancato rientro entro la finestra di 10 secondi; ha motivato l'adeguamento del salto e del test.

## Integrità e limiti

- Esecuzioni headless svolte da `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime`; fixture letto dal sandbox. Nessuna sessione GUI e nessuna scrittura intenzionale nei salvataggi originali.
- Hash originali: **40/40 invariati** rispetto al baseline immediatamente precedente D1.2.19d.2. Contro il manifest originale risultano **38 MATCH** e le **2 divergenze storiche** già registrate: `assets\texture_cache.bin` e `assets\test_packaging_artifacts\pkg_roundtrip_03.json`. Nessun rebaseline o ripristino.
- Manifest post-run: `D1_2_19D2_SANDBOX/logs/originals_post_d19d3.csv`; confronto col manifest originale: `D1_2_19D2_SANDBOX/logs/originals_manifest_comparison_d19d3.csv`.
- La prova headless invia l'azione tramite `DeviceManager::GetInput().isJumping`; verifica il ramo di consumo Space e la fisica, non la cattura reale della tastiera né la sessione GUI.
- Non sono certificati rendering GPU, orientamento visivo della camera, stato Creative runtime, shutdown Vulkan o collisione in tutte le regioni del pianeta.

**Valutazione:** i difetti del fallback e dell'eccessiva altezza/tempo di volo del salto Survival sono corretti e coperti da test CPU. Per il sintomo della sessione originale, la modalità Creative attiva resta una possibile spiegazione del moto verso l'esterno tramite Space, ma non è provata. La verifica GUI autorizzata dall'utente resta necessaria per classificare il comportamento visivo. **D1 non è dichiarato FROZEN.**
