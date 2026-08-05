/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-05 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

// One recorded CPU zone. Times are milliseconds relative to the start of the frame it belongs to,
// so a bar chart / flame graph can place events from every thread on one shared timeline.
struct CpuProfileEvent
{
    const char* name = nullptr;
    float startMs = 0.0f;
    float endMs = 0.0f;
    uint16 depth = 0;

    // A blocking wait (fence, GPU readback) rather than actual CPU work -- see CARAMEL_ZONE_WAIT.
    // Kept distinct so the display can tell "CPU busy" from "CPU idle, stalled on something else"
    // instead of both looking like equally expensive compute.
    bool isWait = false;
};

// One thread's complete, frozen event list for the last fully-recorded frame.
struct CpuProfileThreadSnapshot
{
    String name;
    TArray<CpuProfileEvent> events;
};

// Lightweight always-on multithreaded CPU profiler: nested named zones (CARAMEL_ZONE), one flat
// event list per thread, double-buffered per frame so the display side always reads a frame that
// every contributing thread has finished writing.
//
// Threading contract: a thread's own buffer is only ever written by that thread, and only cleared
// when it is about to record the first zone of a new frame -- so the hot path (PushZone/PopZone)
// takes no lock. This only holds for threads whose zones complete within a bounded number of
// frames of the JobSystem kind (Schedule/ParallelFor/WaitAll, which all block the calling thread
// until finished). Don't use CARAMEL_ZONE inside JobSystem::RunDetached background work (asset
// streaming) -- those threads run unsynchronized with the frame boundary and a zone spanning many
// frames could be read while still being written.
class CpuProfiler
{
public:
    // Main thread only. Call once at the very top of the frame, before any zone is pushed.
    static void BeginFrame();

    // Gives the calling thread's timeline a human-readable name (e.g. "Main Thread"). Optional --
    // an unnamed thread is auto-labeled "Thread N" the first time it records a zone.
    static void SetThreadName(const String& name);

    // Snapshot of the last frame every active thread finished recording, self-times excluded.
    // Rebuilt fresh on every call -- intended to be called once per frame by the display panel.
    static const TArray<CpuProfileThreadSnapshot>& GetLastFrameSnapshot();

    // Used by CpuProfileScope -- not meant to be called directly.
    static uint32 PushZone(const char* name, bool isWait = false);
    static void PopZone(uint32 index);
};

// RAII scope that records one CPU zone on whichever thread constructs it. See CARAMEL_ZONE and
// CARAMEL_ZONE_WAIT.
class CpuProfileScope
{
public:
    explicit CpuProfileScope(const char* name, bool isWait = false) : m_Index(CpuProfiler::PushZone(name, isWait)) {}
    ~CpuProfileScope() { CpuProfiler::PopZone(m_Index); }

    CpuProfileScope(const CpuProfileScope&) = delete;
    CpuProfileScope& operator=(const CpuProfileScope&) = delete;

private:
    uint32 m_Index;
};

#define CARAMEL_ZONE_CONCAT_INNER(a, b) a##b
#define CARAMEL_ZONE_CONCAT(a, b) CARAMEL_ZONE_CONCAT_INNER(a, b)

// Times the enclosing scope on whichever thread runs it. `name` must be a string literal (or at
// least outlive the frame) -- only the pointer is stored, never copied.
#define CARAMEL_ZONE(name) CpuProfileScope CARAMEL_ZONE_CONCAT(_cpuZone, __LINE__)(name)

// Same as CARAMEL_ZONE, but flags the scope as a blocking wait (a fence, a GPU readback, a queue
// drain) instead of actual CPU work -- see CpuProfileEvent::isWait. Use this instead of CARAMEL_ZONE
// when the time being measured is the CPU sitting idle for something else to finish, not computing.
#define CARAMEL_ZONE_WAIT(name) CpuProfileScope CARAMEL_ZONE_CONCAT(_cpuZone, __LINE__)(name, true)
