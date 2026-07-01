#pragma once
#include <glm/glm.hpp>

namespace fw {

    class TimeManager {
    public:
        float GetTimeOfDay() const { return m_timeOfDay; }
        void SetTimeOfDay(float time) { m_timeOfDay = time; }
        
        float GetMoonPhase() const { return 0.5f; }
        int GetCurrentDay() const { return 1; }
        
        void Initialize() {}
        void Update(float dt) {}
        void AdvanceTimeManual(float amount) {}
        void AdvanceDay(int days) {}
        
        glm::vec3 GetSkyColor() const { 
            return glm::vec3(0.4f, 0.6f, 0.9f); // sky blue
        }

    private:
        float m_timeOfDay = 12.0f;
    };

}
