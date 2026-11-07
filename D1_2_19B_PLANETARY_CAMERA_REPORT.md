# FAIRWORLD — D1.2.19b Planetary Camera Report

## Esito

**CPU/headless: PASS.** La causa della discontinuità del riferimento yaw è stata identificata nel codice e corretta con un frame tangente persistente trasportato insieme alla normale radiale. Build Debug completa, Tiny CPU E2E e tutti i contratti richiesti sono PASS. **Input reale, GUI e comportamento GPU: NOT VERIFIED.** Nessun test GUI è stato avviato e non viene dichiarato D1 FROZEN.

## Percorso e ownership

```text
DeviceManager / ActionMap
  → InputState (lookYaw/lookPitch, movimento)
  → PlayerMovementSystem
      → accumula yaw/pitch quando requireFreeCursor è false
      → aggiorna il frame tangente persistente sulla normale radiale del body
      → muove il body nel piano tangente
      → sincronizza Transform posizione = body.position + Up * eyeOffset
  → CameraSystem
      → trasporta il frame sulla normale radiale della camera
      → costruisce front/right/up e scrive CameraComponent + Transform.rotation
  → PlayState::Render
      → interpola posizione/quaternion e crea glm::lookAt(view)
      → activeCameraView alimenta rendering e raycast
```

- `PlayerMovementSystem::Update` possiede la lettura degli input look e modifica `CameraComponent.yaw/pitch`. È anche il consumer del movimento WASD e mantiene il frame planetario (`planetaryFrameUp/Forward`). Con il cursore libero, il look e il movimento sono bloccati, ma la posizione camera continua a seguire il body.
- `CameraSystem::Update` è il writer effettivo di `TransformComponent.rotation`: costruisce il quaternion dalla base camera e aggiorna `CameraComponent.front/right/up/worldUp`. `CameraSyncSystem` conserva la trasformazione precedente per l'interpolazione.
- `PlayState::Render` legge la trasformazione interpolata, ricava gli assi della camera e costruisce `viewMatrix` con `glm::lookAt`.
- L'orientamento del body fisico non è il proprietario della direzione di vista: la direzione utente è rappresentata dagli angoli camera e dal frame tangente.

## Causa confermata

Prima della patch, sia `PlayerMovementSystem` sia `CameraSystem` ricostruivano la direzione yaw-zero usando `cross(up, northGlobale)`, con `northGlobale=(0,1,0)`. Ai poli il prodotto vettoriale diventa nullo o mal condizionato; il ramo di fallback sceglieva invece un asse fisso. Il riferimento quindi poteva cambiare bruscamente durante l'attraversamento della soglia, e i due sistemi duplicavano la costruzione. Questo spiega un salto/rotazione della visuale dipendente dalla latitudine. Nel percorso esaminato non c'è una dipendenza da `AstronomySystem`, orbite o rotazione astronomica: attribuire il sintomo al moto del pianeta non è supportato da queste evidenze.

## Contratto matematico adottato

- `Up` è la normale radiale unitaria rispetto al centro planetario runtime (attualmente origine mondiale).
- `Forward` persistente è una direzione tangente yaw-zero, trasportata dal frame precedente.
- `Right = normalize(cross(ForwardYaw, Up))`; dopo pitch la base è ricostruita e normalizzata.
- Yaw ruota intorno a `Up`; pitch è limitato a `[-89°, +89°]`.
- Tra normali non antiparallele si applica la rotazione minima che porta la normale precedente alla nuova, poi si riproietta `Forward` sul nuovo piano tangente.
- Per normali quasi parallele il frame viene mantenuto e riproiettato. Per il caso antiparallelo esatto, che non ha un asse minimo univoco, si sceglie deterministicamente il forward corrente come asse della mezza rotazione, preservando il heading invece di introdurre un flip arbitrario.
- Input non finiti o vettori degeneri non vengono normalizzati alla cieca. Yaw viene mantenuto nel range periodico con `remainder`.

Il trasporto parallelo è dipendente dal percorso: un giro chiuso su una sfera può produrre olonomia geometrica. Questo è comportamento matematico atteso, non accumulo accidentale di roll; la base viene ricostruita ortonormalmente a ogni aggiornamento.

## Modifiche

- Aggiunto `src/systems/PlanetaryCameraFrame.h` con helper CPU per normalizzazione, inizializzazione deterministica, trasporto, applicazione look e costruzione della base.
- `src/core/entities/Components.h`: memorizza normale e heading tangente persistenti nel `CameraComponent`.
- `src/systems/Systems.cpp`: rimpiazza il riferimento globale ricostruito a ogni frame con il frame trasportato; aggiorna esplicitamente gli assi consumati dalla camera; separa l'applicazione input dalla sincronizzazione della camera quando il cursore è libero.
- `src/states/PlayState.cpp`: inizializza frame e quaternion coerenti allo spawn, evitando il passaggio iniziale dall'identità; l'implementazione esistente dello spawn pending resta parte della baseline D1.2.19a.
- `src/world/PlanetaryVoxelE2ETests.cpp`: aggiunge una prova matematica CPU del frame camera nel runner Tiny già esistente.

Nessuna modifica è stata fatta ad astronomia, orbite, renderer, shader, DMA/VRAM o serializer. Non sono stati modificati dati persistenti da questa validazione. Le numerose altre modifiche già presenti nella working tree sono state preservate e non fanno parte del diff D1.2.19b.

## Test e risultati effettivi

Esecuzioni dalla working directory `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\runtime`, usando gli eseguibili Debug esistenti. Exit code 0 per ogni riga.

| Verifica | Esito | Exit code | Durata registrata |
|---|---:|---:|---:|
| Debug `ALL_BUILD` (`build_r3z7`, MSVC, `/m:1`) | PASS | 0 | non registrata |
| Planetary Tiny CPU E2E, incluso spawn | PASS | 0 | 1321 ms |
| JobSystem contract | PASS, 10/10 | 0 | 468 ms |
| DMA result contract | PASS, 14/14 | 0 | 45 ms |
| VRAM allocator contract | PASS, 7/7 | 0 | 32 ms |
| Transfer/Graphics Sync contract | PASS, 6/6 | 0 | 20 ms |
| Forge Vertex contract | PASS | 0 | 21 ms |
| `FairworldEditor.exe --validate-d12` | PASS, 4/4 gruppi | 0 | 48 ms |
| `git diff --check` sui file di codice D1.2.19b | PASS | 0 | — |

Il test camera CPU ha attraversato 1.440 step: yaw positivo/negativo, limiti pitch, equatore, entrambi i poli, normale antiparallela, sei transizioni di direzione Cube-Sphere e percorso chiuso. Metriche: massimo angolo tra forward consecutivi `0 rad` nei campioni del percorso controllato; errore forward al ritorno chiuso `0`; massimo salto angolare di un singolo step tra facce `0,0624185 rad`. Tutti i campioni hanno base finita e ortonormale secondo le tolleranze del runner; il test è deterministico.

La stessa esecuzione Tiny ha mantenuto 54/54 chunk, 504.877 voxel non-Air, 54 mesh CPU, 135.820 triangoli e zero mismatch di winding/attributi. La risoluzione spawn ha 54/54 campioni validi, clearance minima `0,110001` e 1.458 probe collider; l'hash geometrico stabile osservato è `0x20c0d276200e0e20`. La pubblicazione ECS della mesh asincrona e l'upload GPU restano esplicitamente NOT VERIFIED dal runner CPU.

Log conservati in `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\logs\`:

- `d1_2_19b_build.log`
- `d1_2_19b_camera_e2e.log`
- `d1_2_19b_final_JobSystem.log`
- `d1_2_19b_final_DMA.log`
- `d1_2_19b_final_VRAM.log`
- `d1_2_19b_final_TransferGraphicsSync.log`
- `d1_2_19b_final_ForgeVertex.log`
- `d1_2_19b_final_ValidateD12.log`

## Limiti e rischi residui

- **GUI/input finestra: NOT VERIFIED.** Focus finestra, ImGui capture, cursor lock e sensibilità non sono stati esercitati; per policy non è stata avviata una sessione GUI.
- **GPU/Vulkan: NOT VERIFIED.** Nessuna draw o fence GPU è stata osservata in questa attività.
- Il centro del pianeta nel percorso camera è attualmente assunto come origine mondiale, coerentemente con il codice esistente; un futuro root transform planetario richiederà di passare il centro effettivo.
- Un salto teletrasporto che porta la normale esattamente all'antiparallelo usa la regola deterministica esplicita; l'orientamento desiderato in quel caso non può essere dedotto univocamente dal solo punto di arrivo.
- Eventuali sistemi esterni che teletrasportano il body direttamente mantengono il frame precedente e al tick successivo lo trasportano verso la nuova normale. La GUI non è stata usata per valutare il risultato percepito.

## Stato conclusivo

**Patch CPU/headless verificata; validazione funzionale GUI/GPU incompleta.** La causa statica della degenerazione ai poli è confermata e la correzione locale supera i test. Per verificare controlli reali, comfort visivo e interazione con la finestra è necessaria una successiva sessione GUI autorizzata. Nessun commit o push è stato effettuato e D1 non viene dichiarato FROZEN.
