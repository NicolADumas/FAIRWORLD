#pragma once

#include <string>
#include <vector>

namespace fw {

// =============================================================================
// MEM-01: SUITE TEST REGRESSIONE CONTRATTO SUBSCRIPTION LIFECYCLE (EVT-01 .. EVT-07)
// Verifica formale isolata del ciclo di vita delle sottoscrizioni EventManager/GameWorld.
// =============================================================================

struct EventSubscriptionTestResult {
    std::string testId;
    std::string testName;
    std::string inputDesc;
    std::string expectedDesc;
    std::string observedDesc;
    bool passed = false;
    std::string details;
};

class EventSubscriptionContractTests {
public:
    static std::vector<EventSubscriptionTestResult> RunAllTests();

    // 1. Un GameWorld riceve correttamente gli eventi
    static EventSubscriptionTestResult Test01_SingleWorldReceivesEvents();

    // 2. Due GameWorld ricevono gli eventi previsti
    static EventSubscriptionTestResult Test02_TwoWorldsReceiveEvents();

    // 3. Distruggendo il primo, il secondo continua a ricevere eventi (fix per MEM-01)
    static EventSubscriptionTestResult Test03_DestroyFirstWorldSecondStillReceives();

    // 4. Il mondo distrutto non riceve ulteriori callback (nessun dangling pointer)
    static EventSubscriptionTestResult Test04_DestroyedWorldReceivesNoFurtherCallbacks();

    // 5. Subscription e unsubscribe ripetuti non accumulano listener
    static EventSubscriptionTestResult Test05_RepeatedSubscribeUnsubscribeNoAccumulation();

    // 6. Unsubscribe durante la dispatch degli eventi: sicuro, non invocato, nessun deadlock
    static EventSubscriptionTestResult Test06_UnsubscribeDuringDispatchSafety();

    // 7. Nessuna regressione nei consumer esistenti (Event_BlockMined, Event_BlockPlaced)
    static EventSubscriptionTestResult Test07_ExistingConsumersNoRegression();
};

} // namespace fw
