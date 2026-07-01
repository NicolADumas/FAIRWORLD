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
        void PushFrame(const FrameMetrics& metrics) {}
        void Update(float dt) {}
        void DrawUI() {}
        
        FrameMetrics GetCurrentMetrics() const { return {60.0f, 16.0f, 8.0f, 8.0f}; }
        
        const std::vector<float>& GetFrameTimeHistory() const { return m_history; }
        
        float GetAverageFPS() const { return 60.0f; }
        const float* GetFrameTimeRawData() const { return m_history.data(); }
        int GetFilledCount() const { return 1; }
        int GetHeadIndex() const { return 0; }
        float GetTotalTime() const { return 0.0f; }
        
    private:
        std::vector<float> m_history = { 16.0f };
    };
}
