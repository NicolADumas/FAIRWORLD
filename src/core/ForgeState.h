#pragma once
#include "State.h"

struct SharedContext;
namespace fw {
    class ForgeWorld;
}

class ForgeState : public State {
public:
    explicit ForgeState(SharedContext* context);
    ~ForgeState() override;

    std::expected<void, std::string> Init() override;
    void Update(float dt) override;
    void Render() override;

private:
    SharedContext* m_context;
};
