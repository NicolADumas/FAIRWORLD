#include "pch.h"
#include "XrManager.h"
#include <iostream>

XrManager::XrManager() {}
XrManager::~XrManager() { Shutdown(); }

bool XrManager::Init() {
    // 1. Configurazione delle informazioni dell'applicazione
    XrApplicationInfo appInfo = {};
    strcpy_s(appInfo.applicationName, "FAIRWORLD Action RPG");
    appInfo.applicationVersion = 1;
    strcpy_s(appInfo.engineName, "FairWorldEngine");
    appInfo.engineVersion = 1;
    appInfo.apiVersion = XR_CURRENT_API_VERSION;

    // 2. Richiesta estensioni (Vulkan è fondamentale per noi)
    const char* extensions[] = {
        XR_KHR_VULKAN_ENABLE_EXTENSION_NAME
    };

    XrInstanceCreateInfo createInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
    createInfo.applicationInfo = appInfo;
    createInfo.enabledExtensionCount = 1;
    createInfo.enabledExtensionNames = extensions;

    // 3. Creazione dell'Istanza OpenXR
    XrResult result = xrCreateInstance(&createInfo, &m_xrInstance);
    if (result != XR_SUCCESS) {
        std::cerr << "ERRORE CRITICO: Impossibile inizializzare OpenXR. Assicurati che il visore (es. Quest Link o SteamVR) sia collegato e attivo." << std::endl;
        return false;
    }

    std::cout << "OpenXR Inizializzato con successo! Visore connesso." << std::endl;
    return true;
}

bool XrManager::CreateSession(VkInstance instance, VkDevice device) {
    // TODO: Configurare XrGraphicsBindingVulkan2KHR legando l'istanza e il device Vulkan
    // TODO: xrCreateSession(m_xrInstance, &createInfo, &m_xrSession);
    // TODO: Creare lo spazio di riferimento (XR_REFERENCE_SPACE_TYPE_STAGE) per il tracking in piedi a scala reale
    return true;
}

void XrManager::PollEvents(bool& isRunning) {
    XrEventDataBuffer eventBuffer{XR_TYPE_EVENT_DATA_BUFFER};
    // TODO: Eseguire un ciclo su xrPollEvent
    // Se riceviamo un evento di tipo XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED e lo stato è EXIT, impostiamo isRunning = false
}

bool XrManager::BeginFrame() {
    // TODO: xrWaitFrame per sincronizzarsi con il refresh rate del visore (90/120Hz)
    // TODO: xrBeginFrame
    return true;
}

void XrManager::EndFrame() {
    // TODO: Configurare XrFrameEndInfo con i layer di proiezione per i due occhi
    // TODO: xrEndFrame(m_xrSession, &frameEndInfo);
}

void XrManager::Shutdown() {
    if (m_appSpace != XR_NULL_HANDLE) { xrDestroySpace(m_appSpace); m_appSpace = XR_NULL_HANDLE; }
    if (m_xrSession != XR_NULL_HANDLE) { xrDestroySession(m_xrSession); m_xrSession = XR_NULL_HANDLE; }
    if (m_xrInstance != XR_NULL_HANDLE) { xrDestroyInstance(m_xrInstance); m_xrInstance = XR_NULL_HANDLE; }
}
