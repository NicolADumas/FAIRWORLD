#include "pch.h"
#include "RuntimeManager.h"
#include "SharedContext.h"
#include "RenderManager.h"
#include "TexturePacker.h"
#include "VulkanDmaManager.h"
#include "VramSlabAllocator.h"
#include "JobSystem.h"
#include "FAIRWORLD.h"
#include "MaterialRegistry.h"
#include <iostream>

namespace fw {

RuntimeManager::RuntimeManager(SharedContext* context) : m_context(context) {
    std::cout << "[RuntimeManager] Creato. Nessuna feature attiva.\n";
}

RuntimeManager::~RuntimeManager() {
    std::cout << "[RuntimeManager] Distrutto.\n";
}

void RuntimeManager::RequireFeaturesAsync(RuntimeFeature featureMask) {
    if (m_isLoading.load()) {
        return; // Altre feature sono già in caricamento
    }
    
    // Controlla se le feature richieste sono già attive
    if ((static_cast<uint32_t>(m_activeFeatures) & static_cast<uint32_t>(featureMask)) == static_cast<uint32_t>(featureMask)) {
        return; 
    }

    m_isLoading.store(true);
    
    m_asyncLoadTask = std::async(std::launch::async, [this, featureMask]() {
        if (!HasFeature(static_cast<uint32_t>(m_activeFeatures), RuntimeFeature::GlobalVRAM) && HasFeature(static_cast<uint32_t>(featureMask), RuntimeFeature::GlobalVRAM)) {
            EnsureGlobalVRAM();
        }

        if (!HasFeature(static_cast<uint32_t>(m_activeFeatures), RuntimeFeature::JobSystem) && HasFeature(static_cast<uint32_t>(featureMask), RuntimeFeature::JobSystem)) {
            EnsureJobSystem();
        }

        if (!HasFeature(static_cast<uint32_t>(m_activeFeatures), RuntimeFeature::PBRTextures) && HasFeature(static_cast<uint32_t>(featureMask), RuntimeFeature::PBRTextures)) {
            EnsurePBRTextures();
        }
        
        m_activeFeatures = m_activeFeatures | featureMask;
        m_isLoading.store(false);
    });
}

bool RuntimeManager::IsReady() const {
    return !m_isLoading.load();
}

void RuntimeManager::EnsureGlobalVRAM() {
    std::cout << "[RuntimeManager] Inizializzazione Lazy VRAM Allocator...\n";
    if (m_context && !m_context->dmaManager) {
        m_context->dmaManager = new fw::VulkanDmaManager();
        if (m_context->engine && m_context->engine->GetRenderManager()) {
            auto* rm = m_context->engine->GetRenderManager();
            
            m_context->dmaManager->Initialize(
                rm->GetDevice(),
                rm->GetTransferQueue(),
                rm->GetTransferCommandPool(),
                rm->GetStagingRingBuffer(), // Questo potrebbe essere NULL al momento (allocato lazily)
                rm->GetStagingDeviceMemory(),
                rm->GetMappedStagingData(),
                rm->GetStagingBufferSize(),
                VK_NULL_HANDLE, // globalVramBuffer rimosso
                rm->GetQueueMutex()
            );
        }
    }
    if (m_context && !m_context->vramAllocator) {
        m_context->vramAllocator = new fw::VramSlabAllocator(2048ULL * 1024ULL * 1024ULL); 
        m_context->vramAllocator->SetAllocateCompartmentCallback([this](uint32_t compIdx) {
            if (m_context && m_context->engine && m_context->engine->GetRenderManager()) {
                m_context->engine->GetRenderManager()->AddVramCompartment();
                
                // Aggiorna il DmaManager se lo staging buffer è stato allocato/modificato
                if (m_context->dmaManager) {
                    m_context->dmaManager->UpdateStagingBuffer(
                        m_context->engine->GetRenderManager()->GetStagingRingBuffer(),
                        m_context->engine->GetRenderManager()->GetStagingDeviceMemory(),
                        m_context->engine->GetRenderManager()->GetMappedStagingData()
                    );
                }
            }
        });
    }
}

void RuntimeManager::EnsureJobSystem() {
    std::cout << "[RuntimeManager] Inizializzazione Pool Thread Asincroni...\n";
    if (m_context && !m_context->jobSystem) {
        m_context->jobSystem = new fw::JobSystem();
        m_context->jobSystem->Initialize();
    }
}

void RuntimeManager::EnsurePBRTextures() {
    std::cout << "[RuntimeManager] Avvio TexturePacker PBR...\n";
    if (m_context && m_context->materialRegistry && m_context->engine) {
        if (auto* rm = m_context->engine->GetRenderManager()) {
            fw::TexturePacker packer(512);
            auto pbrData = packer.PackMaterials(m_context->materialRegistry->GetAllMaterials());
            rm->CreatePBRTextures(pbrData);
            std::cout << "[RuntimeManager] Texture PBR caricate e impacchettate con successo.\n";
        }
    }
}

} // namespace fw
