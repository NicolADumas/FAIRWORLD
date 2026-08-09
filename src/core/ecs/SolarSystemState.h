#pragma once
#include "State.h"
#include <memory>
#include <vector>
#include <string>

struct SharedContext;

class SolarSystemState : public State {
public:
    SolarSystemState(SharedContext* context);
    ~SolarSystemState() override;

    bool Init() override;
    void Update(float dt) override;
    void Render() override;

private:
    void RefreshPlanetList();

    SharedContext* m_context;
    bool m_isSimulating = true;
    float m_simulationSpeed = 1000.0f; // Moltiplicatore Time-lapse

    std::vector<std::string> m_availablePlanets;
    int m_selectedPlanetIndex = -1;
};
