#pragma once

#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <string>

using namespace Microsoft::WRL;

class DeskarmWebView {
private:
    ComPtr<ICoreWebView2Environment> webViewEnv;
    ComPtr<ICoreWebView2Controller> webViewController;
    ComPtr<ICoreWebView2> webView;
    
    HWND m_parentHwnd;
    bool m_isVisible;
    bool m_isInitialized;
    
    std::wstring m_pendingUrl;

public:
    DeskarmWebView();
    ~DeskarmWebView();

    // Inizializza il browser attaccandolo all'HWND del gioco
    void Init(HWND parentHwnd);
    
    // Naviga a un URL (es. http://localhost:8000)
    void Navigate(const std::wstring& url);
    
    // Sposta e ridimensiona il browser per sovrapporsi esattamente alla finestra ImGui
    void Resize(int x, int y, int width, int height);
    
    // Mostra o nascondi l'overlay
    void SetVisible(bool visible);
    
    bool IsVisible() const { return m_isVisible; }
    bool IsInitialized() const { return m_isInitialized; }
};
