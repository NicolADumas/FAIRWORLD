#pragma once
#include <glm/glm.hpp>

namespace fw {

    class TimeManager {
    public:
        float GetTimeOfDay() const { return m_timeOfDay; }
        void SetTimeOfDay(float time) { m_timeOfDay = time; }
        
        float GetMoonPhase() const { return m_moonPhase; }
        int GetCurrentDay() const { return m_currentDay; }
        
        void Initialize() {
            m_timeOfDay = 8.0f;
            m_currentDay = 1;
            m_moonPhase = 0.5f;
        }

        void Update(float dt) {
            float inGameHoursPerRealSecond = 1.0f / 60.0f; 
            m_timeOfDay += dt * inGameHoursPerRealSecond * m_timeScale;
            if (m_timeOfDay >= 24.0f) {
                m_timeOfDay -= 24.0f;
                AdvanceDay(1);
            }
        }

        void AdvanceTimeManual(float amount) {
            m_timeOfDay += amount;
            while (m_timeOfDay >= 24.0f) {
                m_timeOfDay -= 24.0f;
                AdvanceDay(1);
            }
            while (m_timeOfDay < 0.0f) {
                m_timeOfDay += 24.0f;
                AdvanceDay(-1);
            }
        }

        void AdvanceDay(int days) {
            m_currentDay += days;
            m_moonPhase += 0.03f * days;
            if (m_moonPhase > 1.0f) m_moonPhase -= 1.0f;
            if (m_moonPhase < 0.0f) m_moonPhase += 1.0f;
        }
        
        glm::vec3 GetSkyColor() const { 
            glm::vec3 colorNight(0.02f, 0.02f, 0.05f);
            glm::vec3 colorDawn(0.8f, 0.4f, 0.2f);
            glm::vec3 colorDay(0.4f, 0.6f, 0.9f);
            glm::vec3 colorSunset(0.8f, 0.3f, 0.1f);
            
            if (m_timeOfDay < 4.0f) return colorNight;
            if (m_timeOfDay < 6.0f) {
                float t = (m_timeOfDay - 4.0f) / 2.0f;
                return glm::mix(colorNight, colorDawn, t);
            }
            if (m_timeOfDay < 8.0f) {
                float t = (m_timeOfDay - 6.0f) / 2.0f;
                return glm::mix(colorDawn, colorDay, t);
            }
            if (m_timeOfDay < 18.0f) return colorDay;
            if (m_timeOfDay < 20.0f) {
                float t = (m_timeOfDay - 18.0f) / 2.0f;
                return glm::mix(colorDay, colorSunset, t);
            }
            if (m_timeOfDay < 22.0f) {
                float t = (m_timeOfDay - 20.0f) / 2.0f;
                return glm::mix(colorSunset, colorNight, t);
            }
            return colorNight;
        }

    private:
        float m_timeOfDay = 12.0f;
        int m_currentDay = 1;
        float m_moonPhase = 0.5f;
        float m_timeScale = 60.0f;
    };

}
