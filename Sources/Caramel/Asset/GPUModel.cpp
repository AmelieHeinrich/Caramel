/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 10:00:30
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "GPUModel.hpp"

#include <Caramel/Renderer/Renderer.hpp>

GPUModel::GPUModel(const ModelMesh& mesh)
{
    agfx::Device& device = Renderer::Get().GetDevice();

    {
        agfx::BufferCreateInfo bufferInfo = agfx::BufferCreateInfo().SetSize(mesh.vertexBufferLength)
                                                                     .SetStride(sizeof(CaramelAsset::Vertex))
                                                                     .SetUsage(agfx::BufferUsage::ShaderRead)
                                                                     .SetMemoryType(agfx::BufferMemoryType::GPUOnly);
        m_VertexBuffer = device.CreateBuffer(bufferInfo);

        agfx::BufferViewCreateInfo viewInfo = agfx::BufferViewCreateInfo().SetBuffer(m_VertexBuffer.Get())
                                                                           .SetType(agfx::BufferViewType::Structured)
                                                                           .SetOffset(0)
                                                                           .SetWriteable(false);
        m_VertexBufferView = device.CreateBufferView(viewInfo);
    }

    for (uint32 lod = 0; lod < CaramelAsset::kLodCount; ++lod)
    {
        const ModelLOD& lodDesc = mesh.lods[lod];

        agfx::BufferCreateInfo meshletInfo = agfx::BufferCreateInfo().SetSize(lodDesc.meshletBufferLength)
                                                                      .SetStride(sizeof(CaramelAsset::MeshletDesc))
                                                                      .SetUsage(agfx::BufferUsage::ShaderRead)
                                                                      .SetMemoryType(agfx::BufferMemoryType::GPUOnly);
        m_MeshletBuffers[lod] = device.CreateBuffer(meshletInfo);
        agfx::BufferViewCreateInfo meshletViewInfo = agfx::BufferViewCreateInfo().SetBuffer(m_MeshletBuffers[lod].Get())
                                                                                  .SetType(agfx::BufferViewType::Structured)
                                                                                  .SetOffset(0)
                                                                                  .SetWriteable(false);
        m_MeshletBufferViews[lod] = device.CreateBufferView(meshletViewInfo);

        agfx::BufferCreateInfo meshletVertexInfo = agfx::BufferCreateInfo().SetSize(lodDesc.meshletVertexBufferLength)
                                                                            .SetStride(sizeof(uint32))
                                                                            .SetUsage(agfx::BufferUsage::ShaderRead)
                                                                            .SetMemoryType(agfx::BufferMemoryType::GPUOnly);
        m_MeshletVertexBuffers[lod] = device.CreateBuffer(meshletVertexInfo);
        agfx::BufferViewCreateInfo meshletVertexViewInfo = agfx::BufferViewCreateInfo().SetBuffer(m_MeshletVertexBuffers[lod].Get())
                                                                                        .SetType(agfx::BufferViewType::Structured)
                                                                                        .SetOffset(0)
                                                                                        .SetWriteable(false);
        m_MeshletVertexBufferViews[lod] = device.CreateBufferView(meshletVertexViewInfo);

        // Raw/ByteAddressBuffer: packed 3-bytes-per-triangle meshlet-local indices, unpacked in the
        // mesh shader with a shift/mask (meshoptimizer's native packing, not stride-addressable).
        // Padded by 4 bytes: the unpack reads a 4-byte-aligned Load2 (8 bytes) starting at the
        // last triangle's byte offset, which can overshoot the true data end by up to 3 bytes --
        // the padding keeps that read inside the buffer instead of past its allocation.
        agfx::BufferCreateInfo meshletTriangleInfo = agfx::BufferCreateInfo().SetSize(lodDesc.meshletTriangleBufferLength + 4)
                                                                              .SetStride(4)
                                                                              .SetUsage(agfx::BufferUsage::ShaderRead)
                                                                              .SetMemoryType(agfx::BufferMemoryType::GPUOnly);
        m_MeshletTriangleBuffers[lod] = device.CreateBuffer(meshletTriangleInfo);
        agfx::BufferViewCreateInfo meshletTriangleViewInfo = agfx::BufferViewCreateInfo().SetBuffer(m_MeshletTriangleBuffers[lod].Get())
                                                                                          .SetType(agfx::BufferViewType::Raw)
                                                                                          .SetOffset(0)
                                                                                          .SetWriteable(false);
        m_MeshletTriangleBufferViews[lod] = device.CreateBufferView(meshletTriangleViewInfo);
    }
}
