#include <iostream>

#if defined(_WIN32) || defined(_WIN64)
#define DLL_EXPORT __declspec(dllexport)
#else
#define DLL_EXPORT
#endif

extern "C" {
    DLL_EXPORT void InitializeModule() {
        std::cout << "[Module_Modeling] Loaded successfully.\n";
    }

    DLL_EXPORT void ShutdownModule() {
        std::cout << "[Module_Modeling] Unloaded successfully.\n";
    }
}
