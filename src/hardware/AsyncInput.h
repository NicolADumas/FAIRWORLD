#pragma once
#include <vector>
#include <mutex>
#include <variant>

namespace fw {

// Definizioni grezze degli eventi input
struct MouseMoveEvent { float dx, dy; };
struct MouseClickEvent { int button; bool pressed; };
struct KeyEvent { int keycode; bool pressed; };

using InputEvent = std::variant<MouseMoveEvent, MouseClickEvent, KeyEvent>;

class AsyncInput {
public:
    AsyncInput();
    ~AsyncInput() = default;

    // Chiamato dal thread OS (es. la message pump Win32) per accumulare eventi.
    // L'inserimento è protetto da un mutex molto leggero o può essere reso lock-free.
    void PushEvent(InputEvent event);

    // Chiamato dall'ECS / Main Loop all'inizio del frame.
    // Scambia i due buffer (Double-Buffering) in un istante, restituendo i vecchi eventi da processare.
    std::vector<InputEvent> SwapBuffersAndConsume();

private:
    // Due buffer per minimizzare la contention
    std::vector<InputEvent> m_bufferA;
    std::vector<InputEvent> m_bufferB;
    
    std::vector<InputEvent>* m_writeBuffer;
    std::vector<InputEvent>* m_readBuffer;

    std::mutex m_swapMutex;
};

} // namespace fw
