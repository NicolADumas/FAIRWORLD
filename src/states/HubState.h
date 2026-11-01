#pragma once
#include "State.h"

struct SharedContext;

class HubState : public State {
public:
    explicit HubState(SharedContext* context);
    ~HubState() override;

    bool Init() override;
    void Update(float dt) override;
    void Render() override;

    // HubState e' una dashboard UI ImGui statica: non richiede simulazione 3D o fisica continua
    bool RequiresContinuousUpdate() const override { return false; }

private:
    SharedContext* m_context;
    float m_simulatedTimeAccumulator;
};
