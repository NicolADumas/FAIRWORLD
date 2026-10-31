#include "pch.h"
#include "EventSubscriptionContractTests.h"
#include "EventManager.h"
#include "GameWorld.h"
#include "SharedContext.h"
#include "PlanetComponents.h"
#include <iostream>
#include <memory>
#include <atomic>

namespace fw {

std::vector<EventSubscriptionTestResult> EventSubscriptionContractTests::RunAllTests() {
    std::vector<EventSubscriptionTestResult> results;
    results.reserve(7);

    results.push_back(Test01_SingleWorldReceivesEvents());
    results.push_back(Test02_TwoWorldsReceiveEvents());
    results.push_back(Test03_DestroyFirstWorldSecondStillReceives());
    results.push_back(Test04_DestroyedWorldReceivesNoFurtherCallbacks());
    results.push_back(Test05_RepeatedSubscribeUnsubscribeNoAccumulation());
    results.push_back(Test06_UnsubscribeDuringDispatchSafety());
    results.push_back(Test07_ExistingConsumersNoRegression());

    return results;
}

EventSubscriptionTestResult EventSubscriptionContractTests::Test01_SingleWorldReceivesEvents() {
    EventSubscriptionTestResult r;
    r.testId = "EVT-01";
    r.testName = "Single GameWorld Event Receipt";
    r.inputDesc = "Initialize single GameWorld, place water, dispatch Event_BlockUpdated";
    r.expectedDesc = "GameWorld receives event and executes fluid update (water flows down)";

    EventManager::Get().Clear();

    SharedContext ctx;
    ctx.isMapBuilderMode = false;
    ctx.isForgeMode = true; // MEM-01: chunk creati marcati isGenerated=true per consentire campionamento voxel
    GameWorld world;
    world.Initialize(&ctx);

    // MEM-01: Disabilita esplicitamente la sfera logica per operare nello spazio cartesiano piatto (voxel flat)
    if (world.GetRegistry().valid(world.GetPlanetEntity()) && 
        world.GetRegistry().all_of<PlanetGeometryComponent>(world.GetPlanetEntity())) {
        world.GetRegistry().get<PlanetGeometryComponent>(world.GetPlanetEntity()).isLogicalSphere = false;
    }

    SubscriptionId subId = world.GetBlockUpdatedSubscriptionId();
    if (subId == INVALID_SUBSCRIPTION_ID) {
        r.passed = false;
        r.observedDesc = "FAIL: SubscriptionId is INVALID_SUBSCRIPTION_ID";
        return r;
    }

    // Set up test voxel state: Water at (10, 60, 10), Air at (10, 59, 10)
    world.SetBlock(10, 60, 10, BlockType::Water);
    world.SetBlock(10, 59, 10, BlockType::Air);

    // 3. Verificare che SetBlock inserisca effettivamente Water
    BlockType initialWater = world.GetBlock(10, 60, 10);
    if (initialWater != BlockType::Water) {
        r.passed = false;
        r.observedDesc = "FAIL: SetBlock non ha inserito Water in (10, 60, 10). Blocco letto: " + 
                         std::to_string(static_cast<int>(initialWater));
        return r;
    }

    // 4. Verificare lo stato iniziale del blocco sottostante
    BlockType initialBelow = world.GetBlock(10, 59, 10);
    if (initialBelow != BlockType::Air) {
        r.passed = false;
        r.observedDesc = "FAIL: Blocco sottostante non e' Air prima dell'aggiornamento. Blocco letto: " + 
                         std::to_string(static_cast<int>(initialBelow));
        return r;
    }

    // 5. Trigger update event
    EventManager::Get().Dispatch(Event_BlockUpdated(glm::ivec3(10, 60, 10)));

    // 6. Verificare il risultato atteso della simulazione
    BlockType below = world.GetBlock(10, 59, 10);
    if (below == BlockType::Water) {
        r.passed = true;
        r.observedDesc = "PASS: Event received and water propagated to (10, 59, 10). SubID=" + std::to_string(subId);
    } else {
        r.passed = false;
        r.observedDesc = "FAIL: Block below is " + std::to_string(static_cast<int>(below)) + " instead of Water";
    }

    EventManager::Get().Clear();
    return r;
}

EventSubscriptionTestResult EventSubscriptionContractTests::Test02_TwoWorldsReceiveEvents() {
    EventSubscriptionTestResult r;
    r.testId = "EVT-02";
    r.testName = "Dual GameWorld Concurrent Reception";
    r.inputDesc = "Initialize two independent GameWorlds, dispatch Event_BlockUpdated";
    r.expectedDesc = "Both GameWorlds have distinct SubscriptionIds and both receive the event";

    EventManager::Get().Clear();

    SharedContext ctx1;
    ctx1.isMapBuilderMode = false;
    ctx1.isForgeMode = true;
    GameWorld world1;
    world1.Initialize(&ctx1);
    if (world1.GetRegistry().valid(world1.GetPlanetEntity()) && 
        world1.GetRegistry().all_of<PlanetGeometryComponent>(world1.GetPlanetEntity())) {
        world1.GetRegistry().get<PlanetGeometryComponent>(world1.GetPlanetEntity()).isLogicalSphere = false;
    }

    SharedContext ctx2;
    ctx2.isMapBuilderMode = false;
    ctx2.isForgeMode = true;
    GameWorld world2;
    world2.Initialize(&ctx2);
    if (world2.GetRegistry().valid(world2.GetPlanetEntity()) && 
        world2.GetRegistry().all_of<PlanetGeometryComponent>(world2.GetPlanetEntity())) {
        world2.GetRegistry().get<PlanetGeometryComponent>(world2.GetPlanetEntity()).isLogicalSphere = false;
    }

    SubscriptionId id1 = world1.GetBlockUpdatedSubscriptionId();
    SubscriptionId id2 = world2.GetBlockUpdatedSubscriptionId();

    if (id1 == INVALID_SUBSCRIPTION_ID || id2 == INVALID_SUBSCRIPTION_ID || id1 == id2) {
        r.passed = false;
        r.observedDesc = "FAIL: Subscription IDs invalid or duplicate. id1=" + std::to_string(id1) + ", id2=" + std::to_string(id2);
        return r;
    }

    world1.SetBlock(15, 60, 15, BlockType::Water);
    world1.SetBlock(15, 59, 15, BlockType::Air);

    world2.SetBlock(15, 60, 15, BlockType::Water);
    world2.SetBlock(15, 59, 15, BlockType::Air);

    // Pre-condition checks
    if (world1.GetBlock(15, 60, 15) != BlockType::Water || world2.GetBlock(15, 60, 15) != BlockType::Water) {
        r.passed = false;
        r.observedDesc = "FAIL: SetBlock non ha inserito Water in posizione sorgente in world1 o world2";
        return r;
    }
    if (world1.GetBlock(15, 59, 15) != BlockType::Air || world2.GetBlock(15, 59, 15) != BlockType::Air) {
        r.passed = false;
        r.observedDesc = "FAIL: Blocco sottostante non e' Air prima del dispatch in world1 o world2";
        return r;
    }

    EventManager::Get().Dispatch(Event_BlockUpdated(glm::ivec3(15, 60, 15)));

    bool w1Flowed = (world1.GetBlock(15, 59, 15) == BlockType::Water);
    bool w2Flowed = (world2.GetBlock(15, 59, 15) == BlockType::Water);

    if (w1Flowed && w2Flowed) {
        r.passed = true;
        r.observedDesc = "PASS: Both worlds received update. SubIds=(" + std::to_string(id1) + ", " + std::to_string(id2) + ")";
    } else {
        r.passed = false;
        r.observedDesc = "FAIL: w1Flowed=" + std::string(w1Flowed ? "true" : "false") + ", w2Flowed=" + std::string(w2Flowed ? "true" : "false");
    }

    EventManager::Get().Clear();
    return r;
}

EventSubscriptionTestResult EventSubscriptionContractTests::Test03_DestroyFirstWorldSecondStillReceives() {
    EventSubscriptionTestResult r;
    r.testId = "EVT-03";
    r.testName = "Destroyed World Does Not Cancel Sibling Subscription (MEM-01 Core Fix)";
    r.inputDesc = "Initialize world1 and world2, destroy world1, dispatch Event_BlockUpdated to world2";
    r.expectedDesc = "world1 unregistration preserves world2 subscription; world2 continues processing events";

    EventManager::Get().Clear();

    SharedContext ctx1;
    ctx1.isMapBuilderMode = false;
    ctx1.isForgeMode = true;

    SharedContext ctx2;
    ctx2.isMapBuilderMode = false;
    ctx2.isForgeMode = true;
    auto world2 = std::make_unique<GameWorld>();
    world2->Initialize(&ctx2);
    if (world2->GetRegistry().valid(world2->GetPlanetEntity()) && 
        world2->GetRegistry().all_of<PlanetGeometryComponent>(world2->GetPlanetEntity())) {
        world2->GetRegistry().get<PlanetGeometryComponent>(world2->GetPlanetEntity()).isLogicalSphere = false;
    }
    SubscriptionId id2 = world2->GetBlockUpdatedSubscriptionId();

    // Create and destroy world1 inside an isolated scope
    {
        auto world1 = std::make_unique<GameWorld>();
        world1->Initialize(&ctx1);
        if (world1->GetRegistry().valid(world1->GetPlanetEntity()) && 
            world1->GetRegistry().all_of<PlanetGeometryComponent>(world1->GetPlanetEntity())) {
            world1->GetRegistry().get<PlanetGeometryComponent>(world1->GetPlanetEntity()).isLogicalSphere = false;
        }
        SubscriptionId id1 = world1->GetBlockUpdatedSubscriptionId();
        (void)id1;
        // Scope exit destroys world1, calling ~GameWorld()
    }

    // Prior to MEM-01 fix: world1 destructor called UnsubscribeAll<Event_BlockUpdated>(), which wiped id2!
    // With MEM-01 fix: id2 MUST still be subscribed.
    bool id2Alive = EventManager::Get().IsSubscribed(id2);
    if (!id2Alive) {
        r.passed = false;
        r.observedDesc = "FAIL: id2 was obliterated when world1 was destroyed (blast radius bug still present)";
        return r;
    }

    world2->SetBlock(20, 60, 20, BlockType::Water);
    world2->SetBlock(20, 59, 20, BlockType::Air);

    // Pre-condition checks
    if (world2->GetBlock(20, 60, 20) != BlockType::Water) {
        r.passed = false;
        r.observedDesc = "FAIL: SetBlock non ha inserito Water in (20, 60, 20) in world2";
        return r;
    }
    if (world2->GetBlock(20, 59, 20) != BlockType::Air) {
        r.passed = false;
        r.observedDesc = "FAIL: Blocco sottostante non e' Air prima del dispatch in world2";
        return r;
    }

    EventManager::Get().Dispatch(Event_BlockUpdated(glm::ivec3(20, 60, 20)));

    bool w2Flowed = (world2->GetBlock(20, 59, 20) == BlockType::Water);
    if (w2Flowed) {
        r.passed = true;
        r.observedDesc = "PASS: world2 retained subscription id=" + std::to_string(id2) + " and processed fluid successfully after world1 destruction.";
    } else {
        r.passed = false;
        r.observedDesc = "FAIL: world2 failed to process event after world1 destruction.";
    }

    EventManager::Get().Clear();
    return r;
}

EventSubscriptionTestResult EventSubscriptionContractTests::Test04_DestroyedWorldReceivesNoFurtherCallbacks() {
    EventSubscriptionTestResult r;
    r.testId = "EVT-04";
    r.testName = "Destroyed Entity Zero Callback Execution";
    r.inputDesc = "Subscribe custom callback, unsubscribe it, dispatch event";
    r.expectedDesc = "Unsubscribed callback is never invoked; zero callback invocations";

    EventManager::Get().Clear();

    int callCount = 0;
    SubscriptionId id = EventManager::Get().Subscribe<Event_BlockUpdated>([&callCount](const Event_BlockUpdated&) {
        callCount++;
    });

    EventManager::Get().Dispatch(Event_BlockUpdated(glm::ivec3(0, 0, 0)));
    if (callCount != 1) {
        r.passed = false;
        r.observedDesc = "FAIL: initial dispatch did not trigger callback";
        return r;
    }

    bool unsubOk = EventManager::Get().Unsubscribe(id);
    if (!unsubOk) {
        r.passed = false;
        r.observedDesc = "FAIL: Unsubscribe returned false for valid id=" + std::to_string(id);
        return r;
    }

    // Second dispatch: must NOT increment callCount
    EventManager::Get().Dispatch(Event_BlockUpdated(glm::ivec3(0, 0, 0)));

    if (callCount == 1 && !EventManager::Get().IsSubscribed(id)) {
        r.passed = true;
        r.observedDesc = "PASS: Call count remained 1 after unsubscription; no dangling invocation.";
    } else {
        r.passed = false;
        r.observedDesc = "FAIL: Call count changed to " + std::to_string(callCount) + " after unsubscription.";
    }

    EventManager::Get().Clear();
    return r;
}

EventSubscriptionTestResult EventSubscriptionContractTests::Test05_RepeatedSubscribeUnsubscribeNoAccumulation() {
    EventSubscriptionTestResult r;
    r.testId = "EVT-05";
    r.testName = "Repeated Lifecycle Zero Listener Accumulation";
    r.inputDesc = "Perform 100 subscribe/unsubscribe cycles, re-initialize GameWorld 5 times";
    r.expectedDesc = "Listener count returns to 0 after cycles, repeated world Init has exactly 1 subscription";

    EventManager::Get().Clear();

    // 1. Raw subscribe / unsubscribe cycles
    for (int i = 0; i < 100; ++i) {
        SubscriptionId id = EventManager::Get().Subscribe<Event_BlockUpdated>([](const Event_BlockUpdated&) {});
        EventManager::Get().Unsubscribe(id);
    }

    size_t activeCount = EventManager::Get().GetTotalListenerCount();
    if (activeCount != 0) {
        r.passed = false;
        r.observedDesc = "FAIL: Active listener count is " + std::to_string(activeCount) + " instead of 0";
        return r;
    }

    // 2. Repeated Initialize on single GameWorld instance
    SharedContext ctx;
    ctx.isMapBuilderMode = false;
    GameWorld world;
    for (int i = 0; i < 5; ++i) {
        world.Initialize(&ctx);
    }

    size_t worldSubs = EventManager::Get().GetListenerCount<Event_BlockUpdated>();
    if (worldSubs != 1) {
        r.passed = false;
        r.observedDesc = "FAIL: Re-initialized GameWorld accumulated " + std::to_string(worldSubs) + " subscriptions instead of 1";
        return r;
    }

    r.passed = true;
    r.observedDesc = "PASS: 100 raw cycles left 0 listeners; 5 world initializations resulted in exactly 1 active subscription.";
    EventManager::Get().Clear();
    return r;
}

EventSubscriptionTestResult EventSubscriptionContractTests::Test06_UnsubscribeDuringDispatchSafety() {
    EventSubscriptionTestResult r;
    r.testId = "EVT-06";
    r.testName = "Safe Unsubscribe During Dispatch (No Deadlock, No Invalid Invocation)";
    r.inputDesc = "Listener A unsubscribes Listener B during Dispatch; verify B does not execute and no deadlock";
    r.expectedDesc = "Listener B is skipped; compaction cleans up tombstone cleanly when dispatch exits";

    EventManager::Get().Clear();

    bool aExecuted = false;
    bool bExecuted = false;
    SubscriptionId idB = INVALID_SUBSCRIPTION_ID;

    SubscriptionId idA = EventManager::Get().Subscribe<Event_BlockUpdated>([&](const Event_BlockUpdated&) {
        aExecuted = true;
        // Unsubscribe B while dispatch is actively progressing
        EventManager::Get().Unsubscribe(idB);
    });

    idB = EventManager::Get().Subscribe<Event_BlockUpdated>([&](const Event_BlockUpdated&) {
        bExecuted = true;
    });

    EventManager::Get().Dispatch(Event_BlockUpdated(glm::ivec3(0, 0, 0)));

    if (aExecuted && !bExecuted && !EventManager::Get().IsSubscribed(idB) && EventManager::Get().IsSubscribed(idA)) {
        size_t finalCount = EventManager::Get().GetListenerCount<Event_BlockUpdated>();
        if (finalCount == 1) {
            r.passed = true;
            r.observedDesc = "PASS: A executed and unsubscribed B. B was not invoked during dispatch. Post-dispatch listener count is 1.";
        } else {
            r.passed = false;
            r.observedDesc = "FAIL: finalCount is " + std::to_string(finalCount) + " instead of 1";
        }
    } else {
        r.passed = false;
        r.observedDesc = "FAIL: aExecuted=" + std::string(aExecuted ? "true" : "false") + ", bExecuted=" + std::string(bExecuted ? "true" : "false");
    }

    EventManager::Get().Clear();
    return r;
}

EventSubscriptionTestResult EventSubscriptionContractTests::Test07_ExistingConsumersNoRegression() {
    EventSubscriptionTestResult r;
    r.testId = "EVT-07";
    r.testName = "Existing Gameplay Consumers Backwards Compatibility";
    r.inputDesc = "Subscribe and dispatch Event_BlockMined and Event_BlockPlaced using standard syntax";
    r.expectedDesc = "Both events dispatch cleanly with accurate payload delivery and zero regressions";

    EventManager::Get().Clear();

    bool minedOk = false;
    ::BlockType minedType = ::BlockType::Air;
    glm::ivec3 minedPos(0);

    EventManager::Get().Subscribe<Event_BlockMined>([&](const Event_BlockMined& e) {
        minedOk = true;
        minedType = e.type;
        minedPos = e.position;
    });

    bool placedOk = false;
    ::BlockType placedType = ::BlockType::Air;
    glm::ivec3 placedPos(0);

    EventManager::Get().Subscribe<Event_BlockPlaced>([&](const Event_BlockPlaced& e) {
        placedOk = true;
        placedType = e.type;
        placedPos = e.position;
    });

    EventManager::Get().Dispatch(Event_BlockMined(glm::ivec3(7, 8, 9), ::BlockType::Stone));
    EventManager::Get().Dispatch(Event_BlockPlaced(glm::ivec3(10, 11, 12), ::BlockType::Wood));

    bool passMined = minedOk && (minedType == ::BlockType::Stone) && (minedPos == glm::ivec3(7, 8, 9));
    bool passPlaced = placedOk && (placedType == ::BlockType::Wood) && (placedPos == glm::ivec3(10, 11, 12));

    if (passMined && passPlaced) {
        r.passed = true;
        r.observedDesc = "PASS: Event_BlockMined and Event_BlockPlaced executed without regression.";
    } else {
        r.passed = false;
        r.observedDesc = "FAIL: passMined=" + std::string(passMined ? "true" : "false") + ", passPlaced=" + std::string(passPlaced ? "true" : "false");
    }

    EventManager::Get().Clear();
    return r;
}

} // namespace fw
