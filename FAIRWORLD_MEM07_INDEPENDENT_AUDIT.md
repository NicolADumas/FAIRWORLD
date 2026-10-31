# FAIRWORLD — MEM-07: INDEPENDENT MEMORY SAFETY & LIFECYCLE AUDIT

**Data:** 30 Ottobre 2026  
**Ambito:** CPU Memory Safety, Editor Real Lifecycle, State Transitions, Destruction Order, Allocator Fallbacks & MSVC AddressSanitizer Analysis  
**Modalità:** AUDIT INDIPENDENTE READ-ONLY (Nessuna modifica al codice sorgente, nessuna build integrata)  
**Stato Precedente Verificato:**
- MEM-04: **7/7 PASS** (Debug & Release)
- MEM-05: **8/8 PASS** (Debug & Release)
- MEM-06: **8/8 PASS** (Debug & Release) — **FROZEN** per la copertura convalidata
- FairworldEditor: build Debug e Release riuscite, exit code = 0

---

## 1. EXECUTIVE SUMMARY & INTENTO DELL'AUDIT

Le suite precedenti (MEM-01..MEM-06) hanno certificato che i singoli sottosistemi di FAIRWORLD — considerati in condizioni di test isolate, cicliche e controllate — non accumulano memoria CPU (zero drift su 100 cicli) e rispettano i contratti base dell'allocatore custom.

Tuttavia, l'applicazione reale **`FairworldEditor`** (`src/core/app/main.cpp`) presenta una complessità di runtime e di interazione asincrona non replicata nei test sintetici:
1. Gli stati reali (`BlockMakerState`, `ForgeState`, `PhysicsLabState`, `HubState`, `PlayState`) scambiano puntatori raw non-owning attraverso il service locator globale [`SharedContext`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/SharedContext.h).
2. Il ciclo di vita di uscita dall'applicazione distrugge le risorse in un ordine differente rispetto all'infrastruttura di test.
3. Il runtime multithreaded (`JobSystem`) e i callback di eventi (`EventManager`) interagiscono con oggetti che possono trovarsi in stati di distruzione parziale.

Questo audit indipendente ha rilevato:
- **1 Bug CRITICAL (Use-After-Free dimostrato):** Corruzione dei puntatori `forgeWorld` e `gameWorld` in `SharedContext` durante la transizione tra `BlockMakerState`, `HubState` e `ForgeState`, con conseguente dereferenziazione di un'istanza `GameWorld` già deallocata.
- **3 Bug HIGH (Gravi violazioni di lifecycle e leak permanenti):**
  - Sottoscrizioni permanenti orfane in [`FairWorldEngine`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/FAIRWORLD.cpp#L284-L320) con capture `[this]` non de-registrabili.
  - Inversione dell'ordine di shutdown in [`main.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/main.cpp#L390-L406): gli stati attivi dello `StateManager` vengono distrutti dopo la dismissione globale di Jolt Physics e del motore grafico.
  - Mancata rimozione dei corpi fisici in [`PhysicsLabState::~PhysicsLabState()`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/PhysicsLabState.cpp#L29-L44) se la simulazione era attiva, provocando distruzione illegale di `PhysicsSystem`.
- **4 Rischi Concreti MEDIUM:** Ri-creazione superflua di componenti ECS e reset del cancellation token durante la distruzione di `GameWorld`, asimmetria tra `_aligned_malloc` e `std::free` nel fallback di `operator delete`, gestione incompleta degli stati in caso di fallimento di `Init()`, e allocazione statica persistente di un `GameWorld` in `PlayState`.
- **3 Miglioramenti LOW:** Assenza di canarini/magic numbers in `FreeListAllocator` per rilevare double-free, assenza di `try/catch` top-level in `main()`, e incoerenza di stack tra `PushState` (Hub) e `ChangeState` (stati figli).

---

## 2. INVENTARIO DEI COMPONENTI ESAMINATI

| Componente | Header | Sorgente | Ruolo nel Lifecycle | Modello di Ownership |
| :--- | :--- | :--- | :--- | :--- |
| **`StateManager`** | [`StateManager.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/StateManager.h) | *Header-only* | Macchina a stati principale; gestisce push/pop/change e dispatch di update/render. | Owning esclusivo (`std::vector<std::unique_ptr<State>>`). |
| **`AppBaseState`** | [`AppBaseState.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/AppBaseState.h) | [`AppBaseState.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/AppBaseState.cpp) | Classe base madre per le App dell'editor (ChunkEditor, PlanetMapper); gestisce header e ritorno a Hub. | Owning di `m_previewWorld`; non-owning di `m_context`. |
| **`HubState`** | [`HubState.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/HubState.h) | [`HubState.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/HubState.cpp) | Dashboard Wii-style; seleziona e spinge sullo stack i tool dell'editor via `PushState`. | Non-owning di `m_context`; non possiede mondi 3D. |
| **`ForgeState`** | [`ForgeState.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/ForgeState.h) | [`ForgeState.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/ForgeState.cpp) | Editor strutture e minivoxel; opera sul mondo fornito da `m_context->forgeWorld`. | Non-owning del `GameWorld`; owning dei sistemi ECS locali. |
| **`BlockMakerState`** | [`BlockMakerState.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/BlockMakerState.h) | [`BlockMakerState.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/BlockMakerState.cpp) | Editor blocchi, mesh modeling e texture painting PBR; crea il proprio `m_previewWorld`. | Owning di `m_previewWorld`; contamina e sovrascrive `SharedContext`. |
| **`PhysicsLabState`** | [`PhysicsLabState.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/PhysicsLabState.h) | [`PhysicsLabState.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/PhysicsLabState.cpp) | Laboratorio fisica, animazioni scheletriche e corpi rigidi Jolt. | Raw owning pointers (`m_labWorld`, `m_joltSystem`). |
| **`ChunkEditorState`** | [`ChunkEditorState.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/ChunkEditorState.h) | [`ChunkEditorState.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/ChunkEditorState.cpp) | Editor 2D/3D di macro-chunk e terreni; deriva da `AppBaseState`. | Owning di `m_previewWorld`; gestisce generatori asincroni. |
| **`PlanetMapperState`** | [`PlanetMapperState.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/PlanetMapperState.h) | [`PlanetMapperState.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/PlanetMapperState.cpp) | Mappatura sferica e LOD planetario; deriva da `AppBaseState`. | Owning di `m_previewWorld`. |
| **`PlayState`** | [`PlayState.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/PlayState.h) | [`PlayState.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/PlayState.cpp) | Modalità di gioco esplorativa; carica mondo data-driven o fallback. | Owning ECS locale; assegna `fallbackWorld` statico se assente. |
| **`GameWorld`** | [`GameWorld.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/world/GameWorld.h) | [`GameWorld.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/world/GameWorld.cpp) | Gestore del mondo voxel, Master Block arena da 128 MB e chunks. | Owning della memoria fisica dell'arena e del registry EnTT. |
| **`EventManager`** | [`EventManager.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/EventManager.h) | *Header-only* | Dispatcher eventi sincrono/differito (singleton globale). | Owning delle subscription lambda via `std::unique_ptr<IEventListener>`. |
| **`JoltPhysicsSystem`** | [`JoltPhysicsSystem.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/systems/JoltPhysicsSystem.h) | [`JoltPhysicsSystem.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/systems/JoltPhysicsSystem.cpp) | Wrapper dell'engine fisico Jolt; thread pool e allocatore temporaneo. | Owning di `PhysicsSystem`, `TempAllocatorImpl`, `JobSystemThreadPool`. |
| **`RuntimeManager`** | [`RuntimeManager.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/RuntimeManager.h) | [`RuntimeManager.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/RuntimeManager.cpp) | Inizializzazione on-demand e shutdown controllato delle feature GPU/CPU. | Owning di `JobSystem`, `VulkanDmaManager`, `VramSlabAllocator`. |
| **`SharedContext`** | [`SharedContext.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/SharedContext.h) | *Header-only* | Bus dei puntatori di servizio tra motore, stati e sistemi. | **Puramente non-owning** (tutti i membri sono puntatori raw). |
| **`FairWorldEngine`** | [`FAIRWORLD.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/FAIRWORLD.h) | [`FAIRWORLD.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/FAIRWORLD.cpp) | Servitore grafico, runtime loop, finestre e master world persistente. | Owning di `m_forgeMaster`, `m_renderManager`, `m_windowManager`. |
| **`MemorySystem`** | [`MemorySystem.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/memory/MemorySystem.h) | [`MemorySystem.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/memory/MemorySystem.cpp) | Heap globale C++17 (256 MB), tombstone bounds guard e tracciamento. | Owning del buffer virtuale dell'heap globale. |

---

## 3. REGISTRO DETTAGLIATO DEI FINDINGS

### 3.1 Sintesi dei Findings per Gravità

```
┌────────────────────────────────────────────────────────────────────────┐
│  CLASSIFICAZIONE FINDINGS AUDIT MEM-07                                 │
├───────────────────┬────────────────────────────────────────────────────┤
│  CRITICAL (1)     │ MEM07-F01: Use-After-Free cross-state SharedContext│
│  HIGH (3)         │ MEM07-F02: Leak permanente subscription Engine     │
│                   │ MEM07-F03: Inversione ordine shutdown main/stati   │
│                   │ MEM07-F04: Distruzione Jolt con corpi attivi Lab   │
│  MEDIUM (4)       │ MEM07-F05: Ri-creazione ECS/cancelToken in ~World  │
│                   │ MEM07-F06: Asimmetria fallback _aligned_malloc/free│
│                   │ MEM07-F07: Transizione incompleta se Init() fallito│
│                   │ MEM07-F08: GameWorld fallback statico in PlayState │
│  LOW (3)          │ MEM07-F09: Assenza canarini in FreeListAllocator   │
│                   │ MEM07-F10: Assenza try/catch top-level in main()   │
│                   │ MEM07-F11: Incoerenza semantica PushState/Change   │
└───────────────────┴────────────────────────────────────────────────────┘
```

---

### 3.2 Schede Dettagliate dei Singoli Findings

#### [MEM07-F01] [CRITICAL] [BUG DIMOSTRATO] Use-After-Free cross-state su SharedContext (`BlockMakerState` $\rightarrow$ `HubState` $\rightarrow$ `ForgeState`)
- **File & Righe Pertinenti:**
  - [`src/world/GameWorld.cpp:110-114`](file:///d:/FAIRWORLD/FAIRWORLD/src/world/GameWorld.cpp#L110-L114)
  - [`src/states/BlockMakerState.cpp:4651-4656`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/BlockMakerState.cpp#L4651-L4656)
  - [`src/states/BlockMakerState.cpp:391-397`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/BlockMakerState.cpp#L391-L397)
  - [`src/states/ForgeState.cpp:105-122`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/ForgeState.cpp#L105-L122)
- **Evidenza Tecnica:**
  In `BlockMakerState::Init()`:
  ```cpp
  m_previewWorld = std::make_unique<fw::GameWorld>();
  m_previewWorld->Initialize(m_context); // Se context->gameWorld == nullptr, assegna context->gameWorld = this!
  ```
  Nel distruttore `BlockMakerState::~BlockMakerState()`:
  ```cpp
  if (m_previewWorld && m_context->forgeWorld == m_previewWorld.get()) {
      m_context->forgeWorld = m_context->gameWorld; // Assegna il valore di gameWorld, che punta a m_previewWorld!
  }
  if (m_previewWorld && m_context->activeRegistry == &m_previewWorld->GetRegistry()) {
      m_context->activeRegistry = m_context->gameWorld ? &m_context->gameWorld->GetRegistry() : nullptr;
  }
  ```
  Al termine del distruttore di `BlockMakerState`, `m_previewWorld` viene deallocato. `m_context->forgeWorld`, `m_context->gameWorld` e `m_context->activeRegistry` rimangono tutti puntatori a memoria deallocata (dangling pointer).
- **Scenario di Riproduzione:**
  1. Avviare l'applicazione in modalità HUB (opzione 0). `context.gameWorld` è `nullptr`, `context.forgeWorld` è `m_forgeMaster`.
  2. Aprire **6. BLOCK MAKER**. `m_previewWorld->Initialize(m_context)` imposta `context.gameWorld = m_previewWorld.get()`.
  3. Cliccare su **Torna all'Hub**. `BlockMakerState` viene distrutto. `context.forgeWorld` e `context.gameWorld` ora puntano al blocco deallocato di `m_previewWorld`.
  4. Dall'Hub, cliccare su **2. LA FORGE**.
  5. `ForgeState::Init()` invoca `m_context->forgeWorld->InitializePhysics()` e `m_context->forgeWorld->EnqueueDeferredMesh(...)`.
  6. **Risultato:** Dereferenziazione di un puntatore deallocato (Use-After-Free) $\rightarrow$ Access Violation `0xC0000005` o corruzione silenziosa dell'heap.
- **Impatto:** Critico. Crash deterministico durante la navigazione reale nell'editor.

---

#### [MEM07-F02] [HIGH] [BUG DIMOSTRATO] Permanent Lambda Listener Leak e Dangling `this` in `FairWorldEngine` Event Subscriptions
- **File & Righe Pertinenti:**
  - [`src/core/app/FAIRWORLD.cpp:284-320`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/FAIRWORLD.cpp#L284-L320)
  - [`src/core/app/FAIRWORLD.cpp:1884-1937`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/FAIRWORLD.cpp#L1884-L1937)
  - [`src/core/app/EventManager.h:128-144`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/EventManager.h#L128-L144)
- **Evidenza Tecnica:**
  In `FairWorldEngine::Init()`:
  ```cpp
  EventManager::Get().Subscribe<Event_BlockMined>([this](const Event_BlockMined& e) {
      fw::BlockType brokenType = m_forgeMaster->GetBlock(e.position.x, e.position.y, e.position.z);
      ...
  });
  EventManager::Get().Subscribe<Event_BlockPlaced>([this](const Event_BlockPlaced& e) {
      m_forgeMaster->SetBlock(e.position.x, e.position.y, e.position.z, ...);
  });
  ```
  I valori restituiti (`SubscriptionId`) vengono scartati e ignorati. In `FairWorldEngine::Shutdown()` e nel distruttore `~FairWorldEngine()`, non viene eseguito alcun `Unsubscribe`.
- **Scenario di Riproduzione:**
  1. Istanziare `FairWorldEngine` ed eseguire `Init()`. Le due subscription vengono registrate nel singleton `EventManager::Get()`.
  2. Eseguire `Shutdown()` o distruggere l'istanza dell'engine.
  3. Il singleton `EventManager::Get()` conserva i due listener con il capture `[this]` che punta alla memoria deallocata di `FairWorldEngine`.
  4. Qualsiasi dispatch di `Event_BlockMined` o `Event_BlockPlaced` invocherà il callback sul puntatore dangling di `FairWorldEngine`, accedendo a `m_forgeMaster` deallocato.
- **Impatto:** Alto. Violazione del contratto di subscription; leak permanente di memoria heap per le lambda e potenziale Use-After-Free in esecuzioni multi-ciclo o test di integrazione dell'engine.

---

#### [MEM07-F03] [HIGH] [BUG DIMOSTRATO] Inversione dell'ordine di shutdown in `main()` — Stato attivo distrutto dopo Engine e Jolt
- **File & Righe Pertinenti:**
  - [`src/core/app/main.cpp:213-225`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/main.cpp#L213-L225)
  - [`src/core/app/main.cpp:330-406`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/main.cpp#L330-L406)
  - [`src/states/PhysicsLabState.cpp:29-44`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/PhysicsLabState.cpp#L29-L44)
  - [`src/systems/JoltPhysicsSystem.cpp:79-83`](file:///d:/FAIRWORLD/FAIRWORLD/src/systems/JoltPhysicsSystem.cpp#L79-L83)
- **Evidenza Tecnica:**
  In `main.cpp`:
  ```cpp
  FairWorldEngine engine;       // Dichiarato a riga 213
  SharedContext context;        // Dichiarato a riga 221
  StateManager stateManager;    // Dichiarato a riga 222
  ...
  while (context.engine->IsRunning()) { ... }
  // Quando la finestra viene chiusa:
  engine.Shutdown();            // Riga 390
  StopAIServer();               // Riga 391
  fw::JoltPhysicsSystem::ShutdownGlobals(); // Riga 392 (elimina JPH::Factory::sInstance e unregister dei tipi)
  ...
  return 0; // Solo qui le variabili locali sullo stack vengono distrutte in ordine inverso!
  ```
  `stateManager` non viene svuotato prima dello shutdown. Se l'utente era in `PhysicsLabState`, `stateManager` contiene ancora l'istanza di `PhysicsLabState`.
  All'uscita da `main()`, `~StateManager()` distrugge `PhysicsLabState`, il quale esegue `delete m_joltSystem;`.
  Tuttavia, `m_joltSystem` viene distrutto **DOPO** che `ShutdownGlobals()` ha già deallocato `JPH::Factory::sInstance`.
- **Scenario di Riproduzione:**
  1. Avviare l'editor ed entrare in **3. PHYSICS LAB**.
  2. Chiudere la finestra premendo la 'X' in alto a destra.
  3. `engine.Shutdown()` e `ShutdownGlobals()` vengono eseguiti con `PhysicsLabState` ancora vivo nello stack di `stateManager`.
  4. Alla chiusura del processo, `~PhysicsLabState()` invoca `delete m_joltSystem`, accedendo al factory Jolt già distrutto.
- **Impatto:** Alto. Violazione dell'ordine di distruzione; crash o asserzione a shutdown dell'applicazione.

---

#### [MEM07-F04] [HIGH] [BUG DIMOSTRATO] Mancata rimozione dei corpi Jolt in `PhysicsLabState::~PhysicsLabState()` durante la simulazione attiva
- **File & Righe Pertinenti:**
  - [`src/states/PhysicsLabState.cpp:29-44`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/PhysicsLabState.cpp#L29-L44)
  - [`src/states/PhysicsLabState.cpp:170-186`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/PhysicsLabState.cpp#L170-L186)
- **Evidenza Tecnica:**
  Quando l'utente avvia la simulazione nel Lab, `StartSimulation()` alloca corpi rigidi e li registra nella `BodyInterface` di Jolt (`m_joltBodies`).
  Il distruttore `PhysicsLabState::~PhysicsLabState()` implementa:
  ```cpp
  PhysicsLabState::~PhysicsLabState() {
      if (m_context) { ... }
      if (m_labWorld) { delete m_labWorld; m_labWorld = nullptr; }
      delete m_joltSystem; // Distrugge direttamente il sistema Jolt!
      m_joltSystem = nullptr;
  }
  ```
  `StopSimulation()` non viene mai invocato in `~PhysicsLabState()`.
  In Jolt Physics, distruggere un'istanza di `PhysicsSystem` mentre contiene corpi attivi non rimossi dalla `BodyInterface` è una violazione delle invarianti di Jolt che genera asserzioni di debug (`mNumBodies == 0`) e memory leak di descrittori interni.
- **Scenario di Riproduzione:**
  1. Entrare in **3. PHYSICS LAB**.
  2. Premere il pulsante **START PHYSICS & AI** (la simulazione si avvia e popola `m_joltBodies`).
  3. Senza premere STOP, premere `ESC` o cliccare su un pulsante di cambio stato (o chiudere l'editor).
  4. `PhysicsLabState` viene distrutto senza eseguire `bodyInterface.RemoveBody()` e `DestroyBody()`.
- **Impatto:** Alto. Leak di risorse fisiche e asserzione immediata in configurazioni Debug.

---

#### [MEM07-F05] [MEDIUM] [RISCHIO PLAUSIBILE] `ClearWorld()` ri-crea entità ECS e resetta il cancellation token durante la distruzione di `GameWorld`
- **File & Righe Pertinenti:**
  - [`src/world/GameWorld.cpp:87-95`](file:///d:/FAIRWORLD/FAIRWORLD/src/world/GameWorld.cpp#L87-L95)
  - [`src/world/GameWorld.cpp:160-162`](file:///d:/FAIRWORLD/FAIRWORLD/src/world/GameWorld.cpp#L160-L162)
  - [`src/world/GameWorld.cpp:190-205`](file:///d:/FAIRWORLD/FAIRWORLD/src/world/GameWorld.cpp#L190-L205)
- **Evidenza Tecnica:**
  Nel distruttore di `GameWorld`:
  ```cpp
  GameWorld::~GameWorld() {
      if (m_cancelToken) { *m_cancelToken = true; }
      ClearWorld(false);
      ...
  }
  ```
  All'interno di `ClearWorld(false)`:
  ```cpp
  if (m_cancelToken) { *m_cancelToken = false; } // Resetta il token a FALSE durante la distruzione!
  ...
  m_registry.clear();
  // Ri-creazione entità pianeta!
  m_planetEntity = m_registry.create();
  m_registry.emplace<PlanetTag>(m_planetEntity);
  m_registry.emplace<PlanetTimeComponent>(m_planetEntity);
  ... // 10 componenti ri-allocati in m_registry
  ```
  Durante la distruzione, `ClearWorld` cancella il registry e poi alloca nuovamente 10 componenti ECS nello heap solo per vederli distrutti mezzo millisecondo dopo dal distruttore implicito di `m_registry`.
  Inoltre, reimpostare `*m_cancelToken = false` durante la distruzione riapre la finestra di esecuzione per job asincroni in volo che avrebbero dovuto abortire.
- **Impatto:** Medio. Churn di allocazioni inutili a distruzione e potenziale race condition sui job asincroni.

---

#### [MEM07-F06] [MEDIUM] [RISCHIO PLAUSIBILE] Asimmetria nel fallback di deallocazione su MSVC (`_aligned_malloc` vs `std::free`)
- **File & Righe Pertinenti:**
  - [`src/core/memory/MemorySystem.cpp:258-288`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/memory/MemorySystem.cpp#L258-L288)
  - [`src/core/memory/MemorySystem.cpp:329-353`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/memory/MemorySystem.cpp#L329-L353)
- **Evidenza Tecnica:**
  In `operator new(size, align_val)` con heap esaurito o non attivo, l'allocazione avviene tramite `#if defined(_MSC_VER) _aligned_malloc(size, alignment)`.
  In `operator delete(void* ptr)` (forma scalare non-allineata):
  ```cpp
  void operator delete(void* ptr) noexcept {
      ...
      if (alloc && g_GlobalHeapBuffer) {
          if (p >= g_TrackedHeapStart && p < g_TrackedHeapEnd) { ... return; }
      }
      std::free(ptr); // Invoca std::free() su QUALSIASI blocco esterno!
  }
  ```
  Se un tipo over-aligned (es. `alignas(32)` o `alignas(64)`) viene deallocato tramite un puntatore alla classe base priva di specifica di over-alignment ma con distruttore virtuale, lo standard C++ può instradare la cancellazione verso `operator delete(void*)`.
  Su MSVC, passare a `std::free()` un puntatore allocato da `_aligned_malloc()` produce un crash immediato per corruzione dell'heap del CRT (`RtlFreeHeap invalid pointer`).
- **Impatto:** Medio. Rischio di crash durante fallback su heap esaurito o per oggetti creati prima dell'attivazione dell'heap globale.

---

#### [MEM07-F07] [MEDIUM] [RISCHIO PLAUSIBILE] Transizione e lifecycle incoerenti in `StateManager` in caso di fallimento di `Init()`
- **File & Righe Pertinenti:**
  - [`src/core/app/StateManager.h:40-45`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/StateManager.h#L40-L45)
  - [`src/core/app/StateManager.h:57-65`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/StateManager.h#L57-L65)
- **Evidenza Tecnica:**
  In `StateManager::ProcessTransitions()`:
  ```cpp
  m_stateStack.push_back(std::move(action.newState));
  bool result = m_stateStack.back()->Init();
  m_stateStack.back()->OnEnter(); // Viene SEMPRE chiamato, anche se Init() ha fallito!
  if (!result) {
      std::cerr << "[StateManager ERROR] Inizializzazione stato fallita.\n";
      m_stateStack.pop_back(); // Viene rimosso SENZA chiamare OnExit()!
  }
  ```
  Se l'inizializzazione di uno stato fallisce (es. file di configurazione mancante, device non pronto):
  1. `OnEnter()` viene invocato su uno stato non inizializzato o parziale.
  2. Lo stato viene rimosso con `pop_back()` senza che venga mai invocato `OnExit()`. Qualsiasi risorsa agganciata in `OnEnter()` (es. lock del cursore, registrazioni listener) rimane orfana.
- **Impatto:** Medio. Incoerenza dello stato applicativo e possibili dangling hooks.

---

#### [MEM07-F08] [MEDIUM] [RISCHIO PLAUSIBILE] Allocazione persistente di un `GameWorld` statico di fallback in `PlayState`
- **File & Righe Pertinenti:**
  - [`src/states/PlayState.cpp:244-249`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/PlayState.cpp#L244-L249)
- **Evidenza Tecnica:**
  In `PlayState::Init()`:
  ```cpp
  if (!m_context->forgeWorld) {
      if (m_context->gameWorld) {
          ...
      } else {
          static auto fallbackWorld = std::make_unique<fw::GameWorld>();
          fallbackWorld->Initialize(m_context);
          fallbackWorld->InitializePhysics();
          m_context->forgeWorld = fallbackWorld.get();
          m_context->gameWorld = fallbackWorld.get();
      }
  }
  ```
  La variabile locale statica `fallbackWorld` alloca un `GameWorld` con la sua arena da 128 MB che non viene mai rilasciata fino alla terminazione del processo (distruttori statici del CRT).
  Se `CanSafelyShutdownGlobalMemory()` viene interrogato al termine dell'esecuzione dell'editor, le allocazioni interne di questo `fallbackWorld` statico impediscono lo shutdown pulito (`liveAllocs > 0`), costringendo la memoria a rimanere residente.
- **Impatto:** Medio. Consumo permanente di 128 MB di virtual memory e interferenza con la diagnostica di drenaggio finale.

---

#### [MEM07-F09] [LOW] [SEMPLICE MIGLIORAMENTO] Assenza di Magic Numbers / Canarini in `FreeListAllocator` per rilevare double-free
- **File & Righe Pertinenti:**
  - [`src/core/memory/FreeListAllocator.h:16-19`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/memory/FreeListAllocator.h#L16-L19)
  - [`src/core/memory/FreeListAllocator.cpp:98-146`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/memory/FreeListAllocator.cpp#L98-L146)
- **Evidenza Tecnica:**
  `AllocationHeader` memorizza unicamente `size` e `adjustment`.
  Se un puntatore viene liberato due volte (`Free(ptr)` duplicato), l'allocatore legge l'header corrotto e inserisce il nodo nella free list ordinata creando un ciclo infinito.
  Aggiungere un campo `uint32_t magic` (es. `0xDEADBEEF` per allocato, `0xFEEDFACE` per liberato) permetterebbe a costo zero in Debug di intercettare immediatamente corruzioni e double-free prima di compromettere la catena dei blocchi liberi.
- **Impatto:** Basso. Miglioramento della manutenibilità e robustezza in fase di debug.

---

#### [MEM07-F10] [LOW] [SEMPLICE MIGLIORAMENTO] Assenza di blocco `try ... catch` top-level in `main()` per chiusura sicura di processi orfani
- **File & Righe Pertinenti:**
  - [`src/core/app/main.cpp:158-407`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/main.cpp#L158-L407)
- **Evidenza Tecnica:**
  Se una qualsiasi funzione all'interno del loop o durante l'avvio solleva un'eccezione non intercettata (es. `std::bad_alloc`, eccezioni filesystem JSON), il runtime C++ invoca immediatamente `std::terminate()`.
  In questo scenario, `StopAIServer()` non viene invocato, lasciando il processo server Python (`ai_server/server.py`) attivo in background nel sistema operativo Windows.
- **Impatto:** Basso. Processi zombie orfani in caso di crash non gestito.

---

#### [MEM07-F11] [LOW] [SEMPLICE MIGLIORAMENTO] Incoerenza tra il modello di navigazione `PushState` (Hub) e `ChangeState` (Tool Figli)
- **File & Righe Pertinenti:**
  - [`src/states/HubState.cpp:106-140`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/HubState.cpp#L106-L140)
  - [`src/states/BlockMakerState.cpp:5163`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/BlockMakerState.cpp#L5163)
  - [`src/states/ForgeState.cpp:511`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/ForgeState.cpp#L511)
  - [`src/states/PhysicsLabState.cpp:489`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/PhysicsLabState.cpp#L489)
- **Evidenza Tecnica:**
  `HubState` apre le applicazioni tramite `PushState()`, mantenendo sé stesso sospeso in fondo allo stack.
  Tuttavia, quando l'utente clicca su "Torna all'Hub", i tool invocano `ChangeState(make_unique<HubState>())`, che svuota l'intero stack (chiamando `OnExit` anche sul vecchio `HubState` sospeso) e alloca una nuova istanza di `HubState`.
  Questo rende inutile la sospensione dell'Hub e produce riallocazioni superflue ad ogni navigazione.
- **Impatto:** Basso. Churn di stati e logica di navigazione non canonica.

---

## 4. FATTIBILITÀ E LIMITI DI ADDRESSSANITIZER (ASAN) SU MSVC

### 4.1 Principio di Funzionamento di ASan su MSVC
AddressSanitizer su Visual Studio (`/fsanitize=address`) intercetta le allocazioni sostituendosi alle chiamate del CRT di sistema (`malloc`, `free`, `operator new`, `operator delete`). ASan circonda ciascuna allocazione con zone di guardia ("redzones") e traccia lo stato di validità di ogni byte nella cosiddetta "Shadow Memory" (rapporto 8:1 tra memoria dell'applicazione e memoria ombra).

### 4.2 Il Problema del "Cieco dell'Arena" (Custom Allocator Invisibility)
Nel modello architetturale di FAIRWORLD:
1. All'avvio, `MemorySystem` richiede al sistema operativo **un singolo blocco contiguo da 256 MB** tramite `std::malloc(GLOBAL_HEAP_SIZE)`.
2. `GameWorld` richiede **un blocco da 128 MB** tramite `malloc()`.
3. Tutti gli operatori C++ globali `new` e `delete` instradano le richieste verso `FreeListAllocator`, il quale suddivide e assegna porzioni di questo grande blocco.

**Conseguenza Tecnica Inevitabile:**
Per MSVC ASan, l'intero blocco da 256 MB è un'unica grande allocazione legittima e accessibile.
- Se il codice scrive oltre il confine logico di un blocco restituito da `FreeListAllocator` (Heap Buffer Overflow logico), ASan **non rileva nulla**, purché la scrittura rimanga all'interno dei 256 MB dell'arena.
- Se il codice dealloca un blocco tramite `FreeListAllocator::Free()` e poi continua a leggerlo o scriverlo (Use-After-Free logico), ASan **non rileva nulla**, perché per il CRT e per la Shadow Memory quel byte fa ancora parte del blocco originale da 256 MB allocato all'avvio.

### 4.3 Requisiti per Rendere ASan Efficace su FAIRWORLD
Per conferire ad ASan la capacità di rilevare bug all'interno degli allocatori custom, è necessario integrare esplicitamente le macro di annotazione fornite da `<sanitizer/asan_interface.h>`:
1. **In `FreeListAllocator::Init()`:** Avvelenare l'intera arena:
   ```cpp
   ASAN_POISON_MEMORY_REGION(m_StartPtr, m_TotalSize);
   ```
2. **In `FreeListAllocator::Allocate()`:** Rimuovere il veleno dall'intervallo concesso all'utente (lasciando avvelenate le redzone e l'`AllocationHeader`):
   ```cpp
   ASAN_UNPOISON_MEMORY_REGION(alignedAddress, size);
   ```
3. **In `FreeListAllocator::Free()`:** Ri-avvelenare immediatamente l'intero blocco restituito:
   ```cpp
   ASAN_POISON_MEMORY_REGION(blockStart, blockSize);
   ```

### 4.4 Vincoli e Limiti dell'Ecosistema MSVC
- **Compatibilità Toolchain:** Richiede MSVC v142 o superiore con componente facoltativo "C++ AddressSanitizer" installato.
- **Restrizioni di Compilazione e Linking:** Incompatibile con `/INCREMENTAL` (linking incrementale) e con `/ZI` (Edit and Continue). Richiede runtime dinamico `/MD` o `/MDd`.
- **Linking Esplicito:** Se si usano le annotazioni manuali senza `/fsanitize=address` abilitato nel progetto, il compilatore genererà errori di simbolo non risolto (`__asan_poison_memory_region`), a meno di non racchiudere tutte le chiamate dietro guardie condizionali `#if defined(__SANITIZE_ADDRESS__)`.

---

## 5. LACUNE DI COPERTURA NEI TEST ESISTENTI (MEM-04 .. MEM-06)

Sebbene MEM-04, MEM-05 e MEM-06 abbiano confermato metriche impeccabili nei rispettivi percorsi, l'audit ha identificato 5 percorsi architetturali critici completamente scoperti da qualsiasi test automatizzato:

1. **Test di Navigazione Cross-State con Stato Condiviso:**
   - *Lacuna:* MEM-06 Scenario 03 ha testato transizioni con `DiagMockState` (stato fittizio privo di interazioni con `SharedContext`).
   - *Effetto:* Nessun test automatizzato ha mai eseguito la sequenza reale: `HubState` $\rightarrow$ `BlockMakerState` $\rightarrow$ `HubState` $\rightarrow$ `ForgeState`. È esattamente in questo percorso non testato che si annida il bug critico [MEM07-F01].
2. **Shutdown con Stack non Vuoto:**
   - *Lacuna:* MEM-06 Scenario 01 e Scenario 08 hanno verificato lo shutdown partendo con 0 allocazioni vive e stack vuoto.
   - *Effetto:* Il caso d'uso primario dell'utente reale (uscita dall'editor con un tool attivo premendo la 'X' della finestra) non è coperto da nessun test, nascondendo l'inversione dell'ordine di shutdown [MEM07-F03].
3. **Persistenza e Tracciamento delle Sottoscrizioni `EventManager` a Livello Engine:**
   - *Lacuna:* MEM-01 e MEM-06 hanno verificato subscription create e distrutte all'interno della stessa funzione di test.
   - *Effetto:* I listener statici di `FairWorldEngine` registrati all'avvio dell'applicazione non sono mai stati sottoposti a verifica di distruzione e disiscrizione.
4. **Ciclo di Vita della Memoria VRAM/GPU:**
   - *Lacuna:* MEM-06 è stato ristretto rigorosamente alla sola CPU gameplay memory (*NO VULKAN REFACTOR*).
   - *Effetto:* Allocazioni e deallocazioni su `VramSlabAllocator`, `VulkanDmaManager` e staging buffers non hanno predicati di zero-drift automatici nei test di endurance.
5. **Resilienza dell'Allocatore a Operazioni Invalide:**
   - *Lacuna:* MEM-04 ha verificato percorsi corretti di allocazione/deallocazione.
   - *Effetto:* Nessun test verifica il comportamento in caso di double-free o deallocazione su puntatori non conformi.

---

## 6. PIANO MINIMO DI REMEDIATION (ORDINATO PER RISCHIO)

Il seguente piano stabilisce gli interventi minimi necessari per sanare i finding riscontrati, rispettando categoricamente il vincolo di non alterare i contratti preesistenti né stravolgere l'architettura.

### Priorità 1: Remediation di [MEM07-F01] (Critical) — Ripristino Corretto del Mondo in SharedContext
- **Azione:** In `BlockMakerState::~BlockMakerState()` (e analogamente in `ChunkEditorState` e `PlanetMapperState` via `AppBaseState`), non ripristinare mai `m_context->forgeWorld` assegnandogli `m_context->gameWorld` se quest'ultimo è nullo o coincide con il preview world in via di distruzione.
- **Regola Rigorosa:** `SharedContext::forgeWorld` deve puntare a `m_forgeMaster` dell'engine quando nessun editor proprietario è attivo. Se un preview world viene distrutto, tutti i puntatori in `SharedContext` che puntavano ad esso devono essere resettati in modo atomico e sicuro a `nullptr` (o al mondo master dell'engine), prevenendo categoricamente qualsiasi dangling pointer.

### Priorità 2: Remediation di [MEM07-F02] e [MEM07-F03] (High) — Disiscrizione Engine e Sequenza di Shutdown in `main()`
- **Azione F02:** Memorizzare i due `SubscriptionId` restituiti da `EventManager::Get().Subscribe` in membri privati di `FairWorldEngine` (`m_subBlockMinedId`, `m_subBlockPlacedId`) e invocare `EventManager::Get().Unsubscribe()` all'interno di `FairWorldEngine::Shutdown()`.
- **Azione F03:** In `main.cpp`, prima di invocare `engine.Shutdown()` e `ShutdownGlobals()`, svuotare esplicitamente lo stack di `stateManager` (`while (stateManager.IsRunning()) stateManager.PopState(); stateManager.ProcessTransitions();`). In questo modo tutti gli stati e le loro risorse fisiche/grafiche vengono deallocati deterministicamente **prima** che i sottosistemi globali (Jolt, Vulkan, RuntimeManager) vengano spenti.

### Priorità 3: Remediation di [MEM07-F04] (High) — Chiusura della Simulazione nel Distruttore del Lab
- **Azione:** In `PhysicsLabState::~PhysicsLabState()`, invocare preliminarmente `StopSimulation()` per rimuovere e distruggere tutti i corpi rigidi presenti in `m_joltBodies` dalla `BodyInterface` prima di deallocare `m_joltSystem`.

### Priorità 4: Remediation di [MEM07-F05], [MEM07-F06], [MEM07-F07] (Medium)
- **Azione F05:** Introdurre un parametro booleano `isDestroying` in `GameWorld::ClearWorld()` o separare la pulizia pura delle risorse dalla ri-creazione delle entità singleton del pianeta, evitando allocazioni inutili in `~GameWorld()`.
- **Azione F06:** Tracciare se un'allocazione esterna fallback era over-aligned, oppure garantire che l'over-aligned delete richiami sempre `_aligned_free` e l'unaligned delete non riceva puntatori `_aligned_malloc`.
- **Azione F07:** In `StateManager::ProcessTransitions()`, invocare `OnEnter()` solo se `Init()` restituisce `true`. Se `Init()` fallisce, rimuovere lo stato senza invocare `OnEnter()`.

---

## 7. CRITERI DI ACCETTAZIONE PER IL FUTURO "MEM-07 FROZEN"

Per poter dichiarare formalmente il modulo **MEM-07 FROZEN** al termine della successiva fase di remediation, dovranno essere soddisfatti e verificati con exit code 0 tutti i seguenti criteri oggettivi:

1. **Test di Navigazione Cross-State Reale (PASS):**
   - Implementazione di un test di regressione che riproduca esattamente la sequenza:
     `HubState` $\rightarrow$ `BlockMakerState` $\rightarrow$ `HubState` $\rightarrow$ `ForgeState` $\rightarrow$ `HubState` $\rightarrow$ `PhysicsLabState` (con simulazione avviata) $\rightarrow$ `HubState`.
   - Nessun crash `0xC0000005`, nessun puntatore dangling in `SharedContext`, delta heap globale identico a 0 B al termine del ciclo.
2. **Verifica Ordine di Shutdown con Stato Attivo (PASS):**
   - Esecuzione dello shutdown dell'applicazione simulando la chiusura della finestra con `PhysicsLabState` e `BlockMakerState` residenti nello stack di `stateManager`.
   - Shutdown di Jolt Physics ed Engine completati senza asserzioni e senza deallocazioni post-shutdown.
3. **Zero Listener Leaks in EventManager (PASS):**
   - `EventManager::Get().GetListenerCount()` deve tornare esattamente al valore di baseline dopo il ciclo completo di vita di `FairWorldEngine`.
4. **Verifica Esterna su Build Debug e Release (PASS):**
   - Suite completa MEM-01..MEM-07 eseguita sia in Debug che in Release con 100% test superati e codice di uscita 0.

---

## 8. STATO DELL'AUDIT

> [!IMPORTANT]
> **STATO: AUDIT INDIPENDENTE READ-ONLY COMPLETATO.**  
> Nessun file di codice è stato alterato. Nessun comando di build è stato eseguito nel terminale integrato.  
> Tutti i finding, le evidenze tecniche e i piani di rimedio sono documentati per la revisione da parte dell'utente.

**STOP OPERATIVO: In attesa delle determinazioni dell'utente prima di qualsiasi attività di implementazione o remediation.**
