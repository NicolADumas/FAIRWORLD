#pragma once
#include <string>

// Forward declaration per l'handle della finestra (Win32)
struct HWND__;
using WindowHandle = HWND__*;

class StateManager;
class FairWorldEngine;

struct SharedContext {
    WindowHandle window = nullptr;
    StateManager* stateManager = nullptr;
    FairWorldEngine* engine = nullptr;
    std::string targetGameJsonPath;
};
