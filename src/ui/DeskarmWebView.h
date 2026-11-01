#pragma once

#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <string>
#include <memory>
#include <atomic>

using namespace Microsoft::WRL;

enum class WebViewStatus {
    Uninitialized,
    Initializing,
    Ready,
    Failed
};

class DeskarmWebView {
private:
    ComPtr<ICoreWebView2Environment> webViewEnv;
    ComPtr<ICoreWebView2Controller> webViewController;
    ComPtr<ICoreWebView2> webView;
    
    HWND m_parentHwnd = nullptr;
    bool m_isVisible = false;
    WebViewStatus m_status = WebViewStatus::Uninitialized;
    
    std::wstring m_pendingUrl;
    std::shared_ptr<std::atomic<bool>> m_lifetimeToken;

public:
    DeskarmWebView();
    ~DeskarmWebView();

    // Lazy init: inizializza solo se Uninitialized; evita duplicate call e gestisce gli stati
    void Init(HWND parentHwnd);
    void EnsureInitialized(HWND parentHwnd);
    void Shutdown();
    
    // Naviga a un URL (es. http://localhost:8000)
    void Navigate(const std::wstring& url);
    
    // Sposta e ridimensiona il browser per sovrapporsi esattamente alla finestra ImGui
    void Resize(int x, int y, int width, int height);
    
    // Mostra o nascondi l'overlay
    void SetVisible(bool visible);
    
    bool IsVisible() const { return m_isVisible; }
    bool IsInitialized() const { return m_status == WebViewStatus::Ready; }
    WebViewStatus GetStatus() const { return m_status; }
    bool IsReady() const { return m_status == WebViewStatus::Ready; }
    bool IsInitializing() const { return m_status == WebViewStatus::Initializing; }
    bool HasFailed() const { return m_status == WebViewStatus::Failed; }
};
