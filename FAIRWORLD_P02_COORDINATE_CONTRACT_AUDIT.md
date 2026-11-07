# FAIRWORLD P02 — Planetary Coordinate Contract Audit

**Modalità:** audit read-only.  
**Esito:** audit statico completato; nessuna modifica funzionale, nessun test/build/GUI eseguiti.  
**Baseline di riferimento:** P01, HEAD `f9c7a3047bf3c8ecaad70de4f1af39943933eaf1`.

## 1. STEP 0 — Baseline Safety Gate

| Controllo | Risultato |
|---|---|
| HEAD | Invariato: `f9c7a3047bf3c8ecaad70de4f1af39943933eaf1`, branch `SPHERE-WORLD-EXPERIMENT`. |
| Inventario Git P01 vs corrente | **424/424 status path uguali**, 0 aggiunti, 0 rimossi, escludendo i tre report P01 creati dopo lo snapshot. La comparazione è stata fatta contro l'appendice di `FAIRWORLD_P01_BASELINE_REPORT.md`. |
| Hash dei 18 file critici P01 | **18/18 MATCH** con gli SHA-256 registrati nel rapporto P01: PlanetRadialMapping, CubeSphereMapping, MapWorldGenerator, GameWorld, VoxelChunkMesher, PhysicsEngine, Systems, PlayState, BlockMakerState, TexturePacker, RenderManager, shader Forge e rapporti D1.2.19d.5/.6/P00. |
| Modifiche funzionali dopo P01 | Nessuna rilevata nel working tree al momento del gate. I tre output P01 restano untracked come previsto. |
| Working tree | Rimane **DIRTY** con la stessa baseline preesistente P01; non è stato pulito né ripristinato. |
| Sicurezza analisi | **GO per audit read-only**: hash e path list critici non sono cambiati. **NO-GO per edit funzionali**: la baseline è ancora un insieme di modifiche locali non isolato da un commit. |

Nessun branch, worktree o commit è stato creato. Non è stato modificato alcun sorgente. I report P01 sono stati letti e i loro hash attuali sono registrati nell'output del gate; la comparazione di contenuto funzionale è basata sui path e sugli hash critici, non su un commit pulito.

## 2. Mappa dei contratti effettivi

### 2.1 Identità, dimensioni e net di chunk

`src/core/utils/PlanetMath.h` definisce:

- `CubeFace`: `+Z=0`, `-Z=1`, `+X=2`, `-X=3`, `+Y=4`, `-Y=5`.
- `PlanetSize` con risoluzioni per faccia `N={3,5,7,11,21,41}` per Tiny…Huge; `GetEditorCanvasExtents=(N-1)/2`, quindi `2E+1=N` per le sei dimensioni valide.
- `PlanetChunkCoord=(planet,face,col,row,layer)` con uguaglianza/hash su tutti i campi.
- Risoluzione faccia dispari e raggio `R=N·HorizontalWorldExtent/2`.

Il layout runtime/dati non è una griglia sferica contigua. `MapWorldGenerator` alloca le sei facce in un net rettangolare `3N×2N`, collocando la faccia `f` a `faceCol=f%3`, `faceRow=f/3`. Per la coordinata logica canonica `(face,col,row)` la chiave manager attuale è:

```text
legacyRow = N - 1 - row
globalX   = (face % 3) * N + col
globalZ   = (face / 3) * N + legacyRow
```

Il net è una rappresentazione di storage/lookup, non la topologia fisica dei vicini della sfera.

### 2.2 Face → UV → chart → direzione

`CubeSphereMapping` usa:

```text
cx = 2u - 1
cy = 1 - 2v
u  = (cx + 1)/2
v  = (1 - cy)/2
```

La mappa cubica, seguita da normalizzazione, è:

| Face | Cube direction non normalizzata `(x,y,z)` | Direzione positiva di col | Direzione positiva di row |
|---|---|---|---|
| `+Z` | `( cx, cy, +1)` | `+X` | `-Y` |
| `-Z` | `(-cx, cy, -1)` | `-X` | `-Y` |
| `+X` | `(+1, cy, -cx)` | `-Z` | `-Y` |
| `-X` | `(-1, cy, +cx)` | `+Z` | `-Y` |
| `+Y` | `( cx,+1, -cy)` | `+X` | `+Z` |
| `-Y` | `( cx,-1, +cy)` | `+X` | `-Z` |

`FaceChartToDirection` converte chart→UV e delega a `FaceUVToDirection`; i sei switch forward sono così centralizzati in `FaceUVToCubeDir`. `DirectionToFaceUV` sceglie l'asse dominante con tie-break deterministico Z, poi X, poi Y; i segni determinano il verso della faccia. Il tie-break sceglie un proprietario unico per direzioni su spigoli/vertici, ma non rende unica la topologia dei vicini in un angolo.

`GridToUV` restituisce i centri `(col+0.5,row+0.5)/N`. `UVToGrid` usa una ricerca binaria dei confini float `float(k)/float(N)` e clampa i risultati a `[0,N-1]`; i valori esattamente al confine appartengono alla cella successiva. L'algoritmo è deterministico per UV e risoluzione validi.

### 2.3 Row legacy e coordinate canoniche

`PlanetGridCompatibility::TryLegacyFaceRowToCanonicalRow` controlla `resolution>0` e `0<=legacyRow<N`, poi applica una riflessione:

```text
canonicalRow = N - 1 - legacyRow
```

Nel `MapWorldGenerator::generateChunk` l'adattatore è applicato nell'assegnazione `PlanetChunkCoord.row`; il chunk manager continua a usare globalX/globalZ legacy e non viene rinumerato. `TerrainSolverSystem` legge `PlanetChunkCoord.col/row` come coordinate canoniche. `PlanetaryMeshGenerator` calcola `v=(row·VoxelsZ+pos.z)/(N·VoxelsZ)`, coerente con row 0 sul bordo chart superiore. `GameWorld::QueryBlockCartesian` invece converte il punto world in coordinate del net legacy per il lookup manager. Questi due versi sono distinti ma hanno un ponte esplicito; il test identity Tiny documenta la relazione sopra.

Non è stata trovata una seconda conversione della row nel percorso `generateChunk → PlanetChunkCoord → TerrainSolver/PlanetaryMeshGenerator`. La conversione non è applicata alla chiave legacy del manager.

### 2.4 Coordinate radiali e trasformazioni

`PlanetRadialMapping` è la fonte condivisa per:

```text
g(layer, localY) = layer·VoxelsY + localY - VoxelsY/2
r               = R + g·VoxelWorldSize
cellCoordinate  = (r-R)/VoxelWorldSize + VoxelsY/2 + 0.5
```

Le celle hanno centro a `localY` intero e superficie esterna a `localY+0.5`. `MapWorldGenerator::WorldToVoxelCoord` applica questo mapping al raggio world; `GameWorld::TryResolvePlanetarySpawn` usa `GetCellOuterRadius`; `PlanetaryMeshGenerator` usa il medesimo offset/distanza. È quindi verificata staticamente la centralizzazione dei termini radiali principali. D1.2.19d.5 documenta la prova CPU Tiny: mesh top e collision/spawn a raggio `42.5`, delta `0` entro tolleranza `1e-4`; la prova non è stata rieseguita in P02.

### 2.5 Formule duplicate fuori dall'helper canonico

`MapWorldGenerator.cpp` ripete le formule faccia/asse in:

- `GetSphericalChunkTransform` (face→cube point→normalizzazione).
- `GetTrueSphericalPosition` (face→cube point→normalizzazione e raggio).
- `WorldToVoxelCoord` (dominant axis, proiezione, net legacy).
- `GetChunkCoordFromPosition` (dominant axis, proiezione e discretizzazione).

Le prime tre conversioni riproducono gli orientamenti face/chart della tabella, ma sono implementazioni separate da `CubeSphereMapping`; una modifica futura a tie-break, chart o orientamento può divergere. La formula radiale è invece delegata al helper condiviso.

`GetChunkCoordFromPosition` non ha callsite nel repository oltre alla dichiarazione/definizione. La sua discretizzazione `floor/ceil` non coincide in generale con `UVToGrid` sui centri/bordi: per esempio, su `+Z`, con `N=3` e chart `cx=-1/3` (`local_cx=-0.5` chunk), `floor(local_cx)+E=0`, mentre `UVToGrid` sul corrispondente UV assegna col 1 (`floor((cx+1)N/2)=1`). Questo conferma una **incoerenza matematica della funzione rispetto al contratto canonico**, ma il suo impatto runtime non è provato perché non esistono chiamanti attivi trovati. Va mantenuta come API legacy finché non è approvata una migrazione.

## 3. Continuità cross-face e chunk

`PlanetMath::GetNeighborCrossFace` contiene una tabella esplicita per i 24 edge orientati; `ResolveNeighbor` gestisce soltanto uno spostamento cardinale di una cella e conserva planet/layer. Confronto algebrico dei versi della tabella CubeSphere indica che gli indici di bordo e le inversioni dichiarate corrispondono agli assi chart riportati sopra. In P02 non sono stati eseguiti test esaustivi di reciprocità/vertici per certificarlo.

È confermato che `ResolveNeighbor` non è usato per fornire i vicini al mesher runtime. `GameWorld::Update`, quando costruisce il vicinato 3×3 per `VoxelChunkMesher::Build`, cerca `GetChunkEntity(chunk.cx+dx, chunk.cz+dz)` nel net rettangolare. Questo trova vicini storage adiacenti, non necessariamente i vicini topologici Cube-Sphere. Per esempio, il lato sinistro della faccia `+Z` confina fisicamente con `-X`, ma il net posiziona `-X` su una diversa riga/colonna non adiacente a `+Z` su quel lato. **La topologia cross-face non è quindi il criterio di vicinato del mesher attuale.** L'effetto (facce interne residue, campionamento AO/light mancante o seam) va misurato su fixture; non si assume che tutte queste conseguenze siano visibili.

`PlanetChunkCoord` è metadato logico, mentre `VoxelChunkComponent.cx/cz` e `WorldChunkManager` usano la chiave net legacy. Il test d'identità Tiny documenta 54/54 corrispondenze nella fixture. Questa separazione va preservata; `Transform.position` non deve diventare fonte dell'identità.

## 4. STEP 2 — Slab, broadphase, DDA e query

| Componente | Implementazione effettiva | Uso e coerenza geometrica |
|---|---|---|
| Ray/AABB intersection / slab | `RaycastSystem::IntersectAABB`, `RaycastSystem.cpp:14-25`: reciproco `1/rayDir`, calcolo `t0/t1`, min/max per asse. | Chiamata solo da `CastVoxel` come broadphase AABB per chunk. Non condivide un helper con la fisica. |
| Broadphase voxel ray | `RaycastSystem::CastVoxel`, `:55+`: scorre tutti i chunk con `VoxelChunkComponent,TransformComponent`; filtro distanza da `transform.location`; quindi inverte la global transform e interseca AABB locale `[-0.5, size-0.5]`. | Il filtro distanza usa `transform.location` locale con `query.ray.origin` world, mentre il passo successivo usa `computeGlobalMatrix`. Se il parent/transform ha traslazione diversa da zero, il filtro mescola spazi. Impatto dipendente dalla scena, non riprodotto in P02. |
| Voxel DDA | `RaycastSystem::CastVoxel`, `:93-169`: DDA 3D per assi, dentro singolo AABB/chunk; gli assi con dir=0 ricevono `float::max` per tDelta/tMax dopo l'intersezione. | DDA assume chunk voxel axis-aligned nel sistema locale del `TransformComponent`. Non usa `WorldToVoxelCoord` né `GameWorld::QueryBlockCartesian`. |
| Trasformazione ray | In `CastVoxel`: `invTransform` ottenuta dalla matrice globale; origine con w=1, direzione con w=0 e normalizzata. | Con trasformazioni non uniformi, la normalizzazione locale cambia il parametro distanza; `worldPosition` è ricostruita usando la direzione world e `currentDist`. Il caso non è coperto da test. |
| Query voxel world | `GameWorld::QueryBlockCartesian`, `GameWorld.cpp:887+`: in modalità sferica chiama `MapWorldGenerator::WorldToVoxelCoord`, discretizza il net con floor e lookup legacy; restituisce `SolidVoxel/Air/Passable/TerrainPending/OutOfBounds`. | È il percorso autorevole usato dalle probe collision sferiche e dalla generazione/radial mapping, ma non dal DDA di `RaycastSystem::CastVoxel`. |
| Sweep fisica cartesiana | `PhysicsEngine::SweepTest` e helper `SweptAABB`, `PhysicsEngine.cpp:646+`: sweep AABB per assi, gestisce esplicitamente velocità zero; campiona blocchi tramite `GameWorld::GetBlock`. | Distinto dalla collisione sferica attiva (`ResolveSphericalCollisions → QueryBlockCartesian`). Nessun riuso dello slab raycast. |
| Collisione sferica | `PhysicsEngine::ResolveSphericalCollisions`, `:741+`: footprint del collider trasformato nel frame planetario; ogni probe chiama `GameWorld::QueryBlockCartesian`. | Condivide il mapping punto world→voxel con `GameWorld`, non usa AABB/DDA. D1.2.19d.5/.6 hanno test CPU specifici per raggio e copertura probe. |

### Problemi statici confermati / impatto da verificare

1. **Slab parallelo senza guardia per direzione zero.** `IntersectAABB` divide per componenti `rayDir` zero; un raggio parallelo a un asse genera infinities e, quando origine coincide con il piano del limite, `0*inf` può produrre NaN. Il codice non tratta separatamente il caso. È confermato il gap di robustezza; il false miss per i diversi casi di bordo è **da riprodurre** con test mirati e comportamento GLM della toolchain.
2. **Raycast voxel non segue il mapping sferico autorevole.** I chunk planetari sono deformati in coordinate planet-local da `PlanetaryMeshGenerator` e i transform dei chunk sferici sono posti all'origine del pianeta; `CastVoxel` usa però un AABB locale rettangolare per ciascun chunk e legge indici DDA locali. Non chiama `QueryBlockCartesian`/`WorldToVoxelCoord`. Il mismatch dei modelli geometrici è confermato dal codice; il risultato di singoli raggi in runtime non è stato osservato.
3. **Broadphase in spazi diversi.** La distanza preliminare usa `TransformComponent.location` locale contro ray origin world, mentre l'intersezione usa la global matrix. Per parent traslati il filtro può scartare un chunk valido; nessuna scena traslata è stata testata.
4. **Trasformazioni non uniformi.** Il parametro DDA viene ricostruito come distanza world dopo normalizzazione in locale. Possibile errore di distanza/posizione per scale non uniformi; non risulta coperto e non è dimostrato un callsite planetario con tale scala.
5. **Broadphase non conservativa per la mesh planetaria.** L'AABB di `CastVoxel` descrive l'array di voxel planare, mentre la mesh planetaria CPU è warped sulla sfera e assoluta rispetto al centro. È un'altra manifestazione del punto 2, non un difetto autonomo della formula slab.

Il raycast `Auto` usa modalità `Voxel` in Play e Chunk Editor, `Sphere` in Planet Mapper; perciò il fatto che Planet Mapper colpisca la preview sferica non dimostra che `CastVoxel` interroghi correttamente chunk planetari di PlayState.

## 5. Problemi confermati e ipotesi

### Confermati staticamente

- Il net 3×2 è storage legacy; `PlanetChunkCoord` row è canonica e si converte a legacy una volta durante la registrazione manager.
- `MapWorldGenerator` ripete formule direzionali già centralizzate in `CubeSphereMapping`.
- `GetChunkCoordFromPosition` è non chiamata e ha esempi di discretizzazione in contrasto con `UVToGrid`.
- `ResolveNeighbor` codifica la topologia cross-face, ma il mesher risolve vicini con coordinate adiacenti del net; la topologia logica non è usata in quel callsite.
- Il ray slab non ha ramo per direzioni parallele/zero; `CastVoxel` non valida all'ingresso origine/direzione finite/nonzero.
- Il ray DDA usa una rappresentazione locale planare distinta dal mapping radiale di `GameWorld::QueryBlockCartesian`.
- `PlanetRadialMapping` è condiviso nei callsite centrali di query, spawn e mesh. D1.2.19d.5 documenta delta radiale CPU zero nel campione Tiny; P02 non ha rieseguito il test.

### Ipotesi/impatti non verificati

- Falsi miss slab per raggi assiali e origini sul piano AABB.
- Seams geometrici/occlusione/AO errati dovuti a vicini del net invece di vicini cross-face.
- Culling prematuro del raycast per pianeti traslati o coordinate locali.
- Errori nei test d'angolo o reciprocità della tabella `ResolveNeighbor`.
- Disallineamenti numerici dovuti a formule duplicate nei punti di confine e alla selezione asse dominante, pur condividendo oggi il tie-break Z/X/Y.
- Scala/parent transform non uniforme nei chunk runtime.

## 6. Test presenti e mancanti

### Presenti nel tree o documentati (P02 non li ha eseguiti)

- `TerrainValidation::TestD12GridRowsAndBounds`: sei PlanetSize, `2E+1=N`, conversione legacy/canonica ogni riga, rifiuto `-1`, `N`, risoluzione zero nell'adattatore, centri UV, endpoint e tie-break ai confini con `nextafter`.
- Test D1.2 di chart/direction/UV, sei facce, tie-break deterministico agli edge/corner e compatibilità spawn JSON/binaria.
- `PlanetaryVoxelE2ETests`: identità Tiny 54 chunk tra coordinate canoniche e chiavi legacy; fixture CPU, query radiali, mesh CPU e cammini che attraversano un confine Cube-Sphere; d5/d6 hanno test documentati per round-trip radiale, collisione, timestep e footprint.
- `GameplayLifecycleStressTests` usa `GetNeighborCrossFace` per una singola verifica di bordo (`Face::Top`, edgePos 0, Medium), non una matrice topologica completa.
- Nessun test `RaycastSystem::IntersectAABB` o DDA dedicato è stato individuato nei target sorgente test ispezionati.

### Da aggiungere/progettare

1. Face→UV/chart→direction→face round-trip per sei facce, tutti i centri, bordi, corner e sei taglie; tie-break e tolleranze espliciti.
2. Comparazione indipendente tra formule `MapWorldGenerator` e `CubeSphereMapping` per tutti i punti campionati; forward/inverse `WorldToVoxelCoord` ↔ punto chunk/voxel e `GetTrueSphericalPosition` per tutte le facce/taglie.
3. `GetChunkCoordFromPosition` contro `UVToGrid` su interni, centri, `nextafter` dei confini, bordi e facce; mantenere test separati del comportamento storico se l'API resta legacy.
4. Test esaustivi `GetNeighborCrossFace`/`ResolveNeighbor`: 24 edge × posizioni, reciprocità, inversioni, tutte le taglie; vertici con più facce candidate e politica esplicita, senza fingere un unico vicino diagonale.
5. Test d'identità net: `(face,col,row)↔(globalX,globalZ)` bijettivo 6·N² per ogni dimensione; storage key invariata e PlanetChunkCoord conservata.
6. Test radial mapping su cella, faccia superiore, lati di layer, layer negativi/positivi, core classification, `nextafter`, mesh/query/spawn stesso voxel.
7. Mesher neighbor test attraverso ogni faccia fisica, verificando che il vicinato logico scelto corrisponda a `ResolveNeighbor` e che nessun dato topografico sia duplicato o perso.
8. Slab unit tests per tutte le direzioni assiali e quasi parallele, raggio dentro/fuori, origine sui sei piani AABB, corner, zero/NaN/infinite, t min/max e hit origin-in-box.
9. DDA tests per direzione positiva/negativa, start inside, tiebreak su edge/corner voxel, confini chunk e cross-face; confrontare la sequenza con query pointwise della geometria autorevole.
10. Transform tests: rotazione, traslazione parent, scala uniforme/non uniforme; controllare distanza hit in world e broadphase conservativa.

## 7. Invarianti matematiche

1. Per ogni PlanetSize valida: `N∈{3,5,7,11,21,41}`, `E=(N-1)/2`, `2E+1=N`.
2. UV valida in `[0,1]²`; chart è `(2u-1,1-2v)` e la direzione faccia è normalizzata e unitaria.
3. `DirectionToFaceUV(FaceUVToDirection(face,uv))` recupera la stessa UV interna alla faccia; sugli spigoli/corner la faccia inversa segue tie-break deterministico e quindi non necessariamente la faccia di input.
4. Griglia canonica: `col=floor(uN)` e `row=floor(vN)` con clamp endpoint; esatto confine `k/N` assegnato alla cella k, float immediatamente sotto a k-1.
5. Conversione legacy/canonica involutiva: `row=N-1-legacyRow`, solo per indici in `[0,N-1]`.
6. Chunk identity bijettiva: il tuple face/col/canonicalRow identifica una sola chiave manager `(globalX,globalZ)` via `legacyRow=N-1-row`; il transform fisico non determina l'identità.
7. Mapping radiale: centri voxel interi, faccia esterna a mezzo voxel; generazione, query e mesh devono assegnare lo stesso raggio al medesimo voxel.
8. Topologia logica cross-face non coincide con adiacenza aritmetica nel net storage.
9. Il broadphase deve essere conservativo nella stessa geometria/spazio del narrowphase; i raggi paralleli non possono essere scartati per divisione indefinita.

## 8. Rischi di regressione

- Cambiare la row nel manager romperebbe fixture, salvataggi o lookup legacy; mantenere conversione sul solo campo PlanetChunkCoord.
- Sostituire `GetChunkCoordFromPosition` con la regola canonica senza stabilire se esistono consumer esterni può cambiare API legacy anche se il repository non la chiama.
- Centralizzare formule faccia può spostare il tie-break ai confini e cambiare indirizzamento delle facce.
- Usare `ResolveNeighbor` nel mesher senza adattare la chiave globale potrebbe confondere coordinate logiche con storage e creare alias.
- DDA planetario basato sulla mesh warp e collisione pointwise devono concordare sui confini radiali e face tie-break.
- Correggere slab/broadphase senza test di start-inside, t parametro e transform potrebbe risolvere raggi assiali ma alterare selezione/drag.
- Estendere test globali o build potrebbe scrivere in `assets/`, `saves/`, `build_r3z7`; P02 non l'ha fatto.

## 9. Proposta di correzioni minime (non applicate)

1. **P02a — test prima delle patch:** aggiungere esclusivamente test CPU di neighbor topology, `GetChunkCoordFromPosition`, query/DDA/slab e mapping mesh/query, fissando i risultati legacy correnti prima di modificarli.
2. **P02b — helper conversioni:** sostituire le duplicazioni di face direction con delega a `CubeSphereMapping` solo dove i test dimostrano identità; lasciare il net legacy come adapter nominato.
3. **P02c — vicinato mesher:** introdurre un risolutore esplicito PlanetChunkCoord→chiave storage per i vicini cross-face; non modificare layout o serializzazione del net.
4. **P02d — query ray:** prima definire se il raycast planetario deve interrogare voxel sulla sfera o mesh locale; quindi testare slab robusto e broadphase nello stesso spazio. Non fare la patch in P02.
5. **P02e — input validation:** documentare e rifiutare direzioni zero/nonfinite, enum face/size invalidi e risoluzioni <=0 in un task dedicato, con compatibilità chiamanti e test prima di restringere API.

Ordine suggerito: P02a → P02b/P02c con edit seriali per `MapWorldGenerator`/`GameWorld`/mesher → P02d → validazione CPU da sandbox. D1.2.19d.5/.6 va mantenuto come baseline di comportamento e rieseguito dopo ogni patch di query/collisione.

## 10. Decisione GO / NO-GO

**GO per la progettazione e l'aggiunta di test CPU isolati in una fase autorizzata successiva. NO-GO per una patch funzionale non ulteriormente approvata.**

La baseline P01 è rimasta invariata sugli hash/path critici e l'audit identifica callsite e mismatch concreti senza alterare codice. Prima di intervenire occorre trasformare le ipotesi di impatto (slab sul piano, seam del mesher, ray DDA planetario) in test deterministici. La funzione `GetChunkCoordFromPosition` ha una discrepanza formula/canonico documentabile ma nessun callsite interno: preservare per ora il comportamento legacy e caratterizzarlo. Nessun test è stato rieseguito, nessuna GUI è stata avviata e Planet Core non è dichiarato FROZEN.
