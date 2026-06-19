#include "pch.h"
#include "DeskarmWebView.h"
#include <iostream>

DeskarmWebView::DeskarmWebView() : m_parentHwnd(nullptr), m_isVisible(false), m_isInitialized(false) {}

DeskarmWebView::~DeskarmWebView() {
    if (webViewController) {
        webViewController->Close();
    }
}

void DeskarmWebView::Init(HWND parentHwnd) {
    if (m_isInitialized) return;
    m_parentHwnd = parentHwnd;

    // CreateCoreWebView2EnvironmentWithOptions è la entry point di WebView2.
    // Usiamo WRL per passare la lambda callback che verrà chiamata asincronamente.
    CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result)) {
                    std::cerr << "[WebView2] Impossibile creare l'ambiente. Assicurati che Edge WebView2 Runtime sia installato." << std::endl;
                    return result;
                }
                
                webViewEnv = env;
                webViewEnv->CreateCoreWebView2Controller(m_parentHwnd, 
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (controller != nullptr) {
                                webViewController = controller;
                                webViewController->get_CoreWebView2(&webView);
                                
                                m_isInitialized = true;
                                std::cout << "[WebView2] Motore Browser integrato con successo!" << std::endl;
                                
                                // Rimuoviamo i bordi per farlo sembrare nativo
                                // (WebView2 si comporta come un HWND child puro)
                                
                                webViewController->put_IsVisible(m_isVisible ? TRUE : FALSE);
                                
                                // Naviga verso l'URL se era stato richiesto prima dell'inizializzazione
                                if (!m_pendingUrl.empty()) {
                                    webView->Navigate(m_pendingUrl.c_str());
                                    m_pendingUrl.clear();
                                }
                            }
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
}

void DeskarmWebView::Navigate(const std::wstring& url) {
    if (m_isInitialized && webView) {
        webView->Navigate(url.c_str());
    } else {
        // Se non è ancora pronto, lo mettiamo in coda
        m_pendingUrl = url;
    }
}

void DeskarmWebView::Resize(int x, int y, int width, int height) {
    if (webViewController) {
        RECT bounds;
        bounds.left = x;
        bounds.top = y;
        bounds.right = x + width;
        bounds.bottom = y + height;
        webViewController->put_Bounds(bounds);
    }
}

void DeskarmWebView::SetVisible(bool visible) {
    m_isVisible = visible;
    if (webViewController) {
        webViewController->put_IsVisible(visible ? TRUE : FALSE);
    }
}
