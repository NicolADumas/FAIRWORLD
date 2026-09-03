#pragma once
#include <memory>
#include <future>
#include <atomic>

struct SharedContext;

namespace fw {

#include <cstdint>

enum class RuntimeFeature : uint32_t {
    None           = 0,
    GlobalVRAM     = 1 << 0, // Mega-Buffer da 2GB e Staging Ring
    JobSystem      = 1 << 1, // Pool di Thread asincroni
    PBRTextures    = 1 << 2, // Esecuzione TexturePacker
    PhysicsEngine  = 1 << 3, // Inizializzazione Mondo Fisico (Futuro)
    WorldSim       = 1 << 4  // Simulazione Meteo, AI Globale (Futuro)
};

inline RuntimeFeature operator|(RuntimeFeature a, RuntimeFeature b) {
    return static_cast<RuntimeFeature>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline RuntimeFeature operator&(RuntimeFeature a, RuntimeFeature b) {
    return static_cast<RuntimeFeature>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool HasFeature(uint32_t mask, RuntimeFeature feature) {
    return (mask & static_cast<uint32_t>(feature)) != 0;
}

class RuntimeManager {
public:
    RuntimeManager(SharedContext* context);
    ~RuntimeManager();

    // Avvia l'attivazione di specifiche feature in background (se non già attive).
    void RequireFeaturesAsync(RuntimeFeature featureMask);
    
    // Controlla se l'operazione in background è terminata
    bool IsReady() const;

    RuntimeFeature GetActiveFeatures() const { return m_activeFeatures; }

private:
    void EnsureGlobalVRAM();
    void EnsureJobSystem();
    void EnsurePBRTextures();

    SharedContext* m_context = nullptr;
    RuntimeFeature m_activeFeatures = RuntimeFeature::None;
    
    std::future<void> m_asyncLoadTask;
    std::atomic<bool> m_isLoading{false};
};

} // namespace fw
