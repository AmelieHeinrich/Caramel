/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-05 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "CpuProfiler.hpp"

#include <atomic>
#include <chrono>
#include <mutex>

namespace
{
    float NowMs()
    {
        static const std::chrono::steady_clock::time_point s_Epoch = std::chrono::steady_clock::now();
        return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - s_Epoch).count();
    }

    // Registered lazily on a thread's first zone (or an explicit SetThreadName), never destroyed --
    // process-lifetime, so other threads may safely keep reading it after its owner exits.
    struct ThreadRecord
    {
        String name;
        uint32 depth = 0;
        uint64 lastFrame = UINT64_MAX;
        TArray<CpuProfileEvent> events[2];
    };

    std::atomic<uint64> s_FrameCounter{ 0 };
    float s_FrameStartMs[2] = { 0.0f, 0.0f };

    std::mutex s_RegistryMutex;
    TArray<TUnique<ThreadRecord>> s_Registry;
    std::atomic<uint32> s_NextThreadIndex{ 0 };

    thread_local ThreadRecord* t_Record = nullptr;

    ThreadRecord& GetThreadRecord()
    {
        if (t_Record)
            return *t_Record;

        TUnique<ThreadRecord> fresh = MakeUnique<ThreadRecord>();
        fresh->name = "Thread " + std::to_string(s_NextThreadIndex.fetch_add(1, std::memory_order_relaxed));
        t_Record = fresh.get();

        std::lock_guard<std::mutex> lock(s_RegistryMutex);
        s_Registry.PushBack(std::move(fresh));
        return *t_Record;
    }
}

void CpuProfiler::BeginFrame()
{
    uint64 frame = s_FrameCounter.fetch_add(1, std::memory_order_relaxed) + 1;
    s_FrameStartMs[frame & 1] = NowMs();
}

void CpuProfiler::SetThreadName(const String& name)
{
    GetThreadRecord().name = name;
}

uint32 CpuProfiler::PushZone(const char* name, bool isWait)
{
    ThreadRecord& record = GetThreadRecord();
    uint64 currentFrame = s_FrameCounter.load(std::memory_order_relaxed);

    if (record.depth == 0 && record.lastFrame != currentFrame) {
        record.lastFrame = currentFrame;
        record.events[currentFrame & 1].Clear();
    }

    TArray<CpuProfileEvent>& buffer = record.events[currentFrame & 1];

    uint32 index = (uint32)buffer.Size();
    buffer.PushBack(CpuProfileEvent{ name, NowMs(), 0.0f, (uint16)record.depth, isWait });
    record.depth++;
    return index;
}

void CpuProfiler::PopZone(uint32 index)
{
    ThreadRecord& record = GetThreadRecord();
    record.depth--;

    uint64 currentFrame = s_FrameCounter.load(std::memory_order_relaxed);
    record.events[currentFrame & 1][index].endMs = NowMs();
}

const TArray<CpuProfileThreadSnapshot>& CpuProfiler::GetLastFrameSnapshot()
{
    static TArray<CpuProfileThreadSnapshot> s_Snapshot;
    s_Snapshot.Clear();

    uint64 currentFrame = s_FrameCounter.load(std::memory_order_relaxed);
    if (currentFrame == 0)
        return s_Snapshot;

    // The slot every active thread is guaranteed to have finished writing: nothing touches it again
    // until the frame parity flips back, which can only happen after this call returns.
    uint32 readBuffer = (uint32)(1 - (currentFrame & 1));
    float origin = s_FrameStartMs[readBuffer];

    std::lock_guard<std::mutex> lock(s_RegistryMutex);
    for (const TUnique<ThreadRecord>& record : s_Registry) {
        // Idle for more than a frame -- its data would just be an ancient, confusing ghost.
        if (currentFrame - record->lastFrame > 1)
            continue;

        const TArray<CpuProfileEvent>& source = record->events[readBuffer];
        if (source.IsEmpty())
            continue;

        CpuProfileThreadSnapshot snapshot;
        snapshot.name = record->name;
        snapshot.events.Reserve(source.Size());
        for (const CpuProfileEvent& evt : source) {
            CpuProfileEvent normalized = evt;
            normalized.startMs -= origin;
            normalized.endMs -= origin;
            snapshot.events.PushBack(normalized);
        }
        s_Snapshot.PushBack(std::move(snapshot));
    }

    return s_Snapshot;
}
