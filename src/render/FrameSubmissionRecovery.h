#pragma once

#include <vulkan/vulkan.h>

namespace fw {

enum class FrameSubmissionRecoveryState {
    DeviceLost,
    RecoverySucceeded,
    RecoveryFailed
};

struct FrameSubmissionRecoveryDecision {
    FrameSubmissionRecoveryState state;
    bool attemptRecovery;
    bool acquireSemaphoreConsumed;
    bool frameFenceWillSignal;
    bool mayWaitForFrameFence;
};

inline FrameSubmissionRecoveryDecision DecideFrameSubmissionRecovery(
    VkResult primarySubmitResult,
    VkResult recoverySubmitResult = VK_SUCCESS) {
    if (primarySubmitResult == VK_ERROR_DEVICE_LOST || recoverySubmitResult == VK_ERROR_DEVICE_LOST) {
        return { FrameSubmissionRecoveryState::DeviceLost, false, false, false, false };
    }
    if (primarySubmitResult == VK_SUCCESS) {
        return { FrameSubmissionRecoveryState::RecoverySucceeded, false, true, true, true };
    }
    if (recoverySubmitResult == VK_SUCCESS) {
        return { FrameSubmissionRecoveryState::RecoverySucceeded, true, true, true, true };
    }
    return { FrameSubmissionRecoveryState::RecoveryFailed, true, false, false, false };
}

template <typename RecoverySubmit>
inline FrameSubmissionRecoveryDecision RunFrameSubmissionRecovery(
    VkResult primarySubmitResult,
    RecoverySubmit&& submitRecovery) {
    if (primarySubmitResult == VK_SUCCESS || primarySubmitResult == VK_ERROR_DEVICE_LOST) {
        return DecideFrameSubmissionRecovery(primarySubmitResult);
    }

    return DecideFrameSubmissionRecovery(primarySubmitResult, submitRecovery());
}

} // namespace fw
