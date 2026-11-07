# FAIRWORLD P00 — Master Integration Audit

**Tipo:** audit del repository e piano d'integrazione.  
**Esito:** audit completato; P01 non avviato.  
**Ambito:** I1 geometria/collisione planetaria, I2 gravità/movimento, I3 integrazione runtime Block Maker, I4 atlas/PBR, I5 neural ray queries.

## 1. Metodo e limiti

L'audit è stato condotto sul codice presente nel working tree `D:\FAIRWORLD\FAIRWORLD` e sui rapporti D1.2.19d.5/.6 già presenti. Sono stati esaminati i callsite, i contratti e i test/report pertinenti. Non sono stati eseguiti build, test o sessioni GUI in questa fase; nessun sorgente, asset o salvataggio è stato modificato da P00. Il solo file nuovo di questo lavoro è questo rapporto.

Il working tree **non è pulito**: contiene numerose modifiche e file non tracciati, inclusi sorgenti, test, log, asset e rapporti delle fasi precedenti. I risultati descritti sono quindi attribuiti al contenuto effettivo attualmente presente, non a un commit pulito identificato. Le modifiche preesistenti sono state preservate e non sono state ripristinate. Prima di P01 va acquisito un inventario/diff baseline condiviso per evitare di confondere cambiamenti preesistenti con nuovi interventi.

Le etichette `PASS CPU`, `GUI NOT VERIFIED` e `GPU NOT VERIFIED` riportano l'evidenza documentata nelle fasi precedenti; non sono una nuova esecuzione né una certificazione complessiva dei sistemi.

## 2. Sintesi esecutiva

| Sistema | Stato supportato dall'evidenza disponibile | Lettura P00 |
|---|---|---|
| I1 — Planetary Geometry & Collision | **CPU PASS limitato** per mapping radiale e regressioni di collisione specifiche; GUI/GPU **NOT VERIFIED** | Il mapping radiale comune è presente. Il test d6 copre casi mirati, non tutte le superfici, traiettorie e condizioni runtime. |
| I2 — Gravity & Player Movement | **CPU PASS parziale** per idle, caduta/salto, patch uniforme, timestep e alcuni attraversamenti; integrazione GUI **NOT VERIFIED** | Gravità, movimento tangenziale e collisione sono distribuiti tra più sistemi. La stabilità percepita nella GUI non è dimostrata dai test headless. |
| I3 — Block Maker Runtime Integration | **Parziale / NOT VERIFIED end-to-end** | Authoring, preview, registri e persistenza esistono; l'identità dei dati e il trasferimento verso il mondo runtime richiedono una prova end-to-end isolata. |
| I4 — Material Atlas & PBR | **CPU/cache e vertex-contract presenti; GPU visuale NOT VERIFIED** | Cache v2, layer PBR e shader Forge esistono. Il caricamento materiale nel flusso runtime e l'aspetto GPU non sono certificati da questo audit. |
| I5 — Neural Ray Queries | **Non implementato come query neurale** | È presente un raycast CPU convenzionale e un riferimento/mockup a input neurale; non è stata trovata una pipeline neural ray query o hardware ray query. È una linea di ricerca, non un difetto funzionale dimostrato. |

**Conclusione:** il repository contiene una base sostanziale, ma le prove sono stratificate e in parte solo CPU. Non c'è evidenza sufficiente per dichiarare i cinque sistemi integrati o congelati. P01 deve iniziare con baseline riproducibile e matrice di gate, prima di ulteriori patch.

## 3. Inventario dei sistemi e dei file

### I1 — Planetary Geometry & Collision

File principali:

- `src/core/utils/PlanetMath.h` — `PlanetSize`, `PlanetChunkCoord`, risoluzione e coordinate/neighbor planetari.
- `src/world/CubeSphereMapping.h/.cpp` — conversioni faccia, UV, direzione e chart.
- `src/world/PlanetRadialMapping.h/.cpp` — conversione distanza radiale ↔ cella e raggio esterno della cella.
- `src/world/PlanetGridCompatibility.h` — adattamento riga legacy/canonica.
- `src/world/MapWorldGenerator.h/.cpp` — chunk e dati planetari a partire dalla mappa.
- `src/world/PlanetaryMeshGenerator.h/.cpp`, `src/world/SphericalLOD.cpp` — geometria procedurale/LOD.
- `src/world/VoxelChunkMesher.h/.cpp`, `src/world/VoxelFaceUV.h` — mesh CPU voxel condivisa e UV di faccia.
- `src/systems/PhysicsEngine.h/.cpp`, `src/world/GameWorld.h/.cpp` — query del terreno e collisione.
- `src/world/PlanetaryVoxelE2ETests.cpp` — test headless del fixture Tiny e dei contratti planetari.

### I2 — Gravity & Player Movement

- `src/systems/Systems.h/.cpp` — `PlayerMovementSystem`, `PhysicsSystem`, camera sync e ordine di esecuzione.
- `src/systems/PhysicsEngine.h/.cpp` — fixed-step, gravità, integrazione, sweep e collisione sferica.
- `src/systems/PlanetaryMovementMath.h`, `src/systems/PlanetaryCameraFrame.h` — helper per movimento tangenziale e frame camera.
- `src/components/ForgeComponents.h`, `src/core/entities/Components.h` — componenti del player, rigid body, chunk e mesh.
- `src/states/PlayState.h/.cpp` — creazione dello stato, input e ordine di aggiornamento.
- `src/core/app/SharedContext.h`, `src/core/hardware/HardwareInputManager.h` — servizi condivisi e input.

### I3 — Block Maker Runtime Integration

- `src/states/BlockMakerState.h/.cpp` — flusso authoring, preview, salvataggio e collegamento ai servizi.
- `src/render/apps/BlockMakerRenderer.h/.cpp` — rendering della UI/preview Block Maker.
- `src/world/MeshGenerators.cpp`, `src/core/utils/World.h`, `src/components/ForgeComponents.h` — primitive e dati mesh/vertex.
- `src/blocks/BlockRegistry.h/.cpp`, `src/blocks/MaterialRegistry.h/.cpp`, `src/blocks/ScientificMaterialRegistry.h/.cpp` — definizioni e registri.
- `src/blocks/factory/*`, `src/blocks/mesh/*`, `src/blocks/texture/*`, `src/blocks/uv/*` — factory e servizi di authoring.
- `src/world/GameWorld.h/.cpp`, `src/states/PlayState.cpp` — destinazione runtime e consumo.

### I4 — Material Atlas & PBR

- `src/render/TexturePacker.h/.cpp` — packing e cache PBR.
- `src/render/RenderManager.h/.cpp` — pipeline Forge, descriptor, texture e draw.
- `src/render/forge.vert`, `src/render/forge.frag` — interfaccia vertex/fragment e sampling atlas.
- `src/components/ForgeComponents.h` e `src/core/utils/World.h` — layout vertex (nella working tree sono presenti definizioni/compatibilità da confrontare prima di ulteriori cambi).
- `src/world/VoxelChunkMesher.cpp`, `src/world/SphericalLOD.cpp`, generatori mesh — producer di UV/material ID.
- `src/blocks/MaterialRegistry.*`, `src/blocks/BlockRegistry.*`, `src/blocks/ScientificMaterialRegistry.*` — definizioni sorgente.
- `src/render/VulkanDmaManager.*`, `src/render/VramSlabAllocator.*`, `src/render/vulkan/VulkanMemory.*` — trasferimento e lifetime dei buffer.

### I5 — Neural Ray Queries

- `src/systems/RaycastSystem.h/.cpp` — query CPU con modalità Voxel, Sphere, Physics e dispatch Auto.
- `src/core/hardware/HardwareInputManager.h` e `src/core/app/SharedContext.h` — input/struttura neurale mockup.
- `src/render/RenderManager.*` — candidato futuro solo se si sceglie una query GPU; nell'ispezione non è stata individuata una pipeline di ray tracing/query neurale.

## 4. Risultati verificati e questioni aperte

### 4.1 D1.2.19d.5 — mapping radiale

Il rapporto `D1_2_19D5_UNIFIED_RADIAL_MAPPING_REPORT.md` documenta la divergenza precedente: collisione/spawn usavano un offset numerico diverso da quello della mesh. Per il voxel Tiny esaminato, il limite collisionale era a raggio 82 mentre la mesh CPU aveva superficie a 42.5. Il rapporto documenta la convergenza dopo l'uso di `PlanetRadialMapping`: entrambi a 42.5, delta 0 nel test con tolleranza `1e-4`.

Nel codice corrente `MapWorldGenerator::WorldToVoxelCoord` usa `GetVoxelCellCoordinateFromDistance`; `GameWorld::TryResolvePlanetarySpawn` usa `GetCellOuterRadius`. La centralizzazione è quindi presente nei callsite principali verificati. Il report d5 registra test CPU e fixture Tiny PASS, ma **GUI/GPU NOT VERIFIED**. Il report menziona inoltre una perdita Grounded durante un tick della camminata, quindi non prova continuità Grounded in ogni tick.

**Stato:** correzione radiale documentata e presente nel codice; copertura runtime non completa.

### 4.2 D1.2.19d.6 — continuità collisionale

Il rapporto `D1_2_19D6_PLAYER_COLLISION_CONTINUITY_REPORT.md` documenta una causa specifica: il footprint cilindrico era campionato con centro, cardinali e diagonali; un percorso `+Z → +X` penetrava un voxel sul ring `0.95r`, angolo 247.5°, fino a 0.998573 unità mentre Grounded era true. Il codice corrente in `PhysicsEngine.cpp::ResolveSphericalCollisions` include gli otto campioni intermedi a metà angolo.

Il confronto A/B documentato riporta zero campioni solidi/penetrazione sullo stesso percorso dopo la modifica. Il rapporto documenta anche incremento massimo da 36 a 68 campioni per query completa (circa +89% nel caso pienamente campionato) senza misura prestazionale isolata.

I test CPU documentati includono idle 120 tick, camminata su patch uniforme, timestep variabile, percorso diagonale, gradino e passaggio `+Z → +X`. I casi diagonale e gradino possono perdere Grounded legittimamente e il test sul gradino non attende l'atterraggio nella stessa traiettoria. GUI/GPU e prestazione interattiva sono **NOT VERIFIED**.

**Stato:** regressione CPU mirata PASS secondo il rapporto; non equivale alla verifica completa del controller nel runtime GUI.

### 4.3 I3 e I4 — separazione authoring/runtime/materiali

Nel codice, `BlockMakerState` possiede un `m_previewWorld` dedicato e contiene logica per collegare/staccare il mondo preview dal contesto condiviso. Questo è un confine concreto tra world di authoring e runtime; i dati visualizzati in preview non sono automaticamente prova di dati caricati/renderizzati da PlayState.

Il flusso Block Maker include aggiornamenti a registri e scritture di definizioni tramite percorsi relativi. È quindi necessario verificare working directory e destinazioni quando si preparano i test; P00 non ha eseguito né modificato queste scritture.

`TexturePacker.cpp` include fingerprint deterministico della definizione e degli asset referenziati, stato di file assente/presente e parametri; il formato v2 è riconoscibile dal magic `FWPBRCH2`. Ciò supporta la validazione della cache CPU, non certifica il risultato visivo GPU.

Gli shader Forge attuali dichiarano location 2 come UV e location 3 come material ID; il fragment shader campiona array PBR usando UV e layer materiale. Nel layout vertex persistono campi/nomenclature compatibili legacy (es. `roughMetal`) con offset ABI espliciti in `ForgeComponents.h`; va mantenuto un unico inventario di tutti i producer prima di rinominare o mutare quel campo.

La matrice ID richiede attenzione: il mesher voxel assegna il block ID al material ID (`materialID = block`), mentre il `MaterialRegistry` è indicizzato per ID blocco e il registro scientifico ha un'identità distinta. La compatibilità numerica oggi usata dal renderer non deve diventare un'assunzione implicita tra Block ID, visual material ID e Scientific Material ID.

**Questioni da provare:** coerenza di tutti i producer con location 2 UV; continuità dei layer e ID nel caricamento runtime; equivalenza tra risorse mostrate nel preview e quelle effettivamente presenti in PlayState. Non si dichiara un difetto visivo specifico senza un campione GPU riproducibile.

### 4.4 I5 — ray query

`RaycastSystem` implementa query CPU per voxel, sfere e fisica. L'input neurale è descritto nel codice di hardware come mockup/decodifica, e `SharedContext` contiene un dato `neuralInput`. Le ricerche nel codice non hanno trovato una neural ray query né una pipeline hardware ray tracing. Questo è un **gap di implementazione rispetto al titolo I5**, non prova che una funzione esistente sia difettosa.

Prima di implementare I5 occorre specificare se il requisito sia: query CPU alimentata da input neurale, accelerazione GPU Vulkan RT, classificazione/applicazione di raggi, o un altro contratto. Nessuna di queste interpretazioni va assunta automaticamente.

## 5. Contratti condivisi e dipendenze

| Contratto | Producer → consumer | Invariante da preservare | Rischio se diverge |
|---|---|---|---|
| Coordinate faccia/griglia | `CubeSphereMapping` / `PlanetGridCompatibility` → generator, raycast, spawn, terrain | Faccia e coordinate discrete devono avere una sola convenzione; la conversione legacy riga→canonica deve avvenire una sola volta. | Selezione o chunk adiacenti errati, doppia inversione ai bordi. |
| Identità chunk | `PlanetChunkCoord` e chiave logica → `WorldChunkManager`, `VoxelChunkComponent`, generator | La chiave logica è distinta dalla posizione fisica trasformata; stesso lookup, componente e registrazione usano la stessa chiave. | Collisioni/alias e chunk mancanti o riusati. |
| Raggio/cella | `PlanetRadialMapping` → generazione, query/collisione, spawn e mesh | Centro voxel e faccia esterna devono condividere la stessa origine e spaziatura radiale. | Collider disallineato, spawn in penetrazione o vuoto tra collisione e superficie. |
| Player frame/fisica | Input e `PlanetaryCameraFrame`/`PlanetaryMovementMath` → movimento/fisica/camera | Up radiale, velocità tangenziale/radiale, body Transform e camera devono essere aggiornati con ordine coerente. | Rotazioni indesiderate, input non applicato, camera/body divergenti. |
| Vertex Forge | producer mesh → `VertexInput`/shader Forge | Location 2 è UV; location 3 è material ID; stride/offset e tipi Vulkan devono corrispondere alle struct CPU e a tutti i producer. | Texture errate, valori interpretati come UV o incompatibilità ABI. |
| Identità materiale | block voxel → visual registry/material layer → fragment shader | L'ID voxel/visual layer e l'identità scientifica sono concetti separati anche se alcune mappe oggi coincidono numericamente. | Layer sbagliato, fallback o binding semanticamente ambiguo. |
| Mesh async/lifetime | `JobSystem` → CPU mesher → DMA → pubblicazione ECS → renderer | Accettazione, completamento CPU, upload DMA, pubblicazione e draw sono stati distinti; l'allocazione non è riusata finché graphics può leggerla. | Pubblicazione falsa, mesh obsolete, use-after-free GPU. |
| Query scena | scene/chunk/voxel → `RaycastSystem` → editor/input | La geometria interrogabile deve essere esplicita e distinta da preview o superficie LOD. | Il rendering visibile può non essere queryable dal raycast voxel. |

### Dipendenze tra i cinque sistemi

| Sistema | Dipende da | Fornisce a |
|---|---|---|
| I1 Geometry & Collision | `PlanetMath`, Cube-Sphere, radial mapping, chunk identity, GameWorld | I2 player/collision; I3 collocazione documentale; I5 query su geometria. |
| I2 Gravity & Movement | I1 query/collisione, player frame, input routing, PhysicsEngine | PlayState e validazione funzionale integrata. |
| I3 Block Maker | registri/ID, mesh compiler/UV, persistence, preview | definizioni e asset per I4 e staging runtime. |
| I4 Atlas & PBR | ID visuali, vertex UV/material, TexturePacker, descriptor/render lifecycle | rendering Block Maker, voxel e superfici planetarie. |
| I5 Neural Ray Queries | semantica input/query e sorgente scena; eventualmente I4/Vulkan RT | ricerca futura; non è prerequisito per I1–I4. |

## 6. Problemi confermati e ipotesi

### Confermati dall'ispezione o dai report

1. **Il vecchio disallineamento radiale era reale ed è stato corretto nei callsite CPU principali** secondo d5; il rapporto documenta coincidenza mesh/collisione sul caso Tiny misurato.
2. **Una lacuna nel campionamento angolare del collider era riproducibile** e la patch presente aggiunge otto probe intermedi; A/B e casi CPU sono documentati in d6.
3. **La copertura d6 non certifica tutta la continuità di gameplay:** GUI/GPU non eseguiti; gradino non segue la stessa traiettoria fino all'atterraggio; costo dei probe non misurato in modo isolato.
4. **Authoring preview e mondo runtime sono distinti:** `BlockMakerState` mantiene un preview world; la vista preview da sola non prova la persistenza/caricabilità runtime.
5. **La nomenclatura/semantica materiale è condivisa tra più strati:** block ID viene usato come material ID nel mesher; registry e layer devono essere validati come contratto, non presunti equivalenti agli ID scientifici.
6. **I5 non ha implementazione di neural ray query identificata** nel codice ispezionato; esiste raycast CPU ordinario e struttura/mockup di input neurale.
7. **Lo stato di lavoro ha molte modifiche preesistenti** (sorgenti, test, report, asset e build locali). Non è sicuro attribuire un futuro diff globale a una singola fase senza baseline esplicita.

### Ipotesi o verifiche mancanti

- Che ogni producer di mesh rispetti il contratto UV/material ID dello shader Forge.
- Che tutti i percorsi PBR (packing iniziale, aggiornamento dinamico da Block Maker, preview e PlayState) convergano alla stessa definizione e allo stesso layer.
- Che i tempi di hashing/packing siano compatibili con il lifecycle runtime su hardware target.
- Che le collisioni d6 si comportino in modo stabile in sessioni GUI reali e su terreni più ampi/irregolari.
- Che gli input WASD/mouse/Space seguano sempre l'Action Map desiderata e non siano bloccati da focus/capture.
- Che la mesh planetaria e quella voxel siano entrambe renderizzate dal percorso asincrono con lifetime DMA/graphics verificato su GPU.
- Che I5 richieda effettivamente Vulkan ray query; manca una specifica di prodotto/ricerca che scelga la semantica.

## 7. Roadmap incrementale P01–P13

Ogni fase separa **audit/design**, **implementazione autorizzata** e **validazione**. I criteri sono gate proposti; la presente roadmap non autorizza modifiche di codice.

| Fase | Scopo e attività | Criterio d'accettazione |
|---|---|---|
| **P01 — Baseline & Evidence Ledger** | Inventario del working tree, mapping report/test a commit o hash locale, definizione di comandi riproducibili e directory sandbox; nessuna modifica funzionale. | Baseline documentata senza reset/rebaseline; ogni PASS ha comando, fixture, output ed exit code; GUI/GPU separati. |
| **P02 — Planetary Coordinate Contract** | Audit e consolidamento dei contratti PlanetSize, face/UV/grid, row legacy, chiave chunk e radial mapping; verifiche al bordo e sui sei orientamenti. | Matrice invarianti completa; test CPU ripetibili per tutte le facce/dimensioni e identità chunk; nessuna doppia conversione; legacy esplicitato. |
| **P03 — Collision Coverage & Cost** | Estendere la matrice CPU di collider: terreno irregolare, gradini con landing, bordi chunk/faccia, frame variabili; misurare costo probe senza cambiare la gravità. | Nessuna penetrazione illegittima; Grounded distinto da supporto; landing del caso gradino esercitato; costo CPU quantificato e budget concordato. |
| **P04 — Player/Input Integration** | Audit e test dell'ordine input→movimento→fisica→Transform→camera; prove di frame planetario, cursor/focus e spawn pending. | Test CPU per invarianti cinematiche/fisiche; procedura GUI autorizzabile con telemetria; nessun PASS GUI implicito da headless. |
| **P05 — Block Maker Data Contract** | Mappare authoring, preview, registri, asset path e serializer fino alla definizione esportata; identificare confine di proprietà del preview world. | Un artefatto sandbox con origine, ID e hash tracciabili; salvataggio/riapertura senza scrivere originali; niente divergenze silenziose tra preview e documento. |
| **P06 — Authoring-to-Runtime Staging** | Progettare e poi implementare, solo dopo P05, il percorso minimo di caricamento dell'artefatto authoring nel world runtime; evitare duplicazione di autorità. | Fixture isolato caricato in GameWorld; entità/chunk/materiali contati e correlati alla sorgente; round-trip documentale preservato. |
| **P07 — Material Identity Contract** | Specificare Block ID, visual material/layer ID e Scientific Material ID e i punti di conversione; testare valori noti e fallback. | Mapping totale/esplicito per gli ID supportati; ID assenti rifiutati o fallback documentato; nessuna rinumerazione o modifica scientifica implicita. |
| **P08 — Atlas/PBR CPU Contract** | Validare definizioni, fingerprint/cache, layer, fallback, asset mancanti e dinamica di ricaricamento senza Vulkan. | Test deterministici per cache e mapping; layer e hash riproducibili; percorsi sandbox verificati. |
| **P09 — GPU Material/Vertex Gate** | Validare vertex input, descriptor, upload texture e mesh, warning/errori Vulkan e draw; prova su GPU reale separata. | Almeno un materiale texture e un fallback attraversano producer→layer→shader→draw completata; nessun warning/error pertinente; log e device identificati. |
| **P10 — CPU End-to-End Fixture** | Integrare fixture isolato Block Maker→documento→planet generator→solver→ECS→mesher CPU e verificare ID/UV/coordinate. | Chunk, voxel, mesh CPU, sorgente e materiali correlati; risultati ripetibili; nessuna pubblicazione GPU simulata. |
| **P11 — GUI Sandbox Integration** | Solo dopo P04/P06/P09/P10: verifica manuale/assistita di authoring, Planet Mapper e PlayState in sandbox con inventario/hash pre-post. | Flussi GUI realmente eseguiti, salvataggi sandbox, log input/fisica/render, zero cambiamenti originali; esito per ogni controllo. |
| **P12 — Neural Ray Query Feasibility** | Definire caso d'uso e confronto CPU; inventariare estensioni Vulkan/device supportati, dati di training/inferenza se pertinenti e metriche. | Specifica approvata con semantica, fallback, latenza/accuratezza e hardware; prototipo solo dopo decisione separata. Non prerequisito per I1–I4. |
| **P13 — Integration Acceptance & Freeze Readiness** | Eseguire suite CPU, integrazione GUI/GPU, regression e review dei residui; preparare decisione per ciascun sistema. | Tutti i gate obbligatori con evidenza; rischi residui espliciti e accettati; readiness proposta, nessun freeze automatico. |

## 8. Ordine e lavoro parallelizzabile

Sequenza dipendente raccomandata: **P01 → P02 → P03 → P04** per la catena planetaria; **P05 → P06** per Block Maker/runtime; **P07 → P08 → P09** per materiali e GPU; poi **P10 → P11 → P13** per integrazione. P12 può essere svolto come studio indipendente dopo P01.

Attività che possono procedere in parallelo senza edit concorrenti:

- P02 può procedere insieme all'inventario P05 e allo studio P12, se ogni filone limita le modifiche ai propri documenti/test e condivide i contratti trovati.
- P03 e P04 condividono `PhysicsEngine`, `Systems` e frame player: la sola raccolta di evidenze può essere parallela, ma le patch vanno serializzate e integrate una alla volta.
- P05/P06 e P07/P08 condividono definizioni e ID materiali: audit/design può essere parallelo; implementazione di schema, ID o loader deve attendere un contratto P07 approvato.
- P09 dipende dal contratto vertex/material e dall'output P08; non va usato per inferire correttezza CPU.
- P10 dipende da coordinate, materiale e staging definiti; P11 richiede sandbox e gate GPU già pronti.

## 9. Rischi di regressione

1. **Doppie autorità coordinate/ID:** conversione riga ripetuta, chunk chiave derivata dalla posizione, block/material/scientific ID confusi.
2. **Collisione contro mesh:** modificare raggio, offset o probe per correggere un singolo terreno può disallineare spawn, collisione e mesh; preservare il mapping d5 e misurare il costo d6.
3. **Player frame:** cambiare ordine input/fisica/camera può introdurre drift, roll o salto alterato anche mantenendo i test matematici.
4. **Preview vs runtime:** `m_previewWorld`, registri o texture caricati dinamicamente possono mascherare un mancato trasferimento/persistenza.
5. **ABI vertex:** la struct ha offset/stride verificati e nomi legacy; rinomina o cambio formato può rompere producer non ovvi o dati temporanei persistiti.
6. **Cache e asset path:** working directory diversa può leggere cache o materiali differenti; test fuori sandbox rischiano scritture in `assets/` o `saves/`.
7. **Lifetime asincrono:** un PASS CPU del builder o del DMA non prova draw completata né safe reuse della VRAM.
8. **Prestazioni collisioni:** 68 probe massimi contro 36 precedenti possono aumentare il costo; il report d6 non include un benchmark isolato.
9. **Working tree preesistente:** cambi globali o formattazione possono includere file già dirty; P01 deve stabilire un delta prima di patchare.
10. **I5 scope creep:** una “neural ray query” non definita può portare a dipendenze GPU/AI e architetture non richieste; mantenere il workstream fuori dal gate I1–I4.

## 10. Matrice dei gate di verifica

| Area | Evidenza CPU disponibile | GUI | GPU |
|---|---|---|---|
| Mapping radiale e fixture Tiny | **PASS documentato** in d5 per casi specifici, inclusa coincidenza raggio mesh/collisione | **NOT VERIFIED** | **NOT VERIFIED** |
| Collisione sferica | **PASS documentato** in d6 per traiettorie e test elencati; non copre tutti i terreni/runtime | **NOT VERIFIED** | Non applicabile alla correttezza CPU; visibilità/integrazione GPU **NOT VERIFIED** |
| Gravità, salto e movimento player | **PASS parziale documentato** in d5/d6 per casi headless | **NOT VERIFIED** | **NOT VERIFIED** |
| Block Maker→PlayState | Infrastruttura presente; E2E corrente **NOT VERIFIED** da P00 | **NOT VERIFIED** | **NOT VERIFIED** |
| Atlas/PBR/vertex | Cache/contratti CPU e test presenti nel tree; nessuna suite eseguita da P00 | **NOT VERIFIED** | **NOT VERIFIED** |
| Neural ray queries | **NOT VERIFIED / non individuata implementazione** | **NOT VERIFIED** | **NOT VERIFIED** |

## 11. Decisione P00

P00 è un audit e un piano; non è un'autorizzazione a modificare i sistemi né una dichiarazione di freeze. Le correzioni d5/d6 risultano supportate da evidenza CPU nei rispettivi rapporti, con limiti espliciti. Per P01 è necessario prima concordare la baseline del working tree già modificato e la forma del ledger di validazione.

**Stato complessivo:** integrazione I1–I4 parziale, I5 da specificare; CPU PASS soltanto dove indicato; GUI NOT VERIFIED; GPU NOT VERIFIED.  
**Prossimo passo proposto:** autorizzazione esplicita a P01 — Baseline & Evidence Ledger.
