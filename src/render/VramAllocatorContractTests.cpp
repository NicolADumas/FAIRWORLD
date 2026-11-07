#include "VramSlabAllocator.h"
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

namespace {
bool Check(bool ok, const char* name) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    return ok;
}
}

int main() {
    int passed = 0, failed = 0;
    auto run = [&](bool ok, const char* name) { (ok ? passed : failed)++; Check(ok, name); };

    {
        fw::VramSlabAllocator allocator;
        const auto handle = allocator.Allocate(10'000);
        const auto info = allocator.GetAllocation(handle);
        const bool allocated = handle != fw::INVALID_VRAM_HANDLE && info.valid &&
            allocator.GetAllocatedBytes() == 10'000 && allocator.GetWastedBytes() == 6'384;
        allocator.Free(handle);
        run(allocated && allocator.GetAllocatedBytes() == 0 && allocator.GetWastedBytes() == 0 &&
            !allocator.GetAllocation(handle).valid, "allocate/free and requested/waste statistics");
    }
    {
        fw::VramSlabAllocator allocator;
        const auto stale = allocator.Allocate(8'192);
        allocator.Free(stale);
        const auto current = allocator.Allocate(8'192);
        allocator.Free(stale);
        run(current != fw::INVALID_VRAM_HANDLE && current != stale &&
            !allocator.GetAllocation(stale).valid && allocator.GetAllocation(current).valid &&
            allocator.GetAllocatedBytes() == 8'192, "generation rejects stale handle after slot reuse");
        allocator.Free(current);
    }
    {
        fw::VramSlabAllocator allocator(256 * 1024 * 1024, 256 * 1024 * 1024, 4 * 1024 * 1024);
        allocator.SetAllocateCompartmentCallback([](uint32_t) { return false; });
        const auto handle = allocator.Allocate(8'192);
        run(handle == fw::INVALID_VRAM_HANDLE && allocator.GetCompartmentCount() == 0 &&
            allocator.GetAllocatedBytes() == 0, "compartment creation failure yields no allocation");
    }
    {
        fw::VramSlabAllocator allocator;
        auto retired = allocator.Allocate(8'192);
        const auto retiredInfo = allocator.GetAllocation(retired);
        const bool marked = allocator.Retire(retired, 42);
        auto replacement = allocator.Allocate(8'192);
        const auto replacementInfo = allocator.GetAllocation(replacement);
        const size_t before = allocator.CollectRetired(41);
        const bool notReusedEarly = allocator.GetAllocation(retired).valid &&
            replacementInfo.offset != retiredInfo.offset && before == 0;
        const size_t collected = allocator.CollectRetired(42);
        const auto recycled = allocator.Allocate(8'192);
        const auto recycledInfo = allocator.GetAllocation(recycled);
        run(marked && notReusedEarly && collected == 1 && recycledInfo.offset == retiredInfo.offset &&
            recycled != retired && !allocator.GetAllocation(retired).valid,
            "retired slot waits for confirmed graphics serial then reuses with new generation");
        allocator.Free(replacement);
        allocator.Free(recycled);
    }
    {
        fw::VramSlabAllocator allocator;
        const auto pending = allocator.Allocate(8'192);
        const auto info = allocator.GetAllocation(pending);
        const bool quarantined = allocator.Quarantine(pending);
        allocator.Free(pending);
        const auto another = allocator.Allocate(8'192);
        const bool retained = allocator.GetAllocationState(pending) == fw::VramAllocationState::Quarantined &&
            allocator.GetAllocation(pending).valid && allocator.GetAllocation(another).offset != info.offset &&
            !allocator.ReleaseQuarantined(pending, false);
        const bool released = allocator.ReleaseQuarantined(pending, true) &&
            !allocator.GetAllocation(pending).valid;
        run(quarantined && retained && released, "unconfirmed transfer quarantine requires positive completion evidence");
        allocator.Free(another);
    }
    {
        fw::VramSlabAllocator allocator;
        const auto mesh = allocator.Allocate(8'192);
        const auto meshInfo = allocator.GetAllocation(mesh);
        // Multiple draws can reference one mesh across frames. Retire at the
        // highest frame serial, including a frame currently being recorded.
        const bool retired = allocator.Retire(mesh, 52);
        auto replacement = allocator.Allocate(8'192);
        const bool stillOwnedAtEarlierFence = allocator.CollectRetired(51) == 0 &&
            allocator.GetAllocation(mesh).valid && allocator.GetAllocation(replacement).offset != meshInfo.offset;
        const bool releasedAtLastFence = allocator.CollectRetired(52) == 1 &&
            !allocator.GetAllocation(mesh).valid;
        run(retired && stillOwnedAtEarlierFence && releasedAtLastFence,
            "mesh replacement retains allocation through two in-flight frame serials");
        allocator.Free(replacement);
    }
    {
        fw::VramSlabAllocator allocator(256 * 1024 * 1024, 64 * 1024 * 1024, 4 * 1024 * 1024);
        std::atomic<uint32_t> compartmentCalls{0};
        allocator.SetAllocateCompartmentCallback([&](uint32_t) { ++compartmentCalls; return true; });
        std::atomic<bool> ok{true};
        std::vector<std::thread> workers;
        for (int t = 0; t < 8; ++t) {
            workers.emplace_back([&] {
                for (int i = 0; i < 1000; ++i) {
                    auto handle = allocator.Allocate(2'048);
                    const auto info = allocator.GetAllocation(handle);
                    if (handle == fw::INVALID_VRAM_HANDLE || !info.valid || info.size < 2'048) ok = false;
                    (void)allocator.GetAllocatedBytes();
                    (void)allocator.GetWastedBytes();
                    allocator.Free(handle);
                }
            });
        }
        for (auto& worker : workers) worker.join();
        run(ok.load() && allocator.GetCompartmentCount() == compartmentCalls.load() &&
            allocator.GetAllocatedBytes() == 0 && allocator.GetWastedBytes() == 0,
            "concurrent allocate/free/get/stat metadata access");
    }

    std::cout << "VRAM ALLOCATOR CONTRACT: " << passed << '/' << (passed + failed) << " PASS\n";
    return failed == 0 ? 0 : 1;
}
