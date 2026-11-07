# FAIRWORLD — D1.2.10 GUI Observability Audit

## Esito e limiti

Audit statico READ-ONLY. Non sono state avviate sessioni GUI, modificate sorgenti/test/salvataggi o alterate configurazioni. Nessun commit o push. D1 non è dichiarata FROZEN.

**Risultati principali:** non esiste un controllo per selezionare direttamente la faccia della camera; la camera è orbitale e la faccia del colpo è derivata dal raycast. `RaycastHit` contiene i valori necessari, ma la GUI non li visualizza e il valore live viene invalidato fuori dalle condizioni del viewport. Il messaggio del catalogo deriva da un filtro di uguaglianza tra `PlanetSize`, e i dati osservati hanno una dimensione pianeta diversa da quella dell'unico template. I nomi di taglia in Planet Mapper sono disallineati dagli enum e hanno conseguenze pratiche sul filtro e sul valore salvato.

## A — Orientamento delle sei facce

### Controlli e stato esistenti

- `PlanetMapperCamera::Update` controlla orbita e zoom: trascinamento mouse per yaw/pitch, rotella per distanza, `W/S` per distanza, `A/D` per yaw e `R/F` per pitch. Non mantiene una variabile `active CubeFace`; mantiene `m_orbitYaw`, `m_orbitPitch`, `m_orbitDistance` e `m_lastRayHit` in `src/states/components/PlanetMapperCamera.h`.
- `PlanetMapperState::Init` inizializza la camera a distanza 250, pitch 20°, yaw −45°. Il `RaycastHit.faceIndex` deriva dal punto colpito, non da una selezione della camera.
- L'unico controllo esplicito di faccia in Planet Mapper è il combo `Faccia Base` degli spawn. I sei valori sono `+Z, -Z, +X, -X, +Y, -Y`; cambia lo spawn, non la camera. Le tab del placement editor selezionano anch'esse una faccia per l'editing delle istanze, non costituiscono un controllo di orientamento camera.
- `Show Orientation Debug` visualizza marker sugli assi cardinali (`PlanetMapperState.cpp`, circa righe 320–365). È una guida visiva, non un display numerico della faccia/UV corrente.
- Il cursore marker mostra il punto d'impatto e la normale quando `m_lastRayHit.hit` è true. Non vengono mostrati `faceIndex`, UV, chart o cella.

### Tabella di osservazione

La numerazione effettiva viene da `CubeFace` in `src/core/utils/PlanetMath.h:17–24`, e il mapping centro faccia da `CubeSphereMapping::FaceUVToCubeDir` in `src/world/CubeSphereMapping.cpp:47–64`.

| Indice | Faccia | Normale/direzione attesa al centro UV `(0.5,0.5)` | Campione effettivo da acquisire |
|---:|---|---|---|
| 0 | `+Z` / PositiveZ | `(0, 0, +1)` | `hit.faceIndex`, `hit.faceNormal`, `hit.uv`; confronto con `DirectionToFaceUV` |
| 1 | `−Z` / NegativeZ | `(0, 0, −1)` | Idem |
| 2 | `+X` / PositiveX | `(+1, 0, 0)` | Idem |
| 3 | `−X` / NegativeX | `(−1, 0, 0)` | Idem |
| 4 | `+Y` / PositiveY | `(0, +1, 0)` | Idem |
| 5 | `−Y` / NegativeY | `(0, −1, 0)` | Idem |

Per ogni faccia il controllo utile è acquisire almeno il hit vicino al centro della faccia e confrontare indice, normale normalizzata e UV con il mapping canonico. I marker cardinali e il combo spawn, da soli, non provano quale faccia il raycast stia selezionando. Non sono state acquisite nuove osservazioni GUI in questo audit.

## B — Percorso raycast e coordinate

### Percorso verificato

```text
mouse / viewport
  → PlanetMapperCamera::Update
  → costruzione ray da mouse NDC + view/projection inverse
  → RaycastQuery (attualmente RaycastMode::Voxel)
  → RaycastSystem::Cast → CastVoxel
  → hit.worldPosition / faceNormal
  → DirectionToFaceUV(normalize(hit.worldPosition), faceIndex, uv)
  → m_lastRayHit
  → PlanetMapperState::RenderApp passa il hit corrente a PlanetMapperUI::Draw
  → Aggiungi Spawn: UV → FaceUVToChartCoordinates → localX/localZ
```

Riferimenti: `src/states/components/PlanetMapperCamera.cpp:18–29,86–120`; `src/systems/RaycastSystem.h:28–43,50–65`; `src/systems/RaycastSystem.cpp:28–51,187–221`; `src/states/PlanetMapperState.cpp:379–389`; `src/states/components/PlanetMapperUI.cpp:28–35,204–220`; `src/world/CubeSphereMapping.cpp:8–15,17–45`.

Il `RaycastHit` espone già `hit`, `worldPosition`, `faceNormal`, `faceIndex` e `uv`. La funzione di camera converte il punto in direzione, poi chiama `DirectionToFaceUV`; il chart si calcola con `FaceUVToChartCoordinates`. La cella si può calcolare con `UVToGrid(uv, GetFaceResolution(size), col, row)`. Nessuna di queste coordinate è esposta in un pannello debug corrente.

### Perché il hit scompare

`PlanetMapperCamera::Update` consente il raycast solo se `!ImGuiIO::WantCaptureMouse` e il mouse è nella porzione destra (x almeno 45% della larghezza). Il raycast viene poi saltato se `ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)` è true. Nel ramo alternativo la funzione imposta esplicitamente `m_lastRayHit.hit = false`. Inoltre, se il raycast restituisce miss, il risultato miss viene assegnato interamente a `m_lastRayHit`.

Di conseguenza, quando si sposta il puntatore sopra il pannello per premere `Aggiungi Punto di Inizio`, il hit ottenuto sopra il globo può essere già perso. `PlanetMapperUI.cpp:208–216` usa il hit live solo se valido; altrimenti crea uno spawn di fallback sulla faccia `+Z`, chart `(0,0)`. Questo spiega perché la UI non può mostrare o consumare un hit precedente dopo aver spostato il puntatore. Non dimostra un errore della matematica UV.

### Strumentazione minima proposta

Per verificare senza influenzare selezione o raycast, preferire un log diagnostico **solo sui hit validi**, limitato a cambi di faccia/cella (per evitare un log per frame), con:

`camera position/direction, worldPosition, faceNormal, faceIndex, UV, chart, N, col, row`.

La cattura dovrebbe avvenire subito dopo `DirectionToFaceUV` in `PlanetMapperCamera.cpp`; chart e griglia sono calcolabili con gli helper già esistenti. Il log non deve riassegnare `m_lastRayHit`, bloccare la perdita del hit né essere usato dal pulsante spawn. Si può così spostare poi il puntatore al log/UI senza perdere l'evidenza già stampata. Per una futura UI interattiva, un pannello read-only potrebbe mostrare uno snapshot “ultimo hit valido”, ma deve rimanere distinto dal hit live consumato per creare spawn. È più invasivo del log e può confondere dati correnti e stale; non è la prima scelta.

## C — Catalogo e terreno

### Causa del messaggio

`PlanetMapperUI::Draw` distingue due casi: libreria vuota (`Nessun modello chunk presente nella libreria`) e libreria non vuota ma senza entry della dimensione esatta del pianeta (`Nessun modello compatibile`). Il filtro è `doc.terrainLibrary[i].planetSize != currentPlanetSize` (`src/states/components/PlanetMapperUI.cpp:172–190`). Quindi il messaggio osservato indica che la libreria è non vuota e nessun `PlanetSize` coincide; non indica da solo un asset mesh mancante.

Nel JSON originale del progetto letto durante l'audit, il pianeta ha `planetSize=0` e l'unico template `Terreno Standard` ha `planetSize=2`. La coppia originale è rimasta verificata dal manifest SHA-256 del sandbox D1.2.8. Nel documento **di prova sandbox** dopo la precedente validazione, il pianeta è `planetSize=1` e il template è ancora `planetSize=2`. Entrambe le coppie non superano il confronto esatto.

Il template è incorporato nel campo `terrainLibrary` del documento mappa JSON/binario (`src/world/MapDocument.cpp:448–481,587–640,881–902`), non scoperto da un catalogo di modelli esterno in `assets`. Il progetto ha una entry `Terreno Standard` con regole di base; il sandbox contiene assets/texture/block definitions. Non è stata trovata una dipendenza di asset esterna che spieghi il testo UI. Non è stato modificato alcun file per questo audit.

### Fixture isolato proposto

Per la fase di verifica successiva, creare una **copia dedicata** del documento mappa nella sottocartella fixture del sandbox, senza usare i salvataggi originali. Partire dal template `Terreno Standard` esistente, creare fixture coerenti con il `PlanetSize` selezionato (una copia template per taglia solo se serve verificare tutte le taglie), e lasciare invariati i documenti di prova correnti. Il fixture deve includere una cella interna e celle di bordo piazzabili, permettendo di provare selezione/rendering. Non creare nuovi modelli o modificare il catalogo di progetto in questa fase.

## D — Teletrasporto

- **Spawn iniziale:** `PlayState::Init` legge il primo spawn del pianeta, ricava `chart=(localX/R, localZ/R)`, chiama `FaceChartToDirection`, e assegna `spawnPos = dir * (R + heightOffset)` (`src/states/PlayState.cpp:159–181`). Esiste già un log della posizione iniziale custom.
- **Teletrasporto UI:** il pannello “Teletrasporto” elenca tutti gli spawn. Il click ricalcola la stessa direzione/posizione, assegna `RigidBodyComponent::body.position`, azzera la velocità e aggiorna `TransformComponent` e valori precedenti (`PlayState.cpp:581–620`). Non stampa posizione precedente, target o delta e il pannello non li mostra.
- **Due spawn distinti:** il Planet Mapper permette di aggiungere più spawn e modificare ciascuno `faceIndex`, `localX`, `localZ`, `heightOffset`, poi salvare (`PlanetMapperUI.cpp:200–253`). Il runner sandbox può quindi usare due destinazioni distinte, su facce o chart diversi, senza dati originali. Non sono stati creati spawn in questo audit.
- **Misura disponibile:** il log iniziale è disponibile, ma non un log pre/post del click. Un debugger con breakpoint nel ramo del pulsante può ispezionare i valori esistenti; per una prova ripetibile, la strumentazione temporanea più chiara è una singola riga diagnostica in `PlayState.cpp` con posizione pre-click, target calcolato, posizione post-assegnazione e `delta = post - pre`. Non cambiare formule o assegnazioni.
- `PortalSystem::CheckAndTeleport` esiste, ma la ricerca statica mostra solo la definizione e nessun call site in `src`; non è un percorso di teleport runtime dimostrato dall'audit.

## E — Nomenclatura `PlanetSize`

### Mapping effettivo

| Valore persistito | Enum in `PlanetMath.h` | Risoluzione | Etichetta Planet Mapper attuale |
|---:|---|---:|---|
| 0 | Tiny | 3 | Tiny |
| 1 | Micro | 5 | Small |
| 2 | Small | 7 | Medium |
| 3 | Medium | 11 | Large |
| 4 | Large | 21 | Huge |
| 5 | Huge | 41 | Gigantic |

L'enum e le risoluzioni sono in `src/core/utils/PlanetMath.h:71–90`. Planet Mapper dichiara le etichette `Tiny, Small, Medium, Large, Huge, Gigantic` e assegna direttamente `static_cast<PlanetSize>(currentSizeIndex)` (`PlanetMapperUI.cpp:119–123`). Chunk Editor invece mostra i nomi enum coerenti con le risoluzioni, inclusi Micro e Huge (`src/states/ChunkEditorState.cpp:745–749`). I serializer JSON scrivono/leggono l'indice intero dell'enum sia per pianeta che per template (`MapDocument.cpp:455,490,593,650–651`); il binario conserva anch'esso l'intero (`MapDocument.cpp:885,902,983,1011`).

**Conclusione:** il valore persistito è l'ordinale dell'enum, perciò i dati non contengono la stringa “Gigantic”. La discrepanza non è una diversa risoluzione implementata: è una nomenclatura Planet Mapper fuori allineamento (Micro omesso, Gigantic introdotto). È però un'incoerenza effettiva dell'interfaccia e del flusso operativo, non innocua solo presentazione: scegliendo la voce mostrata `Small`, viene salvato `Micro` (valore 1, risoluzione 5), e il filtro template confronta proprio quel valore con quello persistito del template. Questo può generare `Nessun modello compatibile` mentre l'utente crede di aver selezionato la dimensione corrispondente.

## F — Matrice e piano minimo

| Requisito | Verificabile oggi | Evidenza / lacuna |
|---|---|---|
| Normali attese delle sei facce | Sì, staticamente | Enum e mapping al centro faccia; non è un'osservazione GUI dei sei hit. |
| Faccia selezionata dal raycast | Parzialmente | `RaycastHit` la contiene; non esiste display numerico e l'hit è live/volatile. |
| UV → chart → griglia | Staticamente/calcolo, non in GUI | Helper disponibili; nessun valore visualizzato. |
| Hit valido dopo aver mosso il cursore al pannello | No | Update assegna `hit=false`; l'UI usa fallback. |
| Catalogo compatibile | Sì staticamente | Filtro enum equality; documenti esaminati non hanno taglia template uguale a quella pianeta. |
| Target e delta teleport | In codice, non osservabile in GUI | Posizione iniziale loggata; click non espone pre/post/delta. |
| Tutte le dimensioni per UI | Sì dal mapping statico | Etichette Planet Mapper errate rispetto all'enum; persiste intero. |

### Proposta ordinata

1. **Solo diagnostica raycast:** log condizionale in `src/states/components/PlanetMapperCamera.cpp` sui hit validi e quando cambia cella/faccia. Riutilizzare `CubeSphereMapping` e `PlanetMath`; nessun cambio alla query o al hit che consuma lo spawn.
2. **Fixture catalogo sandbox:** creare una copia di prova del JSON con template compatibile per la dimensione numerica effettivamente selezionata; nessuna modifica al catalogo originale o agli asset.
3. **Diagnostica teleport:** log pre/target/post/delta in `src/states/PlayState.cpp` nel ramo click, senza alterare assegnazioni. Usare due spawn nel documento fixture sandbox.
4. **Risoluzione nomenclatura separata:** in una patch futura, allineare le sei etichette di Planet Mapper all'enum (`Tiny, Micro, Small, Medium, Large, Huge`) senza rinumerare enum o migrare dati; aggiungere test di mapping label→ordinal→risoluzione. Questa è una correzione UI/semantica distinta dall'osservabilità e richiede approvazione esplicita prima dell'implementazione.

### File che richiederebbero modifica solo per strumentazione

- Log raycast: `src/states/components/PlanetMapperCamera.cpp` (header solo se serve stato diagnostico per deduplicare le righe; si può anche loggare su cambi confrontati con valori locali).
- Log teletrasporto: `src/states/PlayState.cpp`.
- Il fixture della fase successiva sarebbe un file dati separato nel sandbox, non un sorgente.
- L'eventuale correzione label futura toccherebbe `src/states/components/PlanetMapperUI.cpp` e relativi test UI/mapping, non la matematica planetaria né il formato persistente.

### Rischi e rollback

- I log per-frame possono saturare stdout e influenzare la leggibilità; limitare per cambio di cella/faccia o attivare solo in build diagnostica.
- Un valore “ultimo hit valido” mostrato senza indicare che è storico può indurre l'operatore a usarlo come hit corrente. Non usare tale snapshot per creare spawn; preferire inizialmente log-only.
- Un fixture di catalogo può testare solo la copia specificata; verificare path/CWD e hash originali prima e dopo come in D1.2.9.
- Il mismatch label può far salvare un indice diverso da quello atteso dall'utente; evitare di rinominare o riscrivere vecchi indici senza una patch esplicita e test di compatibilità.
- Rollback: rimuovere il solo log diagnostico o il pannello aggiunto nella patch temporanea; eliminare/archiviare solo i fixture di test nel sandbox in una fase autorizzata. Non ripristinare né riscrivere salvataggi originali, che non sono stati toccati.

## Evidenze consultate

- `src/core/utils/PlanetMath.h:17–24,71–102`
- `src/world/CubeSphereMapping.cpp:8–64`
- `src/states/components/PlanetMapperCamera.h` e `.cpp:18–120`
- `src/systems/RaycastSystem.h:28–65`, `src/systems/RaycastSystem.cpp:28–51,187–221`
- `src/states/PlanetMapperState.cpp:130–158,320–389`
- `src/states/components/PlanetMapperUI.cpp:119–123,158–190,200–253,293`
- `src/states/PlayState.cpp:159–181,581–620`
- `src/world/MapDocument.cpp:448–500,587–651,881–902`
- `src/world/WorldProjectManager.cpp:17–56`
- `src/states/ChunkEditorState.cpp:745–749`
- Original JSON e copia sandbox del documento mappa; inventario/hash originale già verificato nel rapporto D1.2.9.
