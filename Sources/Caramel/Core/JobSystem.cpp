/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 07:39:46
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "JobSystem.hpp"

#include <enkiTS/TaskScheduler.h>

JobSystem* s_JobSystem = nullptr;

void JobSystem::Initialize(uint32 threadCount)
{
    if (s_JobSystem)
        return;

    s_JobSystem = new JobSystem();
    s_JobSystem->m_TaskScheduler = new enki::TaskScheduler();

    // enki::TaskScheduler::Initialize(uint32_t) requires a non-zero thread count -- auto-detecting
    // hardware concurrency is only available through the parameterless overload.
    if (threadCount == 0)
        s_JobSystem->m_TaskScheduler->Initialize();
    else
        s_JobSystem->m_TaskScheduler->Initialize(threadCount);
}

void JobSystem::Shutdown()
{
    assert(s_JobSystem);

    s_JobSystem->m_TaskScheduler->WaitforAllAndShutdown();
    delete s_JobSystem->m_TaskScheduler;

    for (enki::TaskSet* job : s_JobSystem->m_DetachedJobs)
        delete job;

    delete s_JobSystem;
}

JobSystem& JobSystem::Get()
{
    assert(s_JobSystem);
    return *s_JobSystem;
}

JobHandle JobSystem::Schedule(JobFunction job)
{
    enki::TaskSet* task = new enki::TaskSet([function = std::move(job)](enki::TaskSetPartition, uint32) { function(); });
    
    m_TaskScheduler->AddTaskSetToPipe(task);
    return JobHandle{ task };
}

void JobSystem::Wait(JobHandle handle)
{
    assert(handle.IsValid());
    m_TaskScheduler->WaitforTask(handle.taskSet);
    delete handle.taskSet;
}

void JobSystem::RunDetached(JobFunction job)
{
    enki::TaskSet* task = new enki::TaskSet([function = std::move(job)](enki::TaskSetPartition, uint32) { function(); });

    {
        std::lock_guard<std::mutex> lock(m_DetachedMutex);
        m_DetachedJobs.push_back(task);
    }

    m_TaskScheduler->AddTaskSetToPipe(task);
}

void JobSystem::CollectGarbage()
{
    std::lock_guard<std::mutex> lock(m_DetachedMutex);

    uint64 writeIndex = 0;
    for (uint64 readIndex = 0; readIndex < m_DetachedJobs.size(); ++readIndex) {
        enki::TaskSet* job = m_DetachedJobs[readIndex];
        if (job->GetIsComplete()) {
            delete job;
        } else {
            m_DetachedJobs[writeIndex++] = job;
        }
    }
    m_DetachedJobs.Resize(writeIndex);
}

void JobSystem::ParallelFor(uint32 itemCount, uint32 minRangeSize, const ParallelForFunction& job)
{
    if (itemCount == 0) return;

    enki::TaskSet task(itemCount, [&job](enki::TaskSetPartition range, uint32 threadIndex) {
        job(range.start, range.end, threadIndex);
    });
    task.m_MinRange = minRangeSize > 0 ? minRangeSize : 1;

    m_TaskScheduler->AddTaskSetToPipe(&task);
    m_TaskScheduler->WaitforTask(&task);
}

void JobSystem::WaitAll()
{
    m_TaskScheduler->WaitforAll();
}

uint32 JobSystem::GetWorkerCount() const
{
    return m_TaskScheduler->GetNumTaskThreads();
}
