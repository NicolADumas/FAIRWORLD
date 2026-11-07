# FAIRWORLD — D1.2.19c Planetary Movement Report

## Esito

**CPU/headless: PASS** per movement math, gravità radiale, contatto sferico, attraversamento di una cella e di un bordo Cube-Sphere, salto/atterraggio, spawn pending e sincronizzazione della camera dopo la fisica. Build Debug `ALL_BUILD` e regressioni richieste PASS. **GUI/input hardware e GPU: NOT VERIFIED.** Nessuna GUI è stata avviata. Nessun commit/push e nessun D1 FROZEN.

## Audit: ownership e data flow

```text
ActionMap / DeviceManager
  → InputState (WASD, Space, mouse look)
  → PlayerMovementSystem
      → frame camera persistente: Up radiale, forward/right tangenti
      → input WASD normalizzato nel piano tangente
      → velocità tangenziale desiderata + componente radiale preesistente
  → PhysicsSystem / PhysicsEngine::StepSimulation
      → gravità + drag → integrazione → collisioni voxel sferiche
      → RigidBody.position/velocity autorevoli
      → Transform camera = body position + radial Up * eyeOffset
  → CameraSystem
      → trasporta frame e scrive orientamento/quaternion
  → PlayState::Render
      → view matrix dalla Transform interpolata
  → GameWorld::Update
      → avanzamento generazione/mesh differita per il frame successivo
```

- La posizione e la velocità autorevoli del player sono `RigidBody.position` e `RigidBody.velocity`; `TransformComponent` è la rappresentazione renderizzata/interpolata della camera.
- `DeviceManager` aggrega le azioni `MOVE_FORWARD/BACKWARD/LEFT/RIGHT` in `InputState`; i binding predefiniti includono W/S/A/D e D-pad, Space è `JUMP`. Il mouse fornisce look delta quando il cursore è catturato.
- `DeviceManager` sopprime le action quando ImGui cattura input e `requireFreeCursor` è attivo. `PlayerMovementSystem` disabilita movimento/look quando `requireFreeCursor` è vero. Focus, cursor lock e input fisico non sono stati esercitati in una finestra reale.
- `PlayerMovementSystem` imposta la velocità tangenziale di controllo; nel modo survival conserva la componente radiale corrente e vi sostituisce quella solo per il salto quando il body è grounded. Non azzera la velocità radiale a ogni frame.
- `PhysicsEngine::StepSimulation` esegue `ApplyGravity`, `ApplyDrag`, `Integrate`, quindi `ResolveCollisions`. Per `isLogicalSphere`, gravità e up derivano dal raggio rispetto al centro (attualmente l’origine mondiale); il solver sferico interroga i voxel tramite `GetBlockCartesian` → `WorldToVoxelCoord` → chunk manager.
- L’ordine PlayState ora è `PlayerMovementSystem → PhysicsSystem → CameraSystem`. `PhysicsSystem` sincronizza la posizione camera dopo integrazione/collisione; `CameraSystem` calcola poi l’orientamento sulla posizione aggiornata. `GameWorld::Update` è successivo ai sistemi, perciò la fisica usa i chunk già pubblicati e la generazione differita diventa disponibile in aggiornamenti seguenti.
- Se `planetarySpawnPending` è vero, movement e PhysicsSystem saltano il player. Il pending viene risolto dopo `GameWorld::Update`; il controllo riprende al tick successivo.

## Cause confermate e correzioni

1. **Input diagonale più veloce:** il controller sommava forward e right a velocità piena, dando `sqrt(2)` volte la velocità nominale. Aggiunto `PlanetaryMovementMath::NormalizePlanarInput` e un costruttore di velocità tangenziale condiviso; cardinali restano a velocità piena, diagonali sono normalizzate.
2. **Frame collisioni discontinuo ai poli:** `ResolveSphericalCollisions` ricostruiva `localRight` con il nord globale e un fallback, indipendentemente dal frame persistente camera. Ora usa il forward tangente fornito dal player oppure il frame collisione persistente del `RigidBody`, trasportato con la normale radiale; il caso antiparallelo usa la regola deterministica del trasporto già verificata in D1.2.19b.
3. **Limite terminale cartesiano su pianeta:** `ApplyDrag` limitava `velocity.y` anche in un mondo sferico, invece della componente radiale entrante. Ora limita solo la velocità inward lungo Up e conserva la velocità tangenziale; nei mondi cartesiani resta la regola Y legacy.
4. **Penetrazione sferica di 0,1 unità:** il body dichiara `position` come base/piedi, ma `isCylinderBlocked` campionava il collider solo da `y=0.1`. La simulazione Tiny mostrava un contatto stabile a `surfaceRadius−0.1`. Aggiunto un probe anche a `y=0`, che porta il body a contatto senza penetrazione misurabile.
5. **Camera indietro di un tick fisico:** CameraSystem precedeva PhysicsSystem e la camera veniva sincronizzata prima della collisione. Riordinati i due sistemi e spostata la sincronizzazione body/eye offset dopo `StepSimulation`, prima del calcolo dell’orientamento.

## Contratto fisico

- `Up = normalize(body.position - planetCenter)` per pianeti sferici; in questo runtime `planetCenter=(0,0,0)`.
- `Gravity` punta radialmente verso il centro. Il test legge `dbg_gravityAccel` e ne verifica il prodotto scalare con Up negativo.
- Il controllo WASD produce velocità sul piano tangente; il modulo dell’input combinato non supera 1.
- `radialSpeed = dot(velocity, Up)` e `tangentVelocity = velocity - Up * radialSpeed`. La gravità, il salto e la caduta agiscono sulla componente radiale senza eliminare la tangenziale.
- Il limite terminale sferico clampa la componente radiale entrante a `55 m/s` (limite già definito dal modello drag) senza tagliare la velocità tangenziale.
- Le collisioni usano campioni del cilindro lungo Up, inclusa la base del body; il salto è verificato con successivo ritorno al contatto.

## Modifiche effettuate

- `src/systems/PlanetaryMovementMath.h`: helper CPU per input diagonale, velocità tangenziale e split/clamp radiale.
- `src/systems/Systems.cpp`: applica la velocità tangenziale normalizzata; passa il forward persistente al solver; sincronizza il transform camera dopo la fisica.
- `src/systems/PhysicsEngine.h/.cpp`: mantiene il frame tangente di collisione, usa terminal speed radiale per pianeti, aggiunge il probe alla base e accetta il forward planetario opzionale.
- `src/states/PlayState.cpp`: ordina movimento → fisica → camera.
- `src/world/PlanetaryVoxelE2ETests.cpp`: aggiunge test CPU input, pending, PhysicsEngine Tiny, frame collisioni, traversate, timestep, grounding e salto.

Nessuna modifica ad AstronomySystem/orbite, JointPhysics, renderer, shader, DMA/VRAM, serializer o salvataggi. Sono state preservate le modifiche preesistenti della working tree.

## Test e risultati

Eseguiti con build Debug `build_r3z7`; gli eseguibili sono partiti dalla working directory `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\runtime`, usando il fixture `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json`.

| Gate | Risultato | Exit code | Durata |
|---|---:|---:|---:|
| Debug `ALL_BUILD` | PASS | 0 | 26.697 ms |
| Tiny CPU E2E + Spawn + Camera + Physics | PASS | 0 | 6.560 ms |
| JobSystem | PASS 10/10 | 0 | 1.656 ms |
| DMA | PASS 14/14 | 0 | 241 ms |
| VRAM allocator | PASS 7/7 | 0 | 139 ms |
| Transfer/Graphics Sync | PASS 6/6 | 0 | 118 ms |
| Forge Vertex | PASS | 0 | 242 ms |
| `--validate-d12` | PASS, 4/4 gruppi | 0 | 959 ms |
| `git diff --check` sui file modificati in questa patch | PASS | 0 | — |

Risultati Tiny specifici:

- WASD avanti/indietro, sinistra/destra e diagonale: PASS; velocità diagonale `5.0`, pari alla velocità cardinale.
- Split e clamp radiale: PASS; velocità inward `-100` limitata a `-55`, componente tangenziale `30` conservata.
- Gravità a raggio di prova: PASS, accelerazione misurata lungo Up `-0,288294`.
- Collision frame: equatore/poli, 6 direzioni Cube-Sphere e caso antiparallelo PASS; salto angolare massimo tra step `0,0624166 rad`.
- Stesso replay a 60 Hz: errore deterministico `0`; confronto 60 Hz / 120 Hz per 2 s: differenza posizione `0,11261`, entrambe le traiettorie finite e limitate.
- Attraversamento fisico: una transizione di faccia (`+Z → +X`) e attraversamento cella sulla faccia (`col 1 → 2`). Clearance locale finale `-0,000946` durante il frame in movimento; contatto fermo dopo assestamento `+0,000137`, grounded stabile.
- Salto: altezza radiale massima `21,9289`; dopo 20 s simulati grounded e clearance `+0,000092`.
- Spawn pending: body, velocità, camera e angoli invariati mentre il flag è attivo.
- Sync camera: posizione camera uguale a `body.position + radialUp * eyeOffset` dopo fisica/collisione.
- Fixture invariato dal runner; si conservano i gate precedenti: 54 chunk, 504.877 voxel non-Air, 54 mesh CPU e 135.820 triangoli.

Log in `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\`:

- `d1_2_19c_all_build_success.log` (build finale, exit 0; durata 26.697 ms).
- `d1_2_19c_all_build.log` (tentativo rediretto iniziale interrotto dopo stallo senza output completo; non è il log PASS).
- `d1_2_19c_tiny_e2e.log`
- `d1_2_19c_JobSystem.log`
- `d1_2_19c_DMA.log`
- `d1_2_19c_VRAM.log`
- `d1_2_19c_TransferGraphicsSync.log`
- `d1_2_19c_ForgeVertex.log`
- `d1_2_19c_ValidateD12.log`

## Limiti e rischi residui

- WASD e look sono verificati a livello matematico e di percorso CPU; input fisico, focus ImGui, cattura/rilascio cursore e percezione in finestra **NOT VERIFIED**.
- Nessuna prova GPU/Vulkan è stata eseguita; mesh CPU non equivale a collisione GPU e qui non è stato richiesto un test GUI.
- La posizione del centro planetario è ancora assunta all’origine nel runtime; un futuro root transform planetario richiederà l’uso del centro effettivo in movimento, gravità e collisioni.
- Il confronto timestep ha scarto misurato `0,11261` dopo 2 s a causa dell’integrazione/collisione discreta. È entro la soglia del test ma non dimostra equivalenza fisica a lungo termine.
- La superficie attraversata può variare fra celle; la prova misura una clearance transitoria inferiore a 0,001 unità durante il moto e clearance positiva a riposo/atterraggio. La stabilità visiva di cammino su terreno irregolare richiede verifica GUI autorizzata.

## Stato conclusivo

**Patch CPU/headless PASS; verifica GUI/GPU NOT VERIFIED.** Nessuna conclusione di freeze D1.2.
