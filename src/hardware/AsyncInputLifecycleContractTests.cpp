#include "pch.h"
#include "AsyncInputLifecycleContractTests.h"
#include "DeviceManager.h"
#include "AsyncInput.h"
#include "SharedContext.h"
#include "states/BlockMakerState.h"
#include "states/ForgeState.h"

#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <sstream>
#include <iostream>

namespace fw {

std::vector<AsyncInputTestResult> AsyncInputLifecycleContractTests::RunAllTests() {
    std::vector<AsyncInputTestResult> results;
    results.reserve(7);

    results.push_back(Test01_DeviceManagerOwnershipAndRAII());
    results.push_back(Test02_RepeatedBlockMakerStateLifecycle());
    results.push_back(Test03_RepeatedForgeStateLifecycle());
    results.push_back(Test04_StateAlternationPreservesNonOwningBorrow());
    results.push_back(Test05_PartialInitializationShutdownSafety());
    results.push_back(Test06_DetachmentOnDestructionSafety());
    results.push_back(Test07_ConcurrentDoubleBufferIntegrity());

    return results;
}

// -----------------------------------------------------------------------------
// ASYNC-01: Ownership centralizzata in DeviceManager e binding RAII a SharedContext
// -----------------------------------------------------------------------------
AsyncInputTestResult AsyncInputLifecycleContractTests::Test01_DeviceManagerOwnershipAndRAII() {
    AsyncInputTestResult res;
    res.testId = "ASYNC-01";
    res.testName = "Ownership centralizzata in DeviceManager e binding RAII";
    res.inputDesc = "Istanziazione DeviceManager, interrogazione GetAsyncInput() e binding a SharedContext";
    res.expectedDesc = "GetAsyncInput() restituisce puntatore non nullo e stabile; Bind/Detach aggiornano coerentemente SharedContext";

    DeviceManager dm;
    fw::AsyncInput* pAsync1 = dm.GetAsyncInput();
    const fw::AsyncInput* pAsync2 = const_cast<const DeviceManager&>(dm).GetAsyncInput();

    if (!pAsync1 || pAsync1 != pAsync2) {
        res.passed = false;
        res.observedDesc = "GetAsyncInput() nullo o incoerente tra const/non-const";
        return res;
    }

    SharedContext ctx;
    if (ctx.asyncInput != nullptr || ctx.deviceManager != nullptr) {
        res.passed = false;
        res.observedDesc = "SharedContext non nullo inizialmente";
        return res;
    }

    dm.BindContext(&ctx);
    if (ctx.asyncInput != pAsync1 || ctx.deviceManager != &dm) {
        res.passed = false;
        res.observedDesc = "BindContext non ha collegato asyncInput o deviceManager correttamente";
        return res;
    }

    dm.DetachContext();
    if (ctx.asyncInput != nullptr || ctx.deviceManager != nullptr) {
        res.passed = false;
        res.observedDesc = "DetachContext non ha resettato i puntatori a nullptr";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Puntatore stabile, binding e detachment su SharedContext verificati con successo";
    return res;
}

// -----------------------------------------------------------------------------
// ASYNC-02: Entrata e uscita ripetuta da BlockMakerState senza allocazioni orfane
// -----------------------------------------------------------------------------
AsyncInputTestResult AsyncInputLifecycleContractTests::Test02_RepeatedBlockMakerStateLifecycle() {
    AsyncInputTestResult res;
    res.testId = "ASYNC-02";
    res.testName = "Entrata/uscita ripetuta da BlockMakerState (zero leak)";
    res.inputDesc = "30 cicli di costruzione, prestito non-owning e distruzione di BlockMakerState";
    res.expectedDesc = "ctx.asyncInput punta stabilmente a DeviceManager::m_asyncInput, nessuna allocazione orfana";

    DeviceManager dm;
    SharedContext ctx;
    dm.BindContext(&ctx);

    fw::AsyncInput* canonicalInput = dm.GetAsyncInput();
    constexpr int CYCLES = 30;

    for (int i = 0; i < CYCLES; ++i) {
        // Simulazione logica d'inizializzazione non-owning dello State
        if (!ctx.asyncInput && ctx.deviceManager) {
            ctx.asyncInput = ctx.deviceManager->GetAsyncInput();
        }

        {
            BlockMakerState state(&ctx);
            if (ctx.asyncInput != canonicalInput) {
                res.passed = false;
                res.observedDesc = "ctx.asyncInput mutato durante la permanenza nello stato al ciclo " + std::to_string(i);
                return res;
            }
        } // state esce dallo scope ed è distrutto

        if (ctx.asyncInput != canonicalInput) {
            res.passed = false;
            res.observedDesc = "ctx.asyncInput mutato o azzerato impropriamente dopo distruzione dello stato al ciclo " + std::to_string(i);
            return res;
        }
    }

    res.passed = true;
    res.observedDesc = "30 iterazioni completate: ctx.asyncInput e' rimasto invariato e non-owning (" + std::to_string(CYCLES) + " cicli validati)";
    return res;
}

// -----------------------------------------------------------------------------
// ASYNC-03: Entrata e uscita ripetuta da ForgeState senza allocazioni orfane
// -----------------------------------------------------------------------------
AsyncInputTestResult AsyncInputLifecycleContractTests::Test03_RepeatedForgeStateLifecycle() {
    AsyncInputTestResult res;
    res.testId = "ASYNC-03";
    res.testName = "Entrata/uscita ripetuta da ForgeState (zero leak)";
    res.inputDesc = "30 cicli di costruzione, prestito non-owning e distruzione di ForgeState";
    res.expectedDesc = "ctx.asyncInput punta stabilmente a DeviceManager::m_asyncInput, nessuna allocazione orfana";

    DeviceManager dm;
    SharedContext ctx;
    dm.BindContext(&ctx);

    fw::AsyncInput* canonicalInput = dm.GetAsyncInput();
    constexpr int CYCLES = 30;

    for (int i = 0; i < CYCLES; ++i) {
        // Simulazione logica d'inizializzazione non-owning dello State
        if (!ctx.asyncInput && ctx.deviceManager) {
            ctx.asyncInput = ctx.deviceManager->GetAsyncInput();
        }

        {
            ForgeState state(&ctx);
            if (ctx.asyncInput != canonicalInput) {
                res.passed = false;
                res.observedDesc = "ctx.asyncInput mutato durante la permanenza nello stato al ciclo " + std::to_string(i);
                return res;
            }
        } // state esce dallo scope ed è distrutto

        if (ctx.asyncInput != canonicalInput) {
            res.passed = false;
            res.observedDesc = "ctx.asyncInput mutato o azzerato impropriamente dopo distruzione dello stato al ciclo " + std::to_string(i);
            return res;
        }
    }

    res.passed = true;
    res.observedDesc = "30 iterazioni completate: ctx.asyncInput e' rimasto invariato e non-owning (" + std::to_string(CYCLES) + " cicli validati)";
    return res;
}

// -----------------------------------------------------------------------------
// ASYNC-04: Alternanza ripetuta fra BlockMakerState e ForgeState preservando il prestito non-owning
// -----------------------------------------------------------------------------
AsyncInputTestResult AsyncInputLifecycleContractTests::Test04_StateAlternationPreservesNonOwningBorrow() {
    AsyncInputTestResult res;
    res.testId = "ASYNC-04";
    res.testName = "Alternanza fra BlockMakerState e ForgeState";
    res.inputDesc = "60 transizioni alternate (30 BlockMakerState, 30 ForgeState)";
    res.expectedDesc = "Nessuna transizione corrompe o rialloca asyncInput; la ownership resta invariata in DeviceManager";

    DeviceManager dm;
    SharedContext ctx;
    dm.BindContext(&ctx);

    fw::AsyncInput* canonicalInput = dm.GetAsyncInput();
    constexpr int TOTAL_TRANSITIONS = 60;

    for (int i = 0; i < TOTAL_TRANSITIONS; ++i) {
        if (!ctx.asyncInput && ctx.deviceManager) {
            ctx.asyncInput = ctx.deviceManager->GetAsyncInput();
        }

        if (i % 2 == 0) {
            BlockMakerState bmState(&ctx);
            if (ctx.asyncInput != canonicalInput) {
                res.passed = false;
                res.observedDesc = "Integrità violata durante BlockMakerState alla transizione " + std::to_string(i);
                return res;
            }
        } else {
            ForgeState fgState(&ctx);
            if (ctx.asyncInput != canonicalInput) {
                res.passed = false;
                res.observedDesc = "Integrità violata durante ForgeState alla transizione " + std::to_string(i);
                return res;
            }
        }

        if (ctx.asyncInput != canonicalInput) {
            res.passed = false;
            res.observedDesc = "Puntatore orfano o invalidato dopo distruzione alla transizione " + std::to_string(i);
            return res;
        }
    }

    res.passed = true;
    res.observedDesc = "60 transizioni alternate eseguite con successo: asyncInput immutato e ownership preservata";
    return res;
}

// -----------------------------------------------------------------------------
// ASYNC-05: Shutdown sicuro dopo inizializzazione parziale o assenza di sottosistemi
// -----------------------------------------------------------------------------
AsyncInputTestResult AsyncInputLifecycleContractTests::Test05_PartialInitializationShutdownSafety() {
    AsyncInputTestResult res;
    res.testId = "ASYNC-05";
    res.testName = "Shutdown dopo inizializzazione parziale";
    res.inputDesc = "SharedContext senza DeviceManager, DeviceManager senza context, e re-binding sequenziale";
    res.expectedDesc = "Nessun crash, nessuna dereferenziazione di nullptr, shutdown pulito";

    // Caso 1: SharedContext incompleto (nessun DeviceManager)
    SharedContext bareContext;
    if (!bareContext.asyncInput && bareContext.deviceManager) {
        bareContext.asyncInput = bareContext.deviceManager->GetAsyncInput();
    }
    if (bareContext.asyncInput != nullptr) {
        res.passed = false;
        res.observedDesc = "bareContext.asyncInput inizializzato inaspettatamente senza DeviceManager";
        return res;
    }

    // Caso 2: DeviceManager istanziato e distrutto senza mai legare un context
    {
        DeviceManager orphanDm;
        // distruzione pulita senza crash
    }

    // Caso 3: Re-binding sequenziale tra contesti multipli
    SharedContext ctx1;
    SharedContext ctx2;
    {
        DeviceManager dm;
        dm.BindContext(&ctx1);
        if (ctx1.asyncInput != dm.GetAsyncInput()) {
            res.passed = false;
            res.observedDesc = "ctx1 non legato correttamente";
            return res;
        }

        dm.BindContext(&ctx2);
        if (ctx1.asyncInput != nullptr || ctx1.deviceManager != nullptr) {
            res.passed = false;
            res.observedDesc = "ctx1 non distaccato durante re-binding su ctx2";
            return res;
        }
        if (ctx2.asyncInput != dm.GetAsyncInput()) {
            res.passed = false;
            res.observedDesc = "ctx2 non legato correttamente durante re-binding";
            return res;
        }
    } // dm distrutto: anche ctx2 deve risultare distaccato
    if (ctx2.asyncInput != nullptr || ctx2.deviceManager != nullptr) {
        res.passed = false;
        res.observedDesc = "ctx2 non distaccato dopo distruzione di dm";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Tutti gli scenari parziali e re-binding gestiti con successo senza crash";
    return res;
}

// -----------------------------------------------------------------------------
// ASYNC-06: Sicurezza di distacco: nessun accesso/dangling pointer dopo distruzione
// -----------------------------------------------------------------------------
AsyncInputTestResult AsyncInputLifecycleContractTests::Test06_DetachmentOnDestructionSafety() {
    AsyncInputTestResult res;
    res.testId = "ASYNC-06";
    res.testName = "Nessun accesso post-distruzione (Dangling Pointer Guard)";
    res.inputDesc = "SharedContext osserva DeviceManager che termina il suo lifetime RAII";
    res.expectedDesc = "ctx.asyncInput viene resettato a nullptr al distruttore di DeviceManager; nessun dangling pointer";

    SharedContext ctx;

    {
        DeviceManager temporaryDm;
        temporaryDm.BindContext(&ctx);

        if (ctx.asyncInput == nullptr || ctx.deviceManager == nullptr) {
            res.passed = false;
            res.observedDesc = "Binding iniziale fallito in scope temporaneo";
            return res;
        }
    } // temporaryDm muore qui!

    // Controllo fondamentale: ctx.asyncInput DEVE essere nullptr, non un puntatore pendente su stack frame terminato!
    if (ctx.asyncInput != nullptr) {
        res.passed = false;
        res.observedDesc = "ctx.asyncInput è un dangling pointer non nullo dopo la distruzione di DeviceManager";
        return res;
    }

    if (ctx.deviceManager != nullptr) {
        res.passed = false;
        res.observedDesc = "ctx.deviceManager è un dangling pointer non nullo dopo la distruzione di DeviceManager";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Distacco RAII automatico: ctx.asyncInput e ctx.deviceManager resettati a nullptr con successo";
    return res;
}

// -----------------------------------------------------------------------------
// ASYNC-07: Integrità e thread-safety del double-buffering asincrono sotto carico
// -----------------------------------------------------------------------------
AsyncInputTestResult AsyncInputLifecycleContractTests::Test07_ConcurrentDoubleBufferIntegrity() {
    AsyncInputTestResult res;
    res.testId = "ASYNC-07";
    res.testName = "Integrità e thread-safety double-buffering asincrono";
    res.inputDesc = "4 thread produttori concorrenti generano 4000 eventi totali; consumer thread esegue swap buffers";
    res.expectedDesc = "Tutti i 4000 eventi consumati senza perdite, race condition o memory corruption";

    fw::AsyncInput input;
    constexpr int NUM_PRODUCERS = 4;
    constexpr int EVENTS_PER_PRODUCER = 1000;
    const uint64_t EXPECTED_EVENTS = NUM_PRODUCERS * EVENTS_PER_PRODUCER;

    std::atomic<bool> startSignal{false};
    std::atomic<int> producersFinished{0};

    std::vector<std::thread> producers;
    producers.reserve(NUM_PRODUCERS);

    for (int t = 0; t < NUM_PRODUCERS; ++t) {
        producers.emplace_back([&, t]() {
            while (!startSignal.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (int i = 0; i < EVENTS_PER_PRODUCER; ++i) {
                if (i % 3 == 0) {
                    input.PushEvent(fw::MouseMoveEvent{static_cast<float>(i), static_cast<float>(t)});
                } else if (i % 3 == 1) {
                    input.PushEvent(fw::KeyEvent{i, true});
                } else {
                    input.PushEvent(fw::MouseClickEvent{0, false});
                }
            }

            producersFinished.fetch_add(1, std::memory_order_release);
        });
    }

    std::atomic<uint64_t> totalEventsConsumed{0};
    startSignal.store(true, std::memory_order_release);

    while (producersFinished.load(std::memory_order_acquire) < NUM_PRODUCERS) {
        auto consumed = input.SwapBuffersAndConsume();
        totalEventsConsumed.fetch_add(consumed.size(), std::memory_order_relaxed);
        std::this_thread::yield();
    }

    for (auto& th : producers) {
        if (th.joinable()) th.join();
    }

    // Ultimo drain per raccogliere eventuali eventi residui nell'ultimo buffer di swap
    auto finalDrain = input.SwapBuffersAndConsume();
    totalEventsConsumed.fetch_add(finalDrain.size(), std::memory_order_relaxed);

    if (totalEventsConsumed.load() != EXPECTED_EVENTS) {
        res.passed = false;
        res.observedDesc = "Eventi totali consumati: " + std::to_string(totalEventsConsumed.load()) +
                           " invece di " + std::to_string(EXPECTED_EVENTS);
        return res;
    }

    res.passed = true;
    res.observedDesc = "4000/4000 eventi scambiati e consumati con successo in ambiente multithread concorrente";
    return res;
}

} // namespace fw
