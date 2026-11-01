#include "pch.h"
#include "DeskarmWebView.h"
#include <iostream>
#include <chrono>
#include <iomanip>

DeskarmWebView::DeskarmWebView() 
    : m_parentHwnd(nullptr), m_isVisible(false), m_status(WebViewStatus::Uninitialized),
      m_lifetimeToken(std::make_shared<std::atomic<bool>>(true)) {}

DeskarmWebView::~DeskarmWebView() {
    if (m_lifetimeToken) {
        m_lifetimeToken->store(false);
    }
    Shutdown();
}

void DeskarmWebView::Shutdown() {
    if (m_lifetimeToken) {
        m_lifetimeToken->store(false);
    }
    m_lifetimeToken = std::make_shared<std::atomic<bool>>(true);
    if (webViewController) {
        webViewController->Close();
        webViewController = nullptr;
    }
    webView = nullptr;
    webViewEnv = nullptr;
    m_status = WebViewStatus::Uninitialized;
    m_isVisible = false;
    m_parentHwnd = nullptr;
}

void DeskarmWebView::EnsureInitialized(HWND parentHwnd) {
    Init(parentHwnd);
}

void DeskarmWebView::Init(HWND parentHwnd) {
    if (m_status == WebViewStatus::Ready || m_status == WebViewStatus::Initializing) {
        return; // Inizializzazione già completata o in corso: nessuna duplicazione
    }
    if (!parentHwnd) {
        std::cerr << "[WebView2] Impossibile inizializzare: HWND nullo.\n";
        m_status = WebViewStatus::Failed;
        return;
    }

    m_parentHwnd = parentHwnd;
    m_status = WebViewStatus::Initializing;
    auto initStartTime = std::chrono::steady_clock::now();
    std::cout << "[WebView2] Inizializzazione on-demand (lazy) avviata...\n";

    auto token = m_lifetimeToken;

    // CreateCoreWebView2EnvironmentWithOptions è la entry point asincrona di WebView2.
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, token, initStartTime](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (!token->load()) {
                    return S_OK; // Oggetto distrutto o dismesso: interrompi senza accedere a 'this'
                }
                if (FAILED(result) || !env) {
                    std::cerr << "[WebView2] Creazione ambiente fallita (hr=" << std::hex << result << std::dec 
                              << "). Verificare la presenza di Microsoft Edge WebView2 Runtime.\n";
                    m_status = WebViewStatus::Failed;
                    return result;
                }
                
                webViewEnv = env;
                webViewEnv->CreateCoreWebView2Controller(m_parentHwnd, 
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, token, initStartTime](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                            if (!token->load()) {
                                if (controller) {
                                    controller->Close();
                                }
                                return S_OK; // Oggetto distrutto o dismesso: interrompi senza accedere a 'this'
                            }
                            if (FAILED(controllerResult) || !controller) {
                                std::cerr << "[WebView2] Creazione controller fallita (hr=" 
                                          << std::hex << controllerResult << std::dec << ").\n";
                                m_status = WebViewStatus::Failed;
                                return controllerResult;
                            }

                            webViewController = controller;
                            webViewController->get_CoreWebView2(&webView);
                            
                            m_status = WebViewStatus::Ready;
                            auto initEndTime = std::chrono::steady_clock::now();
                            double initMs = std::chrono::duration<double, std::milli>(initEndTime - initStartTime).count();
                            std::cout << "[PERF] Prima apertura WebView2 completata in: " 
                                      << std::fixed << std::setprecision(2) << initMs << " ms\n";
                            std::cout << "[WebView2] Controller pronto! Motore browser integrato con successo.\n";
                            
                            webViewController->put_IsVisible(m_isVisible ? TRUE : FALSE);
                            
                            // Naviga verso l'URL se era stato accodato prima del completamento dell'inizializzazione
                            if (!m_pendingUrl.empty() && webView) {
                                webView->Navigate(m_pendingUrl.c_str());
                                m_pendingUrl.clear();
                            }
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    if (FAILED(hr)) {
        std::cerr << "[WebView2] Chiamata a CreateCoreWebView2EnvironmentWithOptions fallita (hr=" 
                  << std::hex << hr << std::dec << ").\n";
        m_status = WebViewStatus::Failed;
    }
}

void DeskarmWebView::Navigate(const std::wstring& url) {
    if (m_status == WebViewStatus::Ready && webView) {
        webView->Navigate(url.c_str());
    } else {
        // Se non e' ancora pronto, accoda l'URL per la navigazione al completamento
        m_pendingUrl = url;
    }
}

void DeskarmWebView::Resize(int x, int y, int width, int height) {
    if (m_status == WebViewStatus::Ready && webViewController) {
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
    if (m_status == WebViewStatus::Ready && webViewController) {
        webViewController->put_IsVisible(visible ? TRUE : FALSE);
    }
}
