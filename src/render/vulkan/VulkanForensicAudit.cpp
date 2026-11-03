#include "pch.h"
#include "VulkanForensicAudit.h"
#include "../VulkanResourceTracker.h"
#include <windows.h>
#include <dbghelp.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <unordered_map>
#include <atomic>

namespace fw::forensic {

static Phase s_currentPhase = Phase::Startup;
static std::mutex s_forensicMutex;
static uint32_t s_framebufferCallCount = 0;
static uint32_t s_semaphoreCreateCount = 0;
static uint32_t s_semaphoreDestroyCount = 0;
static std::atomic<uint32_t> s_validationErrorCount{ 0 };
static VkDebugUtilsMessengerEXT s_debugMessenger = VK_NULL_HANDLE;

struct LiveSemaphoreRecord {
    std::string caller;
    std::string name;
    std::string owner;
    VkDevice device;
    Phase phase;
    std::string callstack;
    uint32_t createId;
};

struct LiveFramebufferRecord {
    std::string caller;
    VkRenderPass renderPass;
    Phase phase;
};

static std::unordered_map<VkSemaphore, LiveSemaphoreRecord> s_liveSemaphores;
static std::unordered_map<VkFramebuffer, LiveFramebufferRecord> s_liveFramebuffers;

void SetPhase(Phase phase) {
    std::lock_guard<std::mutex> lock(s_forensicMutex);
    s_currentPhase = phase;
    std::cerr << "[VULKAN FORENSIC PHASE] -> " << GetPhaseName(phase) << "\n";
}

const char* GetPhaseName(Phase phase) {
    switch (phase) {
        case Phase::Startup: return "Startup";
        case Phase::Runtime: return "Runtime";
        case Phase::Resize:  return "Resize";
        case Phase::Shutdown: return "Shutdown";
        default: return "Unknown";
    }
}

Phase GetPhase() {
    std::lock_guard<std::mutex> lock(s_forensicMutex);
    return s_currentPhase;
}

std::string CaptureCallstack(int skipFrames, int maxFrames) {
    void* stack[32];
    WORD count = CaptureStackBackTrace(skipFrames + 1, (maxFrames > 30 ? 30 : maxFrames), stack, NULL);

    static HMODULE hDbgHelp = LoadLibraryA("dbghelp.dll");
    typedef BOOL (WINAPI *PFN_SymInitialize)(HANDLE, PCSTR, BOOL);
    typedef BOOL (WINAPI *PFN_SymFromAddr)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
    static PFN_SymInitialize pfnSymInit = hDbgHelp ? (PFN_SymInitialize)GetProcAddress(hDbgHelp, "SymInitialize") : nullptr;
    static PFN_SymFromAddr pfnSymFromAddr = hDbgHelp ? (PFN_SymFromAddr)GetProcAddress(hDbgHelp, "SymFromAddr") : nullptr;
    static bool symInitialized = false;

    HANDLE process = GetCurrentProcess();
    if (pfnSymInit && !symInitialized) {
        pfnSymInit(process, NULL, TRUE);
        symInitialized = true;
    }

    std::ostringstream oss;
    for (WORD i = 0; i < count; ++i) {
        DWORD64 address = (DWORD64)(stack[i]);
        if (pfnSymFromAddr && symInitialized) {
            char buffer[sizeof(SYMBOL_INFO) + 256 * sizeof(TCHAR)];
            PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
            pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            pSymbol->MaxNameLen = 255;
            DWORD64 displacement = 0;
            if (pfnSymFromAddr(process, address, &displacement, pSymbol)) {
                oss << "    [" << i << "] " << pSymbol->Name << " + 0x" << std::hex << displacement << std::dec << "\n";
                continue;
            }
        }
        HMODULE hMod = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)address, &hMod);
        char modName[MAX_PATH] = "unknown";
        if (hMod) GetModuleFileNameA(hMod, modName, MAX_PATH);
        const char* baseName = strrchr(modName, '\\');
        baseName = baseName ? baseName + 1 : modName;
        oss << "    [" << i << "] " << baseName << "!0x" << std::hex << address << std::dec << "\n";
    }
    return oss.str();
}

VkResult InterceptCreateFramebuffer(
    const char* caller,
    VkDevice device,
    const VkFramebufferCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkFramebuffer* pFramebuffer
) {
    std::lock_guard<std::mutex> lock(s_forensicMutex);
    s_framebufferCallCount++;
    Phase phase = s_currentPhase;

    std::cerr << "\n[VULKAN FORENSIC][vkCreateFramebuffer #" << s_framebufferCallCount << "]\n";
    std::cerr << "  Caller      : " << (caller ? caller : "unknown") << "\n";
    std::cerr << "  Phase       : " << GetPhaseName(phase) << "\n";
    std::cerr << "  Device      : 0x" << std::hex << (uint64_t)device << std::dec << "\n";
    std::cerr << "  RenderPass  : 0x" << std::hex << (uint64_t)(pCreateInfo ? pCreateInfo->renderPass : 0) << std::dec;
    if (!pCreateInfo || pCreateInfo->renderPass == VK_NULL_HANDLE) {
        std::cerr << " *** [CRITICAL ERROR: VK_NULL_HANDLE DETECTED!] ***\n";
    } else {
        std::cerr << "\n";
    }

    if (pCreateInfo) {
        std::cerr << "  Dimensions  : " << pCreateInfo->width << "x" << pCreateInfo->height << " layers=" << pCreateInfo->layers << "\n";
        std::cerr << "  Attachments : " << pCreateInfo->attachmentCount << " attachments:\n";
        for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
            std::cerr << "    [" << i << "] ImageView: 0x" << std::hex << (uint64_t)pCreateInfo->pAttachments[i] << std::dec << "\n";
        }
    }

    std::string stack = CaptureCallstack(1, 8);
    std::cerr << "  Callstack:\n" << stack;

    VkResult res = vkCreateFramebuffer(device, pCreateInfo, pAllocator, pFramebuffer);
    std::cerr << "  Result      : " << res << " -> Handle: 0x" << std::hex << (pFramebuffer ? (uint64_t)(*pFramebuffer) : 0) << std::dec << "\n\n";

    if (res == VK_SUCCESS && pFramebuffer && *pFramebuffer != VK_NULL_HANDLE) {
        s_liveFramebuffers[*pFramebuffer] = { caller ? caller : "unknown", pCreateInfo ? pCreateInfo->renderPass : VK_NULL_HANDLE, phase };
    }
    return res;
}

void InterceptDestroyFramebuffer(
    const char* caller,
    VkDevice device,
    VkFramebuffer framebuffer,
    const VkAllocationCallbacks* pAllocator
) {
    if (framebuffer == VK_NULL_HANDLE) return;
    std::lock_guard<std::mutex> lock(s_forensicMutex);
    Phase phase = s_currentPhase;

    std::cerr << "\n[VULKAN FORENSIC][vkDestroyFramebuffer]\n";
    std::cerr << "  Handle      : 0x" << std::hex << (uint64_t)framebuffer << std::dec << "\n";
    std::cerr << "  Caller      : " << (caller ? caller : "unknown") << "\n";
    std::cerr << "  Phase       : " << GetPhaseName(phase) << "\n";
    std::cerr << "  Device      : 0x" << std::hex << (uint64_t)device << std::dec << "\n";

    auto it = s_liveFramebuffers.find(framebuffer);
    if (it != s_liveFramebuffers.end()) {
        std::cerr << "  Tracking    : MATCH FOUND (Original Caller: " << it->second.caller << ")\n";
        s_liveFramebuffers.erase(it);
    } else {
        std::cerr << "  Tracking    : [NOTE: Framebuffer was not in active tracked set]\n";
    }

    vkDestroyFramebuffer(device, framebuffer, pAllocator);
}

VkResult InterceptCreateSemaphore(
    const char* caller,
    const char* name,
    const char* owner,
    VkDevice device,
    const VkSemaphoreCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSemaphore* pSemaphore
) {
    std::lock_guard<std::mutex> lock(s_forensicMutex);
    s_semaphoreCreateCount++;
    Phase phase = s_currentPhase;

    VkResult res = vkCreateSemaphore(device, pCreateInfo, pAllocator, pSemaphore);

    std::string stack = CaptureCallstack(1, 8);
    uint64_t semHandle = pSemaphore ? (uint64_t)(*pSemaphore) : 0;

    std::cerr << "\n[VULKAN FORENSIC][vkCreateSemaphore #" << s_semaphoreCreateCount << "]\n";
    std::cerr << "  Handle      : 0x" << std::hex << semHandle << std::dec << "\n";
    std::cerr << "  Caller      : " << (caller ? caller : "unknown") << "\n";
    std::cerr << "  Owner       : " << (owner ? owner : "unknown") << "\n";
    std::cerr << "  Name        : " << (name ? name : "unknown") << "\n";
    std::cerr << "  Phase       : " << GetPhaseName(phase) << "\n";
    std::cerr << "  Device      : 0x" << std::hex << (uint64_t)device << std::dec << "\n";
    std::cerr << "  Result      : " << res << "\n";
    std::cerr << "  Callstack:\n" << stack << "\n";

    if (res == VK_SUCCESS && pSemaphore && *pSemaphore != VK_NULL_HANDLE) {
        s_liveSemaphores[*pSemaphore] = { caller ? caller : "unknown", name ? name : "unknown", owner ? owner : "unknown", device, phase, stack, s_semaphoreCreateCount };
        fw::VulkanResourceTracker::Get().TrackCreate(*pSemaphore, name ? name : "AutoDispatched", owner ? owner : "VulkanHook");
    }
    return res;
}

void InterceptDestroySemaphore(
    const char* caller,
    VkDevice device,
    VkSemaphore semaphore,
    const VkAllocationCallbacks* pAllocator
) {
    if (semaphore == VK_NULL_HANDLE) return;
    std::lock_guard<std::mutex> lock(s_forensicMutex);
    s_semaphoreDestroyCount++;
    Phase phase = s_currentPhase;

    std::cerr << "\n[VULKAN FORENSIC][vkDestroySemaphore #" << s_semaphoreDestroyCount << "]\n";
    std::cerr << "  Handle      : 0x" << std::hex << (uint64_t)semaphore << std::dec << "\n";
    std::cerr << "  Caller      : " << (caller ? caller : "unknown") << "\n";
    std::cerr << "  Phase       : " << GetPhaseName(phase) << "\n";
    std::cerr << "  Device      : 0x" << std::hex << (uint64_t)device << std::dec << "\n";

    auto it = s_liveSemaphores.find(semaphore);
    if (it != s_liveSemaphores.end()) {
        std::cerr << "  Tracking    : MATCH FOUND (Original Owner: " << it->second.owner 
                  << ", Name: " << it->second.name << ", CreateID: #" << it->second.createId << ")\n";
        s_liveSemaphores.erase(it);
    } else {
        std::cerr << "  Tracking    : [NOTE: Semaphore was not in s_liveSemaphores]\n";
    }

    fw::VulkanResourceTracker::Get().TrackDestroy(semaphore);
    vkDestroySemaphore(device, semaphore, pAllocator);
}

void InterceptDestroyDevice(
    const char* caller,
    VkDevice device,
    const VkAllocationCallbacks* pAllocator
) {
    std::lock_guard<std::mutex> lock(s_forensicMutex);
    std::cerr << "\n============================================================\n";
    std::cerr << " [VULKAN FORENSIC AUDIT] FINAL REPORT AT vkDestroyDevice\n";
    std::cerr << "============================================================\n";
    std::cerr << "Caller               : " << (caller ? caller : "unknown") << "\n";
    std::cerr << "Device               : 0x" << std::hex << (uint64_t)device << std::dec << "\n";
    std::cerr << "Total Semaphores Cr. : " << s_semaphoreCreateCount << "\n";
    std::cerr << "Total Semaphores Des.: " << s_semaphoreDestroyCount << "\n";
    std::cerr << "LIVE SEMAPHORES REMAINING: " << s_liveSemaphores.size() << "\n";

    if (!s_liveSemaphores.empty()) {
        std::cerr << "\n*** WARNING: The following semaphores were NOT destroyed before vkDestroyDevice: ***\n";
        for (const auto& pair : s_liveSemaphores) {
            std::cerr << "  Handle    : 0x" << std::hex << (uint64_t)pair.first << std::dec << "\n";
            std::cerr << "  Owner     : " << pair.second.owner << "\n";
            std::cerr << "  Name      : " << pair.second.name << "\n";
            std::cerr << "  Caller    : " << pair.second.caller << "\n";
            std::cerr << "  Phase     : " << GetPhaseName(pair.second.phase) << "\n";
            std::cerr << "  Creation Callstack:\n" << pair.second.callstack << "\n";
        }
    } else {
        std::cerr << "All tracked semaphores were cleanly destroyed!\n";
    }
    std::cerr << "============================================================\n\n";
}

void DumpForensicReport() {
    std::lock_guard<std::mutex> lock(s_forensicMutex);
    std::cerr << "\n[VULKAN FORENSIC AUDIT] Intermediate Report:\n";
    std::cerr << "  Phase: " << GetPhaseName(s_currentPhase) << "\n";
    std::cerr << "  Framebuffers created: " << s_framebufferCallCount << ", currently live: " << s_liveFramebuffers.size() << "\n";
    std::cerr << "  Semaphores created: " << s_semaphoreCreateCount << ", destroyed: " << s_semaphoreDestroyCount << ", currently live: " << s_liveSemaphores.size() << "\n";
    std::cerr << "  Validation Layer Errors: " << s_validationErrorCount.load() << "\n";
}

uint32_t GetValidationErrorCount() {
    return s_validationErrorCount.load();
}

void ResetValidationErrorCount() {
    s_validationErrorCount.store(0);
}

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        s_validationErrorCount.fetch_add(1);
        std::cerr << "\n============================================================\n";
        std::cerr << " [VULKAN VALIDATION LAYER ERROR #" << s_validationErrorCount.load() << "]\n";
        std::cerr << "============================================================\n";
        if (pCallbackData->pMessageIdName) {
            std::cerr << "  Message ID : " << pCallbackData->pMessageIdName << "\n";
        }
        if (pCallbackData->pMessage) {
            std::cerr << "  Details    : " << pCallbackData->pMessage << "\n";
        }
        std::cerr << "  Phase      : " << GetPhaseName(s_currentPhase) << "\n";
        if (pCallbackData->objectCount > 0) {
            std::cerr << "  Objects (" << pCallbackData->objectCount << "):\n";
            for (uint32_t i = 0; i < pCallbackData->objectCount; i++) {
                std::cerr << "    [" << i << "] Type: " << pCallbackData->pObjects[i].objectType
                          << ", Handle: 0x" << std::hex << pCallbackData->pObjects[i].objectHandle << std::dec;
                if (pCallbackData->pObjects[i].pObjectName) {
                    std::cerr << " (" << pCallbackData->pObjects[i].pObjectName << ")";
                }
                std::cerr << "\n";
            }
        }
        std::string stack = CaptureCallstack(1, 14);
        std::cerr << "  Real Callstack at Validation Fault:\n" << stack;
        std::cerr << "============================================================\n\n";
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "[VULKAN VALIDATION WARNING] " << (pCallbackData->pMessage ? pCallbackData->pMessage : "") << "\n";
    }
    return VK_FALSE;
}

void SetupDebugMessenger(VkInstance instance) {
    if (instance == VK_NULL_HANDLE) return;
    std::lock_guard<std::mutex> lock(s_forensicMutex);
    if (s_debugMessenger != VK_NULL_HANDLE) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = DebugUtilsCallback;

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        VkResult res = func(instance, &createInfo, nullptr, &s_debugMessenger);
        if (res == VK_SUCCESS) {
            std::cerr << "[VULKAN FORENSIC] VkDebugUtilsMessengerEXT successfully registered on VkInstance: 0x" 
                      << std::hex << (uint64_t)instance << std::dec << "\n";
        } else {
            std::cerr << "[VULKAN FORENSIC] Failed to register VkDebugUtilsMessengerEXT, VkResult=" << res << "\n";
        }
    } else {
        std::cerr << "[VULKAN FORENSIC] vkCreateDebugUtilsMessengerEXT proc not found on instance.\n";
    }
}

void DestroyDebugMessenger(VkInstance instance) {
    std::lock_guard<std::mutex> lock(s_forensicMutex);
    if (s_debugMessenger != VK_NULL_HANDLE && instance != VK_NULL_HANDLE) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) {
            func(instance, s_debugMessenger, nullptr);
            std::cerr << "[VULKAN FORENSIC] VkDebugUtilsMessengerEXT cleanly destroyed on instance 0x"
                      << std::hex << (uint64_t)instance << std::dec << "\n";
        }
        s_debugMessenger = VK_NULL_HANDLE;
    }
}

} // namespace fw::forensic
