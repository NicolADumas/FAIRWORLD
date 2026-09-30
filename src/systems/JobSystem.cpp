#include "pch.h"
#include "JobSystem.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <iostream>

namespace fw {

class JobQueueImpl {
public:
    JobQueueImpl() : m_shutdown(false), m_activeJobs(0) {}
    
    ~JobQueueImpl() {
        Shutdown();
    }

    void Initialize() {
        m_shutdown = false;
        m_workers.clear();
        
        unsigned int numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 4; // Fallback
        
        // Lasciamo un core (o due) liberi per OS/Renderer.
        unsigned int poolSize = std::max(1u, numThreads - 1);
        
        std::cout << "[JobSystem] Avvio Thread Pool con " << poolSize << " worker threads.\n";
        
        for (unsigned int i = 0; i < poolSize; ++i) {
            m_workers.emplace_back([this]() {
                WorkerLoop();
            });
        }
    }

    void StopAcceptingJobs() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_shutdown = true;
    }

    size_t GetPendingJobCount() {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_jobs.size();
    }

    size_t GetActiveWorkerCount() {
        std::unique_lock<std::mutex> lock(m_mutex);
        return static_cast<size_t>(m_activeJobs);
    }

    bool IsAcceptingJobs() {
        std::unique_lock<std::mutex> lock(m_mutex);
        return !m_shutdown;
    }

    void Shutdown() {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_shutdown = true;
        }
        m_condition.notify_all();
        
        for (std::thread& worker : m_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        m_workers.clear();
    }

    void Enqueue(std::function<void()> job) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_shutdown) return;
            m_jobs.push(std::move(job));
            // std::cout << "[JobSystem] Job accodato. Coda attuale: " << m_jobs.size() << " job.\n";
        }
        m_condition.notify_one();
    }

    void WaitAll() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_waitCondition.wait(lock, [this]() {
            return m_jobs.empty() && m_activeJobs == 0;
        });
    }

private:
    void WorkerLoop() {
        while (true) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_condition.wait(lock, [this]() {
                    return m_shutdown || !m_jobs.empty();
                });
                
                if (m_shutdown && m_jobs.empty()) {
                    return; // Thread exit
                }
                
                job = std::move(m_jobs.front());
                m_jobs.pop();
                m_activeJobs++;
            }
            
            // Execute the job outside the lock
            if (job) {
                // std::cout << "[Worker Thread] Risvegliato! Esecuzione job in corso...\n";
                job();
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_activeJobs--;
                    if (m_jobs.empty() && m_activeJobs == 0) {
                        m_waitCondition.notify_all();
                    }
                }
            }
        }
    }

    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_jobs;
    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::condition_variable m_waitCondition;
    bool m_shutdown;
    int m_activeJobs;
};

JobSystem::JobSystem() : m_queueImpl(std::make_unique<JobQueueImpl>()) {
}

JobSystem::~JobSystem() {
}

void JobSystem::Initialize() {
    m_queueImpl->Initialize();
}

void JobSystem::StopAcceptingJobs() {
    if (m_queueImpl) m_queueImpl->StopAcceptingJobs();
}

void JobSystem::Shutdown() {
    if (m_queueImpl) m_queueImpl->Shutdown();
}

size_t JobSystem::GetPendingJobCount() {
    return m_queueImpl ? m_queueImpl->GetPendingJobCount() : 0;
}

size_t JobSystem::GetActiveWorkerCount() {
    return m_queueImpl ? m_queueImpl->GetActiveWorkerCount() : 0;
}

bool JobSystem::IsAcceptingJobs() {
    return m_queueImpl ? m_queueImpl->IsAcceptingJobs() : false;
}

void JobSystem::WaitAll() {
    m_queueImpl->WaitAll();
}

void JobSystem::Execute(std::function<void()> job) {
    m_queueImpl->Enqueue(std::move(job));
}

} // namespace fw
