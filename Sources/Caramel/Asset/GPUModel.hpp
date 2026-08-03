/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 10:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Asset/Model.hpp>

#include <AGFX/agfx.hpp>

// GPU-side resources for one ModelMesh: a shared vertex buffer (uploaded once, all LODs index into
// it) plus, per LOD, a meshlet/meshlet-vertex/meshlet-triangle buffer trio. All buffers and views
// are created up front from the CPU-side ModelMesh's byte lengths -- mirroring GPUTexture/
// StreamingTexture's "create every mip's view before its data lands" pattern, since a view is just
// a descriptor and can safely predate the bytes it will eventually describe.
class GPUModel
{
public:
    GPUModel() = default;
    GPUModel(const ModelMesh& mesh);

    agfx::Buffer& GetVertexBuffer() { return m_VertexBuffer; }
    agfx::BufferView& GetVertexBufferView() { return m_VertexBufferView; }

    agfx::Buffer& GetMeshletBuffer(uint32 lod) { return m_MeshletBuffers[lod]; }
    agfx::Buffer& GetMeshletVertexBuffer(uint32 lod) { return m_MeshletVertexBuffers[lod]; }
    agfx::Buffer& GetMeshletTriangleBuffer(uint32 lod) { return m_MeshletTriangleBuffers[lod]; }

    agfx::BufferView& GetMeshletBufferView(uint32 lod) { return m_MeshletBufferViews[lod]; }
    agfx::BufferView& GetMeshletVertexBufferView(uint32 lod) { return m_MeshletVertexBufferViews[lod]; }
    agfx::BufferView& GetMeshletTriangleBufferView(uint32 lod) { return m_MeshletTriangleBufferViews[lod]; }

private:
    agfx::Buffer m_VertexBuffer;
    agfx::BufferView m_VertexBufferView;

    agfx::Buffer m_MeshletBuffers[CaramelAsset::kLodCount];
    agfx::Buffer m_MeshletVertexBuffers[CaramelAsset::kLodCount];
    agfx::Buffer m_MeshletTriangleBuffers[CaramelAsset::kLodCount];

    agfx::BufferView m_MeshletBufferViews[CaramelAsset::kLodCount];
    agfx::BufferView m_MeshletVertexBufferViews[CaramelAsset::kLodCount];
    agfx::BufferView m_MeshletTriangleBufferViews[CaramelAsset::kLodCount];
};
