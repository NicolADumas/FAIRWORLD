#pragma once
#include <functional>
#include <future>
#include <memory>

namespace fw {

// Forward declaration della classe interna che gestisce la coda per nascondere i dettagli di implementazione (Mutex vs Lock-free)
class JobQueueImpl;

class JobSystem {
public:
    JobSystem();
    ~JobSystem();

    // Avvia il pool di thread basato sui core hardware disponibili
    void Initialize();
    
    // Ferma i worker e svuota la coda
    void Shutdown();

    // Sottomette un task asincrono fire-and-forget
    void Execute(std::function<void()> job);

    // Sottomette un task asincrono restituendo un future per chi volesse attenderlo (Macro-task)
    template<typename F, typename... Args>
    auto Dispatch(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        using return_type = decltype(f(args...));

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> res = task->get_future();
        
        // Wrap the task in a void() signature per la coda
        Execute([task]() { (*task)(); });
        
        return res;
    }

private:
    std::unique_ptr<JobQueueImpl> m_queueImpl;
};

} // namespace fw
