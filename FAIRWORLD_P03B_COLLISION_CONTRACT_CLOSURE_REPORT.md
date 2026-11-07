# FAIRWORLD P03b — Collision Contract Closure Report

**Esito complessivo: NO-GO per P04.** Gate A determinismo coordinate chiuso per i 54 campioni Tiny; Gate B non verificato perché il fixture upper-body-only non è stato isolato; Gate C richiede una nuova API condivisa e resta in proposta. Il full runner Release presenta inoltre due FAIL indipendenti dal digest coordinate.

## 1. Baseline e sicurezza

- HEAD: `f9c7a3047bf3c8ecaad70de4f1af39943933eaf1`, invariato rispetto alla baseline P01 indicata.
- Il working tree resta dirty: 152 voci `git status --short` al controllo. Le modifiche storiche e i file non tracciati sono stati preservati; nessun reset, clean, commit, push o riconfigurazione CMake.
- Runner e log sono stati eseguiti/scritti nell'area sandbox `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime`; il fixture è `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json`.
- Modifica funzionale circoscritta: `GetChunkCoordFromPosition` passa `worldPos` direttamente a `DirectionToFaceUV`. La funzione proietta dividendo per l'asse dominante, quindi la scala del vettore si cancella e la normalizzazione preliminare non è necessaria.
- Le tracce interne temporanee sono state rimosse; la traccia completa del runner è opt-in tramite `FAIRWORLD_P03B_TRACE`.

## 2. Gate A — determinismo Debug/Release

### A1–A2. Riproduzione e primo punto divergente

Fixture e 54 posizioni world sono identici. Prima della correzione:

| Campione | Debug | Release |
|---|---|---|
| face 0, col 2, row 1 | legacy key `(2,1)` | legacy key `(1,1)` |
| world | `(0x1.74c230p+4, 0, 0x1.1791a4p+5)` | identico |
| local chart X prima del floor | `0x1.000002p+0` = `1.00000012` | `0x1.fffffep-1` = `0.99999994` |
| floor X | 1 | 0 |

La prima divergenza osservata è nel vettore normalizzato: a parità di `worldPos`, Debug produce componenti differenti da Release (`glm::normalize`, che include una normalizzazione basata sulla lunghezza). La differenza si propaga in UV/chart e attraversa il confine intero prima di `floor`; il tie-break floor/ceil non è la causa primaria. Le conversioni int avvengono dopo floor/ceil e il valore è finito e nel dominio del test. I log non dimostrano UB. La configurazione Release del progetto usa `/fp:fast` e ottimizzazione MaxSpeed; i risultati dimostrano una divergenza floating-point dipendente dalla configurazione, non un problema di fixture.

### A3–A4. Correzione e accettazione

La proiezione `DirectionToFaceUV` seleziona l'asse dominante e calcola `direction / maxAxis`; è omogenea rispetto alla scala. Rimuovere il `normalize` precedente conserva la semantica matematica e impedisce che l'errore della radice quadrata sposti il punto oltre il bordo. Non sono stati introdotti epsilon né modificati digest o identità canoniche.

Dopo la modifica, full runner Debug e Release riportano entrambi:

- agreement: `36/54`;
- digest: `0x8429a4e135752c13`;
- `GetChunkCoordFromPosition legacy output digest preserved`: PASS.

Quindi il gate coordinate Debug/Release è **PASS** e l'output storico di Debug è preservato. Il digest non è stato aggiornato.

## 3. Gate B — semantica Grounded e supporto

**Stato: NOT VERIFIED.** Il fixture precedente `[P03A][CASE] overhead` non soddisfa il requisito B1: dopo il solver la query indipendente del footprint dei piedi trova supporto (`footSupport=1`), quindi non isola voxel solidi solo presso upper-body/side. È stato correttamente segnalato come NOT VERIFIED; non è evidenza contro un falso Grounded.

La suite caratterizza altri casi: pavimento e contatto solido, footprint senza supporto, supporto rimosso, query Pending, parete, gradino, soffitto, passable voxel, overlap iniziale e locomozione Tiny. Questi non sostituiscono il fixture richiesto.

Non è stato cambiato il solver. Per chiudere il gate serve un test runner-only che, **prima** del solver, validi con query indipendenti tutti i campioni del footprint inferiore come Air e almeno un campione upper-body come Solid, con nessun campione Missing/Pending/OOB; soltanto dopo si esegue una sweep radiale controllata. Se tale configurazione non è costruibile con il mapping radiale e il volume attuale, il test deve documentare il perché e il contratto Grounded va rivisto separatamente.

## 4. Gate C — disponibilità del terreno

`WorldVoxelQueryStatus` in `src/world/GameWorld.h` distingue Solid, Air, Passable, OutOfBounds, TerrainPending e InvalidInput, ma non Missing. `GameWorld::QueryBlockCartesian` in `src/world/GameWorld.cpp` restituisce `TerrainPending` sia quando `GetChunkEntity` non trova un'entità, sia quando l'entità esiste ma `VoxelChunkComponent::isGenerated` è false.

`WorldChunkManager` (`src/world/WorldChunkManager.h`) conserva soltanto una mappa attiva chiave → entity; non registra richieste, lavoro in-flight o intenzione di streaming. Di conseguenza, per un chunk assente il manager non può stabilire se sia **Missing/unrequested** oppure **Pending/requested**. Un chunk registrato ma non generato è identificabile come pending. Un riferimento entity invalido o un chunk senza componente è attualmente collassato nel ramo pending.

Raycast ha già `RaycastResultStatus::Missing` separato da `Pending`: il campionatore planetario marca Missing quando manca il chunk/entity e Pending quando il chunk c'è ma `isGenerated == false`. La distinzione esiste quindi in un consumer, ma non nel contratto autorevole comune.

### Contratto proposto (non implementato)

```cpp
enum class GeometryState { Solid, Air, Passable, OutOfBounds, InvalidInput };
enum class ChunkAvailability { Available, Missing, Pending, Invalid };
struct WorldVoxelQueryResult {
    GeometryState geometry;
    ChunkAvailability availability;
    BlockType block;
    ivec3 voxelCoord;
};
```

- `Available` accompagna uno stato geometrico determinato.
- `Pending` significa chunk registrato o richiesto e generazione non completata.
- `Missing` significa chunk non disponibile e senza richiesta/in-flight nota; non implica Solid.
- `Invalid` copre entity/component/coordinate non validi.
- Movimento e Grounded restano una policy separata: Missing/Pending non producono contatto solido né Grounded. La politica di movimento deve evitare di accumulare velocità illimitata durante attese e deve poter richiedere/retry lo streaming senza busy wait.

Compatibilità suggerita: introdurre una nuova query tipizzata e mantenere `QueryBlockCartesian` come wrapper legacy che continua a mappare Missing/Pending al valore storico finché i consumer non vengono migrati in patch autorizzate. Callsite da aggiornare in seguito: `GameWorld`, `PhysicsEngine`, raycast e i consumer di spawn/streaming. Nessuna API condivisa è stata modificata.

## 5. Regression gate

### Build

Build MSVC tramite `build_r3z7` e output sandbox:

| Target/configurazione | Esito |
|---|---|
| FairworldCore Debug, compilazione della modifica candidata | PASS, exit 0 |
| FairworldCore Release, compilazione della modifica candidata | PASS, exit 0 |
| Runner Debug, compilazione/link candidata | PASS, exit 0 |
| Runner Release, compilazione/link candidata | PASS, exit 0 |
| Ricompilazione conclusiva dopo aver rimosso la sola traccia interna temporanea e reso opt-in il log del runner | BLOCKED/interrotta: MSBuild non ha prodotto avanzamento osservabile; interrotto ordinatamente, exit 1. La variante funzionale candidata era già stata compilata e testata nei quattro target sopra. |

### Runner completo — fixture Tiny

Comando effettivo: `FairworldPlanetaryVoxelE2ETests.exe`, working directory `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\runtime`, output in `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p03b_candidate_full_{debug,release}.log`.

| Config | Exit | Coordinate | P02a/P02b/P02c/P02d | P03/P03a | Tiny/mesh | Esito |
|---|---:|---|---|---|---|---|
| Debug | 0 | PASS, digest concorde | PASS nei controlli del runner | PASS, P03a summary PASS | PASS; 54 risultati CPU associati, 297.768 vertici/indici, 99.256 triangoli | **PASS full runner** |
| Release | 1 | PASS, digest concorde | PASS nei controlli del runner | P03 e P03a summary PASS | **FAIL** mesh contract: 99.252 triangoli anziché 99.256 e il witness legacy flat conta 135.824 triangoli/hash `0x43bcceb03f81581c` invece del baseline Debug 135.820/hash `0x20c0d276200e0e20`. **FAIL** locomozione integrata: diagonal perde Grounded dopo 2 tick e termina con clearance radiale 2.9188; la suite completa fallisce. | **FAIL full runner** |

La differenza del mesh Release deriva dai risultati float in un percorso di meshing sotto Release; questa audit non l'ha isolata oltre i contatori/hash. La perdita Grounded è osservata nel caso diagonale del runner Release, ma non è stata attribuita a una causa senza una trace first-divergence specifica. Non sono state modificate le aspettative dei test.

### Altri controlli

- P02a/b/c/d, P03 e P03a: eseguiti dentro i due full runner; i rispettivi blocchi iniziali passano in entrambe le configurazioni. Il full Debug li completa; il full Release termina FAIL per i due contratti sopra.
- D1.2.19d.5/.6 separati: NOT RUN come binari indipendenti; i relativi scenari sono inclusi nel full runner Tiny.
- `git diff --check` sui due file interessati: PASS (exit 0).
- `git diff --check` globale: FAIL per trailing whitespace preesistente in file estranei, ad esempio `src/states/BlockMakerState.cpp`; nessuna correzione fuori perimetro.

## 6. Diff e rischi

Diff funzionale P03b: una sola sostituzione in `src/world/MapWorldGenerator.cpp` (`normalize(worldPos)` → `worldPos`) con commento sulla proiezione omogenea. In `src/world/PlanetaryVoxelE2ETests.cpp` la traccia first-divergence è resa opt-in; non modifica risultati. Gli altri cambiamenti preesistenti mostrati da Git non sono stati toccati.

Rischio: `DirectionToFaceUV` presuppone un vettore non nullo; il vecchio `normalize` non proteggeva anch'esso l'origine da input invalido. Il consumer deve continuare a passare posizioni planetarie finite e non nulle; per un hardening di input serve un test/API separato. Non è stata cambiata la semantica legacy `isFlat` né alcuna chiave persistente.

## 7. Decisione

**NO-GO per P04.** Il determinismo richiesto per `GetChunkCoordFromPosition` è chiuso e il digest Debug originale è riprodotto identicamente in Release. Tuttavia:

1. manca un fixture valido per il caso Grounded upper-body-only;
2. QueryBlockCartesian non distingue Missing da Pending e la distinzione richiede un contratto condiviso;
3. il full runner Release ha due FAIL riproducibili (mesh hash/triangoli e diagonal locomotion).

Prossimo passo consigliato: isolare separatamente il primo punto float divergente del mesh Release e il tick esatto della perdita Grounded diagonale; in parallelo progettare e validare il fixture B. Non avviare P04 finché questi gate non sono chiusi o formalmente accettati.
