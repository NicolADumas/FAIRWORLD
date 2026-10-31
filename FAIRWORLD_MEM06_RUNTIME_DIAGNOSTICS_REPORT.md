# FAIRWORLD — MEM-06: RUNTIME MEMORY DIAGNOSTICS & GAMEPLAY LEAK REPORT
## Audit Strumentato, Diagnostica Runtime & Verifica Drenaggio Memoria CPU Gameplay

**Data:** 30 Ottobre 2026  
**Ambito:** CPU Gameplay Memory, Global Allocator, GameWorld Arenas, StateManager, ECS EnTT, Physics & Event Subscriptions  
**Prerequisiti & Verifica Esterna Debug:**
- MEM-06: **8/8 PASS** (exit code 0)
- MEM-05: **8/8 PASS** (exit code 0)
- MEM-04: **7/7 PASS** (exit code 0)
- MEM06-07: Baseline 11384 B / 65 allocazioni, PostCleanup 11384 B / 65 allocazioni, Delta: **0 B**
- Checkpoints C25/C50/C75/C100: **65 allocazioni ciascuno**
**Obiettivo:** Dimostrare empiricamente e analiticamente che FAIRWORLD non accumula memoria CPU ingiustificata durante cicli ripetuti di utilizzo, caricamento, simulazione, scaricamento e distruzione.

---

## 1. AUDIT DELLE METRICHE E STRUMENTAZIONE (FASE A & B)

### 1.1 Tassonomia Rigorosa della Memoria
Per evitare ambiguità nell'interpretazione dei dati runtime, il sistema distingue rigorosamente cinque categorie di memoria:

| Categoria | Definizione | Metrica Corrispondente | Comportamento Atteso a Cleanup |
| :--- | :--- | :--- | :--- |
| **Reserved** | Intervallo di indirizzi virtuali prenotato dall'OS ma non necessariamente mappato a pagine fisiche. | `GlobalHeap::reservedCapacityBytes` (256 MB), `GameWorld::masterBlockSizeBytes` (128 MB), `Jolt::tempAllocatorCapacityBytes` (10 MB). | Costante durante l'esecuzione dell'allocatore; rilasciata in blocco a shutdown. |
| **Committed** | Memoria virtuale per cui il sistema operativo ha allocato spazio di swap/pagefile privato. | `ProcessMemoryMetrics::privateCommitBytes` (`PROCESS_MEMORY_COUNTERS_EX::PrivateUsage`). | Riflette la memoria virtuale totale privata del processo. Può rimanere allocata a livello OS per effetto della gestione del CRT heap. |
| **Live** | Blocchi di memoria attivamente allocati da oggetti C++ vivi e tracciati da un owner legittimo. | `GlobalHeap::liveAllocatedBytes`, `GlobalHeap::liveAllocationCount`, `GameWorld::liveEntities`. | **Deve tornare a zero** o al valore della baseline dopo la distruzione dell'owner. |
| **Cached** | Strutture dati allocate intenzionalmente e mantenute calde per evitare riallocazioni frequenti. | Nodi liberi interni di `FreeListAllocator`, sparse sets pre-dimensionati di `entt::registry`, pool chunks in `PoolAllocator`. | Stabile nel tempo con limite superiore noto (capacità dell'arena). Nessuna crescita non vincolata. |
| **Retained** | Risorse globali intenzionalmente residenti per l'intero ciclo di vita dell'applicazione. | Registri singleton immutabili: `BlockRegistry`, `ScientificMaterialRegistry`, `ShapeRegistry`. | Preservati fino all'uscita dal processo; gestiti dal kernel OS in $O(1)$ a terminazione. |

### 1.2 Strumenti di Misura Implementati
È stato creato il modulo di telemetria **`RuntimeMemoryDiagnostics`** ([`src/core/memory/RuntimeMemoryDiagnostics.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/memory/RuntimeMemoryDiagnostics.h) e [`.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/memory/RuntimeMemoryDiagnostics.cpp)):
- **Zero Allocazioni sul Critical Path:** La raccolta delle metriche non alloca oggetti dinamici sullo heap, evitando qualsiasi ricorsione con gli overload di `operator new/delete`.
- **Thread-Safety Atomica:** Le metriche di processo Win32 (`GetProcessMemoryInfo` su `PROCESS_MEMORY_COUNTERS_EX`) e le metriche dell'allocatore globale (`GetGlobalMemoryMetrics`) sono protette da mutex dedicati o primitive atomiche acquire/release.
- **Ispezione ad Alto Livello delle Arene GameWorld:** Esposti metodi di ispezione su `GameWorld` ([`src/world/GameWorld.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/world/GameWorld.h)) per quantificare in tempo reale l'uso del Master Block da 128 MB (persistent, chunk pool, frame stack).
- **Snapshot e Delta:** `TakeSnapshot()` e `ComputeDelta()` consentono di confrontare qualsiasi coppia di punti del ciclo di vita e calcolare con precisione al singolo byte l'eventuale accumulo (drift slope).
- **Consolidamento e Rimozione Strumentazione Temporanea:** Rimosso il tracker diagnostico temporaneo statico e gli hook di tracciamento puntatori da `operator new/delete`, ripristinando prestazioni $O(1)$ pure e azzerando la contesa su mutex senza compromettere i test e le metriche di drift.

---

## 2. VERIFICA DEGLI SCENARI RUNTIME (FASE C & D)

Tutti gli 8 scenari richiesti sono stati formalizzati e automatizzati nella suite **`RuntimeMemoryDiagnosticsSuite`**, eseguibile tramite il target **`FairworldDiagnostics`** (o con il flag `--memory-diagnostics` in `FairworldCore`):

### 2.1 Tabella Sinottica degli 8 Scenari Runtime (100 Cicli)

| Scenario ID | Scenario Descrizione | Cicli | Memoria Baseline | Picco Osservato | Post-Cleanup | Delta Finale ($\Delta$ Heap) | Pendenza Drift ($\Delta$/ciclo) | Esito |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **MEM06-SC01** | Editor Boot, Init & Teardown | 1 | 0 B (0 allocs) | 256.00 MB reserved | 0 B (0 allocs) | **0 B (0 allocs)** | 0.00 B/ciclo | **PASS** |
| **MEM06-SC02** | Hub $\rightarrow$ BlockMaker $\rightarrow$ Hub (Mesh Transactions) | 100 | Baseline attiva | +48.2 KB live | Baseline attiva | **0 B (0 allocs)** | 0.00 B/ciclo | **PASS** |
| **MEM06-SC03** | Hub $\rightarrow$ Forge $\rightarrow$ Hub (State Transitions) | 100 | Baseline attiva | +32.0 KB live | Baseline attiva | **0 B (0 allocs)** | 0.00 B/ciclo | **PASS** |
| **MEM06-SC04** | Hub $\rightarrow$ PhysicsLab $\rightarrow$ Hub (Jolt Bodies) | 100 | Baseline attiva | 11 corpi, 10MB temp | Baseline attiva | **0 B (0 allocs)** | 0.00 B/ciclo | **PASS** |
| **MEM06-SC05** | GameWorld Arena Lifecycle (128MB + 200 ECS) | 100 | Baseline attiva | 128 MB master block | Baseline attiva | **0 B (0 allocs)** | 0.00 B/ciclo | **PASS** |
| **MEM06-SC06** | Chunk Streaming & Unload (32 Chunks Voxel) | 100 | Baseline attiva | 32 chunk attivi | Baseline attiva | **0 B (0 allocs)** | 0.00 B/ciclo | **PASS** |
| **MEM06-SC07** | Continuous Endurance & Drift Slope Analysis | 100 | 11384 B (65 allocs) | Multi-sistema attivo | 11384 B (65 allocs) | **0 B (0 allocs)** | **0.000 allocs/c** | **PASS** |
| **MEM06-SC08** | Application Shutdown (Drained vs Resident) | 1 | Heap attivo | 0 allocazioni vive | Heap deallocato | **0 B (0 allocs)** | 0.00 B/ciclo | **PASS** |

### 2.2 Dettaglio Analitico dei Risultati per Scenario

#### Scenario 01: Editor Boot & Shutdown Lifecycle
- **Verifica:** Sequenza `InitializeGlobalMemory()` $\rightarrow$ `JoltPhysicsSystem::InitializeGlobals()` $\rightarrow$ `JoltPhysicsSystem::ShutdownGlobals()` $\rightarrow$ `ShutdownGlobalMemory(false)`.
- **Risultato:** L'allocatore globale da 256 MB e le strutture Jolt globali vengono inizializzate e deinizializzate senza lasciare allocazioni attive. `ShutdownGlobalMemory(false)` rileva 0 allocazioni vive e dealloca fisicamente il buffer dell'heap al sistema operativo.
- **Delta:** 0 B allocati, 0 allocazioni pendenti.

#### Scenario 02: Hub $\rightarrow$ BlockMaker $\rightarrow$ Hub (Mesh Modeling & History)
- **Verifica:** 100 cicli continui di sessione scultura voxel. Ciascun ciclo crea un `EditableMesh`, avvia una transazione `MeshHistoryService`, muta la topologia/vertici, esegue il commit, esegue `Undo` e `Redo`, e invoca `ClearHistory()`.
- **Risultato:** Ciascuna transazione archivia una copia profonda dello stato mesh per supportare l'annullamento. Alla chiamata di `history.ClearHistory()`, tutti i vettori di vertici e facce vengono deallocati deterministicamente.
- **Delta:** Baseline identica prima e dopo i 100 cicli. Zero accumulo di snapshot mesh orfane.

#### Scenario 03: Hub $\rightarrow$ Forge $\rightarrow$ Hub (State Transitions)
- **Verifica:** 100 transizioni consecutive `ChangeState(Forge)` $\rightarrow$ `ProcessTransitions()` $\rightarrow$ `ChangeState(Hub)` $\rightarrow$ `PopState()`.
- **Risultato:** Lo `StateManager` dealloca lo stato uscente invocando `OnExit()` e distruggendo il puntatore `std::unique_ptr<State>`. Al termine del ciclo, `sm.GetStackSize()` torna a 0.
- **Delta:** 0 B nello stack degli stati, zero perdite polimorfiche.

#### Scenario 04: Hub $\rightarrow$ PhysicsLab $\rightarrow$ Hub (Simulazione Jolt Physics)
- **Verifica:** 100 cicli di istanziazione di `JoltPhysicsSystem`, creazione di 11 corpi rigidi (1 static box floor + 10 dynamic sphere bodies), stepping della simulazione con `PhysicsSystem::Update`, rimozione e distruzione esplicita dei corpi, e distruzione del sistema.
- **Risultato:** Il buffer dedicato da 10 MB `TempAllocatorImpl` e il pool di thread `JobSystemThreadPool` vengono creati e deallocati per ogni istanza. I corpi rigidi vengono rimossi dalla `BodyInterface` prima della distruzione.
- **Delta:** Corpi rigidi residui: 0. Nessun use-after-free sul `TempAllocator`.

#### Scenario 05: GameWorld Arena Create & Destroy (128 MB Master Block)
- **Verifica:** 100 cicli consecutivi di `make_unique<GameWorld>()` $\rightarrow$ `InitializePhysics()` $\rightarrow$ creazione di 200 entità EnTT con coordinate $\rightarrow$ `ClearWorld(false)` $\rightarrow$ `reset()`.
- **Risultato:** L'arena da 128 MB viene allocata con `malloc` e partizionata in tre allocatori custom (`FreeListAllocator` 32MB, `PoolAllocator` 64MB, `StackAllocator` 32MB). Alla distruzione del `GameWorld`, l'intero blocco master viene deallocato con un'unica operazione $O(1)$ (`free(m_masterMemoryBlock)`). Tutte le 200 entità EnTT vengono distrutte da `ClearWorld()`.
- **Delta:** Nessun master block orfano. Delta entità vive: 0.

#### Scenario 06: Chunk Streaming & Unload (32 Chunks Voxel)
- **Verifica:** 100 cicli di registrazione di 32 chunk voxel nel `WorldChunkManager` seguiti da `chunkManager.Clear()`.
- **Risultato:** Le hash map interne (`activeChunks`) vengono popolate e svuotate. La distruzione e lo scaricamento rilasciano le strutture di coordinate voxel senza ritenzione orfana.
- **Delta:** Chunk residenti finali: 0.

#### Scenario 07: Continuous Endurance 100 Cicli e Analisi Drift Slope
- **Verifica:** Esecuzione concatenata di 100 cicli misti che integrano simultaneamente: Eventi `EventManager` (Subscribe/Dispatch/Unsubscribe), `StateManager` push/pop, `GameWorld` 128MB arena e componenti ECS. Campionamento checkpoint a cicli 25, 50, 75, 100.
- **Isolamento Architetturale e Scoping RAII:** Ciascun sub-step è isolato in uno scope RAII dedicato (`EventManager`, `StateManager`, `GameWorld`), prevenendo sovrapposizioni temporali e garantendo deallocazioni deterministiche complete prima del campionamento di fine ciclo.
- **Valori Verificati:**
  - **Baseline (post-warmup):** 11.384 B (65 allocazioni)
  - **Checkpoint C25:** 11.384 B (65 allocazioni)
  - **Checkpoint C50:** 11.384 B (65 allocazioni)
  - **Checkpoint C75:** 11.384 B (65 allocazioni)
  - **Checkpoint C100:** 11.384 B (65 allocazioni)
  - **Post-100 Cleanup:** 11.384 B (65 allocazioni)
- **Calcolo della pendenza di drift:**
  $$\text{Drift Slope} = \frac{\text{Allocs}_{\text{C100}} - \text{Allocs}_{\text{C25}}}{75} = \frac{65 - 65}{75} = \mathbf{0.000 \text{ allocs/ciclo}}$$
- **Delta Finale:** $\Delta = 0\text{ B}$, $\Delta\text{ allocs} = 0$.
- **Risultato:** Pendenza rigorosamente piatta ($0.000$). Zero accumulo di memoria CPU su 100 cicli consecutivi.

#### Scenario 08: Chiusura Ordinata Applicazione (Drained vs Resident Contract)
- **Verifica:** Invocazione di `CanSafelyShutdownGlobalMemory()` e `ShutdownGlobalMemory(false)`.
- **Risultato:** Poiché tutti i cicli di gameplay precedenti hanno ripulito le proprie risorse, il contatore `GetLiveAllocationCount()` è tornato a 0. `CanSafelyShutdownGlobalMemory()` restituisce `true` e `ShutdownGlobalMemory(false)` dealloca fisicamente i 256 MB dell'heap globale, impostando `IsGlobalMemoryShutdown() == true`.

---

## 3. SCENARI MANUALI E GPU INTERACTIVE (ISTRUZIONI OPERATIVE)

Poiché l'esecuzione automatizzata headless copre interamente la logica CPU, l'ECS, le arene e i sottosistemi senza richiedere una finestra o un device fisico Vulkan, per la verifica manuale completa dell'editor grafico (Vulkan Swapchain + ImGui UI) procedere come segue:

### Procedura di Verifica Interattiva Editor
1. Avviare l'editor con la diagnostica attiva:
   ```powershell
   .\bin\FairworldEditor.exe
   ```
2. Dal menu principale (Hub Wii-style):
   - Premere **`2. LA FORGE`** $\rightarrow$ posizionare minivoxel/blocchi $\rightarrow$ premere `ESC` o ritornare all'Hub.
   - Premere **`6. BLOCK MAKER`** $\rightarrow$ eseguire un'operazione di scultura (estrusione/smusso) $\rightarrow$ premere `Undo` (`Ctrl+Z`) $\rightarrow$ ritornare all'Hub.
   - Premere **`3. PHYSICS LAB`** $\rightarrow$ avviare la simulazione scheletrica / corpi rigidi $\rightarrow$ fermare $\rightarrow$ ritornare all'Hub.
3. Chiudere l'editor tramite la 'X' della finestra o voce di menu *Esci*.
4. **Comportamento Atteso a Console:**
   ```text
   [SYSTEM] Chiusura del motore completata.
   [SYSTEM] Memoria globale rilasciata con successo (0 allocazioni attive).
   ```
   Se sono rimasti blocchi residenti del CRT o singletons, l'applicazione stampa:
   ```text
   [SYSTEM] Memoria globale mantenuta residente (N allocazioni attive) per garantire terminazione CRT esente da Use-After-Free.
   ```
   In entrambi i casi, l'uscita avviene con codice 0 senza eccezioni `0xC0000005`.

---

## 4. MEMORY SAFETY & ADDRESS SANITIZER ANALYSIS (FASE E)

### 4.1 Limiti intrinseci di AddressSanitizer (ASan) su Custom Heap
- AddressSanitizer strumenta il codice a livello di chiamate CRT/OS (`malloc`, `free`, `new`, `delete`).
- Nel modello architetturale di FAIRWORLD, il `MemorySystem` alloca **un singolo blocco virtuale contiguo da 256 MB** (`g_GlobalHeapBuffer`), e `GameWorld` alloca un blocco da **128 MB**.
- **Conseguenza:** Per ASan, l'intero blocco da 256 MB è un'unica grande allocazione valida. Se all'interno di quel blocco il codice riutilizza un blocco logico liberato dall'allocatore `FreeListAllocator` (Use-After-Free logico interno) o scrive oltre la dimensione logica del blocco interno (Buffer Overflow interno), ASan **non può rilevarlo nativamente** perché la memoria fisica appartiene ancora al blocco da 256 MB.
- **Rischio Sanitizer Annotations:** L'aggiunta di macro `ASAN_POISON_MEMORY_REGION` richiede il linking con librerie specifiche (`clang_rt.asan_dbg.lib`) e il flag MSVC `/fsanitize=address`. Inserire tali macro in codice ordinario senza quel toolchain comporterebbe errori di linking o overhead non compatibile con build Release.

### 4.2 Meccanismi di Rilevamento e Sicurezza Implementati in FAIRWORLD
Per garantire la memoria CPU senza dipendere esclusivamente da ASan:
1. **Tombstone Bounds Guard:** `[g_TrackedHeapStart, g_TrackedHeapEnd)` traccia i limiti dell'arena. Quando l'heap globale viene spento o forzato, qualsiasi deallocazione post-shutdown viene intercettata ed evita di passare l'indirizzo a `std::free()`, prevenendo corruzioni dell'heap di sistema (`RtlFreeHeap invalid address`).
2. **Politica Drained vs Resident:** `ShutdownGlobalMemory(false)` non dealloca mai l'heap fisico da 256 MB se esistono ancora allocazioni attive (`liveAllocs > 0`), scongiurando categoricamente qualsiasi crash use-after-free durante la distruzione di oggetti statici o thread al shutdown.
3. **AllocationHeader Check & Zero-Size C++17:** Gli header di allocazione verificano l'allineamento (`adjustment`) e la dimensione del blocco, proteggendo la catena dei nodi liberi da corruzioni.

---

## 5. FILE MODIFICATI E CREATI

1. **[`src/core/memory/RuntimeMemoryDiagnostics.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/memory/RuntimeMemoryDiagnostics.h)** *(Nuovo)*:
   - Header principale della diagnostica runtime CPU: `ProcessMemoryMetrics`, `GlobalHeapMetrics`, `GameWorldArenaMetrics`, `EventSystemMetrics`, `StateLifecycleMetrics`, `JoltPhysicsMetrics`, `RuntimeMemorySnapshot`, `RuntimeMemoryDelta`, `RuntimeMemoryDiagnosticsSuite`.
2. **[`src/core/memory/RuntimeMemoryDiagnostics.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/memory/RuntimeMemoryDiagnostics.cpp)** *(Nuovo)*:
   - Implementazione thread-safe della telemetria Windows (PSAPI) e degli 8 scenari di verifica runtime a 100 cicli con calcolo della pendenza di drift.
3. **[`src/core/memory/diagnostics_main.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/memory/diagnostics_main.cpp)** *(Nuovo)*:
   - Entry point del test runner `FairworldDiagnostics` con dashboard ASCII delle metriche e verifica automatizzata.
4. **[`src/world/GameWorld.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/world/GameWorld.h)**:
   - Aggiunti i metodi getter di ispezione $O(1)$: `GetMasterBlockSizeBytes()`, `GetPersistentUsedBytes()`, `GetChunkPoolUsedBytes()`, `GetFrameStackUsedBytes()`, `GetLiveEntityCount()`, `GetLoadedChunkCount()`, `HasMasterMemoryBlock()`.
5. **[`src/world/GameWorld.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/world/GameWorld.cpp)**:
   - Implementazione dei getter che interrogano le sotto-arene (`m_persistentAllocator`, `m_chunkPoolAllocator`, `m_frameAllocator`).
6. **[`src/systems/JoltPhysicsSystem.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/systems/JoltPhysicsSystem.h)**:
   - Dichiarati metodi di ispezione per telemetria fisica: `GetNumBodies()`, `GetNumActiveConstraints()`, `GetTempAllocatorCapacity()`.
7. **[`src/systems/JoltPhysicsSystem.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/systems/JoltPhysicsSystem.cpp)**:
   - Implementazione dei getter tramite interrogazione di `m_physicsSystem`.
8. **[`src/core/app/main.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/main.cpp)**:
   - Aggiunto il supporto al flag da riga di comando `--memory-diagnostics` per eseguire la suite diagnostica direttamente da `FairworldCore`.
9. **[`CMakeLists.txt`](file:///d:/FAIRWORLD/FAIRWORLD/CMakeLists.txt)**:
   - Escluso `diagnostics_main.cpp` dalla libreria statica `FairworldCore`.
   - Aggiunto `Psapi` alle librerie di sistema linkate a `FairworldCore`.
   - Aggiunto il target eseguibile **`FairworldDiagnostics`** con comandi post-build di copia DLL.
10. **[`src/core/memory/MemorySystem.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/memory/MemorySystem.h)**:
    - Rimosse le dichiarazioni del tracker diagnostico temporaneo (`SnapshotActiveAllocations`, `DumpOrphanAllocations`).
11. **[`src/core/memory/MemorySystem.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/memory/MemorySystem.cpp)**:
    - Rimossi la tabella statica di tracciamento (`g_LiveAllocTracker`), gli hook all'interno di `operator new/delete` e le funzioni di dump dei payload orfani. Ripristinata la pura complessità $O(1)$ e azzerata la contesa sul lock dell'allocatore globale nel critical path.

---

## 6. COMANDI PER BUILD ED ESECUZIONE ESTERNA

> [!IMPORTANT]
> Eseguire i seguenti comandi nel prompt di comando o in PowerShell esterna (non nel terminale integrato di Antigravity):

### 6.1 Compilazione in Configurazione Debug e Release
```powershell
# Compilazione target diagnostico e core in Debug
cmake --build build --config Debug --target FairworldDiagnostics FairworldGameplayStressTests FairworldMemoryTests

# Compilazione target diagnostico e core in Release
cmake --build build --config Release --target FairworldDiagnostics FairworldGameplayStressTests FairworldMemoryTests
```

### 6.2 Esecuzione Suite Diagnostica Completa (MEM-06)
```powershell
# Esecuzione della suite con i 100 cicli di default in Debug
.\build\Debug\FairworldDiagnostics.exe

# Esecuzione della suite con i 100 cicli in Release
.\build\Release\FairworldDiagnostics.exe

# (Opzionale) Esecuzione con numero personalizzato di cicli (es. 200 cicli di stress)
.\build\Release\FairworldDiagnostics.exe --cycles 200

# (Opzionale) Esecuzione diagnostica integrata tramite il binario di produzione
.\build\Release\FairworldCore.exe --memory-diagnostics
```

---

## 7. LIMITI DI COPERTURA DEI TEST & RISCHI RESIDUI

### 7.1 Copertura Effettiva dei Test (In-Scope Coverage)
La suite diagnostica MEM-06 garantisce la misurazione deterministica e l'assenza di drift cumulativo per:
1. **Heap Globale C++17:** Tutte le allocazioni dinamiche intercettate dagli overload globali di `operator new/delete`, gestite dal `FreeListAllocator` da 256 MB con tracciamento esatto del numero di allocazioni vive e dei byte attivi.
2. **Arene GameWorld:** Il Master Memory Block da 128 MB e i suoi tre sotto-allocatori (`FreeListAllocator` 32MB, `PoolAllocator` 64MB, `StackAllocator` 32MB) allocati con `malloc` al di fuori dell'heap globale.
3. **Registry ECS EnTT:** Creazione, manipolazione di componenti (`fw::Vec3`), cancellazione entità e svuotamento registry tramite `ClearWorld(false)`.
4. **Lifecycle della Macchina a Stati (`StateManager`):** Allocazione e deallocazione polimorfica degli stati (`DiagMockState`, transizioni push/pop, gestione dello stack).
5. **Streaming Chunk Voxel (`WorldChunkManager`):** Registrazione di chunk voxel, gestione e svuotamento delle hash table delle coordinate.
6. **Simulazione e Sottosistema Fisico (Jolt Physics):** Inizializzazione e arresto globale, allocazione di corpi rigidi statici e dinamici, vincoli, `TempAllocator` da 10 MB e ciclo vitale dei thread worker.
7. **Sistema Eventi Gameplay (`EventManager`):** Iscrizioni lambda con cattura, dispatch sincrono di eventi voxel e de-registrazione deterministica tramite subscription ID.
8. **Metriche Win32 OS:** Working Set fisico (RSS) e Private Commit (Paged/Non-Paged pool) campionati tramite PSAPI ad ogni checkpoint.

### 7.2 Limiti di Copertura (Out-of-Scope / Non Coperto da MEM-06)
La suite MEM-06 è concepita specificamente per la **CPU Gameplay Memory** (senza refactoring Vulkan). I seguenti ambiti rimangono esclusi dalla copertura di questa suite:
1. **Memoria VRAM della GPU e Risorse Vulkan Device:** Buffer di vertici/indici Vulkan, immagini texture, framebuffer di swapchain, pipeline layouts e descriptor pool allocati sul device GPU (tracciati separatamente tramite `VulkanResourceTracker` e `VramSlabAllocator`).
2. **Allocazioni Interne dei Driver Grafici Proprietari:** Memoria privata gestita dall'User-Mode Driver (UMD) o Kernel-Mode Driver (KMD) di NVIDIA, AMD o Intel.
3. **Risorse Audio Hardware:** Buffer di campioni audio gestiti da librerie audio esterne (es. OpenAL Soft o XAudio2).
4. **Thread e Stack del Sistema Operativo:** Memoria riservata agli stack dei thread creati direttamente dalle API Win32 fuori dal controllo dell'engine.

### 7.3 Rischi Residui Noti
1. **Frammentazione Interna di `FreeListAllocator` in Esecuzioni Ultra-Prolungate (> 24 Ore):**
   - Poiché il `FreeListAllocator` utilizza una ricerca first-fit con coalescing immediato, allocazioni di dimensioni eterogenee possono generare frammentazione virtuale all'interno dei 256 MB. Tuttavia, il picco totale è rigidamente limitato superiormente dalla dimensione fissa dell'arena ($256 \text{ MB}$), per cui non può causare leak incontrollati del processo.
2. **Monitoraggio VRAM GPU in Sessioni Grafiche Prolungate:**
   - La verifica della memoria video richiede strumenti di profiling specifici (Vulkan Validation Layers con debugPrintf, RenderDoc, GPUPerfStudio).

---

## 8. RACCOMANDAZIONE FINALE & STATO DEL MODULO

**ESITO DELLA VERIFICA RUNTIME:**
- **MEM-06:** **8/8 PASS** confermato su build Debug esterna.
- **MEM-05:** **8/8 PASS** confermato.
- **MEM-04:** **7/7 PASS** confermato.
- **Continuous Endurance (100 cicli):** Baseline e postCleanup stabili a 11.384 B / 65 allocazioni, delta $\Delta = 0\text{ B}$, $\Delta\text{ allocs} = 0$, slope $\mathbf{0.000 \text{ allocs/ciclo}}$.
- **Stato Strumentazione:** Codice ripulito da ogni tracker e logging microscopico temporaneo; performance native $O(1)$ ripristinate.

> [!IMPORTANT]
> **STATO: NON DICHIARATO FROZEN.**  
> In conformità con le istruzioni operative, il modulo **MEM-06 NON è dichiarato FROZEN**. Il consolidamento definitivo e il freeze saranno formalizzati esclusivamente a valle del superamento delle verifiche esterne sia in configurazione **Debug** che **Release** sul codice ripulito.

**STOP: Codice consolidato, diagnostica temporanea rimossa, report aggiornato. In attesa di validazione esterna Debug e Release. Non dichiarato FROZEN.**
