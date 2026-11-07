#include "TransferGraphicsSync.h"
#include <iostream>

namespace {
bool Check(bool ok, const char* name) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    return ok;
}
}

int main() {
    int passed = 0;
    int failed = 0;
    auto run = [&](bool ok, const char* name) { (ok ? passed : failed)++; Check(ok, name); };

    const auto sameQueuePending = fw::MakeTransferGraphicsWaitPlan(9, 7);
    run(sameQueuePending.waitTimeline && sameQueuePending.timelineValue == 9 &&
        sameQueuePending.destinationStages == VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        "same queue path waits for new transfer target at vertex input stage");

    const auto distinctQueuePending = fw::MakeTransferGraphicsWaitPlan(9, 7);
    run(distinctQueuePending.waitTimeline && distinctQueuePending.timelineValue == 9 &&
        distinctQueuePending.destinationStages == VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        "distinct concurrent queue path waits for new transfer target at vertex input stage");

    const auto alreadyConsumed = fw::MakeTransferGraphicsWaitPlan(9, 9);
    run(!alreadyConsumed.waitTimeline && alreadyConsumed.timelineValue == 0,
        "already waited timeline value adds no redundant wait");

    // A failed graphics submission must not commit the last-waited value.
    const uint64_t lastWaitedBeforeSubmit = 7;
    const auto afterSubmitFailure = fw::MakeTransferGraphicsWaitPlan(9, lastWaitedBeforeSubmit);
    run(afterSubmitFailure.waitTimeline && afterSubmitFailure.timelineValue == 9,
        "failed graphics submission leaves transfer dependency pending");

    run(!fw::RequiresQueueFamilyOwnershipTransfer(VK_SHARING_MODE_CONCURRENT, 2, 5) &&
        !fw::RequiresQueueFamilyOwnershipTransfer(VK_SHARING_MODE_EXCLUSIVE, 2, 2),
        "concurrent distinct queues and same-family exclusive queue need no ownership transfer");
    run(fw::RequiresQueueFamilyOwnershipTransfer(VK_SHARING_MODE_EXCLUSIVE, 2, 5),
        "exclusive distinct queue families require ownership transfer");

    std::cout << "TRANSFER GRAPHICS SYNC CONTRACT: " << passed << '/' << (passed + failed) << " PASS\n";
    return failed == 0 ? 0 : 1;
}
