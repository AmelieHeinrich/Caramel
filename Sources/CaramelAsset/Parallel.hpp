/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 12:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <atomic>
#include <cstdlib>
#include <thread>

namespace CaramelAsset
{
    inline uint32 GetWorkerCount()
    {
        if (const char* override = std::getenv("CARAMEL_ASSET_THREADS"))
        {
            int parsed = std::atoi(override);
            if (parsed > 0)
                return static_cast<uint32>(parsed);
        }

        uint32 count = std::thread::hardware_concurrency();
        return count > 0 ? count : 4;
    }

    template <typename Body>
    void ParallelFor(size_t count, Body&& body)
    {
        if (count == 0)
            return;

        uint32 workerCount = std::min<uint32>(GetWorkerCount(), static_cast<uint32>(count));
        if (workerCount <= 1)
        {
            for (size_t i = 0; i < count; i++)
                body(i);
            return;
        }

        std::atomic<size_t> nextIndex{ 0 };
        TArray<std::thread> workers;
        workers.Reserve(workerCount);

        for (uint32 w = 0; w < workerCount; w++)
        {
            workers.PushBack(std::thread([&]() {
                for (;;)
                {
                    size_t index = nextIndex.fetch_add(1, std::memory_order_relaxed);
                    if (index >= count)
                        return;
                    body(index);
                }
            }));
        }

        for (std::thread& worker : workers)
            worker.join();
    }
}
