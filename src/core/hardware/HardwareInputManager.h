#pragma once
#include <atomic>
#include <thread>
#include <iostream>

namespace fw {

struct HardwareIntent {
    enum class Command { 
        Idle, 
        MoveForward, 
        TriggerAction, 
        FocusBending 
    };
    Command cmd = Command::Idle;
    float signalStrength = 0.0f; // Potenza del segnale catturata dall'STM32
};

// Coda SPSC (Single-Producer, Single-Consumer) senza blocchi
class LockFreeIntentQueue {
private:
    static constexpr size_t CAPACITY = 1024; // Buffer circolare profondo
    HardwareIntent buffer[CAPACITY];
    
    // Allineamento cache line per evitare false sharing tra thread
    alignas(64) std::atomic<size_t> head{0};
    alignas(64) std::atomic<size_t> tail{0};

public:
    // Eseguito dal thread Hardware (Producer)
    bool Push(const HardwareIntent& intent) {
        size_t currentHead = head.load(std::memory_order_relaxed);
        size_t nextHead = (currentHead + 1) % CAPACITY;
        
        if (nextHead == tail.load(std::memory_order_acquire)) {
            return false; // Coda piena, droppiamo il frame hardware
        }
        
        buffer[currentHead] = intent;
        head.store(nextHead, std::memory_order_release);
        return true;
    }

    // Eseguito dal loop dell'ECS di FAIRWORLD (Consumer)
    bool Pop(HardwareIntent& outIntent) {
        size_t currentTail = tail.load(std::memory_order_relaxed);
        
        if (currentTail == head.load(std::memory_order_acquire)) {
            return false; // Coda vuota, nessun nuovo input
        }
        
        outIntent = buffer[currentTail];
        tail.store((currentTail + 1) % CAPACITY, std::memory_order_release);
        return true;
    }
};

class HardwareInputManager {
private:
    LockFreeIntentQueue m_queue;
    std::thread m_hardwareThread;
    std::atomic<bool> m_isRunning{false};

    void HardwareLoop() {
        // Qui inizializzi la porta seriale/USB verso il custom PCB STM32
        // SerialPort port("COM3", 115200);
        
        while (m_isRunning.load(std::memory_order_relaxed)) {
            // 1. Leggi il flusso degli elettrodi attivi
            // byte[] rawData = port.Read();
            
            // 2. Classificazione del segnale locale
            HardwareIntent intent;
            
            // Mockup della decodifica neurale
            bool detectedSpike = false; // logic_to_detect_spike(rawData);
            if (detectedSpike) {
                intent.cmd = HardwareIntent::Command::MoveForward;
                intent.signalStrength = 0.95f;
                m_queue.Push(intent);
            }
            
            // Polling rate altissimo, es 1000Hz (1ms)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

public:
    void Start() {
        m_isRunning.store(true, std::memory_order_relaxed);
        m_hardwareThread = std::thread(&HardwareInputManager::HardwareLoop, this);
    }

    void Shutdown() {
        m_isRunning.store(false, std::memory_order_relaxed);
        if (m_hardwareThread.joinable()) {
            m_hardwareThread.join();
        }
    }

    // Da richiamare nel FixedUpdate di PlayState
    void ProcessInputs(/* entt::registry& registry */) {
        HardwareIntent intent;
        while (m_queue.Pop(intent)) {
            // Instrada il comando hardware nel sistema N-Body o nel player controller
            if (intent.cmd == HardwareIntent::Command::MoveForward) {
                // registry.get<RigidBodyComponent>(playerEntity).velocity += ...
            }
        }
    }
};

} // namespace fw
