/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 10:00:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "StreamBuffer.hpp"

#include <cstring>

void StreamBuffer::Init(uint32 framesInFlight)
{
    buffers.Resize(framesInFlight);
    views.Resize(framesInFlight);
    capacities.Resize(framesInFlight, 0);
}

void StreamBuffer::Reserve(agfx::Device& device, uint32 frameIndex, const char* name, uint32 stride, uint64 bytes)
{
    if (bytes <= capacities[frameIndex])
        return;

    // Overshoot so a steadily growing frame does not reallocate every single frame.
    uint64 newCapacity = bytes + bytes / 2 + (uint64)stride * 256;

    agfx::BufferCreateInfo bufferInfo;
    bufferInfo.SetSize(newCapacity).SetStride(stride).SetUsage(agfx::BufferUsage::ShaderRead).SetMemoryType(agfx::BufferMemoryType::CPUToGPU);
    buffers[frameIndex] = device.CreateBuffer(bufferInfo);
    buffers[frameIndex].SetName(name);

    agfx::BufferViewCreateInfo viewInfo;
    viewInfo.SetBuffer(buffers[frameIndex].Get()).SetType(agfx::BufferViewType::Structured).SetOffset(0).SetWriteable(false);
    views[frameIndex] = device.CreateBufferView(viewInfo);

    capacities[frameIndex] = newCapacity;
    device.MakeResourcesResident();
}

bool StreamBuffer::Upload(agfx::Device& device, uint32 frameIndex, const char* name, uint32 stride,
                          const void* data, uint64 bytes)
{
    return Upload(device, frameIndex, name, stride, data, bytes, nullptr, 0);
}

bool StreamBuffer::Upload(agfx::Device& device, uint32 frameIndex, const char* name, uint32 stride,
                          const void* first, uint64 firstBytes, const void* second, uint64 secondBytes)
{
    uint64 totalBytes = firstBytes + secondBytes;
    if (totalBytes == 0)
        return false;

    Reserve(device, frameIndex, name, stride, totalBytes);

    agfx::MappedBuffer mapped(buffers[frameIndex]);
    uint8* dst = mapped.As<uint8>();
    if (firstBytes > 0)
        std::memcpy(dst, first, firstBytes);
    if (secondBytes > 0)
        std::memcpy(dst + firstBytes, second, secondBytes);
    return true;
}
