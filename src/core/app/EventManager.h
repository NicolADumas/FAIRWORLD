#pragma once
#include <functional>
#include <vector>
#include <unordered_map>
#include <memory>
#include <typeindex>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <glm/glm.hpp>
#include "World.h" // Per BlockType

// =====================================================================
// EVENT MANAGER - Architettura Observer Event-Driven
// =====================================================================
// Disaccoppia la logica hardware (Input) dalla logica del mondo.
// Evita "ghost blocks" ed esecuzioni multiple per frame.

// Classe base per tutti gli eventi
struct IEvent {
    virtual ~IEvent() = default;
};

// Evento: Blocco Scavato/Distrutto
struct Event_BlockMined : public IEvent {
    glm::ivec3 position;
    BlockType type;
    Event_BlockMined(glm::ivec3 pos, BlockType t) : position(pos), type(t) {}
};

// Evento: Blocco Piazzato
struct Event_BlockPlaced : public IEvent {
    glm::ivec3 position;
    BlockType type;
    Event_BlockPlaced(glm::ivec3 pos, BlockType t) : position(pos), type(t) {}
};

// Evento: Aggiornamento Blocco (Triggerato quando un blocco adiacente cambia)
// Usato per propagare la fisica dei fluidi (acqua/lava che cadono) e termodinamica
struct Event_BlockUpdated : public IEvent {
    glm::ivec3 position;
    Event_BlockUpdated(glm::ivec3 pos) : position(pos) {}
};

// Interfaccia Listener generica per l'archiviazione
class IEventListener {
public:
    virtual ~IEventListener() = default;
    virtual void Execute(const IEvent* e) = 0;
};

// Listener Tipizzato
template<typename T>
class EventListener : public IEventListener {
    std::function<void(const T&)> callback;
public:
    EventListener(std::function<void(const T&)> cb) : callback(cb) {}
    void Execute(const IEvent* e) override {
        callback(*static_cast<const T*>(e));
    }
};

// Singleton EventManager
class EventManager {
private:
    std::unordered_map<std::type_index, std::vector<std::unique_ptr<IEventListener>>> listeners;
    std::shared_mutex listenersMutex;

    std::vector<std::function<void()>> eventQueue;
    std::mutex queueMutex;

    EventManager() = default;
    ~EventManager() = default;

public:
    static EventManager& Get() {
        static EventManager instance;
        return instance;
    }

    EventManager(const EventManager&) = delete;
    EventManager& operator=(const EventManager&) = delete;

    template<typename EventType>
    void Subscribe(std::function<void(const EventType&)> callback) {
        std::unique_lock<std::shared_mutex> lock(listenersMutex);
        listeners[typeid(EventType)].push_back(std::make_unique<EventListener<EventType>>(callback));
    }

    // Dispatch Immediato (Sincrono)
    template<typename EventType>
    void Dispatch(const EventType& e) {
        std::shared_lock<std::shared_mutex> lock(listenersMutex);
        auto it = listeners.find(typeid(EventType));
        if (it != listeners.end()) {
            for (auto& listener : it->second) {
                listener->Execute(&e);
            }
        }
    }

    // Dispatch Differito (Asincrono/Coda)
    template<typename EventType>
    void QueueEvent(const EventType& e) {
        std::lock_guard<std::mutex> lock(queueMutex);
        eventQueue.push_back([this, e]() {
            this->Dispatch(e);
        });
    }

    // Processa tutti gli eventi in coda in blocco
    void ProcessEvents() {
        std::vector<std::function<void()>> currentQueue;
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (eventQueue.empty()) return;
            
            // Spostiamo la coda attuale in una locale per permettere l'aggiunta 
            // di nuovi eventi durante il processing senza finire in deadlock
            currentQueue = std::move(eventQueue);
            eventQueue.clear();
        }
        
        for (auto& task : currentQueue) {
            task();
        }
    }
};
