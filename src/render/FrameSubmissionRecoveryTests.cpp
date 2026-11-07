#include "FrameSubmissionRecovery.h"

#include <iostream>

namespace {
bool Check(bool condition, const char* name) {
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    return condition;
}
}

int main() {
    int failed = 0;

    int deviceLostRecoveryCalls = 0;
    const auto deviceLost = fw::RunFrameSubmissionRecovery(VK_ERROR_DEVICE_LOST, [&]() {
        ++deviceLostRecoveryCalls;
        return VK_SUCCESS;
    });
    failed += !Check(deviceLost.state == fw::FrameSubmissionRecoveryState::DeviceLost &&
                     !deviceLost.attemptRecovery && !deviceLost.mayWaitForFrameFence &&
                     deviceLostRecoveryCalls == 0,
                     "device lost skips recovery and fence wait");

    int successfulRecoveryCalls = 0;
    const auto recoverySucceeded = fw::RunFrameSubmissionRecovery(VK_ERROR_OUT_OF_HOST_MEMORY, [&]() {
        ++successfulRecoveryCalls;
        return VK_SUCCESS;
    });
    failed += !Check(recoverySucceeded.state == fw::FrameSubmissionRecoveryState::RecoverySucceeded &&
                     recoverySucceeded.attemptRecovery && recoverySucceeded.acquireSemaphoreConsumed &&
                     recoverySucceeded.frameFenceWillSignal && recoverySucceeded.mayWaitForFrameFence &&
                     successfulRecoveryCalls == 1,
                     "successful recovery consumes acquire semaphore and signals fence");

    int failedRecoveryCalls = 0;
    const auto recoveryFailed = fw::RunFrameSubmissionRecovery(VK_ERROR_OUT_OF_HOST_MEMORY, [&]() {
        ++failedRecoveryCalls;
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    });
    failed += !Check(recoveryFailed.state == fw::FrameSubmissionRecoveryState::RecoveryFailed &&
                     recoveryFailed.attemptRecovery && !recoveryFailed.acquireSemaphoreConsumed &&
                     !recoveryFailed.frameFenceWillSignal && !recoveryFailed.mayWaitForFrameFence &&
                     failedRecoveryCalls == 1,
                     "failed recovery quarantines acquire semaphore and skips unsignaled fence");

    std::cout << "TOTAL: 3, PASSED: " << (3 - failed) << ", FAILED: " << failed << '\n';
    return failed == 0 ? 0 : 1;
}
