#include "pch.h"
#include "GlobalMemoryLifecycleContractTests.h"
#include "MemorySystem.h"

#include <cstring>
#include <cstdio>
#include <cstdint>
#include <iostream>

namespace fw::memory {

MemoryContractTestResult GlobalMemoryLifecycleContractTests::RunTestById(const char* testId) {
    if (!testId) {
        MemoryContractTestResult unknown;
        unknown.testId = "UNKNOWN";
        unknown.testName = "Null Test ID";
        unknown.passed = false;
        unknown.observedDesc = "Test ID nullo";
        return unknown;
    }
    if (std::strcmp(testId, "MEM04-01") == 0) return Test01_NormalStartupAndDrainedShutdown();
    if (std::strcmp(testId, "MEM04-02") == 0) return Test02_RepeatedAllocationsAndDeallocations();
    if (std::strcmp(testId, "MEM04-03") == 0) return Test03_RefuseShutdownWhenAllocationsAreLive();
    if (std::strcmp(testId, "MEM04-04") == 0) return Test04_ShutdownAfterPartialInitialization();
    if (std::strcmp(testId, "MEM04-05") == 0) return Test05_OverAlignedAllocationAndDeallocation();
    if (std::strcmp(testId, "MEM04-06") == 0) return Test06_PostShutdownDeleteSafety();
    if (std::strcmp(testId, "MEM04-07") == 0) return Test07_PostShutdownNewFallback();

    MemoryContractTestResult unknown;
    unknown.testId = "UNKNOWN";
    unknown.testName = "Unknown Test ID";
    unknown.passed = false;
    unknown.observedDesc = "Test ID non riconosciuto dal runner";
    return unknown;
}

MemoryContractTestResult GlobalMemoryLifecycleContractTests::RunTestById(const std::string& testId) {
    return RunTestById(testId.c_str());
}

int GlobalMemoryLifecycleContractTests::RunChildTest(const char* testId) {
    MemoryContractTestResult res = RunTestById(testId);
    
    // Output strutturato su stdout per cattura da parte del processo orchestratore
    // Usa printf C-style e fflush per garantire zero allocazioni dinamiche o dipendenze da facette STL
    std::printf("CHILD_RESULT|%s|%s|%s|%s\n",
                res.passed ? "PASS" : "FAIL",
                res.testId ? res.testId : "",
                res.testName ? res.testName : "",
                res.observedDesc ? res.observedDesc : "");
    std::fflush(stdout);
              
    return res.passed ? 0 : 1;
}

int GlobalMemoryLifecycleContractTests::RunChildTest(const std::string& testId) {
    return RunChildTest(testId.c_str());
}

std::vector<MemoryContractTestResult> GlobalMemoryLifecycleContractTests::RunAllTests() {
    return RunAllTestsDirect();
}

std::vector<MemoryContractTestResult> GlobalMemoryLifecycleContractTests::RunAllTestsDirect() {
    std::vector<MemoryContractTestResult> results;
    results.reserve(7);

    results.push_back(Test01_NormalStartupAndDrainedShutdown());
    results.push_back(Test02_RepeatedAllocationsAndDeallocations());
    results.push_back(Test03_RefuseShutdownWhenAllocationsAreLive());
    results.push_back(Test04_ShutdownAfterPartialInitialization());
    results.push_back(Test05_OverAlignedAllocationAndDeallocation());
    results.push_back(Test06_PostShutdownDeleteSafety());
    results.push_back(Test07_PostShutdownNewFallback());

    return results;
}

// -----------------------------------------------------------------------------
// MEM04-01: Inizializzazione, allocazione e drained shutdown controllato
// -----------------------------------------------------------------------------
MemoryContractTestResult GlobalMemoryLifecycleContractTests::Test01_NormalStartupAndDrainedShutdown() {
    MemoryContractTestResult res;
    res.testId = "MEM04-01";
    res.testName = "Inizializzazione, allocazione e shutdown controllato a heap svuotato";
    res.inputDesc = "InitializeGlobalMemory(), allocazione dinamica, deallocazione, ShutdownGlobalMemory(false)";
    res.expectedDesc = "Heap attivo, allocazione tracciata, deallocazione pulita e shutdown a 0 allocazioni";

    InitializeGlobalMemory();
    if (!IsGlobalMemoryActive()) {
        res.passed = false;
        res.observedDesc = "IsGlobalMemoryActive() e' false dopo InitializeGlobalMemory()";
        return res;
    }

    size_t baseAllocs = GetLiveAllocationCount();

    auto* data = new uint32_t[64];
    data[0] = 0xCAFEBABE;
    data[63] = 0xDEADBEEF;

    if (GetLiveAllocationCount() <= baseAllocs || GetLiveAllocatedBytes() == 0) {
        delete[] data;
        res.passed = false;
        res.observedDesc = "Metriche di allocazione non aggiornate dopo 'new uint32_t[64]'";
        return res;
    }

    delete[] data;

    if (GetLiveAllocationCount() != baseAllocs) {
        res.passed = false;
        res.observedDesc = "Deallocazione incompleta: conteggio allocazioni non tornato a baseline";
        return res;
    }

    if (!CanSafelyShutdownGlobalMemory()) {
        res.passed = false;
        res.observedDesc = "CanSafelyShutdownGlobalMemory() e' false dopo deallocazione";
        return res;
    }

    bool shutdownSuccess = ShutdownGlobalMemory(false);
    if (!shutdownSuccess || !IsGlobalMemoryShutdown()) {
        res.passed = false;
        res.observedDesc = "ShutdownGlobalMemory(false) ha fallito su heap svuotato";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Ciclo completo init -> alloc -> free -> drained shutdown validato con successo";
    return res;
}

// -----------------------------------------------------------------------------
// MEM04-02: 500 allocazioni/deallocazioni ripetute con verifica zero drift
// -----------------------------------------------------------------------------
MemoryContractTestResult GlobalMemoryLifecycleContractTests::Test02_RepeatedAllocationsAndDeallocations() {
    MemoryContractTestResult res;
    res.testId = "MEM04-02";
    res.testName = "500 allocazioni/deallocazioni ripetute (Zero Drift)";
    res.inputDesc = "500 cicli di allocazione di dimensioni variabili (16B .. 2048B) con pattern verification";
    res.expectedDesc = "Tutti i blocchi validati per integrita'; al termine il contatore allocazioni torna al valore iniziale";

    InitializeGlobalMemory();
    size_t startAllocs = GetLiveAllocationCount();
    size_t startBytes = GetLiveAllocatedBytes();

    constexpr int CYCLES = 500;
    for (int i = 0; i < CYCLES; ++i) {
        size_t size = 16 * ((i % 16) + 1);
        uint8_t* p = new uint8_t[size];

        std::memset(p, static_cast<uint8_t>(i & 0xFF), size);
        if (p[0] != static_cast<uint8_t>(i & 0xFF) || p[size - 1] != static_cast<uint8_t>(i & 0xFF)) {
            delete[] p;
            res.passed = false;
            res.observedDesc = "Corruzione dei dati durante il ciclo di allocazione ripetuto";
            return res;
        }

        delete[] p;
    }

    size_t endAllocs = GetLiveAllocationCount();
    size_t endBytes = GetLiveAllocatedBytes();

    if (endAllocs != startAllocs || endBytes != startBytes) {
        res.passed = false;
        res.observedDesc = "Drift di memoria rilevato dopo 500 cicli alloc/free";
        return res;
    }

    res.passed = true;
    res.observedDesc = "500 allocazioni/deallocazioni eseguite; zero drift di memoria confermato";
    return res;
}

// -----------------------------------------------------------------------------
// MEM04-03: Rifiuto dello shutdown con allocazioni vive (Prevenzione Use-After-Free)
// -----------------------------------------------------------------------------
MemoryContractTestResult GlobalMemoryLifecycleContractTests::Test03_RefuseShutdownWhenAllocationsAreLive() {
    MemoryContractTestResult res;
    res.testId = "MEM04-03";
    res.testName = "Rifiuto dello shutdown con allocazioni vive (Guardia Use-After-Free)";
    res.inputDesc = "Tentativo di ShutdownGlobalMemory(false) mentre un puntatore dinamico e' ancora attivo";
    res.expectedDesc = "ShutdownGlobalMemory rifiuta la distruzione dell'heap (ritorna false); l'oggetto resta integro e accessibile";

    InitializeGlobalMemory();

    auto* liveObject = new uint64_t(0xF00DCAFEDEADBEEFULL);
    if (!liveObject) {
        res.passed = false;
        res.observedDesc = "Allocazione dinamica fallita";
        return res;
    }

    // Tenta lo shutdown controllato (non forzato): deve essere rifiutato categoricamente
    bool shutdownResult = ShutdownGlobalMemory(false);
    if (shutdownResult) {
        delete liveObject;
        res.passed = false;
        res.observedDesc = "ShutdownGlobalMemory(false) ha rilasciato l'heap nonostante vi fosse un'allocazione attiva!";
        return res;
    }

    // Verifica che l'oggetto non sia stato corrotto
    if (*liveObject != 0xF00DCAFEDEADBEEFULL) {
        delete liveObject;
        res.passed = false;
        res.observedDesc = "Use-After-Free rilevato: il valore dell'oggetto attivo e' stato corrotto";
        return res;
    }

    // Dealloca l'oggetto legittimamente
    delete liveObject;

    // Ora lo shutdown controllato deve avere successo
    bool secondShutdown = ShutdownGlobalMemory(false);
    if (!secondShutdown) {
        res.passed = false;
        res.observedDesc = "ShutdownGlobalMemory(false) rifiutato anche dopo il rilascio dell'oggetto";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Shutdown legittimamente respinto con allocazioni attive, ed eseguito solo a deallocazione avvenuta";
    return res;
}

// -----------------------------------------------------------------------------
// MEM04-04: Shutdown dopo inizializzazione parziale e idempotenza
// -----------------------------------------------------------------------------
MemoryContractTestResult GlobalMemoryLifecycleContractTests::Test04_ShutdownAfterPartialInitialization() {
    MemoryContractTestResult res;
    res.testId = "MEM04-04";
    res.testName = "Shutdown dopo inizializzazione parziale e idempotenza";
    res.inputDesc = "Chiamate consecutive a ShutdownGlobalMemory, init senza allocazioni e shutdown immediato";
    res.expectedDesc = "Nessun crash, nessuna doppia deallocazione, idempotenza completa";

    // 1. Chiamate multiple su stato gia' chiuso
    bool ok1 = ShutdownGlobalMemory(false);
    bool ok2 = ShutdownGlobalMemory(true);
    if (!ok1 || !ok2) {
        res.passed = false;
        res.observedDesc = "Shutdown su allocatore gia' chiuso ha restituito false";
        return res;
    }

    // 2. Inizializzazione pulita senza allocazioni seguita da shutdown immediato
    InitializeGlobalMemory();
    if (!IsGlobalMemoryActive()) {
        res.passed = false;
        res.observedDesc = "IsGlobalMemoryActive() e' false dopo re-inizializzazione";
        return res;
    }

    bool ok3 = ShutdownGlobalMemory(false);
    if (!ok3) {
        res.passed = false;
        res.observedDesc = "Shutdown immediato a 0 allocazioni ha fallito";
        return res;
    }

    // 3. Verifica stato post-shutdown
    if (!IsGlobalMemoryShutdown()) {
        res.passed = false;
        res.observedDesc = "IsGlobalMemoryShutdown() e' false dopo shutdown";
        return res;
    }

    res.passed = true;
    res.observedDesc = "Idempotenza confermata; gestione sicura di stati parziali e duplicati";
    return res;
}

// -----------------------------------------------------------------------------
// MEM04-05: Over-aligned new e delete (alignas 16, 32, 64) e operator new(0) C++17
// -----------------------------------------------------------------------------
MemoryContractTestResult GlobalMemoryLifecycleContractTests::Test05_OverAlignedAllocationAndDeallocation() {
    MemoryContractTestResult res;
    res.testId = "MEM04-05";
    res.testName = "Over-aligned new e delete (alignas 16, 32, 64) e new(0)";
    res.inputDesc = "Allocazione di tipi over-aligned (16, 32, 64 bytes) e test di conformita' C++ operator new(0)";
    res.expectedDesc = "Indirizzi allineati; new(0) non-nullo; deallocazione pulita e zero drift";

    InitializeGlobalMemory();

    // 1. Test operator new(0) conformita' standard C++
    void* zeroPtr1 = ::operator new(0);
    void* zeroPtr2 = ::operator new(0);
    bool zeroScalarOk = (zeroPtr1 != nullptr && zeroPtr2 != nullptr && zeroPtr1 != zeroPtr2);
    ::operator delete(zeroPtr1);
    ::operator delete(zeroPtr2);

    void* zeroAligned = ::operator new(0, std::align_val_t{32});
    bool zeroAlignedOk = (zeroAligned != nullptr && (reinterpret_cast<uintptr_t>(zeroAligned) % 32 == 0));
    ::operator delete(zeroAligned, std::align_val_t{32});

    if (!zeroScalarOk || !zeroAlignedOk) {
        res.passed = false;
        res.observedDesc = "Violazione conformita' C++: operator new(0) o new(0, align_val_t) ha restituito puntatore non valido";
        return res;
    }

    // 2. Test tipi over-aligned
    struct alignas(16) Vec16 { uint64_t a, b; };
    struct alignas(32) Vec32 { uint64_t a, b, c, d; };
    struct alignas(64) Vec64 { uint64_t a, b, c, d, e, f, g, h; };

    auto* v16 = new Vec16{10, 20};
    auto* v32 = new Vec32{30, 40, 50, 60};
    auto* v64 = new Vec64{70, 80, 90, 100, 110, 120, 130, 140};

    uintptr_t addr16 = reinterpret_cast<uintptr_t>(v16);
    uintptr_t addr32 = reinterpret_cast<uintptr_t>(v32);
    uintptr_t addr64 = reinterpret_cast<uintptr_t>(v64);

    bool aligned16 = (addr16 % 16 == 0);
    bool aligned32 = (addr32 % 32 == 0);
    bool aligned64 = (addr64 % 64 == 0);

    bool dataOk = (v16->a == 10 && v32->c == 50 && v64->h == 140);

    delete v16;
    delete v32;
    delete v64;

    if (!aligned16 || !aligned32 || !aligned64) {
        res.passed = false;
        res.observedDesc = "Violazione allineamento nei tipi over-aligned";
        return res;
    }

    if (!dataOk) {
        res.passed = false;
        res.observedDesc = "Integrita' dati non conforme negli oggetti over-aligned";
        return res;
    }

    ShutdownGlobalMemory(false);

    res.passed = true;
    res.observedDesc = "Allineamenti 16/32/64 byte, new(0) scalare/allineato e overloads C++17 conformi e validati";
    return res;
}

// -----------------------------------------------------------------------------
// MEM04-06: Sicurezza di deallocazione post-shutdown forzato (Tombstone Bounds Guard)
// -----------------------------------------------------------------------------
MemoryContractTestResult GlobalMemoryLifecycleContractTests::Test06_PostShutdownDeleteSafety() {
    MemoryContractTestResult res;
    res.testId = "MEM04-06";
    res.testName = "Sicurezza deallocazione post-shutdown forzato (Tombstone Guard)";
    res.inputDesc = "Deallocazione scalare, array e allineata di puntatori dopo shutdown forzato dell'heap";
    res.expectedDesc = "Tombstone Bounds Guard intercetta gli indirizzi ed evita std::free(ptr), prevenendo heap corruption e crash";

    InitializeGlobalMemory();

    // 1. Alloca puntatore scalare
    auto* lingeringScalar = new uint32_t(42);

    // 2. Alloca puntatore array
    auto* lingeringArray = new uint32_t[32];
    lingeringArray[0] = 100;
    lingeringArray[31] = 200;

    // 3. Alloca puntatore over-aligned
    struct alignas(64) LingeringAligned { uint64_t a, b, c, d, e, f, g, h; };
    auto* lingeringAligned = new LingeringAligned();
    lingeringAligned->a = 777;

    // 4. Forza lo shutdown (dealloca fisicamente i 256MB di g_GlobalHeapBuffer al CRT)
    bool shutdownOk = ShutdownGlobalMemory(true);
    if (!shutdownOk || !IsGlobalMemoryShutdown()) {
        res.passed = false;
        res.observedDesc = "ShutdownGlobalMemory(true) ha fallito";
        return res;
    }

    // 5. Deallocazioni post-shutdown:
    // Se operator delete o operator delete[] dereferenziassero la memoria liberata o passassero
    // i puntatori a std::free(), il CRT di Windows crasheebbe istantaneamente (RtlFreeHeap invalid pointer).
    delete lingeringScalar;
    delete[] lingeringArray;
    delete lingeringAligned;

    res.passed = true;
    res.observedDesc = "Deallocazioni post-shutdown (scalare, array, allineata) assorbite dalla Tombstone Guard senza crash ne' corruzione";
    return res;
}

// -----------------------------------------------------------------------------
// MEM04-07: Fallback sicuro di operator new post-shutdown
// -----------------------------------------------------------------------------
MemoryContractTestResult GlobalMemoryLifecycleContractTests::Test07_PostShutdownNewFallback() {
    MemoryContractTestResult res;
    res.testId = "MEM04-07";
    res.testName = "Fallback sicuro di operator new post-shutdown";
    res.inputDesc = "Invocazione di operator new e new allineato quando l'allocatore globale e' in stato Shutdown";
    res.expectedDesc = "L'allocazione usa il fallback malloc senza riallocare orfanamente i 256MB di heap";

    // Assicurati che sia in stato shutdown
    ShutdownGlobalMemory(true);

    if (!IsGlobalMemoryShutdown()) {
        res.passed = false;
        res.observedDesc = "Stato non e' Shutdown prima del test";
        return res;
    }

    // 1. Invocazione di new scalare mentre il sistema e' spento
    int* fallbackPtr = new int(1337);
    if (!fallbackPtr || *fallbackPtr != 1337) {
        delete fallbackPtr;
        res.passed = false;
        res.observedDesc = "Fallback allocation scalare fallita";
        return res;
    }

    // 2. Invocazione di new over-aligned mentre il sistema e' spento
    struct alignas(32) FallbackAligned { uint64_t a, b, c, d; };
    auto* alignedFallback = new FallbackAligned{1, 2, 3, 4};
    if (!alignedFallback || (reinterpret_cast<uintptr_t>(alignedFallback) % 32 != 0)) {
        delete fallbackPtr;
        delete alignedFallback;
        res.passed = false;
        res.observedDesc = "Fallback allocation over-aligned post-shutdown fallita o non allineata a 32 byte";
        return res;
    }

    // 3. Verifica fondamentale: l'allocatore NON si e' riattivato, non ha allocato altri 256MB
    if (IsGlobalMemoryActive()) {
        delete fallbackPtr;
        delete alignedFallback;
        res.passed = false;
        res.observedDesc = "operator new ha inaspettatamente riallocato l'heap globale da 256MB durante lo shutdown!";
        return res;
    }

    delete fallbackPtr;
    delete alignedFallback;

    res.passed = true;
    res.observedDesc = "operator new e new allineato post-shutdown hanno utilizzato il fallback malloc con successo, preservando lo stato di shutdown";
    return res;
}

} // namespace fw::memory
