#pragma once
#include "State.h"

struct SharedContext;

class HubState : public State {
public:
    explicit HubState(SharedContext* context);
    ~HubState() override;

    std::expected<void, std::string> Init() override;
    void Update(float dt) override;
    void Render() override;

private:
    SharedContext* m_context;
    float m_simulatedTimeAccumulator;
};
