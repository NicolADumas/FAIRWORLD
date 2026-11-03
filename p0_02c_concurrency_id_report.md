# FAIRWORLD — P0.2C: REGISTRY CONCURRENCY & STABLE CUSTOM IDs REPORT

## 1. Cause dei Rischi Individuati (Audit della Concorrenza e Ciclo di Vita ID)

Prima dell'intervento su `ScientificMaterialRegistry`, l'audit architetturale ha evidenziato le seguenti criticità:

### A. Data Race sull'Accesso e Pubblicazione dello Snapshot
- **Presupposto errato**: la presenza di `std::shared_ptr<const RegistrySnapshot>` non garantisce thread-safety durante la pubblicazione.
- **Rischio verificato**: la scrittura simultanea `m_activeSnapshot = candidateSnapshot` mentre un altro thread consumer esegue `GetSnapshotHandle()` o interroga `m_activeSnapshot` causa un data race con Undefined Behavior sul reference counter e sui puntatori di controllo di `std::shared_ptr`.
- **Accoppiamento revisione/dati**: l'avanzamento separato di `m_revision++` e `m_activeSnapshot` esponeva una finestra temporale in cui un lettore poteva osservare una revisione aggiornata abbinata ai vecchi dati dello snapshot precedente (o viceversa).

### B. Rischio Deadlock su Operazioni Composte
- I metodi di ciclo di vita `PublishMaterial`, `DeprecateMaterial` e `SetEnabledInBlockMaker` convalidano la transizione e invocano internamente `RegisterMaterial(updatedDef, true)`.
- Se `RegisterMaterial` e `PublishMaterial` acquisissero ciascuno un lock esclusivo su un medesimo mutex non-ricorsivo, si verificherebbe un deadlock immediato (self-deadlock). L'uso di lock ricorsivi avrebbe mascherato problemi strutturali senza risolvere l'ordine di serializzazione.

### C. Blocco e Latenza da Disk I/O sotto Lock
- Le funzioni di persistenza `LoadCustomMaterialsFromJson` e `SaveCustomMaterialsToJson` eseguivano l'intero flusso di parsing JSON e scrittura disco (inclusa la sostituzione Win32 `ReplaceFileW`/`MoveFileExW`).
- Se queste operazioni venissero racchiuse all'interno di un lock esclusivo, qualsiasi thread lettore o scrittore in background verrebbe bloccato per millisecondi (o secondi in presenza di dischi lenti o I/O wait), degradando le prestazioni del motore.

### D. Riutilizzo Accidentale degli Identificatori Custom (ID Collision Risk)
- `AllocateNextCustomMaterialId()` calcolava l'ID basandosi sul massimo ID presente in memoria (`std::max_element(m_materials.begin(), ...)`).
- **Vulnerabilità critiche**:
  1. **Dopo `Clear()`**: cancellando tutti i materiali, il massimo ritornava a 0 o al valore standard, e la successiva allocazione ripartiva da 1000, riassegnando lo stesso ID a un materiale concettualmente diverso (corruzione degli asset storici).
  2. **Dopo cancellazione / caricamento di cataloghi ridotti**: se un utente creava i materiali 1000, 1001 e 1002, poi cancellava il 1001 o salvava solo 1000 e 1002, il caricamento di un catalogo parziale poteva riallocare 1001.
  3. **Concorrenza non serializzata**: due thread invocanti simultaneamente l'allocazione ottenevano lo stesso identificatore prima che uno di essi registrasse la propria definizione.
  4. **Assenza di distinzione tra consultazione e prenotazione**: i client che volevano solo visualizzare l'ID provvisorio nella UI (peek) rischiavano di alterare lo stato o di non poter bloccare l'ID desiderato.

---

## 2. Modello di Sincronizzazione Scelto

È stato implementato un modello di sincronizzazione thread-safe, deterministico e verificabile basato su `std::shared_mutex`:

```
                       ┌──────────────────────────────────────────────┐
                       │          ScientificMaterialRegistry          │
                       │           (mutable std::shared_mutex)        │
                       └──────────────────────┬───────────────────────┘
                                              │
                    ┌─────────────────────────┴────────────────────────┐
                    ▼                                                  ▼
      ┌───────────────────────────┐                      ┌───────────────────────────┐
      │     Lettori Concorrenti   │                      │    Scrittori Concorrenti  │
      │ std::shared_lock<m_mutex> │                      │ std::unique_lock<m_mutex> │
      └─────────────┬─────────────┘                      └─────────────┬─────────────┘
                    │                                                  │
                    ▼                                                  ▼
     Acquisizione snapshot CoW Handle                 Mutazioni isolate su candidati
      - GetSnapshotHandle()                            - RegisterMaterialLocked()
      - GetRevision()                                  - Publish/DeprecateLocked()
      - QueryExtendedProperty()                        - Transazione CoW atomica:
      - Validazione & Lookup                            * m_revision++
                    │                                   * candidateSnapshot->revision = rev
                    ▼                                   * m_activeSnapshot = candidate
       Iterazione lock-free su                          * HWM avanzato monotonicamente
       snapshot immutabile                            Decoupled File I/O (senza lock)
```

### Principi Architetturali Applicati:
1. **Letture coerenti e isolate**: `GetSnapshotHandle()`, `GetRevision()`, `GetMaterial()` e i metodi di query acquisiscono `std::shared_lock<std::shared_mutex>`. Una volta ottenuto l'handle immutabile `RegistrySnapshotHandle`, le iterazioni (come `GetSelectableMaterialCopies()`) avvengono a lock rilasciato, garantendo zero contesa per i lettori e nessuna copia ridondante dello storage.
2. **Scritture serializzate e CoW Atomico**: Tutte le mutazioni (`RegisterMaterial`, `PublishMaterial`, `DeprecateMaterial`, `SetEnabledInBlockMaker`, `Clear`, `LoadCustomMaterialsFromJson`) acquisiscono `std::unique_lock<std::shared_mutex>`. La mutazione prepara un grafo candidato (`candidateMaterials`, `candidateCanonicalMap`, `candidateSnapshot`) e committa atomicamente la nuova revisione e il nuovo snapshot contemporaneamente sotto lock.
3. **Prevenzione Deadlock tramite Pattern Public/Locked**:
   - I metodi pubblici acquisiscono il lock esclusivo e delegano alle controparti private `*Locked`:
     - `RegisterMaterial` -> `RegisterMaterialLocked`
     - `PublishMaterial` -> `PublishMaterialLocked` -> chiama `RegisterMaterialLocked`
     - `DeprecateMaterial` -> `DeprecateMaterialLocked` -> chiama `RegisterMaterialLocked`
     - `SetEnabledInBlockMaker` -> `SetEnabledInBlockMakerLocked` -> chiama `RegisterMaterialLocked`
     - `Clear` -> `ClearLocked`
     - `ValidateMaterialDefinition` -> `ValidateMaterialDefinitionLocked`
   - Questo elimina qualsiasi acquisizione ricorsiva o rischio di deadlock.
4. **I/O Asincrono Svincolato dal Lock**:
   - `LoadCustomMaterialsFromJson`: lettura e parsing del file JSON su filesystem avvengono a lock libero; solo la validazione transazionale e il commit del catalogo acquisiscono `std::unique_lock`.
   - `SaveCustomMaterialsToJson`: serializzazione dello snapshot in memoria sotto `std::shared_lock`; rilascio del lock; scrittura su file temporaneo `.tmp` e sostituzione sicura con `ReplaceFileW` / `MoveFileExW` completamente al di fuori del lock.

---

## 3. Contratto degli Identificatori Custom (Stable Custom IDs & High-Water Mark)

Il ciclo di vita degli ID custom è regolato dal seguente contratto formale:

1. **Range e Base**:
   - Tutti gli ID custom appartengono all'intervallo `[CUSTOM_SCIENTIFIC_MATERIAL_ID_BASE, UINT32_MAX]` (`base = 1000`).
   - Gli ID standard (`< 1000`) rimangono protetti da collisioni e modifiche.
2. **Separazione Consultazione vs Prenotazione**:
   - `PeekNextCustomMaterialId() const` (e l'alias retrocompatibile `AllocateNextCustomMaterialId()`): operazione di sola lettura sotto `shared_lock` che restituisce il prossimo ID senza avanzare lo stato. Invocazioni ripetute sono rigorosamente idempotenti.
   - `ReserveNextCustomMaterialId()`: operazione di mutazione atomica sotto `unique_lock` che riserva univocamente l'ID e incrementa monotonicamente l'high-water mark in memoria (`m_customIdHighWaterMark++`).
3. **Monotonicità e Mancato Riutilizzo (Anti-Recycling Contract)**:
   - `Clear()`: svuota il catalogo e incrementa la revisione, ma **NON resetta** `m_customIdHighWaterMark`. Gli identificatori già assegnati nella storia dell'istanza non vengono mai riutilizzati.
   - `LoadCustomMaterialsFromJson`: confronta il `customIdHighWaterMark` memorizzato nel JSON, l'HWM attuale in memoria e il massimo ID custom presente nel catalogo caricato (`std::max({m_customIdHighWaterMark, fileHwm, maxCustomIdInFile + 1})`). Anche se il catalogo caricato è ridotto (es. materiali cancellati prima del salvataggio), gli ID intermedi cancellati non vengono mai riciclati.
4. **Persistenza e Retrocompatibilità**:
   - Il formato JSON schema v3 serializza il campo `customIdHighWaterMark`.
   - Per cataloghi legacy privi di tale campo, l'HWM viene impostato deterministicamente su `std::max(1000, maxLoadedId + 1)`.
5. **Overflow Protection**:
   - Se `m_customIdHighWaterMark == UINT32_MAX`, sia `PeekNextCustomMaterialId()` che `ReserveNextCustomMaterialId()` restituiscono `INVALID_SCIENTIFIC_MATERIAL_ID` (0) senza causare wrap-around o corruzione della memoria.

---

## 4. File Modificati

| File | Percorso | Modifiche Principali |
|---|---|---|
| [`ScientificMaterialRegistry.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/blocks/ScientificMaterialRegistry.h) | `src/blocks/ScientificMaterialRegistry.h` | Inclusione `<shared_mutex>` e `<mutex>`. Dichiarazione `m_mutex`, `m_customIdHighWaterMark`. Dichiarazione `PeekNextCustomMaterialId()`, `ReserveNextCustomMaterialId()`, `GetCustomIdHighWaterMark()`, `SetCustomIdHighWaterMarkForTesting()`. Dichiarazione metodi privati `*Locked`. |
| [`ScientificMaterialRegistry.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/blocks/ScientificMaterialRegistry.cpp) | `src/blocks/ScientificMaterialRegistry.cpp` | Implementazione completa della strategia di lock `std::shared_mutex` (shared per lettori, unique per scrittori). Separazione di `RegisterMaterial`, `PublishMaterial`, `DeprecateMaterial`, `SetEnabledInBlockMaker`, `ValidateMaterialDefinition` e `Clear` in varianti esterne (locking) e interne (`*Locked`). Svincolamento totale delle operazioni di disk I/O dai mutex in `LoadCustomMaterialsFromJson` e `SaveCustomMaterialsToJson`. Gestione persistente dell'HWM in JSON schema v3. |
| [`CustomScientificMaterialContractTests.h`](file:///d:/FAIRWORLD/FAIRWORLD/src/blocks/CustomScientificMaterialContractTests.h) | `src/blocks/CustomScientificMaterialContractTests.h` | Dichiarazione di `Test20`, `Test21` e `Test22`. |
| [`CustomScientificMaterialContractTests.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/blocks/CustomScientificMaterialContractTests.cpp) | `src/blocks/CustomScientificMaterialContractTests.cpp` | Inclusione `<thread>`, `<atomic>`, `<set>`. Registrazione in `RunAllTests()`. Implementazione completa dei test CUST-20, CUST-21 e CUST-22 con sincronizzazione deterministica via barriere atomiche. |
| [`custom_scientific_tests_main.cpp`](file:///d:/FAIRWORLD/FAIRWORLD/src/blocks/custom_scientific_tests_main.cpp) | `src/blocks/custom_scientific_tests_main.cpp` | Aggiornamento banner e condizioni di successo per 22 test (`results.size() == 22`). |

---

## 5. Test Contrattuali Aggiunti

### `CUST-20`: Lettori Concorrenti Durante Aggiornamenti e Coerenza Snapshot
- **Descrizione**: 4 thread lettori eseguono ciclicamente centinaia di letture di `GetSnapshotHandle()`, validando:
  - Validità del puntatore dello snapshot.
  - Corrispondenza biunivoca tra `materials` e `canonicalToIdMap` dello snapshot.
  - Invariante di monotonia `snapshot->GetRevision() <= reg.GetRevision()`.
  - Assenza di puntatori nulli o `canonicalId` vuoti.
  - Mantenimento di snapshot storici validi attraverso `Clear()` e re-inizializzazioni concorrenti del writer.
- **Sincronizzazione**: `std::atomic<bool> startFlag` per partenza simultanea, `std::atomic<bool> stopFlag` impostato al termine del writer.

### `CUST-21`: Scrittori Concorrenti con Serializzazione e Zero Aggiornamenti Persi
- **Descrizione**: 6 thread scrittori registrano contemporaneamente 25 materiali custom ciascuno (150 materiali totali) con identificatori e canonical distinti.
- **Verifica**: partenza sincronizzata per massimizzare la contesa; al termine di tutti i thread:
  - Zero fallimenti di registrazione.
  - Conteggio catalogo esattamente pari a $4 \text{ standard} + 150 = 154$.
  - Mappatura biunivoca verificata per ogni singolo materiale.
  - Revisione aumentata di esattamente 150 unità. Zero aggiornamenti persi.

### `CUST-22`: Contratto Allocazione ID Custom, Persistenza High-Water Mark e Overflow
- **Descrizione**:
  1. **Prenotazione atomica concorrente**: 6 thread effettuano ciascuno 50 chiamate a `ReserveNextCustomMaterialId()` (300 totali). Verifica di 300 ID univoci senza collisioni nel range $[1000, 1300)$.
  2. **Invarianza Peek**: chiamate ripetute a `PeekNextCustomMaterialId()` restituiscono 1300 senza avanzare l'HWM.
  3. **Non-riciclo dopo `Clear()`**: dopo `Clear()`, l'HWM rimane 1300; la successiva prenotazione ottiene 1300 (l'ID 1000 non viene mai riutilizzato).
  4. **Persistenza catalogo ridotto**: salvataggio di un catalogo con HWM a 1304; ricaricamento in una nuova istanza; la nuova istanza mantiene HWM a 1304 e la prima prenotazione ottiene 1304 (gli ID 1301..1303 intermedi eliminati non vengono riutilizzati).
  5. **Retrocompatibilità legacy**: caricamento di file JSON privo di HWM con materiale ID 1050 inizializza l'HWM a 1051.
  6. **Overflow Protection**: impostazione HWM a `UINT32_MAX`; `PeekNextCustomMaterialId()` e `ReserveNextCustomMaterialId()` restituiscono `INVALID_SCIENTIFIC_MATERIAL_ID` (0) in sicurezza senza wrap-around.

---

## 6. Risultati Reali di Verifica

> [!IMPORTANT]
> **STATO DI VERIFICA IN QUESTA SESSIONE**: **NON VERIFICATO**
> 
> Durante la sessione, il tentativo di esecuzione del comando:
> `cmake --build build --config Debug --target FairworldCustomScientificTests`
> è stato respinto dall'ambiente utente (`user denied permission to run command`).
> 
> In conformità alle direttive contrattuali e al principio di trasparenza, non è stato fabbricato alcun output di compilazione o di CTest. Il codice sorgente C++23 è completo, rigorosamente strutturato e pronto per essere verificato ed eseguito.

### Istruzioni per la Verifica Manuale da Terminale PowerShell

Per compilare ed eseguire l'intera suite dei 22 test contrattuali (Debug e Release), eseguire i seguenti comandi:

```powershell
# 1. Compilazione ed esecuzione in Debug
cmake --build build --config Debug --target FairworldCustomScientificTests
.\bin\Debug\FairworldCustomScientificTests.exe

# 2. Compilazione ed esecuzione in Release
cmake --build build --config Release --target FairworldCustomScientificTests
.\bin\Release\FairworldCustomScientificTests.exe

# 3. Esecuzione suite CTest completa
ctest --test-dir build -C Debug --output-on-failure
ctest --test-dir build -C Release --output-on-failure
```

---

## 7. Rischi Residui

1. **Uso concorrente non protetto di `m_fallbackMaterial` per riferimento**:
   - `m_fallbackMaterial` è restituito per valore (`ScientificMaterialDef GetMaterial(...) const`), eliminando i rischi di data race per i consumatori.
2. **File I/O su Filesystem Esterni (Condivisioni di Rete / Dischi Lenti)**:
   - Poiché il file I/O è completamente disaccoppiato da `m_mutex`, operazioni lente su disco influenzeranno solo il thread chiamante di `SaveCustomMaterialsToJson` o `LoadCustomMaterialsFromJson`, lasciando tutti i thread di simulazione e rendering liberi di interrogare il registry tramite snapshot in-memory.
3. **Esaustione range ID (32-bit)**:
   - Lo spazio a 32 bit per materiali custom ammette fino a $\approx 4.29 \times 10^9$ definizioni. Il caso di overflow è gestito con restituzione deterministica di `INVALID_SCIENTIFIC_MATERIAL_ID` (0) e log di errore senza crash.
