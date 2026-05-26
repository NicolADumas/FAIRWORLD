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

private:
    HWND m_hwnd;
    HINSTANCE m_hInstance;
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
};
