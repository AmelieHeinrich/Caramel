/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Renderer/RenderGraph/RenderGraphTypes.hpp>

#include <AGFX/agfx.hpp>

struct RGAttachmentDesc
{
    RGTextureHandle texture;
    agfx::LoadOp loadOp = agfx::LoadOp::Load;
    agfx::StoreOp storeOp = agfx::StoreOp::Store;
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float clearDepth = 1.0f;
};

class RenderGraph;
struct RGPass;

// Declares a pass's reads/writes/attachments/queue against the graph. Passed into a pass's setup
// callback, which runs synchronously (immediately, not deferred) when the pass is registered --
// only the separately-supplied execute callback is deferred to RenderGraph::Execute().
class RGPassBuilder
{
public:
    RGPassBuilder(RenderGraph& graph, RGPass& pass) : m_Graph(graph), m_Pass(pass) {}

    void SetQueue(RGQueue queue);

    void ReadTexture(RGTextureHandle handle, agfx::ResourceState requiredState);
    void WriteTexture(RGTextureHandle handle, agfx::ResourceState requiredState);
    void ReadBuffer(RGBufferHandle handle, agfx::ResourceState requiredState);
    void WriteBuffer(RGBufferHandle handle, agfx::ResourceState requiredState);

    // Attachment-pass only (see RenderGraph::AddAttachmentPass); also declares the matching write.
    void AddColorAttachment(const RGAttachmentDesc& desc);
    void SetDepthAttachment(const RGAttachmentDesc& desc);

    // Marks the resource as the swap chain back buffer -- see RGTextureDesc::isSwapchainResource.
    void MarkSwapchainEdge(RGTextureHandle handle);

    // See RGTextureDesc::externallyRead.
    void MarkAsExternallyRead(RGTextureHandle handle, agfx::ResourceState state);

private:
    RenderGraph& m_Graph;
    RGPass& m_Pass;
};
