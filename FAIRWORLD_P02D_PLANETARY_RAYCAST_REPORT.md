# FAIRWORLD P02d — Planetary Raycast & Slab Robustness

**Esito:** PASS CPU/headless con limiti documentati; GUI/GPU NOT RUN.  
**HEAD mantenuto:** `f9c7a3047bf3c8ecaad70de4f1af39943933eaf1`.  
**Decisione:** GO condizionato per la pianificazione di P03 Collision System; questo rapporto non autorizza l'avvio di P03.

## 1. Safety gate e perimetro

- Il working tree era già dirty. Sono state preservate le modifiche preesistenti; nessun reset, clean, commit, push o riconfigurazione CMake.
- Le modifiche funzionali di questa fase sono limitate a `src/systems/RaycastSystem.cpp` e all'estensione additiva di `RaycastHit` in `src/systems/RaycastSystem.h`. I test sono stati aggiunti al runner già non tracciato `src/world/PlanetaryVoxelE2ETests.cpp`.
- Core, runner, log e output di build sono stati prodotti sotto `D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime`. Il fixture `D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json` è stato soltanto letto.
- SHA-256 del fixture dopo le prove: `47AF4358E4BD663926E64A43ACF2B116F3A2939BA5605FC89B2983D32A9AD503`, uguale al valore registrato nei rapporti P02a/P02c.
- `git diff --check -- src/systems/RaycastSystem.cpp src/systems/RaycastSystem.h`: **PASS**, exit 0. La scansione whitespace del runner non ha trovato righe con spazi finali.
- `git diff --check` globale: **FAIL**, exit 2, per spazi finali in `src/states/BlockMakerState.cpp` alle righe 4652–4658, 4668–4669, 11563, 11570–11571, 11806, 11828 e 11830. Il file era già modificato fuori da P02d e non è stato toccato.

## 2. Root cause confermata

La vecchia `RaycastSystem::CastVoxel` trattava il mondo planetario come chunk cartesiani con AABB locali. Sul fixture Tiny la query autorevole individuava voxel solidi in 6/6 direzioni centrali, mentre il vecchio raycast ne colpiva 1/6; il bounding box cartesiano non rappresentava l'indirizzamento Face/UV/Grid e il raggio poteva iniziare dentro un AABB di chunk con coordinate non coerenti con la sfera.

Lo Slab Test precedente calcolava `1 / direction` componente per componente. Componenti zero potevano produrre infinito e, sui piani del box, combinazioni `0 * Inf`/NaN; il DDA inoltre iniziava da una cella clampata anche quando l'origine era fuori dal box, riportando distanze e normali errate. La caratterizzazione P02a aveva già documentato i raggi paralleli, la distanza, il parent transform e i probe planetari discordanti.

Durante la prova del nuovo percorso è emersa anche una prima versione incompleta dell'insieme di eventi: includeva i confini dei chunk, ma non i confini dei voxel angolari interni al chunk. Un test tangenziale ha dimostrato il problema: il raycast segnalava il voxel alla sola entrata nello shell radiale. La versione finale aggiunge gli eventi sui piani di tutti i voxel U/V, e il hit tangenziale attraversa correttamente la seam +Z/+X e arriva al voxel a distanza 32.

## 3. Contratto e algoritmo

### Raycast cartesiano legacy

- `Cast` rifiuta input non finiti, distanza massima negativa e direzione nulla/quasi nulla; normalizza la direzione, quindi `distance` e `maxDistance` sono espressi in unità world.
- Lo slab calcola gli intervalli in `double`, tratta esplicitamente le componenti direzionali esattamente nulle e rifiuta box/input non validi.
- Il DDA parte dal punto d'ingresso nello spazio locale, rispetta l'intervallo AABB e il limite di distanza. Mantiene il parametro `t` world attraverso la trasformazione inversa senza normalizzare il vettore locale; le normali sono trasformate con la matrice inverse-transpose.
- I chiamanti legacy continuano a usare `RaycastHit::hit`; la semantica booleana è preservata.

### Raycast planetario Cube-Sphere

Quando `CastVoxel` rileva una sola `PlanetGeometryComponent` logica sferica, instrada la query al ramo planetario invece di usare AABB cartesiane.

1. Interseca il raggio con lo shell radiale esterno del pianeta e limita l'intervallo a `maxDistance`.
2. Genera eventi esatti sulle superfici sferiche che separano le celle radiali `localY` e sui piani passanti per il centro che separano ogni cella voxel U/V delle sei facce. I piani vengono considerati solo nella porzione della faccia corrispondente.
3. Ordina gli eventi; tra due eventi consecutivi Face/UV/Grid e `floor(localY)` restano costanti. Il punto medio dell'intervallo seleziona quindi una singola cella senza usare passi fissi o una griglia cartesiana planetaria.
4. Risolve Face/UV con `CubeSphereMapping`, griglia e chiave storage con `PlanetGridCompatibility`, e legge il block ID dal chunk identificato. Le celle Air/passabili vengono attraversate; il primo voxel solido restituisce hit, entity, coordinate net legacy, faccia, UV, punto, distanza e normale.
5. Un chunk assente produce `Missing`; un chunk presente ma non generato produce `Pending`; le celle interne fuori intervallo producono `OutOfBounds`. `Air`, `PassableVoxel`, `SolidVoxel` e input invalidi restano distinti nell'elaborazione interna.

La query non si affida a `WorldToVoxelCoord` per il raycast, perché il test ai bordi aveva mostrato una codifica legacy ambigua per alcune chart edge. L'identità storage resta convertita tramite l'adattatore canonico già esistente; nessuna chiave persistente è cambiata.

## 4. File e callsite

| File | Modifica |
|---|---|
| `src/systems/RaycastSystem.cpp` | Slab robusto; validazione/normalizzazione input; branch planetario per eventi radiali e angolari a granularità voxel; DDA legacy corretto per ingresso, distanza, transform e normale; stato di risultato per sphere hit. |
| `src/systems/RaycastSystem.h` | Aggiunti `RaycastResultStatus` e il campo `RaycastHit::status`; `hit` e gli altri campi esistenti restano disponibili. |
| `src/world/PlanetaryVoxelE2ETests.cpp` | Estesi i test CPU P02a e aggiunti probe planetari su sei taglie, facce, edges/corners, cella solida tangenziale cross-face e stati Missing/Pending/OOB. Il runner era già non tracciato prima di P02d. |

Callsite di produzione trovati: `PlanetMapperCamera.cpp` e `FAIRWORLD.cpp` invocano `RaycastSystem::Cast`. Non leggono il nuovo campo `status` e continuano a usare il booleano `hit`; pertanto non richiedono cambi di chiamata. `CastPhysics` resta placeholder e non è certificato da P02d.

## 5. Verifica e regressioni

### Build

Build isolata MSVC Debug con Visual Studio 18 2026, senza cambiare generatore o cache:

```powershell
& 'D:\Visual Code\vs\MSBuild\Current\Bin\MSBuild.exe' `
  'D:\FAIRWORLD\FAIRWORLD\build_r3z7\FairworldCore.vcxproj' /m:1 /v:quiet `
  /p:Configuration=Debug `
  /p:OutDir='D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p02d-core-bin\' `
  /p:IntDir='D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p02d-core-obj\'
```

Core Debug: **PASS, exit 0** (`p02d-core-build-validation_20261106.log`). Il runner diagnostico, copiato nel sandbox e collegato alla `FairworldCore.lib` isolata, ha compilato: **PASS, exit 0** (`p02d-runner-build-acceptance_20261106.log`). La build emette warning `MSB8028` sull'IntDir condivisa tra progetti generati e il messaggio GLM già noto; non sono errori e la build termina correttamente.

### Test finali

Comando eseguito due volte dalla working directory isolata:

```powershell
& 'D:\FAIRWORLD\D1_2_19D2_SANDBOX\runtime\p02d-runner-bin\FairworldPlanetaryVoxelE2ETests.exe' `
  --fixture 'D:\FAIRWORLD\D1_2_17_HEADLESS_SANDBOX\fixtures\TinySphere\world_map.json'
```

| Run / log | Exit | Durata | Risultato raycast |
|---|---:|---:|---|
| `p02d-complete1_20261106.log` | 0 | 11.197 s | 324/324 hit; faccia/chunk/prima cella/distanza/normale 324/324 ciascuno; `Pending=1`, `Missing=1`, tangenziale cross-face PASS a 32, OOB PASS. |
| `p02d-complete2_20261106.log` | 0 | 11.033 s | stessi risultati e hash mesh; `castNs=48,522,400`, media `149,760 ns/ray` per i 324 probe sintetici. |

I log includono e riportano PASS per P02a coordinate e query, P02b compatibilità legacy, P02c 72/72 edge diretti e 2.112/2.112 relazioni reciproche, Tiny CPU E2E e i controlli CPU D1.2.19d.5/d6 presenti nel runner. Risultati Tiny invariati nei due run: 54 chunk generati, 504.877 voxel non-Air, 54 mesh CPU, 297.756 vertici/indici, 99.252 triangoli, zero UV/normali/indici invalidi o mismatch di winding/associazione; `stableMeshHash=0xaa8de4f86721f55c`. Il baseline legacy P02c resta `0x20c0d276200e0e20` con 135.820 triangoli. La pubblicazione MeshComponent/VRAM/DMA/GPU è esplicitamente **NOT VERIFIED** dal runner.

### Copertura dei contratti P02d

| Caso | Esito | Evidenza |
|---|---|---|
| Sei facce × sei PlanetSize, centro, quattro edge e quattro corner | **PASS** | 324/324 hit con faccia, chunk, voxel, distanza 5.5 e normale attesi. |
| Raggi radiali su voxel solidi Tiny | **PASS** | Query autorevole e `CastVoxel` solidi 6/6. |
| Attraversamento tangenziale e seam +Z→+X | **PASS** | Fixture con Air generato sui 54 chunk; primo voxel corretto a distanza 32, limite inferiore miss e superiore hit, normale attesa. |
| Origine dentro/fuori voxel e AABB; parallelismo, bordo e maxDistance | **PASS** | Slab/DDA cartesiano: hit assiale a 4.5, origine esterna a 14.5, parallelo sul bordo a 4.5, cella half-open sul bordo miss, origine solida hit a 0 e clipping verificato. |
| Transform parent e scala cartesiani | **PASS** | Parent traslato hit; scale hit con distanza world 9 e distanza world invariata sotto scale uniforme. |
| Direzioni NaN/Inf e input invalido | **PASS** | Rifiutati senza hit; i test invalid-input confermano `InvalidInput`. |
| Air, Missing, Pending, OOB | **PASS** | Cella Air attraversata; storage assente → Missing; chunk esistente non generato → Pending; query nella cavità interna → OOB. |
| Distanza e normali planetarie | **PASS CPU** | 324/324 probe e attraversamento tangenziale con riferimento geometrico indipendente. |
| Raycast Sphere e query fisiche | **NOT VERIFIED** | Sphere mode e `CastPhysics` non fanno parte del contratto voxel slab P02d; nessun nuovo comportamento Sphere è incluso nel fix finale. |
| Fisica condivisa, GUI e GPU | **NOT VERIFIED** | Nessuna GUI/GPU avviata; nessuna modifica alle collisioni fisiche. |

## 6. Benchmark

Debug, stesso runner P02a, fixture Tiny, singola macchina/run; numeri descrittivi, non soglie prestazionali:

| Misura | P02a baseline | P02d final run 3 | P02d final run 4 |
|---|---:|---:|---:|
| Cast cartesiano isolato, 4.000 cast | 49.332.300 ns (12.333 ns/cast) | 43.482.100 ns (10.871 ns/cast) | 44.068.400 ns (11.017 ns/cast) |
| Raycast planetario completo, probe misti di sei taglie | Non disponibile: percorso precedente discordante (1/6 hit) | 56.522.500 ns / 324 (174.452 ns/ray) | 48.522.400 ns / 324 (149.760 ns/ray) |

Il confronto cartesiano è lo stesso workload diagnostico di 4.000 cast e varia circa −11,9%/−10,7% rispetto al valore P02a; la variazione è entro la natura rumorosa del benchmark Debug e non è una promessa di accelerazione. Non esiste un benchmark planetario P02a corretto da confrontare: la vecchia implementazione non restituiva risultati coerenti e il dato temporale non sarebbe comparabile. Il costo corrente include costruzione/sort degli eventi, mapping, lookup e traversal; non misura separatamente le fasi.

## 7. Limiti residui e API

- Il modello attuale supporta una sola `PlanetGeometryComponent` logica sferica e assume il centro planetario all'origine world, coerentemente con `GameWorld::QueryBlockCartesian`. Più pianeti, transform non identità del pianeta e un centro arbitrario non sono verificati e il raycast li rifiuta/non li rappresenta.
- Il layer radiale attraversato è `layer=0`, come nelle query planetarie autorevoli correnti. Layer multipli non sono coperti.
- Il contratto query esistente non distingue un chunk mancante da terreno in streaming; il nuovo risultato distingue `Missing` da `Pending` perché il ramo raycast ispeziona direttamente l'entità nel ChunkManager. Su un tratto non noto il raycast termina con quello stato anziché asserire un hit successivo.
- `RaycastHit::status` è un'estensione additiva della struct pubblica; i consumer osservati non dipendono dall'ordinamento binario o dall'assenza del campo e continuano a controllare `hit`. Una ricompilazione completa dei moduli che includono l'header non è stata eseguita: è stato compilato il target `FairworldCore` e il runner collegato.
- La verifica dei normali e delle distanze è CPU-only. Nessun claim su frame Vulkan, shader, GPU, input o sincronizzazione.

## 8. Decisione per P03

**GO condizionato per pianificare P03 Collision System**, perché il raycast CPU ora usa le coordinate planetarie canoniche e i casi Tiny, cross-face e legacy richiesti sono ripetibili. Prerequisiti: P03 deve mantenere l'assunzione di singolo pianeta centrato all'origine oppure definire prima il contratto per centro/transform e PlanetID multipli; `Missing` e `Pending` non devono essere trattati come solidi. La verifica runtime GUI/GPU resta un gate separato. P03 non è stato avviato.

**Planet Core non è dichiarato FROZEN.**
