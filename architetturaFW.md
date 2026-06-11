# FAIRWORLD - Task Briefing: Boot Sequence & State Transition V2.0

Architettura della macchina a stati aggiornata per la Fase 1: Zero astrazioni inutili, Strict Memory Safety (no copy), Gestione errori modern C++20 (`std::expected`) e Timing Reale (`std::chrono`).

## 1. Architettura di Riferimento

- **Strict Memory Safety**: La classe base `State` impedisce esplicitamente il copy-constructor e il copy-assignment per non sdoppiare stati in memoria.
- **Gestione Errori**: I fallimenti vengono propagati pulitamente usando `std::expected` invece di lanciare eccezioni `throw`.
- **Memory Isolation (Isolamento Memoria)**: `StateManager` chiama `.reset()` sullo stato corrente per deallocare tutte le risorse prima che il nuovo stato venga costruito e inizializzato.
- **Real Timing**: Il Main Loop passa il reale `deltaTime` misurato ad alta precisione.
- **Zero Allocazioni nel Loop**: Le allocazioni avvengono *esclusivamente* in modo differito durante `ProcessTransitions()`. Il ciclo di update non effettua lock ne chiama l'heap scheduler di sistema per gli stati.

---

## 2. Codici da implementare

### `SharedContext.h`
Contiene le dipendenze globali di base e funge da veicolo dati (il "bus") tra uno stato e l'altro.

```cpp
#pragma once
#include <string>

// Forward declaration generica per l'handle della finestra (es. HWND)
struct HWND__;
using WindowHandle = HWND__*;

class StateManager;

struct SharedContext {
    WindowHandle window = nullptr;
    StateManager* stateManager = nullptr;

    // Payload scritto dall'HubState e letto dal PlayState
    std::string targetGameJsonPath;
};
```

### `State.h`
L'interfaccia base. Nessuna copia consentita, inizializzazione type-safe e no exceptions.

```cpp
#pragma once
#include <expected>
#include <string>

class State {
public:
    State() = default;
    virtual ~State() = default;

    // Strict Memory Safety: Niente copie accidentali
    State(const State&) = delete;
    State& operator=(const State&) = delete;

    // Init fallibile che non solleva eccezioni, ma restituisce la motivazione in caso di errore
    virtual std::expected<void, std::string> Init() = 0;
    virtual void Update(float dt) = 0;
    virtual void Render() = 0;
};
```

### `StateManager.h`
Un gestore che dealloca rigorosamente il vecchio stato *prima* di muovere e chiamare `Init()` sul nuovo.

```cpp
#pragma once
#include <memory>
#include <iostream>
#include "State.h"

class StateManager {
public:
    void ChangeState(std::unique_ptr<State> newState) {
        // Mette in coda il cambio di stato: NON alloca nulla ora (L'allocazione avviene dal chiamante o viene deferita)
        m_pendingState = std::move(newState);
    }

    void ProcessTransitions() {
        if (m_pendingState) {
            // Memory Isolation: Distrugge esplicitamente lo stato corrente PRIMA di muovere il nuovo
            m_currentState.reset();
            
            m_currentState = std::move(m_pendingState);
            
            auto result = m_currentState->Init();
            if (!result.has_value()) {
                std::cerr << "[StateManager ERROR] Inizializzazione stato fallita: " << result.error() << "\n";
                // Gestione fallimento: Reset forzato, chiudendo il motore.
                m_currentState.reset();
            }
        }
    }

    void Update(float dt) {
        if (m_currentState) m_currentState->Update(dt);
    }

    void Render() {
        if (m_currentState) m_currentState->Render();
    }

    bool IsRunning() const { 
        return m_currentState != nullptr; 
    }

private:
    std::unique_ptr<State> m_currentState = nullptr;
    std::unique_ptr<State> m_pendingState = nullptr;
};
```

### `HubState.h` e `HubState.cpp`
Il menu iniziale mockato con il nuovo sistema.

```cpp
// HubState.h
#pragma once
#include "State.h"

struct SharedContext;

class HubState : public State {
public:
    explicit HubState(SharedContext* context);
    ~HubState() override;

    std::expected<void, std::string> Init() override;
    void Update(float dt) override;
    void Render() override;

private:
    SharedContext* m_context;
    float m_simulatedTimeAccumulator;
};
```

```cpp
// HubState.cpp
#include "HubState.h"
#include "SharedContext.h"
#include "StateManager.h"
#include "PlayState.h"
#include <iostream>

HubState::HubState(SharedContext* context) : m_context(context), m_simulatedTimeAccumulator(0.0f) {
    std::cout << "[HubState] Costruito.\n";
}

HubState::~HubState() {
    std::cout << "[HubState] Distrutto. Isolamento memoria garantito.\n";
}

std::expected<void, std::string> HubState::Init() {
    std::cout << "[HubState] Inizializzazione completata. In attesa di input.\n";
    return {}; // Ritorna void atteso
}

void HubState::Update(float dt) {
    m_simulatedTimeAccumulator += dt;
    
    // Simula la pressione di "Gioca" dopo circa 1 secondo reale
    if (m_simulatedTimeAccumulator >= 1.0f) {
        std::cout << "[HubState] Input 'Gioca' simulato. Innesco transizione...\n";
        
        // 1. Popola il contesto globale
        m_context->targetGameJsonPath = "projects/game_config.json";
        
        // 2. Innesca la transizione
        m_context->stateManager->ChangeState(std::make_unique<PlayState>(m_context));
        
        // Disattiva il trigger per evitare inneschi multipli nello stesso o in successivi frame prima della morte
        m_simulatedTimeAccumulator = -9999.0f; 
    }
}

void HubState::Render() {
    // Rendering del menu...
}
```

### `PlayState.h` e `PlayState.cpp`
Stato di gioco Data-Driven.

```cpp
// PlayState.h
#pragma once
#include "State.h"

struct SharedContext;

class PlayState : public State {
public:
    explicit PlayState(SharedContext* context);
    ~PlayState() override;

    std::expected<void, std::string> Init() override;
    void Update(float dt) override;
    void Render() override;

private:
    SharedContext* m_context;
};
```

```cpp
// PlayState.cpp
#include "PlayState.h"
#include "SharedContext.h"
#include <iostream>

PlayState::PlayState(SharedContext* context) : m_context(context) {
    std::cout << "[PlayState] Costruito.\n";
}

PlayState::~PlayState() {
    std::cout << "[PlayState] Distrutto.\n";
}

std::expected<void, std::string> PlayState::Init() {
    if (m_context->targetGameJsonPath.empty()) {
        return std::unexpected("Percorso JSON non specificato dal contesto globale!");
    }

    std::cout << "[PlayState] Inizializzato con successo.\n";
    std::cout << "[PlayState] Avvio parsing configurazione Data-Driven da: " << m_context->targetGameJsonPath << "\n";
    std::cout << "[PlayState] Creazione entities EnTT dal JSON in corso...\n";
    
    // Logica reale
    return {};
}

void PlayState::Update(float dt) {
    // Game Loop
}

void PlayState::Render() {
    // Vulkan Rendering
}
```

### `main.cpp` (Entry Point Aggiornato)
Timing reale e ciclo messaggi.

```cpp
// main.cpp
#include "pch.h"
#include <iostream>
#include <windows.h>
#include <chrono>

#include "SharedContext.h"
#include "StateManager.h"
#include "HubState.h"

// Attesa eventi Win32 base
bool ProcessWindowEvents() {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return true;
}

int main() {
    std::cout << "==========================================\n";
    std::cout << "    FAIRWORLD ENGINE - BOOT SEQUENCE V2   \n";
    std::cout << "==========================================\n\n";

    // 1. Istanzia Context e StateManager
    SharedContext context;
    StateManager stateManager;
    context.stateManager = &stateManager;

    // TODO: Inizializzazione base del sottosistema finestre
    // context.window = ...

    // 2. Bootstrap
    stateManager.ChangeState(std::make_unique<HubState>(&context));

    std::cout << "\n[SYSTEM] Entro nel main loop...\n";

    // Setup Timer
    auto lastTime = std::chrono::high_resolution_clock::now();

    // 3. Main Loop
    while (ProcessWindowEvents()) {
        
        // Calcolo Real Timing Delta (dt)
        auto currentTime = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        // A. Transizioni (fuori dal ciclo logico/memoria isolata)
        stateManager.ProcessTransitions();

        // Se l'unico stato rimasto si è chiuso o è fallito, esci
        if (!stateManager.IsRunning()) {
            break; 
        }

        // B. Update Data-Driven
        stateManager.Update(dt);
        
        // C. Render Hardware
        stateManager.Render();
        
        // Sleep base per il mock (da rimuovere in produzione con VSnyc)
        Sleep(1); 
    }

    std::cout << "[SYSTEM] Chiusura del motore completata.\n";
    return 0;
}
```

1. Il Valore Analogico (Axis Mapping)
Attualmente la nostra funzione IsActionActive restituisce un bool (Vero o Falso). Va benissimo per saltare o sparare. Ma cosa succede se vuoi mappare l'azione "ACCELERA_AUTO" sul grilletto R2 del gamepad? Vuoi sapere quanto l'utente sta premendo il grilletto, non solo se lo sta sfiorando.

Il "Massimo": Il sistema restituirebbe un float (da 0.0 a 1.0) o un Vector2D (per il movimento fluido del joystick), normalizzando in automatico input digitali (tastiera) e analogici (gamepad).

2. Lo Stack dei Contesti (Input Contexts)
Nel nostro sistema attuale, l'azione "JUMP" è sempre in ascolto. Ma cosa succede se il tuo personaggio entra in un menu, o sale in macchina, o inizia a nuotare? Il tasto "Spazio" non dovrebbe più farlo saltare.

Il "Massimo": Si implementa uno "Stack" di mappe (es. Context_Gameplay, Context_UI, Context_Vehicle). Il motore attiva e disattiva interi pacchetti di comandi a seconda della situazione, evitando che tu debba riempire il codice del giocatore con centinaia di controlli tipo if (isPlaying && !inMenu && !inCar).

3. La Serializzazione Permanente (Salvataggio)
Se apri il menu ImGui e rimappi il salto dal tasto Spazio al tasto Invio, funziona tutto a meraviglia. Ma quando chiudi FAIRWORLD e lo riapri, le modifiche svaniscono.

Il "Massimo": Quando l'utente chiude l'HubState, l'OS prende l'ActionMap, lo converte e lo scrive sul disco in un file user_bindings.json (esattamente come fanno i giochi PC nella cartella AppData). Al prossimo avvio, il SharedContext ricarica le preferenze dell'utente.

## Checklist di Implementazione (Fasi)
- [x] **Fase 1.1**: Aggiornamento progetto a C++23 e creazione dei file strutturali base (SharedContext, State, StateManager, HubState, PlayState) integrati in Visual Studio.
- [x] **Fase 1.2**: Sostituzione temporanea del loop in `main.cpp` e validazione della Boot Sequence mockata (per verificare le stampe a schermo).
- [x] **Fase 1.3**: Ricollegamento del vero `FairWorldEngine` all'interno dell'ecosistema `PlayState`.
- [x] **Fase 2.1**: Setup del Registro EnTT nel `PlayState`, parsing no-throw JSON e creazione Entità base.
- [x] **Fase 2.2**: Migrazione della Telecamera (Camera Component).
- [x] **Fase 2.3**: Spostamento dell'Input System in `PlayState`.
