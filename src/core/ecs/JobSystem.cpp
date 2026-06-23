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
    JobQueueImpl() : m_shutdown(false) {}
    
    ~JobQueueImpl() {
        Shutdown();
    }

    void Initialize() {
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

    void Shutdown() {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_shutdown) return;
            m_shutdown = true;
        }
        m_condition.notify_all();
        
        for (std::thread& worker : m_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    void Enqueue(std::function<void()> job) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_jobs.push(std::move(job));
            // std::cout << "[JobSystem] Job accodato. Coda attuale: " << m_jobs.size() << " job.\n";
        }
        m_condition.notify_one();
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
            }
            
            // Execute the job outside the lock
            if (job) {
                // std::cout << "[Worker Thread] Risvegliato! Esecuzione job in corso...\n";
                job();
            }
        }
    }

    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_jobs;
    std::mutex m_mutex;
    std::condition_variable m_condition;
    bool m_shutdown;
};

JobSystem::JobSystem() : m_queueImpl(std::make_unique<JobQueueImpl>()) {
}

JobSystem::~JobSystem() {
}

void JobSystem::Initialize() {
    m_queueImpl->Initialize();
}

void JobSystem::Shutdown() {
    m_queueImpl->Shutdown();
}

void JobSystem::Execute(std::function<void()> job) {
    m_queueImpl->Enqueue(std::move(job));
}

} // namespace fw
