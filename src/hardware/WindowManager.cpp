#include "pch.h"
#include "WindowManager.h"
#include <iostream>
#include <imgui.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WindowManager::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam))
        return true;

    WindowManager* wm = reinterpret_cast<WindowManager*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (uMsg) {
        case WM_ENTERSIZEMOVE:
            SetTimer(hwnd, 1, 16, NULL); // Timer a ~60fps
            return 0;
        case WM_EXITSIZEMOVE:
            KillTimer(hwnd, 1);
            return 0;
        case WM_TIMER:
            return 0;
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED && wm) {
                wm->SetResizeFlag();
                // NON renderizzare durante WM_SIZE per evitare freeze del Swapchain
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}


WindowManager::WindowManager() : m_hwnd(nullptr), m_hInstance(GetModuleHandle(nullptr)) {}
WindowManager::~WindowManager() { Shutdown(); }

bool WindowManager::Init(int width, int height, const std::string& title) {
    const char* CLASS_NAME = "FairWorldDesktopClass";

    WNDCLASS wc = { };
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = m_hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    m_hwnd = CreateWindowEx(
        0, CLASS_NAME, title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        nullptr, nullptr, m_hInstance, nullptr
    );

    if (m_hwnd == nullptr) {
        return false;
    }

    SetWindowLongPtr(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    ShowWindow(m_hwnd, SW_SHOW);
    std::cout << "[SYSTEM] Finestra Desktop creata con successo." << std::endl;
    return true;
}

bool WindowManager::PollEvents() {
    MSG msg = { };
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return true;
}

void WindowManager::Shutdown() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}
