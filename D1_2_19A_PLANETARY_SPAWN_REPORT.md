# FAIRWORLD — D1.2.19a Planetary Player Spawn Report

## Esito

**Causa confermata; correzione minima applicata.** Lo spawn custom sommava `heightOffset` al raggio nominale, senza consultare la superficie voxel già prodotta dal `TerrainSolver`. Nel fixture Tiny ciò collocava il corpo 48 unità dentro il bordo solido. Ora il player resta in stato pending, non riceve input né simulazione fisica, e viene trasferito solo dopo che il terreno sotto l'intera impronta del collider è interrogabile.

**Build Debug: PASS. Test CPU Tiny: PASS. `--validate-d12`: PASS. GUI/GPU: NOT VERIFIED** (nessuna GUI avviata, come richiesto). D1 non è dichiarato FROZEN.

## Audit del percorso e causa

Il percorso verificato è:

```text
SpawnPoint JSON
  -> PlayState::Init
  -> chart = (localX/R, localZ/R)
  -> CubeSphereMapping::FaceChartToDirection
  -> posizione radiale del body
  -> PhysicsEngine::ResolveSphericalCollisions / GameWorld::GetBlockCartesian
  -> PlayerMovementSystem: camera = body + up * eyeOffset
```

- `SpawnPoint::heightOffset` è documentato in [MapDocument.h](/D:/FAIRWORLD/FAIRWORLD/src/world/MapDocument.h:368) come altezza dalla superficie planetaria. Il JSON del fixture `TinySphere/world_map.json` contiene `heightOffset=10` sul centro della faccia 0.
- In precedenza `PlayState::Init` trasformava il chart in direzione, poi assegnava `dir * (R + heightOffset)`. Questa è una distanza dal raggio nominale, non dalla superficie voxel effettiva. La UI di teletrasporto usava la stessa formula.
- Per Tiny, `N=3` e `R=24`. `TerrainSolver::EvaluateLayers` classifica come solidi gli indici radiali fino al relativo `surfaceHeight`; `MapWorldGenerator` converte il confine della cella `y` in `R + (y - 24)`. Il query CPU del fixture trova il bordo esterno solido del footprint spawn a raggio `82`.
- Quindi la formula precedente produceva raggio `24+10=34`, pari a **48 unità di penetrazione** nel solido rilevato. Il dato è stato misurato dopo aver eseguito il `TerrainSolver`, non inferito dal solo rendering.
- `PhysicsEngine::ResolveSphericalCollisions` campiona il cilindro del giocatore da `0.1` a `height-0.1` e usa il raggio locale per il footprint. La fisica veniva eseguita prima di `GameWorld::Update`, quindi prima del primo batch del `TerrainSolver`; quando il chunk non era pronto, `OutOfBounds` veniva trattato come solido sotto `R+25`. La depenetrazione tenta al massimo `0.5` unità, insufficiente rispetto alle 48 unità misurate: il corpo non disponeva quindi né della quota reale né di una correzione fisica capace di recuperare lo spawn.
- `RigidBody` usa `radius=0.3`, `height=1.8`, `eyeOffset=1.6`. `PlayerMovementSystem` colloca la camera a `body.position + up*eyeOffset`. La correzione conserva questa separazione: il body è posizionato sopra il terreno e la camera segue il body lungo la normale radiale.
- Non è emersa evidenza che colleghi questo difetto alla rotazione astronomica o alla rotazione del pianeta.

## Correzione applicata

La query condivisa `GameWorld::TryResolvePlanetarySpawn`:

1. Rifiuta input non finiti o un mondo non sferico.
2. Determina la cella/chunk planetaria sotto ciascuno dei nove campioni orizzontali usati dal collider.
3. Restituisce `TerrainPending` se un chunk manca o non è ancora `isGenerated`; non interpreta `OutOfBounds` come superficie valida.
4. Cerca dall'esterno verso l'interno il primo voxel solido di ogni colonna e ne usa il confine radiale superiore.
5. Sceglie il raggio massimo tra i campioni del footprint e aggiunge l'offset salvato. Con offset nullo usa un minimo di `0.11` unità, corrispondente al probe inferiore della collisione (`0.1`) più `0.01` per il margine numerico.
6. Restituisce distintamente `Ready`, `TerrainPending`, `NoSolidSurface` o `InvalidInput`.

In `PlayState`, lo spawn custom inizia a una posizione temporanea radiale sicura, mantenendo la stessa direzione. Il componente `PlayerControllerComponent` espone `planetarySpawnPending`; mentre è true, movimento e fisica saltano il player. Dopo `GameWorld::Update`, quando la superficie è pronta, il body viene spostato alla posizione risolta, la camera viene riallineata al relativo eye offset e il pending viene rimosso. Il teletrasporto usa lo stesso resolver e non cambia posizione se il terreno non è pronto.

Se una colonna generata non contiene alcun solido interrogabile, il resolver non inventa una superficie: il player resta pending e viene emesso un messaggio diagnostico. Questo caso è un limite operativo esplicito da verificare su mappe intenzionalmente vuote.

## File interessati

- `src/world/GameWorld.h/.cpp` — stato della query e calcolo della superficie voxel sotto il footprint.
- `src/states/PlayState.h/.cpp` — spawn pending, risoluzione dopo l'update del mondo, teletrasporto coerente.
- `src/core/entities/Components.h` e `src/systems/Systems.cpp` — blocco di input e fisica finché lo spawn è pending.
- `src/world/PlanetaryVoxelE2ETests.cpp` — copertura headless del pending, sei facce, bordi, angoli, determinismo e collider.

Questa working tree conteneva già modifiche D1.2 precedenti nei medesimi file e in altri percorsi. Non sono state ripristinate, pulite, committate o inviate; la revisione non ha usato `git checkout`, `reset` o `clean`.

## Validazione effettuata

Working directory del runner: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\runtime`. Fixture originale del sandbox: `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json`.

| Verifica | Risultato | Exit code / evidenza |
| --- | --- | --- |
| `cmake --build build_r3z7 --target ALL_BUILD --config Debug -- /m:1 /verbosity:minimal` | **PASS** | Exit code 0; `FairworldEditor.exe`, `FairworldCore` e target di test Debug compilati. Log: [d1_2_19a_build.log](/D:/FAIRWORLD/D1_2_17_HEADLESS_SANDBOX/logs/d1_2_19a_build.log). |
| `FairworldPlanetaryVoxelE2ETests.exe --fixture <TinySphere/world_map.json>` | **PASS** | Exit code 0; durata 1299 ms. Log: [d1_2_19a_spawn_e2e.log](/D:/FAIRWORLD/D1_2_17_HEADLESS_SANDBOX/logs/d1_2_19a_spawn_e2e.log). |
| Spawn pending prima del solver | **PASS** | Il resolver restituisce `TerrainPending` prima che il target chunk sia marcato generato. |
| Fixture Tiny dopo TerrainSolver | **PASS** | 54/54 chunk; 504.877 voxel non-Air; TerrainSolver completa. |
| Spawn su facce/bordi/angoli | **PASS** | 54 campioni deterministici; direzione radiale conservata; 1.458 probe di collider senza voxel solidi. Clearance minima osservata: `0.110001`. |
| Formula precedente sullo spawn fixture | **FAIL witness atteso / regressione rilevata** | superficie `r=82`; vecchia formula `r=34`; penetrazione `48`. Il test richiede che la vecchia formula resti identificata come errata. |
| `FairworldEditor.exe --validate-d12` | **PASS** | Exit code 0; suite D1.2 PASS. Durata 271 ms. Log: [d1_2_19a_validate_d12.log](/D:/FAIRWORLD/D1_2_17_HEADLESS_SANDBOX/logs/d1_2_19a_validate_d12.log). |
| Whitespace / diff check | **PASS** | `git diff --check` sui sorgenti tracciati interessati PASS; il runner non tracciato non contiene trailing whitespace. |
| Integrità fixture | **PASS** | SHA-256 `47AF4358E4BD663926E64A43ACF2B116F3A2939BA5605FC89B2983D32A9AD503`, uguale al baseline D1.2.18; 5.384 byte. |
| GUI / collisioni GPU reali | **NOT VERIFIED** | Nessuna GUI avviata. La verifica CPU non certifica il comportamento grafico né il runtime Vulkan. |

Il test headless copre il resolver con geometria e voxel reali del fixture. Non istanzia `PlayState` con input, finestra o fisica Vulkan; il collegamento runtime pending→Ready è stato compilato ma non esercitato in una sessione GUI.

## Rischi residui

- Mappe con chunk non generati restano in pending fino a quando la generazione produce dati interrogabili. La GUI deve verificare che il player venga attivato dopo il completamento del chunk di spawn.
- Un'area generata senza alcun voxel solido mantiene lo spawn pending; non viene usato un fallback che possa collocare il giocatore dentro il terreno.
- I test di collider riproducono i nove offset orizzontali e tre altezze campionati dall'attuale `PhysicsEngine`, ma non sono una simulazione completa della fisica dinamica.
- Rendering, teletrasporto GUI e shutdown Vulkan restano non verificati visivamente.

## Decisione

**Patch CPU/headless: PASS.** Causa confermata e mitigata senza modificare geometria planetaria, astronomia, rotazione, renderer, DMA/VRAM, shader o serializer. **Gate GUI/GPU: NOT VERIFIED**, in attesa di una nuova autorizzazione per test grafici. Nessun commit/push eseguito; D1 non dichiarato FROZEN.
