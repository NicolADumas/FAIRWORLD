# D1.1 — Mathematical Convention Validation

**Stato:** READ-ONLY · nessuna correzione applicata · nessun test/build eseguito  
**Ambito esclusivo:** A) segno verticale spawn point; B) orientamento righe canonico vs legacy; C) contratto proposto; D) piano minimo di correzione.  
**Decisione:** richiede revisione/approvazione prima di qualsiasi patch.

## Sintesi esecutiva

1. **Spawn:** per il contratto del mapper canonico, la produzione UI ha il segno errato. `PlanetMapperUI::Draw` salva `localZ/R = 2v-1`, mentre `PlanetMapperState::RenderApp` e `PlayState::Init`/`PlayState::Render` lo interpretano come `cy` della chart, per la quale `cy=1-2v`. Pertanto un hit a `v` viene ricostruito a `1-v`. I due consumer concordano fra loro e con `CubeSphereMapping`; la prima divergenza è il producer. Il significato dei campi `localX/localZ` non è documentato nello schema, quindi questa conclusione vale adottando il contratto canonico proposto sotto.
2. **Righe:** le formule di faccia del legacy sono allineate alla chart cubica canonica per il segno della coordinata orizzontale `cx`; la coordinata verticale locale `cy` è anch’essa la stessa coordinata chart. La divergenza avviene quando il legacy assegna `row`: incrementa `row` insieme a `cy`, mentre `CubeSphereMapping` definisce `row` crescente insieme a `v`, cioè al diminuire di `cy`. Risultato: a parità di faccia e centro cella, l’indice legacy `row` corrisponde alla riga canonica riflessa `N-1-row`.
3. **Compatibilità:** le due correzioni possono essere locali, ma non è possibile inferire dal solo `localZ` persistito se un vecchio spawn deriva da una hit o da una modifica manuale del cursore. Una migrazione retroattiva richiede provenienza/versione o una decisione esplicita di compatibilità.
4. Il contratto canonico proposto mantiene `CubeSphereMapping` e definisce `col` crescente con U, `row` crescente con V (verso basso nella chart). Il documento non definisce né anticipa la topologia cross-face di D6.

## Evidenze e classificazione

- **Fatto verificato:** formula presente nel sorgente e percorso chiamante identificato.
- **Conclusione matematica:** conseguenza algebrica delle formule verificate.
- **Decisione di contratto:** convenzione proposta; non è un fatto circa l’intento storico dei dati.
- **Non verificato:** richiede esecuzione o test d’integrazione; non viene presentato come comportamento runtime osservato.

# A — Spawn Point Vertical Sign

## Percorso e funzioni

| Passo | File/funzione | Sistema di riferimento e formula | Segno verticale |
|---|---|---|---|
| Hit → face/UV | `src/states/components/PlanetMapperCamera.cpp`, aggiornamento della hit (circa riga 115); mapper `CubeSphereMapping::DirectionToFaceUV` in `src/world/CubeSphereMapping.cpp:8-34` | La hit fornisce `faceIndex` e `uv`. La chart usa `cx=2u-1`, `cy=1-2v`. | `v` cresce verso il basso; `cy` positivo verso l’alto della chart. |
| Creazione spawn | `src/states/components/PlanetMapperUI.cpp`, `PlanetMapperUI::Draw`, righe 200-219 | Da hit: `localU=2u-1`, `localV=2v-1`; poi `localX=R*localU`, `localZ=R*localV`. | `localZ/R=2v-1=-cy`: positivo verso V/basso, opposto a `cy`. |
| Marker preview | `src/states/PlanetMapperState.cpp`, `PlanetMapperState::RenderApp`, righe 242-253 | `cx=localX/R`, `cy=localZ/R`; `uv=((cx+1)/2,(1-cy)/2)`; poi `FaceUVToDirection(face,uv)`. | Legge `localZ/R` come `cy`, positivo verso l’alto della chart. |
| Spawn iniziale | `src/states/PlayState.cpp`, `PlayState::Init`, righe 168-184 circa | `cx=localX/R`, `cy=localZ/R`; switch delle sei facce identico a `FaceUVToCubeDir`, quindi normalizza. | Legge `localZ/R` come `cy`, positivo verso l’alto della chart. |
| Teletrasporto | `src/states/PlayState.cpp`, `PlayState::Render`, righe 595-630 circa | Stessa lettura `cx/cy` e stesso switch delle facce dello spawn iniziale. | Come sopra. |
| Persistenza | `src/world/MapDocument.cpp`, serializzazione/deserializzazione spawn in JSON e binario | Conserva `localX` e `localZ` numerici; non registra quale formula o origine li ha prodotti. | Nessuna conversione del segno rilevata nel percorso di persistenza. |

La UI permette anche di modificare `localZ` tramite uno slider denominato “Offset Z”. Lo schema `SpawnPoint` conserva un `float localZ`, ma non dichiara se sia la coordinata chart `cy` o una coordinata immagine `2v-1`. La convenzione canonica viene quindi stabilita dal mapper e dai consumer, non dal nome del campo.

## Verifica algebrica con esempio

Per un hit su qualunque faccia con `v=0.25`:

- Canonico: `cy = 1 - 2(0.25) = +0.5`.
- Producer attuale: `localZ/R = 2(0.25)-1 = -0.5`.
- Consumer attuale: `cyRead = localZ/R = -0.5`; quindi `vRead = (1-cyRead)/2 = 0.75`.

Per la faccia `+Z`, `u=0.5`, l’hit atteso è la direzione normalizzata di `(0,+0.5,1)`; entrambi i consumer la ricostruiscono dalla direzione `(0,-0.5,1)`. La differenza è una riflessione sulla coordinata locale verticale della faccia, non un cambiamento di faccia.

## Determinazione: produzione o interpretazione?

**Conclusione matematica rispetto al contratto canonico:** il difetto primario è nella produzione delle coordinate in `PlanetMapperUI::Draw`: il producer scrive `2v-1`, ma i campi vengono successivamente consumati come `cy=1-2v`. `PlanetMapperState` e `PlayState` applicano entrambi le formule inverse canoniche e concordano anche con `FaceUVToCubeDir`.

**Ambiguità documentale verificata:** manca una dichiarazione esplicita dello spazio/significato di `SpawnPoint.localZ`. Se il contratto storico volesse invece `localZ/R=2v-1`, allora entrambi i consumer sarebbero da adattare e il dato non rappresenterebbe `cy`. Questo contratto non coincide però con la convenzione già usata dai consumer e dal mapper canonico. La patch minima raccomandata è quindi allineare il producer a `localZ=R*(1-2v)` e lasciare i consumer canonici coerenti, previa approvazione del contratto.

**Compatibilità legacy:** dopo la correzione del producer, gli spawn nuovi da hit si ricostruiscono nel punto cliccato. I valori già salvati restano numericamente invariati e continuano a indicare il punto determinato dalla lettura attuale del consumer; per gli spawn hit-originati questo può essere l’emisfero verticale opposto a quello cliccato. Non si può migrare automaticamente ogni record senza sapere se è stato generato da hit o modificato manualmente. Sono necessarie una decisione esplicita e, se si vuole migrazione retroattiva, provenienza/versionamento o conversione assistita.

# B — Canonical vs Legacy Row Orientation

## B.1 Chart canonica `CubeSphereMapping`

In `src/world/CubeSphereMapping.cpp:8-34`, `DirectionToFaceUV` sceglie l’asse dominante fra `|z|`, `|x|`, `|y|`. In parità, la sequenza di confronti stretti `>` dà priorità **Z > X > Y**. La divisione per l’asse dominante produce una faccia cubica indipendente dalla lunghezza della direzione (per input finiti non nulli).

Definiamo `p=d/max(|x|,|y|,|z|)`. Le coordinate canoniche sono:

| Face / indice | Centro | `cx` | `cy` | Direzione `U+` (col crescente) | Direzione `V+` (row crescente) |
|---|---|---|---|---|---|
| `+Z` / 0 | `(0,0,+1)` | `p.x` | `p.y` | `+X` | `-Y` |
| `-Z` / 1 | `(0,0,-1)` | `-p.x` | `p.y` | `-X` | `-Y` |
| `+X` / 2 | `(+1,0,0)` | `-p.z` | `p.y` | `-Z` | `-Y` |
| `-X` / 3 | `(-1,0,0)` | `p.z` | `p.y` | `+Z` | `-Y` |
| `+Y` / 4 | `(0,+1,0)` | `p.x` | `-p.z` | `+X` | `+Z` |
| `-Y` / 5 | `(0,-1,0)` | `p.x` | `p.z` | `+X` | `-Z` |

Le direzioni U+/V+ in tabella sono derivate dagli assi della chart cubica ai centri faccia; la proiezione normalizzata sulla sfera è curva fuori dal centro, ma mantiene l’orientamento locale.

Forward chart→cube (`CubeSphereMapping::FaceUVToCubeDir`, righe 38-52):

```text
cx = 2u - 1
cy = 1 - 2v

+Z: ( cx,  cy, +1)     -Z: (-cx,  cy, -1)
+X: (+1,   cy, -cx)    -X: (-1,   cy,  cx)
+Y: ( cx, +1,  -cy)    -Y: ( cx, -1,   cy)
```

La direzione sferica è `normalize(FaceUVToCubeDir(face, uv))`. U crescente significa chart verso destra secondo le sei direzioni U+. V crescente/row crescente significa verso il basso nella chart, secondo le direzioni V+ sopra.

Inverse direzione→faccia/UV: scelto l’asse dominante e la faccia, si ricavano `(cx,cy)` dalla tabella di orientamento; poi `u=(cx+1)/2`, `v=(1-cy)/2`.

## B.2 UV e discretizzazione canonica

In `CubeSphereMapping::UVToGrid` (righe 58-61) il codice attuale calcola `int(u*N)` e `int(v*N)`, poi clampa a `[0,N-1]`. Nel dominio finito `[0,1]`, la conversione positiva a `int` equivale a `floor`; gli endpoint 1 sono assegnati all’ultima cella.

Contratto matematico esplicito proposto, equivalente nell’input valido al codice attuale:

```text
N > 0
q = clamp(uv, 0, 1)                 // componenti finite
col = min(floor(q.u * N), N - 1)
row = min(floor(q.v * N), N - 1)
GridToUV(col,row,N) = ((col+1/2)/N, (row+1/2)/N)
```

Gli indici devono appartenere a `[0,N)`. Per una UV esattamente su una linea interna `k/N`, la regola `floor` assegna la cella dal lato crescente (`k`); per `u=1`/`v=1`, il clamp assegna l’ultima cella. Il contratto non assegna proprietà di vicinato fra facce.

## B.3 Implementazioni e chiamanti legacy

| Implementazione | Formula/convenzione verificata | Relazione con il canonico |
|---|---|---|
| `MapWorldGenerator::GetSphericalChunkTransform`, `:238-286` | Facce cubiche costruite esplicitamente; coordinate centrate `cx,cy` da `-extent` a `+extent`; chunk center locale viene normalizzato a raggio R. La griglia 3×2 usa `face_col=face%3`, `face_row=face/3`. | I sei segni dei componenti locali corrispondono alla chart cubica. Il `cy` locale cresce verso `+Y` sulle facce laterali. Gli indici globali `row` crescono con `cy`, dunque in verso opposto al row canonico. |
| `MapWorldGenerator::GetTrueSphericalPosition`, `:289-328` | Stesse formule faccia per i componenti locali; aggiunge `local_x-8` e `local_z-8` per collocare i campioni intorno al centro chunk; normalizza la posizione. | Medesima opposizione di riga. La formula radiale `R+(local_y-25)` è fuori dall’oggetto di questa validazione, salvo come contesto preesistente. |
| `MapWorldGenerator::WorldToVoxelCoord`, `:331-373` | Selezione faccia con tie-break `Z, X, Y`; rapporti direzionali espliciti; `local_cx/local_cy = (cx/cy chart) * R/16`; appende entrambi in modo crescente a `global_cx/global_cy`. | Il `local_cx` coincide con `cx` canonico in scala chunk. Il `local_cy` coincide con `cy` canonico in scala chunk, ma viene sommato alla coordinata di riga crescente. Per il row canonico servirebbe il verso opposto. |
| `MapWorldGenerator::GetChunkCoordFromPosition`, `:376-425` | Ripete selezione e rapporti; applica combinazioni faccia-specifiche `floor`/`ceil`, clampa a `[-extent,+extent]`, poi appende `cx/cy` alla griglia 3×2. | Stessa opposizione nel segno della coordinata assegnata al `row`. Le regole floor/ceil sono una discretizzazione legacy separata: non sono la regola canonica UV→Grid e non vanno implicitamente sostituite senza test di compatibilità. |
| `MapWorldGenerator::Generate`, `:176-197` e lambda chunk `:68-104` | Enumerazione `cy=-extent..+extent`; `global_cz=face_row*stride+(cy+extent)`; `planetCoord.row=local_cz+extent`. | Prova diretta che il numero di riga legacy aumenta insieme al `cy` locale positivo. |
| `PlanetaryMeshGenerator::ConvertToPlanetaryPositions`, `src/world/PlanetaryMeshGenerator.cpp:10-30` | `v=(planetCoord.row*VoxelsZ+pos.z)/(N*VoxelsZ)`, poi `FaceUVToDirection`. | Row aumenta con V, cioè il chart `cy=1-2v` diminuisce. Orientamento opposto alla numerazione row legacy per la medesima faccia. |
| `TerrainSolver::GetVoxelSpherePos`, `src/world/TerrainSolver.cpp:82-104` | `u=globalX/faceGridResolution`, `v=globalZ/faceGridResolution`, chiamata diretta a `FaceUVToDirection`. | Campioni in direzione canonica: Z globale crescente equivale a V crescente e chart cy decrescente. Nessun mezzo voxel aggiunto nel percorso ispezionato. |
| `PlanetMapperUI::Draw`, griglia celle circa `:365-470` | Centro cella: `cx=2(col+0.5)/N-1`, `cy=1-2(row+0.5)/N`; costruisce UV e chiama `FaceUVToDirection`. | Coincide col canonico. `gridY`/row cresce verso V positiva/basso. |
| `SphericalLOD` (sezione grid mapping circa `:230-270`) | Selezione faccia dominante e formule per `cx/cy`; `gridRow=floor((1-cy)*0.5*N)`. | Orientamento verticale equivalente al canonico; formula duplicata, non centralizzata. |
| `terrain_generation.comp::normalToFaceUV`, `:154-171` e chiamante `main` circa `:200-220` | GLSL restituisce coordinate chart in `[-1,+1]`, tie-break `Z >= X >= Y` in ordine Z poi X poi Y; segni faccia della tabella canonica. Il chiamante converte il chart in UV/coordinate per il resto del compute. | Formule di orientamento compatibili col canonico ispezionate; non risulta un test di equivalenza CPU/GLSL. |
| `PlanetMapperCompiler` | Costruisce angoli con UV e `FaceUVToCubeDir`. | Canonico; gli intervalli riga Y usano gli estremi UV della cella. |
| `PlayState::Init` e `PlayState::Render` | Switch locale delle sei formule `(cx,cy)` per gli spawn, seguito da normalizzazione. | Formula d’inversa conforme a `FaceUVToCubeDir`; la duplicazione con il mapper crea rischio di deriva, ma non è una convenzione diversa. |
| `PlanetMapperState::RenderApp` | Convert local `cx/cy`→UV canonica→direzione. | Canonico; il dato prodotto con segno attuale è la divergenza descritta in A. |

### Direzione col/row e esempio numerico

Nel contratto canonico, `col` crescente è U crescente (tabella facce); `row` crescente è V crescente, cioè `cy` decrescente. Nel mapping legacy 3×2, `col` crescente aumenta il locale `cx` e quindi è allineato con U; `row` crescente aumenta il locale `cy` e quindi è opposto a V. La disposizione delle facce in una griglia 3×2 è solo un atlas di chunk e non definisce adiacenza topologica.

Esempio su `+Z`, `N=11`, `extent=5`, cella `(col=2,row=0)`:

- Canonico centro UV: `u=2.5/11≈0.22727`, `v=0.5/11≈0.04545`; quindi `cx≈-0.54545`, `cy≈+0.90909` e direzione cubica `(-0.54545,+0.90909,1)` prima della normalizzazione.
- Legacy: il centro chart della stessa colonna è `cx=2-5=-3` chunk, cioè `2cx/N≈-0.54545`; per `row=0`, `cy=0-5=-5` chunk, cioè `2cy/N≈-0.90909`; direzione cubica `(-0.54545,-0.90909,1)` prima della normalizzazione.
- La direzione legacy corrisponde al centro canonico di `(col=2,row=10)`. La relazione è la riflessione `rowCanonical = N-1-rowLegacy` per i centri di griglia, non una rotazione delle facce.

**Impatto staticamente dimostrato:** la stessa coppia face/row non individua lo stesso centro di faccia nei percorsi legacy e canonico.  
**Non verificato:** se ogni specifico percorso runtime incrocia tali coordinate nel modo sufficiente a produrre un errore visibile o di gameplay. È necessario il test di round-trip/integrazione indicato sotto.

## B.4 Spigoli e angoli — limite dell’ambito

Una UV di bordo (`u/v=0` o `1`) è valida sulla faccia esplicitamente passata. Nella conversione inversa di una direzione, un bordo o angolo può corrispondere a più facce; l’implementazione sceglie deterministicamente la faccia con priorità dominante Z>X>Y. La direzione ricostruita è la stessa anche se la chart scelta cambia.

Per un lookup discreto, il contratto proposto usa `floor` e saturazione endpoint sopra descritti. Questa scelta determina una cella per la coppia faccia/UV; non specifica come raggiungere celle di un’altra faccia, come trattare vicini diagonali o come attraversare una cucitura. Tali regole topologiche rimangono a D6 e non sono definite né implementate qui.

# C — Contratto matematico proposto

## C.1 Orientamento unico

Adottare la tabella canonica della sezione B.1 senza modificare l’enumerazione corrente. Per ogni faccia:

- `u=0.5,v=0.5` è il centro e indica l’asse faccia.
- `u` crescente definisce U+/col positivo.
- `v` crescente definisce V+/row positivo.
- La coordinata cubica verticale è sempre `cy=1-2v`.
- Eventuali campi locali salvati come coppia chart `(cx,cy)` devono usare `localX/R=cx` e `localZ/R=cy`; i nomi X/Z non significano assi globali del mondo.

## C.2 Forward e inverse

Forward:

```text
(cx,cy) = (2u-1, 1-2v)
q = FaceCubeVector(face,cx,cy)   // tabella esplicita in B.1
D = q / length(q)
```

Inverse, per `d != 0`, finita:

```text
m = max(abs(dx), abs(dy), abs(dz))
p = d / m
face = asse dominante con priorità Z > X > Y e segno del componente
(cx,cy) = FaceChartCoordinates(face,p)  // tabella B.1
(u,v) = ((cx+1)/2, (1-cy)/2)
```

Il round-trip è geometrico: `FaceUVToDirection(face,uv)` deve essere uguale, entro tolleranza angolare, alla direzione ottenuta da `DirectionToFaceUV` e viceversa. Sui bordi la faccia assegnata dall’inversa può cambiare per tie-break; non è richiesto conservare la faccia originale se la direzione giace su più facce.

## C.3 UV → cella

Per `N>0`, UV finite e faccia valida, limitare UV al quadrato e applicare `floor(u*N)`, `floor(v*N)`, saturando il caso endpoint a `N-1`. La cella inversa usa il centro `(col+0.5,row+0.5)/N`. Fuori dominio e input non finiti dovranno avere status/error policy definita in una patch separata; questa proposta non pretende che il codice attuale li validi.

## C.4 Bordi e angoli

- Una UV esattamente sul bordo resta appartenente alla faccia/UV esplicita e viene assegnata all’ultima o prima cella secondo la saturazione endpoint.
- Una direzione esattamente su uno spigolo/angolo viene assegnata dalla regola dominante deterministica Z>X>Y.
- La continuità richiesta è della direzione ricostruita, non dell’etichetta della faccia o della coppia UV fra chart diverse.
- Nessuna adiacenza cross-face o ownership delle celle vicine viene introdotta: è fuori contratto D1.1 e riservata alla topologia D6.

# D — Piano correttivo minimo

## 1. Bug matematici dimostrati

1. **Producer spawn:** sostituire, dopo approvazione, la codifica verticale del click con quella della chart canonica: `localZ=R*(1-2*v)`; mantenere `localX=R*(2*u-1)`. Documentare che il campo è coordinata chart locale, non world-Z.
2. **Numerazione legacy row:** quando coordinate di griglia/face `PlanetChunkCoord.row` vengono passate a un consumer canonico `row→v`, introdurre la conversione mirata `rowCanonical=N-1-rowLegacy` (o convertire una sola volta al confine dell’API). Non invertire le formule di faccia, che sono già orientate coerentemente.
3. **Non duplicare una correzione in ogni consumer:** scegliere un boundary adapter per i valori legacy e lasciare un solo contratto interno. Non è richiesta una sostituzione generale delle formule duplicate per correggere questi due bug.

## 2. Adattamenti necessari nei consumer

- Per nuovi spawn, `PlanetMapperState` e `PlayState` possono mantenere le formule canoniche correnti; raccomandato che entrambe usino la stessa funzione di conversione per impedire divergenza, ma è un adattamento di consolidamento separato dalla correzione algebrica.
- Per le coordinate row, identificare al boundary il punto preciso in cui `MapWorldGenerator`/`GameWorld` trasferisce coordinate legacy verso `PlanetaryMeshGenerator`/`PlanetChunkCoord`. Applicare una sola riflessione in quel boundary solo se il chiamante è effettivamente legacy. Non riflettere di nuovo `TerrainSolver`, PlanetMapper UI/compiler, LOD o shader, che usano già V+/row-down canonico.
- Verificare prima dell’implementazione se i chunk attualmente generati sono sempre `PlanetChunkCoord` canonici o se qualche percorso conserva coordinate legacy flatten; il codice mostra entrambi i sistemi ma non giustifica un’inversione indiscriminata a tutti i consumer.

## 3. Compatibilità dei dati legacy

- Non migrare automaticamente `SpawnPoint.localZ` senza una decisione: hit-origin e modifiche manuali hanno la stessa forma persistita e non sono distinguibili.
- Opzioni da sottoporre ad approvazione: (a) lasciare i record esistenti con l’attuale interpretazione e correggere solo nuovi spawn; (b) migrazione esplicita/versionata dei soli record che hanno provenienza hit certa; (c) utility di conversione assistita dall’utente. I dati senza provenienza non devono essere riflessi silenziosamente.
- Per griglie legacy, mantenere un adapter di lettura al confine, se documenti/coordinate salvate usano row-up. Non riscrivere serializzazione dei dati senza test di fixture e mappa preesistente.

## 4. Test di regressione da introdurre

### Spawn

1. Parametrizzare le sei facce e UV rappresentative (`center`, `v=0.25`, `v=0.75`, `u/v` prossimi a 0 e 1). Simulare producer UI → serializzazione JSON e binaria → `PlanetMapperState` marker e `PlayState` spawn/teleport; la direzione ricostruita deve coincidere con `FaceUVToDirection(face,uv)` entro epsilon angolare.
2. Testare `localX/localZ` esplicitamente: `localX/R=2u-1`, `localZ/R=1-2v`; cambiare `v` in `1-v` deve riflettere solo la componente verticale della chart, non U o face.
3. Separare test di compatibilità dei record legacy manuali da test per record creati da hit; non fare migrazione implicita nel test.

### Face / UV / Grid / row

4. Per tutte le sei facce e tutte le celle per più risoluzioni dispari (almeno 3, 11, 41), verificare `cell→GridToUV→FaceUVToDirection` e che la conversione inversa indichi stessa faccia/cella per i centri interni.
5. Verificare in modo mirato la relazione legacy/canonica ai centri: `canonicalRow=N-1-legacyRow`, stessa colonna e direzione entro tolleranza per tutte le facce e righe/colonne.
6. Eseguire round-trip world direction → legacy `GetChunkCoordFromPosition` e inverse `GetTrueSphericalPosition`/posizione chunk per punti interni ai centri, includendo limiti di cella per caratterizzare le regole `floor`/`ceil` legacy prima di sostituirle.
7. Testare UV ai bordi e agli angoli: endpoint cella saturato, tie-break dominante stabile, direzione uguale entro tolleranza. Nessuna asserzione di vicinato cross-face in D1.1.
8. Comparare gli orientamenti CPU (`CubeSphereMapping`, mapper legacy, LOD) e shader con fixture condivise o un test offline, includendo assi U+/V+ e tie-break esatto.

## Limiti e richiesta

Questo rapporto verifica formule e chiamanti staticamente; non dimostra quale vecchio documento sia stato creato via hit né quale percorso runtime mostri già la riflessione. Le proposte di migrazione/adapter sono subordinate a una verifica d’integrazione e all’approvazione del contratto.

**Nessuna modifica al codice, ai dati, a Foundation, Block Maker, ECS, World o JointPhysics. Nessun commit o push. Attendere approvazione prima delle patch.**
