# D1.2 — Final Pre-Patch Verification

**Modalità:** READ-ONLY  
**Riferimenti:** `D1_PLANET_SPATIAL_AUDIT.md`, `D1_1_MATHEMATICAL_CONVENTIONS.md`, proposta D1.2  
**Esito operativo:** nessun sorgente o dato modificato; nessun test/build eseguito; nessun commit/push. Questo rapporto è l’unico file creato.

## Sintesi

- Per tutte le sei taglie attuali, `N` è dispari e `E=GetEditorCanvasExtents=(N-1)/2`. I loop di generazione percorrono esattamente `local_cz∈[-E,+E]`, quindi `legacyRow=local_cz+E∈[0,N-1]` e `canonicalRow=N-1-legacyRow∈[0,N-1]`.
- La riflessione matematica è un’involuzione: applicata una volta al confine legacy→canonico produce gli indici validi; una seconda applicazione li riporterebbe al valore legacy e ricreerebbe il disallineamento.
- `PlanetChunkCoord` non risulta serializzato nel repository. La riflessione modifica i valori della coordinata runtime e quindi la sua identità secondo `operator==`/`PlanetChunkCoordHash`, ma non cambia `PlanetChunkInstance.gridY`, i dati JSON/binari di `MapDocument` o gli indici flatten `chunk.cx/cz`.
- `MapWorldGenerator` è l’unico producer runtime di coordinate planetarie valide trovato. `TerrainSolverSystem` e `GameWorld`/`PlanetaryMeshGenerator` le consumano; non c’è un secondo adattatore row nel percorso individuato.
- Per gli spawn, lasciare invariati consumer e dati persistiti preserva la posizione attualmente visualizzata dei vecchi record, se il helper sostituisce esattamente la stessa conversione dei consumer. Il producer corretto cambierebbe soltanto i nuovi valori creati da hit.
- **Punto da aggiungere ai test proposti:** verifiche esplicite dei due input fuori intervallo dell’adattatore e controllo end-to-end che la riga non sia riflessa una seconda volta.

## A — Grid Bounds

### Formula verificata

Da `src/core/utils/PlanetMath.h`:

```text
N = GetFaceResolution(size)
E = GetEditorCanvasExtents(size) = (N - 1) / 2
local_cz = -E, ..., +E                    // loop in MapWorldGenerator::Generate
legacyRow = local_cz + E
canonicalRow = N - 1 - legacyRow
```

L’intervallo generato di `local_cz` ha `2E+1=N` valori. La traslazione `+E` lo porta in `0..2E = 0..N-1`; la riflessione lo riordina in `N-1..0` senza modificarne l’insieme.

### Risultati per tutte le taglie

| PlanetSize | `N` da `GetFaceResolution` | `E` da `GetEditorCanvasExtents` | `local_cz` generati | `legacyRow` | `canonicalRow` nell’ordine del loop | Intervallo valido canonico |
|---|---:|---:|---:|---:|---:|---:|
| Tiny | 3 | 1 | `-1..+1` | `0..2` | `2, 1, 0` | `[0,2]` |
| Micro | 5 | 2 | `-2..+2` | `0..4` | `4, 3, 2, 1, 0` | `[0,4]` |
| Small | 7 | 3 | `-3..+3` | `0..6` | `6, 5, 4, 3, 2, 1, 0` | `[0,6]` |
| Medium | 11 | 5 | `-5..+5` | `0..10` | `10..0` | `[0,10]` |
| Large | 21 | 10 | `-10..+10` | `0..20` | `20..0` | `[0,20]` |
| Huge | 41 | 20 | `-20..+20` | `0..40` | `40..0` | `[0,40]` |

Quindi ogni valore prodotto dal loop è in `[0,N-1]` sia prima sia dopo l’adattatore.

### Casi fuori intervallo

La formula di conversione, considerata isolatamente, non valida l’input. Per un passo fuori dai limiti:

```text
local_cz = -E - 1 → legacyRow = -1 → canonicalRow = N
local_cz = +E + 1 → legacyRow = N  → canonicalRow = -1
```

Entrambi gli output canonici sono fuori `[0,N-1]`. Valori arbitrariamente più esterni restano fuori intervallo; la riflessione non è una saturazione.

**Fatto verificato sul chiamante:** `MapWorldGenerator::Generate` itera `cy` inclusivamente da `-E` a `+E`, calcola il locale chunk `local_cz=cy` e lo passa alla lambda `generateChunk`. Per quel percorso di produzione i due casi fuori intervallo non sono generati.

**Rischio della proposta:** l’helper inline proposto in D1.2 (`N-1-row`) non ha guardia. La sicurezza dipende dal pre-requisito `0≤row<N`. In implementazione andrà scelto esplicitamente un contratto: assert/status per input errato oppure precondizione documentata con test del chiamante. Non usare clamp, perché maschererebbe un errore d’indice e farebbe collassare più righe su un bordo.

**Enum non valido:** `GetFaceResolution` restituisce 11 nel ramo default e `GetEditorCanvasExtents` conseguentemente 5. La coppia rimane aritmeticamente coerente per i limiti, ma l’enum non è rifiutato: un valore `PlanetSize` corrotto può quindi produrre una griglia Medium invece di un errore. È comportamento esistente, non introdotto dall’adattatore.

## B — PlanetChunkCoord: creazione, persistenza, chiavi e consumer

### Costruzione e assegnazione individuate

| Punto | File/funzione | Uso |
|---|---|---|
| Default componente | `src/components/ForgeComponents.h:180-186`, `VoxelChunkComponent` | Inizializza `planetCoord` con `PlanetID::Invalid()`, faccia +Z e coordinate zero. È un sentinel, non una coordinata planetaria attiva. |
| Producer runtime | `src/world/MapWorldGenerator.cpp:68-105`, lambda `generateChunk` | Per facce 0–5 assegna `planet`, `face`, `col=local_cx+E`, `row=local_cz+E`, `layer=0`. Per chunk non planetari assegna `PlanetID::Invalid()`. È il solo assegnamento produttivo diretto trovato. |
| Neighbor value | `src/core/utils/PlanetMath.h:170-216`, `ResolveNeighbor` | Copia una `PlanetChunkCoord`, cambia col/row o face/col/row e restituisce il valore. La ricerca nel repository non trova chiamanti di `ResolveNeighbor` oltre alla sua definizione; non è un producer runtime osservato. |
| Fixture test | `src/world/terrain/validation/TerrainValidation.cpp:835-850` | Costruisce valori aggregati artificiali per uguaglianza/hash; non alimenta la world generation. |

### Serializzazione e dati persistenti

La ricerca di `PlanetChunkCoord`, `planetCoord` e di serializzatori di `VoxelChunkComponent` nel codice applicativo non trova serializzazione/deserializzazione di questo tipo.

`MapDocument` persiste un tipo distinto, `PlanetChunkInstance`, con `faceIndex`, `gridX`, `gridY` (`src/world/MapDocument.h`, struttura `PlanetChunkInstance`; JSON e binario in `src/world/MapDocument.cpp`). Non vi è un campo `PlanetChunkCoord` nel formato del documento. Le coordinate `global_cx/global_cz` usate per i chunk flatten e il manager sono anch’esse separate.

**Conclusione:** la riflessione proposta in `MapWorldGenerator::generateChunk` non riscrive identità persistenti né `gridY` salvati. Cambia la coordinata `row` del componente ECS runtime prodotto al caricamento/generazione del mondo. Alla successiva rigenerazione, il valore riflesso viene ricostruito dal documento e dalla conversione nel codice.

### Uguaglianza, hash e lookup

`PlanetChunkCoord::operator==` confronta tutti i campi, incluso `row` (`PlanetMath.h:33-44`). `PlanetChunkCoordHash` incorpora tutti i campi, incluso `row` (`:46-56`). Pertanto una riflessione cambia il valore di uguaglianza/hash del chunk rispetto alla build precedente, tranne quando la riga è centrale (`row=(N-1)/2`).

Nel repository ispezionato:

- `PlanetChunkCoordHash` è definito e usato nel test `TestPlanetChunkCoord`; non è stata trovata una mappa/cache runtime indicizzata con questo hash.
- Non è stato trovato un lookup runtime per `PlanetChunkCoord`; il chunk manager continua a identificare chunk con coordinate flatten `cx/cz`.
- Non è stata trovata un’operazione che confronti direttamente un `PlanetChunkCoord` con `global_cx/global_cz`, `chunk.cx/cz` o `PlanetChunkInstance.gridX/gridY`. I confronti trovati sono fra due `PlanetChunkCoord` e fanno parte dell’uguaglianza/hash del tipo o dei test.
- `PlanetChunkCoord` resta comunque una **identità logica runtime**: confronti futuri o codice esterno che conservi valori della convenzione legacy potrebbero non corrispondere ai valori canonici dopo la patch.

### Passaggio a terrain, mesh e rendering

1. `MapWorldGenerator::generateChunk` assegna il valore runtime.
2. `TerrainSolverSystem::Update` (`src/systems/TerrainSolverSystem.cpp:56-59`) legge `face`, `col`, `row`, `layer` senza conversione e li copia nel `TerrainGenerationContext`.
3. `TerrainSolver::GetVoxelSpherePos` usa le coordinate del contesto per calcolare U/V; il row ricevuto è consumato direttamente come asse V canonico.
4. `GameWorld` chiama `PlanetaryMeshGenerator::ConvertToPlanetaryPositions` (`src/world/GameWorld.cpp:583-584`); il generatore calcola `v=(row*VoxelsZ+pos.z)/(N*VoxelsZ)` in `src/world/PlanetaryMeshGenerator.cpp:22-27`.
5. `RenderManager` controlla se `PlanetID` è valido e stampa i campi per diagnostica (`src/render/RenderManager.cpp:1175, 1228-1238, 1329-1332`); non converte la riga.
6. `PlanetMath::ResolveNeighbor` assume coordinate faccia/cella nell’intervallo e interpreta row 0 come bordo Top per `dRow=-1`; non applica la riflessione. Non è stato trovato un callsite runtime di questa funzione nel repository.

`GameWorld::GetBlockCartesian`, `GetBlock`, `SetBlock` chiamano `MapWorldGenerator::WorldToVoxelCoord` e poi accedono ai chunk flatten (`src/world/GameWorld.cpp:776, 800, 897 e percorsi vicini`). Tale mapping legacy resta nel sistema di coordinate flatten: la proposta converte soltanto `PlanetChunkCoord.row` al passaggio dalla lambda di generazione al consumer canonico.

## C — Patch Safety

### Spawn e vecchi salvataggi

**Producer:** la ricerca di `spawnPoints.push_back`, assegnamenti a `sp.localX/localZ` e coordinate spawn nei consumer trova `PlanetMapperUI::Draw` come producer da hit; il ramo senza hit crea la posizione centrale `(0,0)`. Gli slider ImGui modificano direttamente i valori locali, senza trasformazioni UV aggiuntive. `MapDocument` JSON/binario li deserializza e serializza come numeri.

Se si aggiungono helper equivalenti a:

```text
FaceUVToChart(uv) = (2u-1, 1-2v)
FaceChartToDirection(face,(cx,cy))
  = FaceUVToDirection(face, ((cx+1)/2, (1-cy)/2))
```

allora i consumer usano la stessa interpretazione numerica attuale (`localZ/R` è `cy`). Per un vecchio record invariato, l’input del consumer e la direzione risultante restano invariati. La correzione del producer cambia solo il numero scritto per i nuovi spawn creati da hit.

**Condizione di sicurezza:** non aggiungere conversione di segno al caricamento, ai consumer o alla serializzazione. Una migrazione o seconda inversione cambierebbe i vecchi valori effettivi. Il significato storico/intento del click nei record precedenti resta non recuperabile senza metadati, ma la loro posizione attualmente visualizzata è preservabile.

### Helper chart e duplicazioni

La proposta è sicura rispetto alla deriva se le formule vengono centralizzate una volta in `CubeSphereMapping`:

- producer UI: `FaceUVToChartCoordinates`;
- preview e Play: `FaceChartToDirection`;
- `FaceChartToDirection` delega a `FaceUVToDirection`, senza copiare i sei switch.

Gli helper devono essere la fonte comune; mantenere in UI/Play/PlanetMapperState un’altra formula manuale lascerebbe duplicazione e indebolirebbe questa garanzia. `FaceChartToDirection` deve avere la stessa gestione degli enum invalidi di `FaceUVToDirection` oppure dichiarare un contratto più rigoroso tramite test; non introdurre fallback di segno differenti.

### Altri producer spawn

Nella ricerca del codice applicativo non risultano altri producer che convertano un hit UV in `SpawnPoint.localX/localZ`. Sono presenti il default centrale, il caricamento da documento e gli slider manuali; nessuno di questi richiede una conversione producer aggiuntiva. I due consumer attivi individuati sono `PlanetMapperState::RenderApp` e `PlayState::Init`/`PlayState::Render`.

### Doppia inversione della riga

Il percorso sicuro proposto applica `N-1-row` **una volta sola** nella lambda `MapWorldGenerator::generateChunk`, al confine da `legacyRow` a `PlanetChunkCoord.row` canonico. `TerrainSolverSystem`, `TerrainSolver` e `PlanetaryMeshGenerator` ricevono/usano il row canonico invariato. `WorldToVoxelCoord`, `GetChunkCoordFromPosition` e la griglia flatten legacy non devono ricevere un’inversione aggiuntiva.

Un secondo adattatore in `TerrainSolverSystem` o `PlanetaryMeshGenerator` annullerebbe la riflessione e ricreerebbe la discrepanza. Il test d’integrazione deve controllare il valore finale del contesto e la direzione del mesh, così da rilevare anche un doppio adapter.

### Copertura test richiesta

La proposta precedente copre facce, centri, edge/corner, conversione legacy/canonica e round-trip. Per verificare integralmente questo pre-patch audit aggiungere esplicitamente:

- Tutte le 6 risoluzioni/taglie e i due estremi `local_cz=-E,+E` → `legacyRow=0,N-1` → `canonicalRow=N-1,0`.
- Tutte le righe valide per le sei taglie; round-trip dell’adattatore inverso.
- Due casi fuori range per ogni taglia: `legacyRow=-1` e `legacyRow=N`, che producono rispettivamente `canonicalRow=N` e `-1`; assert/status deve rifiutarli, non clamping.
- Centri cella per tutte le facce e round-trip UV↔grid; test su UV ai limiti 0 e 1 e linee interne `k/N` secondo la regola `floor`/endpoint.
- Bordi e angoli su tutte le facce: direzione ricostruita uguale entro tolleranza e tie-break deterministico, senza test di adiacenza topologica D6.
- Per il row adapter: confronto della direzione legacy di tutti i centri con quella canonica per stessa colonna e `rowCanonical=N-1-rowLegacy`, più asserzione che il row passato da `TerrainSolverSystem` al mesh mapping è già canonico e non viene riflesso una seconda volta.
- Fixture spawn JSON e binaria pre-patch con confronto dei campi grezzi e della direzione effettiva pre/post helper; producer nuovo verificato su tutte le sei facce.

Non ho eseguito questi test. Il loro stato è **proposti, non verificati**.

## Risultato finale

| Verifica | Esito read-only |
|---|---|
| Limiti per Tiny/Micro/Small/Medium/Large/Huge | PASS matematico per il loop corrente; intervalli e formule nella tabella §A |
| Input row fuori intervallo | Identificati; helper aritmetico puro non li blocca e li restituisce fuori range |
| Persistenza `PlanetChunkCoord` | Nessun serializzatore trovato; riflessione solo runtime nel percorso individuato |
| Identità runtime/equality/hash | Cambia perché `row` partecipa a equality/hash; nessuna cache runtime indicizzata trovata |
| Vecchi spawn | Comportamento numerico/visivo preservabile senza migrazione, mantenendo invariati consumer e dati |
| Producer spawn addizionali | Nessun altro producer da hit trovato; slider/default/load sono percorsi separati |
| Doppia inversione | Evitata se l’adapter è applicato esclusivamente nell’assegnamento `MapWorldGenerator`→`PlanetChunkCoord` |
| Copertura proposta | Adeguata dopo l’aggiunta esplicita di out-of-range e test end-to-end anti doppia inversione |
| Modifiche o test eseguiti | Nessuno |

**Raccomandazione pre-patch:** mantenere l’adattatore row in un singolo confine, rendere esplicito il pre-requisito o l’errore per indici fuori intervallo e non migrare gli spawn persistiti. Nessuna patch è stata applicata; è necessaria l’autorizzazione esplicita dell’utente prima di procedere.
