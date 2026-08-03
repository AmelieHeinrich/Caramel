/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 07:36:51
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <functional>
#include <mutex>

namespace enki {
    class TaskScheduler;
    class TaskSet;
}

using JobFunction = std::function<void()>;
using ParallelForFunction = std::function<void(uint32 start, uint32 end, uint32 threadIndex)>;

struct JobHandle
{
    enki::TaskSet* taskSet = nullptr;
    
    bool IsValid() const { return taskSet != nullptr; }
};

class JobSystem
{
public:
    static void Initialize(uint32 threadCount = 0);
    static void Shutdown();
    static JobSystem& Get();

    JobHandle Schedule(JobFunction job);
    void Wait(JobHandle handle);
    
    void RunDetached(JobFunction job);
    void CollectGarbage();

    void ParallelFor(uint32 itemCount, uint32 minRangeSize, const ParallelForFunction& job);
    void WaitAll();

    uint32 GetWorkerCount() const;

private:
    JobSystem() = default;
    ~JobSystem() = default;

    enki::TaskScheduler* m_TaskScheduler = nullptr;
    
    std::mutex m_DetachedMutex;
    TArray<enki::TaskSet*> m_DetachedJobs;
};

