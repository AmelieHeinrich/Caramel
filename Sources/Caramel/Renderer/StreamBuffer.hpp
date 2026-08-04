/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 10:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <AGFX/agfx.hpp>

struct StreamBuffer
{
    TArray<agfx::Buffer> buffers;
    TArray<agfx::BufferView> views;
    TArray<uint64> capacities;

    void Init(uint32 framesInFlight);

    bool Upload(agfx::Device& device, uint32 frameIndex, const char* name, uint32 stride,
                const void* data, uint64 bytes);

    bool Upload(agfx::Device& device, uint32 frameIndex, const char* name, uint32 stride,
                const void* first, uint64 firstBytes, const void* second, uint64 secondBytes);

private:
    void Reserve(agfx::Device& device, uint32 frameIndex, const char* name, uint32 stride, uint64 bytes);
};
