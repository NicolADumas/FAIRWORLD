#pragma once
#include <windows.h>
#include <string>
#include <functional>

class WindowManager {
public:
    WindowManager();
    ~WindowManager();

    bool Init(int width, int height, const std::string& title);
    bool PollEvents(); // Ritorna false se la finestra viene chiusa
    void Shutdown();

    HWND GetWindowHandle() const { return m_hwnd; }
    
    bool WasWindowResized() const { return m_framebufferResized; }
    void ResetResizeFlag() { m_framebufferResized = false; }
    void SetResizeFlag() { m_framebufferResized = true; }

    void SetRenderCallback(std::function<void()> callback) { m_renderCallback = callback; }
    void InvokeRenderCallback() { if (m_renderCallback) m_renderCallback(); }

private:
    HWND m_hwnd;
    HINSTANCE m_hInstance;
    bool m_framebufferResized = false;
    std::function<void()> m_renderCallback;
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
};
