#include "pch.h"
#include "JobSystem.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <iostream>
#include <atomic>
#include <cstdlib>
#include <exception>

namespace fw {

namespace {
bool JobTraceEnabled() {
    const char* value = std::getenv("FAIRWORLD_JOB_TRACE");
    return value && value[0] != '\0' && value[0] != '0';
}
}

class JobQueueImpl {
public:
    JobQueueImpl() : m_shutdown(true), m_accepting(false), m_activeJobs(0) {}
    
    ~JobQueueImpl() {
        Shutdown();
    }

    void Initialize(size_t requestedWorkerCount) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (!m_workers.empty()) return;
            m_shutdown = false;
            m_accepting = false;
        }

        unsigned int poolSize = static_cast<unsigned int>(requestedWorkerCount);
        if (poolSize == 0) {
            unsigned int numThreads = std::thread::hardware_concurrency();
            if (numThreads == 0) numThreads = 4;
            poolSize = std::max(1u, numThreads - 1);
        }
        
        std::cout << "[JobSystem] Avvio Thread Pool con " << poolSize << " worker threads.\n";
        
        for (unsigned int i = 0; i < poolSize; ++i) {
            m_workers.emplace_back([this]() {
                WorkerLoop();
            });
        }

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_accepting = true;
        }
    }

    void StopAcceptingJobs() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_accepting = false;
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
        return m_accepting && !m_shutdown;
    }

    void Shutdown() {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_shutdown = true;
            m_accepting = false;
        }
        m_condition.notify_all();
        
        for (std::thread& worker : m_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        m_workers.clear();
    }

    JobSubmission Enqueue(std::function<void(uint64_t)> job, bool tracked) {
        if (!job) return {0, JobSubmissionStatus::RejectedInvalid};
        uint64_t requestId = 0;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (!m_accepting || m_shutdown) return {0, JobSubmissionStatus::RejectedShutdown};
            requestId = ++m_nextRequestId;
            m_jobs.push(QueuedJob{requestId, std::move(job), tracked});
            if (tracked && JobTraceEnabled()) {
                std::cout << "[JobTrace] request=" << requestId << " state=accepted\n";
            }
        }
        m_condition.notify_one();
        return {requestId, JobSubmissionStatus::Accepted};
    }

    void WaitAll() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_waitCondition.wait(lock, [this]() {
            return m_jobs.empty() && m_activeJobs == 0;
        });
    }

private:
    struct QueuedJob {
        uint64_t requestId;
        std::function<void(uint64_t)> run;
        bool tracked;
    };

    void WorkerLoop() {
        while (true) {
            QueuedJob job{};
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
            if (job.run) {
                if (job.tracked && JobTraceEnabled()) {
                    std::cout << "[JobTrace] request=" << job.requestId << " state=started\n";
                }
                try {
                    job.run(job.requestId);
                } catch (...) {
                    if (job.tracked && JobTraceEnabled()) {
                        std::cerr << "[JobTrace] request=" << job.requestId << " state=failed exception=unhandled\n";
                    }
                }
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
    std::queue<QueuedJob> m_jobs;
    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::condition_variable m_waitCondition;
    bool m_shutdown;
    bool m_accepting;
    int m_activeJobs;
    uint64_t m_nextRequestId = 0;
};

JobSystem::JobSystem() : m_queueImpl(std::make_unique<JobQueueImpl>()) {
}

JobSystem::~JobSystem() {
}

void JobSystem::Initialize(size_t workerCount) {
    m_queueImpl->Initialize(workerCount);
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
    m_queueImpl->Enqueue([job = std::move(job)](uint64_t) { job(); }, false);
}

JobSubmission JobSystem::ExecuteTracked(
    std::function<JobExecutionStatus(uint64_t)> job,
    std::function<void(const JobExecutionResult&)> onFinished) {
    if (!job) return {0, JobSubmissionStatus::RejectedInvalid};
    return m_queueImpl->Enqueue([job = std::move(job), onFinished = std::move(onFinished)](uint64_t requestId) {
        JobExecutionResult result;
        result.requestId = requestId;
        try {
            result.status = job(requestId);
        } catch (...) {
            result.status = JobExecutionStatus::Failed;
            result.exception = std::current_exception();
        }
        if (JobTraceEnabled()) {
            const char* state = result.status == JobExecutionStatus::Completed ? "completed" :
                result.status == JobExecutionStatus::Cancelled ? "cancelled" : "failed";
            std::cout << "[JobTrace] request=" << requestId << " state=" << state << '\n';
        }
        if (onFinished) {
            try {
                onFinished(result);
            } catch (...) {
                if (JobTraceEnabled()) {
                    std::cerr << "[JobTrace] request=" << requestId << " state=completion-callback-failed\n";
                }
            }
        }
    }, true);
}

} // namespace fw
