#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace fw {

struct TransferGraphicsWaitPlan {
    bool waitTimeline = false;
    uint64_t timelineValue = 0;
    VkPipelineStageFlags destinationStages = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
};

inline TransferGraphicsWaitPlan MakeTransferGraphicsWaitPlan(uint64_t latestSubmittedTransfer,
    uint64_t lastGraphicsWaitedTransfer) {
    TransferGraphicsWaitPlan plan;
    plan.waitTimeline = latestSubmittedTransfer > lastGraphicsWaitedTransfer;
    plan.timelineValue = plan.waitTimeline ? latestSubmittedTransfer : 0;
    return plan;
}

inline bool RequiresQueueFamilyOwnershipTransfer(VkSharingMode sharingMode,
    uint32_t transferFamily, uint32_t graphicsFamily) {
    return sharingMode == VK_SHARING_MODE_EXCLUSIVE && transferFamily != graphicsFamily;
}

} // namespace fw
