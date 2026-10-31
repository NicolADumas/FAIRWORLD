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

using SubscriptionId = uint64_t;
constexpr SubscriptionId INVALID_SUBSCRIPTION_ID = 0;

// Singleton EventManager
class EventManager {
private:
    struct ListenerEntry {
        SubscriptionId id = INVALID_SUBSCRIPTION_ID;
        std::unique_ptr<IEventListener> listener;
        bool isRemoved = false;
    };

    struct DispatchScope {
        size_t& depth;
        EventManager& mgr;
        DispatchScope(size_t& d, EventManager& m) : depth(d), mgr(m) { ++depth; }
        ~DispatchScope() {
            --depth;
            if (depth == 0) {
                mgr.CompactAllListeners();
            }
        }
    };

    std::unordered_map<std::type_index, std::vector<ListenerEntry>> listeners;
    std::unordered_map<SubscriptionId, std::type_index> subscriptionTypeMap;
    mutable std::recursive_mutex listenersMutex;
    SubscriptionId nextSubscriptionId = 1;
    size_t dispatchDepth = 0;

    std::vector<std::function<void()>> eventQueue;
    std::mutex queueMutex;

    EventManager() = default;
    ~EventManager() = default;

    void CompactListenersForType(std::vector<ListenerEntry>& vec) {
        vec.erase(
            std::remove_if(vec.begin(), vec.end(), [](const ListenerEntry& entry) {
                return entry.isRemoved;
            }),
            vec.end()
        );
    }

    void CompactAllListeners() {
        for (auto it = listeners.begin(); it != listeners.end();) {
            CompactListenersForType(it->second);
            if (it->second.empty()) {
                it = listeners.erase(it);
            } else {
                ++it;
            }
        }
    }

public:
    static EventManager& Get() {
        static EventManager instance;
        return instance;
    }

    EventManager(const EventManager&) = delete;
    EventManager& operator=(const EventManager&) = delete;

    template<typename EventType>
    SubscriptionId Subscribe(std::function<void(const EventType&)> callback) {
        if (!callback) return INVALID_SUBSCRIPTION_ID;
        std::lock_guard<std::recursive_mutex> lock(listenersMutex);
        SubscriptionId id = nextSubscriptionId++;
        if (nextSubscriptionId == INVALID_SUBSCRIPTION_ID) {
            nextSubscriptionId = 1;
        }
        std::type_index typeIdx = typeid(EventType);
        listeners[typeIdx].push_back(ListenerEntry{
            id,
            std::make_unique<EventListener<EventType>>(std::move(callback)),
            false
        });
        subscriptionTypeMap.insert_or_assign(id, typeIdx);
        return id;
    }

    bool Unsubscribe(SubscriptionId id) {
        if (id == INVALID_SUBSCRIPTION_ID) return false;
        std::lock_guard<std::recursive_mutex> lock(listenersMutex);
        auto itType = subscriptionTypeMap.find(id);
        if (itType == subscriptionTypeMap.end()) {
            return false;
        }
        std::type_index typeIdx = itType->second;
        subscriptionTypeMap.erase(itType);

        auto itList = listeners.find(typeIdx);
        if (itList != listeners.end()) {
            for (auto& entry : itList->second) {
                if (entry.id == id) {
                    entry.isRemoved = true;
                    break;
                }
            }
            if (dispatchDepth == 0) {
                CompactListenersForType(itList->second);
                if (itList->second.empty()) {
                    listeners.erase(itList);
                }
            }
        }
        return true;
    }

    template<typename EventType>
    void UnsubscribeAll() {
        std::lock_guard<std::recursive_mutex> lock(listenersMutex);
        std::type_index typeIdx = typeid(EventType);
        auto itList = listeners.find(typeIdx);
        if (itList != listeners.end()) {
            for (auto& entry : itList->second) {
                entry.isRemoved = true;
                subscriptionTypeMap.erase(entry.id);
            }
            if (dispatchDepth == 0) {
                listeners.erase(itList);
            }
        }
    }

    // Dispatch Immediato (Sincrono, re-entrancy safe e thread-safe)
    template<typename EventType>
    void Dispatch(const EventType& e) {
        std::lock_guard<std::recursive_mutex> lock(listenersMutex);
        auto it = listeners.find(typeid(EventType));
        if (it == listeners.end() || it->second.empty()) {
            return;
        }

        DispatchScope scope(dispatchDepth, *this);
        auto& vec = it->second;
        size_t count = vec.size();
        for (size_t i = 0; i < count; ++i) {
            if (!vec[i].isRemoved && vec[i].listener) {
                vec[i].listener->Execute(&e);
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
            currentQueue = std::move(eventQueue);
            eventQueue.clear();
        }
        
        for (auto& task : currentQueue) {
            task();
        }
    }

    // Reset totale (per test o riavvio)
    void Clear() {
        std::lock_guard<std::recursive_mutex> lock(listenersMutex);
        listeners.clear();
        subscriptionTypeMap.clear();
        dispatchDepth = 0;
        std::lock_guard<std::mutex> qLock(queueMutex);
        eventQueue.clear();
    }

    bool IsSubscribed(SubscriptionId id) const {
        if (id == INVALID_SUBSCRIPTION_ID) return false;
        std::lock_guard<std::recursive_mutex> lock(listenersMutex);
        return subscriptionTypeMap.find(id) != subscriptionTypeMap.end();
    }

    size_t GetTotalListenerCount() const {
        std::lock_guard<std::recursive_mutex> lock(listenersMutex);
        size_t total = 0;
        for (const auto& pair : listeners) {
            for (const auto& entry : pair.second) {
                if (!entry.isRemoved) {
                    total++;
                }
            }
        }
        return total;
    }

    template<typename EventType>
    size_t GetListenerCount() const {
        std::lock_guard<std::recursive_mutex> lock(listenersMutex);
        auto it = listeners.find(typeid(EventType));
        if (it == listeners.end()) return 0;
        size_t count = 0;
        for (const auto& entry : it->second) {
            if (!entry.isRemoved) {
                count++;
            }
        }
        return count;
    }
};
