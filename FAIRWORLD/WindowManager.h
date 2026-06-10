#pragma once
#include <windows.h>
#include <string>

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

private:
    HWND m_hwnd;
    HINSTANCE m_hInstance;
    bool m_framebufferResized = false;
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
};
