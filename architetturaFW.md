# FAIRWORLD - FORGE Rewrite Architecture
# FORGE Engine - Rewrite & Modernization Plan

L'obiettivo di questo piano è riprogettare l'applicazione FORGE (la modalità sandbox/building) trasformandola in un motore ad altissime prestazioni, basato su **Data-Oriented Design (ECS)**, un **Job System asincrono**, e un **partizionamento della memoria dedicato**.

## User Review Required

> [!IMPORTANT]
> **Architettura ECS e Job System**: Il piano prevede di "esplodere" la vecchia struttura ad oggetti per passare al Registry di EnTT e wrappare ogni calcolo pesante (Sculpting, Modeling) in Task asincroni inviati a thread worker. Confermi che questa è la direzione per il core della FORGE?

> [!IMPORTANT]
> **Allocazione per Chunk**: Utilizzeremo un `PoolAllocator` dedicato per i chunk del mondo, e uno `StackAllocator` azzerato a ogni frame per i calcoli temporanei (transient memory). Sei d'accordo con questo layout di memoria per FORGE?

## Proposed Changes

### 1. Il Core Matematico (Trivially Copyable)
Importeremo intatte le strutture matematiche (`Vec2`, `Vec3`, `Vec4`, `Mat4`, `AABB`) e la logica PBR di base.
- Queste strutture sono perfette per il calcolo vettoriale spinto e verranno usate come dati grezzi da passare ai buffer Vulkan.
- Funzioni generatrici (`makeCube`, `makeSphere`) verranno usate come moduli per generare dati da iniettare nell'allocatore a doppio buffer.

### 2. Data-Oriented Design e ECS (EnTT)
Rimozione del concetto classico di "SceneObject". La scena diventerà un Registry (EnTT).
- **Array Contigui**: Array separati per trasformazioni (`TransformState`), materiali (`PBRMaterial`), e metadati mesh.
- Iterazione iper-veloce per l'Animation System e la sottomissione a Vulkan, senza "cache miss" dovuti a dati non necessari.

### 3. Job System e Moduli Dinamici
Tutte le funzioni pesanti diventeranno Task asincroni (Fibers/Job System).
- **Sculpting Asincrono**: `applyBrush` non bloccherà mai il main thread. L'input genererà un Job in background che, a calcolo finito, passerà i dati al RenderGraph.
- **Geometry Nodes & Fallback**: I grafi nodali (es. `CompGraph::execute`) gireranno in parallelo prima dell'assemblaggio del frame. Il vecchio Software Renderer sarà riciclato per light baking CPU-based in background.

### 4. Memory Partitioning
L'infrastruttura di memoria per la FORGE verrà separata dall'heap globale, usando tre "Tier":
1. **Persistent Forge Memory** (`FreeListAllocator`): Dati stabili per l'intera sessione (giocatore, metadata).
2. **Chunk Memory Pool** (`PoolAllocator`): Dedicato ai voxel chunk (stessa dimensione, zero frammentazione).
3. **Frame/Transient Memory** (`StackAllocator` / `LinearAllocator`): Azzerato a ogni frame (raycasting, code di render).

### 5. Rimozione I/O Sincrono
- Rimozione di qualsiasi logica basata su `std::getline` o terminale bloccante.
- Main loop puramente guidato da eventi Win32, smistati verso i sistemi asincroni e la UI immediata (ImGui).

## Verification Plan

### Automated Tests
- Test unitari sull'allocazione/deallocazione massiva dei chunk (memory leak detection alla chiusura di FORGE).
- Benchmark di performance per l'iterazione sulle `TransformState` tramite EnTT rispetto al vecchio approccio ad oggetti.

### Manual Verification
1. Generare una scena massiva (10.000+ chunk).
2. Eseguire operazioni di Sculpting asincrone e verificare l'assenza di frame drop/micro-stutter nel main loop.
3. Tornare all'Hub e assicurarsi tramite debugger che il blocco di memoria della FORGE venga rilasciato in $O(1)$ istantaneamente.
 
Questo documento descrive le fasi e l'architettura per la riscrittura del modulo FORGE in C++, basata su alte prestazioni, Data-Oriented Design, Job System e totale asincronia.

## 1. Cosa importiamo intatto (Il Core Matematico)
Le fondamenta matematiche e i dati crudi sono perfetti e possono essere portati direttamente nei moduli condivisi del nuovo motore.

* **Math**: `Vec2`, `Vec3`, `Vec4`, `Mat4`, `AABB`. Queste struct sono banalmente copiabili (Trivially Copyable) e ideali per i calcoli vettoriali spinti.
* **PBR Logic**: La logica `shade()` del Cook-Torrance la terremo come riferimento CPU, ma i parametri (baseColor, metallic, roughness) diventeranno i dati grezzi che passeremo ai buffer Vulkan.
* **Generators**: Funzioni come `makeCube`, `makeSphere` e `makeCylinder` sono ottime. Le useremo dentro la DLL del modulo di Modeling per generare i dati da spingere poi nella memoria dell'allocatore a doppio buffer.

## 2. Cosa trasformiamo in Data-Oriented Design (L'ECS)
Dobbiamo "esplodere" la vecchia struct `Scene` e `SceneObject`. In un motore serio non esistono "oggetti", ma array di dati.

* **La Scena Diventa il Registry (EnTT)**: Invece di `std::vector<SceneObject> objects`, avremo array separati: un array per le trasformazioni (`TransformState`), uno per i materiali (`PBRMaterial`), e uno per i metadati delle mesh.
* **Vantaggio Prestazionale**: Quando l'Animation System o il ciclo di rendering Vulkan dovranno aggiornare le matrici del mondo, itereranno solo su un array contiguo di `Mat4`, sfrecciando alla massima velocità senza dover "saltare" le informazioni dei vertici o dei materiali che non gli servono in quel momento.

## 3. Cosa wrappiamo nel Job System (I Moduli Dinamici)
Tutte le funzioni pesanti diventeranno dei Task asincroni che girano sui thread worker (Fibers/Job System).

* **Sculpting & Modeling**: Metodi come `applyBrush` o `subdivide` non verranno chiamati nel thread principale bloccando tutto. Saranno impacchettati in un Job: quando l'utente clicca, l'input genera un Job che calcola i nuovi vertici in background e poi li passa al RenderGraph per l'aggiornamento dei buffer Vulkan.
* **Compositing & Geometry Nodes**: I `CompGraph::execute` e `GeometryNodeTree` sono la base perfetta per le DLL ricaricabili a caldo (Hot Reload). Verranno eseguiti in parallelo, calcolando i grafi prima che Vulkan assembli il frame.
* **Software Renderer**: Più che usarlo come output primario, questo modulo diventerà uno strumento potentissimo di fallback o di Light Baking eseguito via CPU nei thread in background, lasciando a Vulkan il rendering real-time.

## 4. Cosa dobbiamo eliminare (L'I/O Sincrono)
Il codice legato alle stampe a terminale bloccanti e ai cicli I/O standard deve essere rimosso.

* **Rimozione I/O Sincrono**: L'intera logica da terminale e i cicli `std::getline(std::cin)` (come i prompt del menu App) devono essere rimossi.
* **Main Loop Asincrono**: Un main loop asincrono di basso livello non aspetta mai input bloccanti. La logica di smistamento (es. l'utente preme il pulsante per lo Smooth Brush) sarà guidata dagli eventi grezzi delle API Win32 pompati nel sistema di input, interfacciati con una GUI immediata (ImGui).

## Obiettivo Finale
Fondendo queste direttive con un partizionamento di memoria dedicato, otterremo il codice algoritmico brillante originale, ma spinto da un vero e proprio reattore nucleare sotto il cofano.
