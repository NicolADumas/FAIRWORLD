#pragma once
#include <vector>

namespace fw {
    struct FrameMetrics {
        float fps;
        float frameTimeMs;
        float physicsTimeMs;
        float entityCountF;
    };

    class DiagnosticsManager {
    public:
        DiagnosticsManager() : m_history(100, 0.0f), m_head(0), m_filled(0), m_totalTime(0.0f) {}

        void PushFrame(const FrameMetrics& metrics) {
            m_currentMetrics = metrics;
            m_history[m_head] = metrics.frameTimeMs;
            m_head = (m_head + 1) % m_history.size();
            if (m_filled < (int)m_history.size()) m_filled++;
            
            m_totalTime += (metrics.frameTimeMs / 1000.0f);
        }
        
        void Update(float dt) {}
        void DrawUI() {}
        
        FrameMetrics GetCurrentMetrics() const { return m_currentMetrics; }
        
        const std::vector<float>& GetFrameTimeHistory() const { return m_history; }
        
        float GetAverageFPS() const { 
            if (m_filled == 0) return 0.0f;
            float sum = 0.0f;
            for (int i = 0; i < m_filled; ++i) sum += m_history[i];
            float avgMs = sum / m_filled;
            return avgMs > 0.0f ? (1000.0f / avgMs) : 0.0f;
        }
        
        const float* GetFrameTimeRawData() const { return m_history.data(); }
        int GetFilledCount() const { return m_filled; }
        int GetHeadIndex() const { return m_head; }
        float GetTotalTime() const { return m_totalTime; }
        
    private:
        FrameMetrics m_currentMetrics = {};
        std::vector<float> m_history;
        int m_head;
        int m_filled;
        float m_totalTime;
    };
}
