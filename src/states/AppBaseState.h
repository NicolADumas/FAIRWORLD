#pragma once
#include "State.h"
#include "SharedContext.h"
#include "GameWorld.h"
#include <memory>
#include <string>

// Architettura Madre: classe base universale per tutte le App dell'OS FAIRWORLD
class AppBaseState : public State {
public:
    explicit AppBaseState(SharedContext* context);
    ~AppBaseState() override;

    bool Init() override final;
    void Update(float dt) override final;
    void Render() override final;

protected:
    // --- ARCHITETTURA MADRE (Funzioni e componenti universali dell'OS) ---
    // Gestisce l'header madre (pulsante "Torna all'Hub"). Ritorna true se si è chiusa l'app.
    bool DrawMotherHeader(const char* appTitle);
    void ReturnToHub();

    // --- ARCHITETTURA SPECIFICA (Da implementare nella singola App) ---
    virtual bool InitApp() = 0;
    virtual void UpdateApp(float dt) = 0;
    virtual void RenderApp() = 0;

    SharedContext* m_context;
    std::unique_ptr<fw::GameWorld> m_previewWorld;
};
