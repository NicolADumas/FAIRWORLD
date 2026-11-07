# D1.2.19d.6 — Planetary Player Collision Continuity

## Esito

**CPU: PASS per la regressione d'intersezione al bordo, per il cammino sul patch uniforme misurato, per il percorso a timestep variabile e per la suite Tiny E2E.**

**GUI/GPU: NOT VERIFIED.** Questa fase non ha avviato la GUI, come richiesto. Nessun risultato CPU certifica il rendering o il comportamento interattivo su GPU.

La causa riprodotta era una copertura angolare insufficiente del footprint cilindrico: i nove punti esistenti potevano lasciare passare una porzione del collider dentro un voxel durante un attraversamento tra facce. La correzione aggiunge otto campioni a metà angolo sul perimetro interno del collider. Non cambia gravità, salto, mapping radiale, posizione del body o regole di Grounded.

## Percorso fisico controllato

Il percorso seguito è:

`PlayerMovementSystem → PhysicsSystem::Update → PhysicsEngine::StepSimulation → Integrate → ResolveCollisions → ResolveSphericalCollisions → GameWorld::QueryBlockCartesian`.

Il movimento richiesto è proiettato sul piano tangente nel frame planetario persistente. Il solver separa `dy` radiale da `dx/dz` tangenziali e avanza con incrementi massimi di 0,1 unità. `Grounded` viene impostato solo quando una sweep radiale verso il basso incontra `SolidVoxel`; `TerrainPending` e `OutOfBounds` non vengono considerati contatti solidi. Una discesa oltre un gradino può quindi togliere Grounded legittimamente.

Prima della correzione, ogni query del cilindro usava quattro altezze e nove punti in footprint: centro, quattro punti cardinali a raggio `r` e quattro diagonali a `0,707r`. Questo campionamento non copriva l'intera circonferenza.

## Causa e prova A/B

La diagnostica CPU campiona il volume del collider con tre raggi (0, 0,5r, 0,95r), 16 angoli e passi verticali di circa 0,1. Il fixture Tiny ha risoluzione 3, 54 chunk e superficie con top voxel tra 66 e 87.

Sul percorso corretto `+Z → +X`, con il solver originale a nove punti, il runner ha trovato il voxel `104,69,23` al campione `ring=0,95`, `angleIndex=11` (247,5°). Quel punto non apparteneva al set dei nove probe e risultava dentro il voxel per **0,998573 unità**, mentre il corpo era segnalato grounded. Il punto radiale centrale era sulla superficie (`radialSurfaceClearance=0`): l'intersezione avveniva sul perimetro del collider, non per un offset del centro o per la gravità.

Con otto probe aggiuntivi a raggio `0,95r` e angoli intermedi `22,5° + 45°·i`, lo stesso percorso attraversa la faccia 0→2 (+Z→+X), finisce grounded e la scansione densa rileva **0 voxel solidi nel volume del collider** e **0 penetrazione**. Il confronto è stato eseguito sullo stesso fixture e con la stessa traiettoria.

Un primo esperimento, poi scartato, usava un segno yaw errato nel runner e un ultimo timestep quasi nullo; i relativi risultati non sono usati come evidenza né come criterio d'accettazione.

## Modifiche

- `src/systems/PhysicsEngine.cpp`: conserva i nove probe storici e aggiunge otto punti a metà angolo a `0,95r`, per ciascuna delle quattro altezze del cilindro. L'incremento massimo per query passa da 36 a 68 campioni (circa +89% nel caso in cui tutti i probe vengano visitati); il costo prestazionale non è stato misurato in modo isolato.
- `src/systems/PhysicsEngine.h/.cpp`: diagnostica di audit già presente nella working tree per classificare SolidVoxel/Air/PassableVoxel/OutOfBounds/TerrainPending e distinguere contatto radiale da blocco tangenziale. I campi diagnostici non governano la fisica.
- `src/world/PlanetaryVoxelE2ETests.cpp`: audit denso del collider, trace solo alle transizioni, profilo terreno del fixture e casi deterministici per patch uniforme, gradino, diagonale, timestep variabile e attraversamento faccia. Il runner evita timestep terminali quasi nulli usando cicli completi.

Non è stata modificata la correzione d'inizial overlap: un esperimento più ampio era stato valutato e rimosso perché la traccia finale non dimostrava che fosse il meccanismo responsabile. Non sono state modificate gravità, impulso di salto, `PlanetRadialMapping`, streaming, renderer o salvataggi.

## Risultati CPU

Build Debug del target `FairworldPlanetaryVoxelE2ETests` con `build_r3z7`, MSVC/Visual Studio 18 2026: **PASS, exit code 0**.

Il comando effettivo è:

```powershell
& 'D:\Visual Code\vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build build_r3z7 --config Debug --target FairworldPlanetaryVoxelE2ETests -- /m:1 /v:minimal
```

Runner eseguito due volte dal runtime sandbox, usando `D1_2_19D2_SANDBOX\runtime` come working directory:

```powershell
& 'D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldPlanetaryVoxelE2ETests.exe' --fixture 'D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\saves\map\world_map.json'
```

Entrambe le esecuzioni hanno exit code **0** e i riepiloghi dei casi sono identici.

| Caso CPU | Risultato osservato | Esito |
|---|---|---|
| Stand fermo / caduta / salto e atterraggio | Test preesistente PASS; contatto idle stabile 120 tick; jump e re-landing confermati | PASS |
| Cammino sul patch uniforme reale 3×3 (`topY=78`) | 24 tick grounded, zero perdite Grounded, zero campioni solidi penetranti | PASS |
| Cammino Tiny a timestep fisso | 60/60 tick grounded, zero penetrazione densa | PASS |
| Timestep variabile `1/120, 1/30, 1/60 s` | 51 tick grounded, zero penetrazione densa; sequenza ripetibile | PASS |
| Percorso diagonale | perde Grounded due volte, termina in aria; clearance radiale dal solido locale `+2,91878`; penetrazione massima `0,000320435` | PASS per assenza di attraversamento; perdita di supporto coerente con dislivello |
| Gradino del fixture (`Δ top=-3`) | perde Grounded durante la discesa; termina ancora in aria a `+2,26039` dalla superficie locale; penetrazione `0,00050354` | PASS per discesa senza penetrazione; atterraggio successivo non esercitato in questo caso |
| Attraversamento bordo Cube-Sphere `+Z → +X` | 24 tick grounded, 1 perdita transitoria, termina grounded; 0 pending e 0 penetrazione densa | PASS |
| Cammino irregolare 600 tick già presente | grounded perso al tick 1 e ripreso al tick 2; nessun TerrainPending; penetrazione massima `0,000854492`; nessuna velocità radiale outward indotta dalla correzione collisionale | PASS |
| Verifica “Grounded” separata dalla penetrazione | il test conta stati Grounded e campioni SolidVoxel separatamente; nessuna perdita legittima viene trasformata in supporto forzato | PASS |

Il fixture è stato solo letto. L'integrità dei **40/40 file originali** confrontati con il baseline disponibile è rimasta `UNCHANGED`; inventario/hash post-run in [d19d6_original_integrity_post.csv](D:/FAIRWORLD/D1_2_19D2_SANDBOX/logs/d19d6_original_integrity_post.csv).

Log completi:

- [d19d6_baseline9probes.log](D:/FAIRWORLD/D1_2_19D2_SANDBOX/logs/d19d6_baseline9probes.log) — confronto A/B senza probe aggiuntivi, runner fallito per la penetrazione riprodotta.
- [d19d6_final_cert_run1.log](D:/FAIRWORLD/D1_2_19D2_SANDBOX/logs/d19d6_final_cert_run1.log) — exit code 0.
- [d19d6_final_cert_run2.log](D:/FAIRWORLD/D1_2_19D2_SANDBOX/logs/d19d6_final_cert_run2.log) — exit code 0, stesso riepilogo.

`git diff --check` sui tre file coinvolti termina con exit code 0. Il controllo globale segnala invece whitespace preesistente in `src/states/BlockMakerState.cpp`, fuori da questo intervento; non è stato modificato.

## Limiti e rischi residui

- Il test del gradino osserva la perdita di supporto e l'assenza di penetrazione, ma non attende il successivo atterraggio. L'atterraggio è coperto dal test esistente di salto/caduta, non dalla stessa traiettoria di gradino.
- La risoluzione di spawn basata su nove campioni può differire di un voxel dalla misura radiale del centro in alcuni punti; il report usa la superficie radiale del voxel centrale per separare clearance centrale e impronta. Non è stata cambiata la logica di spawn.
- Il costo GPU/renderer e la prestazione interattiva non sono misurati.
- Il conteggio dei probe aumenta da 36 a 68 per una query completa; va osservato in una successiva sessione GUI autorizzata.
- GUI, GPU, collisioni durante una sessione reale e telemetria Vulkan: **NOT VERIFIED**.

## Decisione

La regressione CPU dimostrata di penetrazione sul bordo è corretta dal campionamento intermedio e ripetuta PASS in due esecuzioni. I test disponibili non dimostrano ogni caso del gameplay reale; quindi questo rapporto **non certifica GUI/GPU né il freeze D1**.
