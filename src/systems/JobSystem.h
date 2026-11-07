#pragma once
#include <functional>
#include <future>
#include <memory>
#include <exception>
#include <cstdint>
#include <type_traits>
#include <cstddef>

namespace fw {

// Forward declaration della classe interna che gestisce la coda per nascondere i dettagli di implementazione (Mutex vs Lock-free)
class JobQueueImpl;

enum class JobSubmissionStatus : uint8_t {
    Accepted,
    RejectedShutdown,
    RejectedInvalid
};

enum class JobExecutionStatus : uint8_t {
    Completed,
    Cancelled,
    Failed
};

struct JobSubmission {
    uint64_t requestId = 0;
    JobSubmissionStatus status = JobSubmissionStatus::RejectedInvalid;
    bool accepted() const { return status == JobSubmissionStatus::Accepted; }
};

struct JobExecutionResult {
    uint64_t requestId = 0;
    JobExecutionStatus status = JobExecutionStatus::Failed;
    std::exception_ptr exception;
};

class JobSystem {
public:
    JobSystem();
    ~JobSystem();

    // Avvia il pool di thread basato sui core hardware disponibili
    // workerCount=0 conserva il dimensionamento automatico di produzione.
    void Initialize(size_t workerCount = 0);
    
    // Ferma l'accettazione di nuovi job
    void StopAcceptingJobs();

    // Ferma i worker e svuota la coda
    void Shutdown();
    
    // Ottiene il numero di job pendenti nella coda
    size_t GetPendingJobCount();

    // C4 Diagnostic getters — read-only, zero runtime overhead
    size_t GetActiveWorkerCount();
    bool   IsAcceptingJobs();

    // Attende che tutti i job in coda e in esecuzione siano completati
    void WaitAll();

    // Sottomette un task asincrono fire-and-forget
    void Execute(std::function<void()> job);

    // Percorso esplicito per lavori con stato terminale e acknowledgement d'accettazione.
    JobSubmission ExecuteTracked(
        std::function<JobExecutionStatus(uint64_t)> job,
        std::function<void(const JobExecutionResult&)> onFinished = {});

    // Sottomette un task asincrono restituendo un future per chi volesse attenderlo (Macro-task)
    template<typename F, typename... Args>
    auto Dispatch(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        using return_type = decltype(f(args...));

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> res = task->get_future();
        
        // Wrap the task in a void() signature per la coda
        // In caso di shutdown prima dell'enqueue, il future viene risolto come
        // broken_promise invece di restare pendente per sempre.
        ExecuteTracked([task](uint64_t) {
            (*task)();
            return JobExecutionStatus::Completed;
        });
        
        return res;
    }

private:
    std::unique_ptr<JobQueueImpl> m_queueImpl;
};

} // namespace fw
