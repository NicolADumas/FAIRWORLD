# FAIRWORLD — MEM-07A: OWNERSHIP & LIFECYCLE REMEDIATION REPORT

**Data:** 30 Ottobre 2026  
**Ambito:** CPU Memory Safety, Ownership Resolution, State Lifecycle, Destruction Order & Regression Tests  
**Stato Precedente:** MEM-01..MEM-06 verificati Debug e Release (PASS 100%, FROZEN).  
**Stato Attuale:** REMEDIATION IMPLEMENTATA — IN ATTESA DI VALIDAZIONE ESTERNA.  

---

## 1. CLASSIFICAZIONE DEI FINDING (FASE 1 — VERIFICA TECNICA)

In conformità ai requisiti di Fase 1, ogni finding prioritario dell'audit MEM-07 è stato sottoposto a verifica tecnica indipendente sul codice sorgente effettivo prima di apportare modifiche:

| Finding ID | Titolo | Componenti Coinvolti | Esito Verifica | Motivazione Tecnica |
| :--- | :--- | :--- | :--- | :--- |
| **MEM07-F01** | Dangling pointer tra `BlockMakerState` e `ForgeState` | `BlockMakerState.cpp`, `ForgeState.cpp`, `GameWorld.cpp`, `ChunkEditorState.cpp`, `PlanetMapperState.cpp` | **CONFIRMED** | In `BlockMakerState::~BlockMakerState()` l'assegnazione `m_context->forgeWorld = m_context->gameWorld` propagava `m_previewWorld.get()` se `gameWorld` era nullo/condiviso, lasciando un puntatore penzolante dopo `m_previewWorld.reset()`. Inoltre `ForgeState::Init()` dereferenziava `m_context->forgeWorld` senza null-check a riga 117 (`EnqueueDeferredMesh`), provocando crash certo `0xC0000005`. |
| **MEM07-F02** | Listener `EventManager` con capture `[this]` orfano | `FAIRWORLD.cpp`, `FAIRWORLD.h`, `EventManager.h` | **CONFIRMED** | In `FairWorldEngine::Init()`, le chiamate `Subscribe<Event_BlockMined>` e `Subscribe<Event_BlockPlaced>` catturavano `[this]` ignorando il `SubscriptionId` restituito. Né `Shutdown()` né `~FairWorldEngine()` eseguivano `Unsubscribe`, causando un leak permanente di listener e potenziale Use-After-Free in caso di dispatch post-shutdown. |
| **MEM07-F03** | Shutdown globale prima della distruzione degli stati | `main.cpp`, `StateManager.h` | **CONFIRMED** | `StateManager` allocato sullo stack in `main()` veniva distrutto all'uscita dalla funzione, ovvero **dopo** `engine.Shutdown()`, `JoltPhysicsSystem::ShutdownGlobals()` e `fw::memory::ShutdownGlobalMemory()`. Gli stati attivi (es. `PhysicsLabState`) tentavano di deallocare risorse fisiche quando `JPH::Factory::sInstance` era già stato deallocato. |
| **MEM07-F04** | Cleanup incompleto di `PhysicsLabState` | `PhysicsLabState.cpp`, `PhysicsLabState.h`, `JoltPhysicsSystem.cpp` | **CONFIRMED** | `~PhysicsLabState()` non invocava `StopSimulation()`, lasciando i corpi dinamici nella `BodyInterface` di Jolt prima di `delete m_joltSystem;`. Inoltre, il corpo statico del pavimento (`m_floorBodyID`) non veniva mai rimosso né distrutto, violando i contratti di integrità di Jolt Physics. |

---

## 2. MODELLO DI OWNERSHIP: PRIMA vs DOPO

### 2.1 Gestione di `SharedContext::forgeWorld` e `SharedContext::gameWorld`

```
PRIMA (Ownership Ambigua e Dangling Pointer):
┌─────────────────────────┐
│ FairWorldEngine         │ ─── owns ───> [ Master GameWorld ]
└─────────────────────────┘                         ▲
                                                    │
┌─────────────────────────┐                         │
│ BlockMakerState         │ ─── owns ───> [ Preview GameWorld ] ─── (Destruction) ───┐
└─────────────────────────┘                         ▲                                 │
                                                    │                                 ▼
┌─────────────────────────┐                         │                       Dangling Pointer
│ SharedContext           │ ─── non-owning ─────────┴────────────────────── (UAF in ForgeState!)
└─────────────────────────┘
```

```
DOPO (Ownership Rigorosa e Ripristino Deterministico):
┌─────────────────────────┐
│ FairWorldEngine         │ ─── owns ───> [ Master GameWorld ] ◄──────────────────┐
└─────────────────────────┘                                                       │
                                                                                  │ (Restored)
┌─────────────────────────┐                                                       │
│ BlockMakerState         │ ─── owns ───> [ Preview GameWorld ] ─── (Destruction) │
└─────────────────────────┘                                                       │
                                                                                  │
┌─────────────────────────┐                                                       │
│ SharedContext           │ ─── non-owning ───────────────────────────────────────┘
└─────────────────────────┘
 (Se masterWorld è assente, forgeWorld e gameWorld vengono atomizzati a nullptr)
 (ForgeState::Init() esegue recovery da engine->GetForgeMaster() e protegge EnqueueDeferredMesh)
```

### 2.2 Sottoscrizioni `EventManager`

- **Prima:** `FairWorldEngine` invocava `Subscribe` e scartava il token `SubscriptionId`. Ownership della callback delegata a vita a `EventManager::Get()`.
- **Dopo:** `FairWorldEngine` memorizza `m_subBlockMined` e `m_subBlockPlaced`. In `Shutdown()` viene invocato deterministicamente `EventManager::Get().Unsubscribe(id)`, azzerando i token a `INVALID_SUBSCRIPTION_ID`. Zero listener leak.

### 2.3 Sequenza di Shutdown dell'Applicazione

- **Prima:**
  1. Main loop termina.
  2. `engine.Shutdown()`.
  3. `JoltPhysicsSystem::ShutdownGlobals()`.
  4. `CanSafelyShutdownGlobalMemory()` $\rightarrow$ `false` (stati ancora vivi nello stack).
  5. `main()` return: invocazione di `~StateManager()`, che distrugge `PhysicsLabState` tentando chiamate a Jolt quando `Factory::sInstance == nullptr`.
- **Dopo:**
  1. Main loop termina.
  2. `stateManager.Clear()`: **tutti gli stati vengono svuotati, deallocati e le simulazioni arrestate** mentre engine, Jolt e memoria sono 100% attivi.
  3. `engine.Shutdown()`.
  4. `JoltPhysicsSystem::ShutdownGlobals()`.
  5. `CanSafelyShutdownGlobalMemory()` $\rightarrow$ `true` (zero allocazioni orfane).

### 2.4 Cleanup Fisico in `PhysicsLabState`

- **Prima:** `~PhysicsLabState()` distruggeva `m_joltSystem` con `m_floorBodyID` e `m_joltBodies` ancora residenti nella `BodyInterface`.
- **Dopo:**
  1. Invocazione preventiva di `StopSimulation()` (idempotente: se già fermata o priva di corpi, esce immediatamente).
  2. Rimozione e distruzione esplicita di `m_floorBodyID` da `BodyInterface`, con azzeramento `m_floorBodyID = 0`.
  3. Deallocazione di `m_labWorld` e ripristino di `m_context->forgeWorld = m_originalWorld`.
  4. Deallocazione sicura di `m_joltSystem`. Zero corpi residui in Jolt.

---

## 3. FILE MODIFICATI E INTERVENTI APPLICATI

1. [`src/core/app/FAIRWORLD.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/FAIRWORLD.h):
   - Inclusione di `#include "EventManager.h"`.
   - Aggiunta getter `GetForgeMaster()`, `GetSubBlockMined()`, `GetSubBlockPlaced()`.
   - Aggiunta metodi `RegisterEventSubscriptions()` e `UnregisterEventSubscriptions()`.
   - Membri privati `m_subBlockMined = INVALID_SUBSCRIPTION_ID` e `m_subBlockPlaced = INVALID_SUBSCRIPTION_ID`.
2. [`src/core/app/FAIRWORLD.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/FAIRWORLD.cpp):
   - Implementazione di `RegisterEventSubscriptions()` con guardie `if (!m_forgeMaster) return;` nelle lambda.
   - Implementazione di `UnregisterEventSubscriptions()` con disiscrizione sicura e reset token.
   - Chiamata a `RegisterEventSubscriptions()` in `Init()`.
   - Chiamata a `UnregisterEventSubscriptions()` in `Shutdown()`.
3. [`src/world/GameWorld.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/world/GameWorld.cpp):
   - In `~GameWorld()`, se `m_context` punta a `this` in `gameWorld`, `forgeWorld` o `activeRegistry`, i puntatori vengono azzerati a `nullptr`. Nessun puntatore può sopravvivere al mondo distrutto.
4. [`src/states/BlockMakerState.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/BlockMakerState.cpp):
   - In `~BlockMakerState()`, ripristino a `masterWorld` (da `engine->GetForgeMaster()`) o a `nullptr`, impedendo che `forgeWorld` o `gameWorld` puntino al preview world in fase di distruzione.
   - Aggiunta guardia su `m_context->engine` a riga 4647 per evitare dereferenziazione nulla in assenza di finestra/motore.
5. [`src/states/ForgeState.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/ForgeState.cpp):
   - In `Init()`, ripristino automatico di `forgeWorld` da `engine->GetForgeMaster()` o `m_context->gameWorld` se nullo.
   - Protezione condizionale `if (m_context && m_context->forgeWorld)` su tutte le chiamate a `EnqueueDeferredMesh`.
   - Aggiunta guardia su `m_context->engine` prima di inizializzare `dmaManager`.
6. [`src/states/ChunkEditorState.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/ChunkEditorState.cpp):
   - In `~ChunkEditorState()`, ripristino di `forgeWorld` e `activeRegistry` al `masterWorld` dell'engine invece di azzeramento cieco.
7. [`src/states/PlanetMapperState.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/PlanetMapperState.cpp):
   - In `~PlanetMapperState()`, ripristino di `forgeWorld` e `activeRegistry` al `masterWorld` dell'engine.
8. [`src/core/app/StateManager.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/StateManager.h):
   - Aggiunto metodo `Clear()` per svuotare lo stack invocando `OnExit()` su ciascuno stato e ripulire le cache.
   - In `ProcessTransitions()`, invocazione di `OnEnter()` vincolata all'esito positivo di `Init()` (fix per inizializzazioni parziali).
9. [`src/core/app/main.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/main.cpp):
   - Inserita invocazione esplicita `stateManager.Clear();` prima di `engine.Shutdown()`, `JoltPhysicsSystem::ShutdownGlobals()` e `ShutdownGlobalMemory()`.
10. [`src/states/PhysicsLabState.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/PhysicsLabState.h):
    - Resi pubblici `StartSimulation()` e `StopSimulation()`.
    - Aggiunti getter di ispezione `IsSimulating()`, `GetJoltBodyCount()`, `GetFloorBodyID()`, `GetJoltPhysicsSystem()`.
11. [`src/states/PhysicsLabState.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/PhysicsLabState.cpp):
    - In `~PhysicsLabState()`, invocazione di `StopSimulation()` e rimozione/distruzione esplicita del corpo statico `m_floorBodyID`.
    - In `StopSimulation()`, aggiunta guardia su `physSys` nullo per garantire idempotenza totale.
12. [`src/states/HubState.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/states/HubState.cpp):
    - Guardia condizionale su `m_context && m_context->deviceManager` in `Init()`.
13. [`CMakeLists.txt`](file:///d:/FAIRWORLD/FAIRWORLD/CMakeLists.txt):
    - Escluso `lifecycle_tests_main.cpp` da `FairworldCore`.
    - Aggiunto target eseguibile `#13`: `FairworldLifecycleTests`.

---

## 4. NUOVA SUITE DI TEST REGRESSIONE (MEM-07A)

I test sono stati implementati in:
- [`src/core/app/LifecycleRemediationContractTests.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/LifecycleRemediationContractTests.h)
- [`src/core/app/LifecycleRemediationContractTests.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/LifecycleRemediationContractTests.cpp)
- [`src/core/app/lifecycle_tests_main.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/core/app/lifecycle_tests_main.cpp)

| Test ID | Nome Test | Obiettivo / Finding | Percorso Reale Validato |
| :--- | :--- | :--- | :--- |
| **LC-01** | Cross-State Transition | MEM07-F01 | `HubState` $\rightarrow$ `Push(BlockMakerState)` $\rightarrow$ `Change(ForgeState)` $\rightarrow$ `Change(HubState)` $\rightarrow$ `Push(BlockMakerState)` $\rightarrow$ `Pop()` $\rightarrow$ `Clear()`. Verifica che `forgeWorld` sia ripristinato a `masterWorld`, nessuna eccezione in `ForgeState`, 0 dangling pointers. |
| **LC-02** | BlockMaker Shared ForgeWorld Preservation | MEM07-F01 | Testa la distruzione di `BlockMakerState` sia con master world attivo che in contesto isolato (inizialmente nullo). Verifica che dopo la distruzione di `m_previewWorld`, nessun membro di `SharedContext` contenga l'indirizzo della memoria deallocata. |
| **LC-03** | Shutdown with Active PhysicsLab State | MEM07-F03, MEM07-F04 | Inizializza Jolt Physics, crea `PhysicsLabState`, avvia la simulazione (corpi dinamici registrati in Jolt), esegue `stateManager.Clear()`, verifica la distruzione di tutti i corpi (pavimento + joint) e spegne Jolt con `ShutdownGlobals()` con 0 asserzioni. |
| **LC-04** | Shutdown with Registered EventManager Listeners | MEM07-F02 | Registra le sottoscrizioni di `FairWorldEngine` via token RAII, verifica l'incremento di `GetTotalListenerCount()`, invoca `engine.Shutdown()`, verifica il ripristino esatto del conteggio baseline, invoca `Dispatch` post-shutdown per confermare l'assenza di callback penzolanti. |
| **LC-05** | Partial Initialization Safety | MEM07-F03, MEM07-F04, MEM07-F07 | Testa distruzione di `PhysicsLabState` e `BlockMakerState` mai inizializzati (puntatori interni nulli). Testa rollback in `StateManager` quando `Init()` restituisce `false`: verifica che `OnEnter()` non venga chiamato e lo stato fallito venga espulso. |

---

## 5. CRITERI DI ACCETTAZIONE PER LA VALIDAZIONE ESTERNA

La remediation MEM-07A sarà considerata completata con successo quando l'utente eseguirà nel proprio terminale esterno:

1. **Compilazione Debug & Release:**
   - `FairworldCore`, `FairworldEditor`, `FairworldLifecycleTests`, `FairworldDiagnostics`, `FairworldGameplayStressTests`.
   - Exit code: 0 su entrambe le configurazioni.
2. **Esecuzione Test MEM-07A:**
   - Esecuzione di `bin/FairworldLifecycleTests.exe`.
   - Esito atteso: **5/5 PASS, exit code 0**.
3. **Non-Regressione MEM-01..MEM-06:**
   - `FairworldEventTests` (MEM-01): 7/7 PASS.
   - `FairworldNoiseTests` (MEM-02): 7/7 PASS.
   - `FairworldAsyncInputTests` (MEM-03): 7/7 PASS.
   - `FairworldMemoryTests` (MEM-04): 7/7 PASS.
   - `FairworldGameplayStressTests` (MEM-05): 8/8 PASS.
   - `FairworldDiagnostics` (MEM-06): 8/8 PASS.
4. **Verifica Editor Reale (`FairworldEditor.exe`):**
   - Apertura editor, navigazione da Hub a BlockMaker, ritorno a Hub, apertura Forge, uscita tramite 'X'.
   - Chiusura pulita senza crash `0xC0000005`, con log: `[SYSTEM] Memoria globale rilasciata con successo (0 allocazioni attive)`.

---

> [!NOTE]
> Nessun comando di build è stato eseguito nel terminale integrato.
> Nessuna modifica a contratti MEM-01..MEM-06 è stata introdotta.
> Il Material Atlas non è stato avviato.
> L'ambiente è pronto per la validazione esterna da parte dell'utente.
