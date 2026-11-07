#include "pch.h"
#include "JobSystem.h"
#include "GameWorld.h"
#include "BlockRegistry.h"
#include "MaterialRegistry.h"
#include "SharedContext.h"
#include "ForgeComponents.h"
#include "VramSlabAllocator.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {
using namespace std::chrono_literals;

struct Gate {
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false;
    bool released = false;

    void Block() {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        cv.notify_all();
        cv.wait(lock, [&] { return released; });
    }

    bool WaitEntered() {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, 3s, [&] { return entered; });
    }

    void Release() {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
        cv.notify_all();
    }
};

bool TestAcceptedCompleted() {
    fw::JobSystem jobs;
    jobs.Initialize(1);
    std::promise<fw::JobExecutionResult> completion;
    auto future = completion.get_future();
    auto submission = jobs.ExecuteTracked(
        [](uint64_t) { return fw::JobExecutionStatus::Completed; },
        [&completion](const fw::JobExecutionResult& result) { completion.set_value(result); });
    const bool accepted = submission.accepted() && submission.requestId != 0;
    const bool finished = future.wait_for(3s) == std::future_status::ready;
    const auto result = finished ? future.get() : fw::JobExecutionResult{};
    jobs.Shutdown();
    return accepted && finished && result.requestId == submission.requestId &&
        result.status == fw::JobExecutionStatus::Completed;
}

bool TestRejectAndRetry() {
    fw::JobSystem jobs;
    jobs.Initialize(1);
    jobs.StopAcceptingJobs();
    auto rejected = jobs.ExecuteTracked([](uint64_t) { return fw::JobExecutionStatus::Completed; });
    auto invalid = jobs.ExecuteTracked(std::function<fw::JobExecutionStatus(uint64_t)>{});
    auto rejectedFuture = jobs.Dispatch([] { return 7; });
    bool brokenPromise = false;
    try { (void)rejectedFuture.get(); }
    catch (const std::future_error& error) { brokenPromise = error.code() == std::make_error_code(std::future_errc::broken_promise); }
    const bool rejectedAsShutdown = !rejected.accepted() &&
        rejected.status == fw::JobSubmissionStatus::RejectedShutdown && !jobs.IsAcceptingJobs() &&
        !invalid.accepted() && invalid.status == fw::JobSubmissionStatus::RejectedInvalid && brokenPromise;
    jobs.Shutdown();

    auto stillRejected = jobs.ExecuteTracked([](uint64_t) { return fw::JobExecutionStatus::Completed; });
    jobs.Initialize(1);
    std::promise<fw::JobExecutionResult> completion;
    auto future = completion.get_future();
    auto retried = jobs.ExecuteTracked(
        [](uint64_t) { return fw::JobExecutionStatus::Completed; },
        [&completion](const fw::JobExecutionResult& result) { completion.set_value(result); });
    const bool retryCompleted = retried.accepted() && future.wait_for(3s) == std::future_status::ready &&
        future.get().status == fw::JobExecutionStatus::Completed;
    jobs.Shutdown();
    return rejectedAsShutdown && !stillRejected.accepted() && retryCompleted;
}

bool TestCancellationBeforeStart() {
    fw::JobSystem jobs;
    jobs.Initialize(1);
    Gate gate;
    jobs.Execute([&] { gate.Block(); });
    if (!gate.WaitEntered()) { gate.Release(); jobs.Shutdown(); return false; }

    auto cancel = std::make_shared<std::atomic<bool>>(false);
    std::promise<fw::JobExecutionResult> completion;
    auto future = completion.get_future();
    auto submission = jobs.ExecuteTracked(
        [cancel](uint64_t) { return cancel->load() ? fw::JobExecutionStatus::Cancelled : fw::JobExecutionStatus::Completed; },
        [&completion](const fw::JobExecutionResult& result) { completion.set_value(result); });
    cancel->store(true);
    gate.Release();
    const bool finished = future.wait_for(3s) == std::future_status::ready;
    const auto result = finished ? future.get() : fw::JobExecutionResult{};
    jobs.Shutdown();
    return submission.accepted() && finished && result.status == fw::JobExecutionStatus::Cancelled;
}

bool TestCancellationDuringWork() {
    fw::JobSystem jobs;
    jobs.Initialize(1);
    Gate gate;
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    std::promise<fw::JobExecutionResult> completion;
    auto future = completion.get_future();
    auto submission = jobs.ExecuteTracked(
        [&gate, cancel](uint64_t) {
            gate.Block();
            return cancel->load() ? fw::JobExecutionStatus::Cancelled : fw::JobExecutionStatus::Completed;
        },
        [&completion](const fw::JobExecutionResult& result) { completion.set_value(result); });
    const bool started = gate.WaitEntered();
    if (!started) { gate.Release(); jobs.Shutdown(); return false; }
    cancel->store(true);
    gate.Release();
    const bool finished = future.wait_for(3s) == std::future_status::ready;
    const auto result = finished ? future.get() : fw::JobExecutionResult{};
    jobs.Shutdown();
    return submission.accepted() && finished && result.status == fw::JobExecutionStatus::Cancelled;
}

bool TestExceptionAndWorkerSurvives() {
    fw::JobSystem jobs;
    jobs.Initialize(1);
    std::promise<fw::JobExecutionResult> failedPromise;
    auto failedFuture = failedPromise.get_future();
    auto failedSubmission = jobs.ExecuteTracked(
        [](uint64_t) -> fw::JobExecutionStatus { throw std::runtime_error("injected worker failure"); },
        [&failedPromise](const fw::JobExecutionResult& result) { failedPromise.set_value(result); });
    const bool failedReady = failedFuture.wait_for(3s) == std::future_status::ready;
    const auto failed = failedReady ? failedFuture.get() : fw::JobExecutionResult{};

    std::promise<fw::JobExecutionResult> nextPromise;
    auto nextFuture = nextPromise.get_future();
    auto next = jobs.ExecuteTracked(
        [](uint64_t) { return fw::JobExecutionStatus::Completed; },
        [&nextPromise](const fw::JobExecutionResult& result) { nextPromise.set_value(result); });
    const bool nextReady = nextFuture.wait_for(3s) == std::future_status::ready;
    const auto nextResult = nextReady ? nextFuture.get() : fw::JobExecutionResult{};
    jobs.Shutdown();
    return failedSubmission.accepted() && failedReady && failed.status == fw::JobExecutionStatus::Failed &&
        failed.exception && next.accepted() && nextReady && nextResult.status == fw::JobExecutionStatus::Completed;
}

struct WorldFixture {
    fw::BlockRegistry blocks;
    fw::MaterialRegistry materials;
    fw::JobSystem jobs;
    SharedContext context{};
    fw::GameWorld world;

    explicit WorldFixture(size_t workers = 1) {
        blocks.Initialize();
        materials.Initialize();
        jobs.Initialize(workers);
        context.blockRegistry = &blocks;
        context.materialRegistry = &materials;
        context.jobSystem = &jobs;
        context.isMapBuilderMode = true;
        context.isForgeMode = false;
        world.Initialize(&context);
    }

    ~WorldFixture() {
        world.TeardownResources();
        jobs.Shutdown();
    }

    entt::entity AddEmptyChunk() {
        auto& registry = world.GetRegistry();
        const entt::entity entity = registry.create();
        auto& chunk = registry.emplace<fw::VoxelChunkComponent>(entity);
        chunk.isGenerated = true;
        chunk.planetCoord.planet = fw::PlanetID::Invalid();
        world.MarkMeshDirty(entity);
        return entity;
    }
};

bool TestGameWorldRebuildDuringActiveJob() {
    WorldFixture fixture;
    Gate gate;
    fixture.jobs.Execute([&] { gate.Block(); });
    if (!gate.WaitEntered()) { gate.Release(); return false; }
    const entt::entity entity = fixture.AddEmptyChunk();
    fixture.world.Update(0.0f);
    auto& registry = fixture.world.GetRegistry();
    const auto& dirty = registry.get<fw::ChunkDirtyComponent>(entity);
    const uint64_t firstRequest = dirty.activeRequestId;
    const uint64_t firstRevision = registry.get<fw::VoxelChunkComponent>(entity).meshRevision;
    if (!dirty.pendingJob || firstRequest == 0) { gate.Release(); return false; }

    fixture.world.MarkMeshDirty(entity);
    fixture.world.Update(0.0f);
    const bool singleRequest = fixture.jobs.GetPendingJobCount() == 1 &&
        registry.get<fw::ChunkDirtyComponent>(entity).activeRequestId == firstRequest;
    gate.Release();
    fixture.jobs.WaitAll();
    fixture.world.Update(0.0f); // stale result discarded; exactly one fresh request is dispatched
    const bool secondRequest = registry.valid(entity) && registry.all_of<fw::ChunkDirtyComponent>(entity) &&
        registry.get<fw::ChunkDirtyComponent>(entity).pendingJob &&
        registry.get<fw::VoxelChunkComponent>(entity).meshRevision > firstRevision;
    fixture.jobs.WaitAll();
    fixture.world.Update(0.0f);
    return singleRequest && secondRequest && registry.valid(entity) &&
        !registry.all_of<fw::ChunkDirtyComponent>(entity) && !registry.all_of<fw::MeshComponent>(entity);
}

bool TestGameWorldCancelAndRetry() {
    WorldFixture fixture;
    Gate gate;
    fixture.jobs.Execute([&] { gate.Block(); });
    if (!gate.WaitEntered()) { gate.Release(); return false; }
    const entt::entity entity = fixture.AddEmptyChunk();
    fixture.world.Update(0.0f);
    const bool accepted = fixture.world.GetRegistry().get<fw::ChunkDirtyComponent>(entity).pendingJob;
    fixture.world.CancelJobs();
    gate.Release();
    fixture.jobs.WaitAll();
    fixture.world.Update(0.0f);
    auto& registry = fixture.world.GetRegistry();
    const bool cancelledClearedPending = registry.valid(entity) && registry.all_of<fw::ChunkDirtyComponent>(entity) &&
        !registry.get<fw::ChunkDirtyComponent>(entity).pendingJob;

    fixture.world.MarkMeshDirty(entity); // opens a fresh cancellation epoch for this new request
    fixture.world.Update(0.0f);
    const bool retried = registry.get<fw::ChunkDirtyComponent>(entity).pendingJob;
    fixture.jobs.WaitAll();
    fixture.world.Update(0.0f);
    return accepted && cancelledClearedPending && retried && !registry.all_of<fw::ChunkDirtyComponent>(entity);
}

bool TestGameWorldRejectedQueueRetainsRetryableDirtyState() {
    WorldFixture fixture;
    const entt::entity entity = fixture.AddEmptyChunk();
    fixture.jobs.StopAcceptingJobs();
    fixture.world.Update(0.0f);
    auto& registry = fixture.world.GetRegistry();
    const bool rejectionDidNotStickPending = registry.all_of<fw::ChunkDirtyComponent>(entity) &&
        !registry.get<fw::ChunkDirtyComponent>(entity).pendingJob && fixture.jobs.GetPendingJobCount() == 0;

    fixture.jobs.Shutdown();
    fixture.jobs.Initialize(1);
    fixture.world.Update(0.0f);
    const bool retryAccepted = registry.all_of<fw::ChunkDirtyComponent>(entity) &&
        registry.get<fw::ChunkDirtyComponent>(entity).pendingJob &&
        registry.get<fw::ChunkDirtyComponent>(entity).activeRequestId != 0;
    fixture.jobs.WaitAll();
    fixture.world.Update(0.0f);
    return rejectionDidNotStickPending && retryAccepted && !registry.all_of<fw::ChunkDirtyComponent>(entity);
}

bool TestGameWorldWorkerFailureClearsPendingWithoutRetryLoop() {
    fw::BlockRegistry blocks;
    fw::MaterialRegistry materials;
    fw::JobSystem jobs;
    fw::VramSlabAllocator allocator(4u * 1024u * 1024u, 256u * 1024u * 1024u, 4u * 1024u * 1024u);
    blocks.Initialize();
    materials.Initialize();
    jobs.Initialize(1);
    SharedContext context{};
    context.blockRegistry = &blocks;
    context.materialRegistry = &materials;
    context.jobSystem = &jobs;
    context.vramAllocator = &allocator;
    context.isMapBuilderMode = true;
    fw::GameWorld world;
    world.Initialize(&context);
    auto& registry = world.GetRegistry();
    const entt::entity entity = registry.create();
    auto& chunk = registry.emplace<fw::VoxelChunkComponent>(entity);
    chunk.isGenerated = true;
    for (int x = 0; x < CHUNK_SIZE; ++x)
        for (int y = 0; y < CHUNK_HEIGHT; ++y)
            for (int z = 0; z < CHUNK_SIZE; ++z)
                if (((x + y + z) & 1) == 0) chunk.blocks[x][y][z] = 3;
    world.MarkMeshDirty(entity);
    world.Update(0.0f);
    jobs.WaitAll();
    world.Update(0.0f);
    const bool terminalFailureClearedPending = registry.all_of<fw::ChunkDirtyComponent>(entity) &&
        !registry.get<fw::ChunkDirtyComponent>(entity).pendingJob;
    world.Update(0.0f);
    const bool noAutomaticRetryLoop = jobs.GetPendingJobCount() == 0 && jobs.GetActiveWorkerCount() == 0 &&
        registry.all_of<fw::ChunkDirtyComponent>(entity) && !registry.get<fw::ChunkDirtyComponent>(entity).pendingJob;

    world.TeardownResources();
    jobs.Shutdown();
    return terminalFailureClearedPending && noAutomaticRetryLoop;
}

bool TestTeardownWaitsAndRejectsNewWorldJobs() {
    WorldFixture fixture;
    Gate gate;
    fixture.jobs.Execute([&] { gate.Block(); });
    if (!gate.WaitEntered()) { gate.Release(); return false; }
    const entt::entity entity = fixture.AddEmptyChunk();
    fixture.world.Update(0.0f);

    auto teardown = std::async(std::launch::async, [&] { fixture.world.TeardownResources(); });
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (fixture.world.IsAcceptingMeshJobs() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    const bool shutdownStarted = !fixture.world.IsAcceptingMeshJobs();
    gate.Release();
    const bool returned = teardown.wait_for(3s) == std::future_status::ready;
    if (returned) teardown.get();
    const bool quiesced = returned && fixture.jobs.GetPendingJobCount() == 0 && fixture.jobs.GetActiveWorkerCount() == 0;

    const entt::entity lateEntity = fixture.world.GetRegistry().create();
    fixture.world.GetRegistry().emplace<fw::VoxelChunkComponent>(lateEntity);
    fixture.world.MarkMeshDirty(lateEntity);
    fixture.world.Update(0.0f);
    const bool rejectedAfterTeardown = !fixture.world.IsAcceptingMeshJobs() &&
        !fixture.world.GetRegistry().all_of<fw::ChunkDirtyComponent>(lateEntity) &&
        fixture.jobs.GetPendingJobCount() == 0;
    return shutdownStarted && quiesced && rejectedAfterTeardown && !fixture.world.GetRegistry().valid(entity);
}

} // namespace

int main() {
    const std::pair<const char*, bool> tests[] = {
        {"accepted/completed", TestAcceptedCompleted()},
        {"shutdown rejection and later retry", TestRejectAndRetry()},
        {"cancellation before start", TestCancellationBeforeStart()},
        {"cancellation during work", TestCancellationDuringWork()},
        {"worker exception and pool recovery", TestExceptionAndWorkerSurvives()},
        {"rebuild during active mesh job / stale result rejection", TestGameWorldRebuildDuringActiveJob()},
        {"cancelled mesh job clears pending and permits retry", TestGameWorldCancelAndRetry()},
        {"rejected world job remains retryable", TestGameWorldRejectedQueueRetainsRetryableDirtyState()},
        {"worker failure clears pending without retry loop", TestGameWorldWorkerFailureClearsPendingWithoutRetryLoop()},
        {"teardown waits and blocks new world jobs", TestTeardownWaitsAndRejectsNewWorldJobs()}
    };
    bool allPassed = true;
    for (const auto& [name, pass] : tests) {
        std::cout << (pass ? "[PASS] " : "[FAIL] ") << name << '\n';
        allPassed = allPassed && pass;
    }
    std::cout << "JobSystem contract tests: " << (allPassed ? "PASS" : "FAIL") << " (10 scenarios)\n";
    return allPassed ? 0 : 1;
}
