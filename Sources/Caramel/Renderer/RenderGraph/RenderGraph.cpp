/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "RenderGraph.hpp"

// ------------------------------------------------------------------------------------------
// RGPassBuilder
// ------------------------------------------------------------------------------------------

void RGPassBuilder::SetQueue(RGQueue queue)
{
    m_Pass.queue = queue;
}

void RGPassBuilder::ReadTexture(RGTextureHandle handle, agfx::ResourceState requiredState)
{
    m_Pass.accesses.PushBack({ RGResourceType::Texture, handle.index, requiredState, false });
}

void RGPassBuilder::WriteTexture(RGTextureHandle handle, agfx::ResourceState requiredState)
{
    m_Pass.accesses.PushBack({ RGResourceType::Texture, handle.index, requiredState, true });
}

void RGPassBuilder::ReadBuffer(RGBufferHandle handle, agfx::ResourceState requiredState)
{
    m_Pass.accesses.PushBack({ RGResourceType::Buffer, handle.index, requiredState, false });
}

void RGPassBuilder::WriteBuffer(RGBufferHandle handle, agfx::ResourceState requiredState)
{
    m_Pass.accesses.PushBack({ RGResourceType::Buffer, handle.index, requiredState, true });
}

void RGPassBuilder::AddColorAttachment(const RGAttachmentDesc& desc)
{
    m_Pass.colorAttachments.PushBack(desc);
    WriteTexture(desc.texture, agfx::ResourceState::RenderTarget);
}

void RGPassBuilder::SetDepthAttachment(const RGAttachmentDesc& desc)
{
    m_Pass.hasDepthAttachment = true;
    m_Pass.depthAttachment = desc;
    WriteTexture(desc.texture, agfx::ResourceState::DepthWrite);
}

void RGPassBuilder::MarkSwapchainEdge(RGTextureHandle handle)
{
    m_Graph.m_Textures[handle.index].isSwapchainResource = true;
}

void RGPassBuilder::AlwaysExecute()
{
    m_Pass.forceKeep = true;
}

void RGPassBuilder::MarkAsExternallyRead(RGTextureHandle handle, agfx::ResourceState state)
{
    RGTextureDesc& tex = m_Graph.m_Textures[handle.index];
    tex.externallyRead = true;
    tex.externallyReadState = state;
}

// ------------------------------------------------------------------------------------------
// RGResolveContext
// ------------------------------------------------------------------------------------------

agfx::Texture& RGResolveContext::ResolveTexture(RGTextureHandle handle)
{
    return m_Graph.m_Textures[handle.index].Resolve();
}

agfx::Buffer& RGResolveContext::ResolveBuffer(RGBufferHandle handle)
{
    return m_Graph.m_Buffers[handle.index].Resolve();
}

agfx::RenderTarget& RGResolveContext::ResolveRenderTarget(RGTextureHandle handle, bool isDepth)
{
    return m_Graph.ResolveRenderTargetInternal(handle.index, isDepth);
}

uint64 RGResolveContext::ResolveBindlessTexture(RGTextureHandle handle)
{
    RGTextureDesc& tex = m_Graph.m_Textures[handle.index];

    auto it = m_Graph.m_BindlessViewCache.Find(handle.index);
    if (it != m_Graph.m_BindlessViewCache.End())
        return it->second.GetHandle();

    agfx::TextureFormat format = tex.isImported ? tex.importedTexture->GetInfo().GetFormat() : tex.createInfo.GetFormat();

    agfx::TextureViewCreateInfo viewInfo{};
    viewInfo.SetTexture(tex.Resolve()).SetFormat(format).SetMipRange(0, 1).SetArrayRange(0, 1).SetWriteable(false);

    auto [inserted, ok] = m_Graph.m_BindlessViewCache.emplace(handle.index, m_Graph.m_Device->CreateTextureView(viewInfo));
    return inserted->second.GetHandle();
}

// ------------------------------------------------------------------------------------------
// RenderGraph
// ------------------------------------------------------------------------------------------

RenderGraph::RenderGraph(agfx::Device& device, RenderGraphAllocator* allocator)
    : m_Device(&device)
    , m_Allocator(allocator)
{
}

RGTextureHandle RenderGraph::ImportTexture(const char* name, agfx::Texture& texture, agfx::ResourceState currentState, RGQueue currentQueue)
{
    RGTextureDesc desc{};
    desc.name = name;
    desc.isImported = true;
    desc.importedTexture = &texture;
    desc.currentState = currentState;
    desc.currentQueue = currentQueue;
    m_Textures.PushBack(std::move(desc));
    return RGTextureHandle{ (uint32)m_Textures.Size() - 1 };
}

RGTextureHandle RenderGraph::CreateTexture(const char* name, const agfx::TextureCreateInfo& info)
{
    RGTextureDesc desc{};
    desc.name = name;
    desc.isImported = false;
    desc.createInfo = info;
    desc.currentState = agfx::ResourceState::Common;
    m_Textures.PushBack(std::move(desc));
    return RGTextureHandle{ (uint32)m_Textures.Size() - 1 };
}

RGBufferHandle RenderGraph::ImportBuffer(const char* name, agfx::Buffer& buffer, agfx::ResourceState currentState, RGQueue currentQueue)
{
    RGBufferDesc desc{};
    desc.name = name;
    desc.isImported = true;
    desc.importedBuffer = &buffer;
    desc.currentState = currentState;
    desc.currentQueue = currentQueue;
    m_Buffers.PushBack(std::move(desc));
    return RGBufferHandle{ (uint32)m_Buffers.Size() - 1 };
}

RGBufferHandle RenderGraph::CreateBuffer(const char* name, const agfx::BufferCreateInfo& info)
{
    RGBufferDesc desc{};
    desc.name = name;
    desc.isImported = false;
    desc.createInfo = info;
    desc.currentState = agfx::ResourceState::Common;
    m_Buffers.PushBack(std::move(desc));
    return RGBufferHandle{ (uint32)m_Buffers.Size() - 1 };
}

void RenderGraph::AddAttachmentPass(const char* name, SetupFn setup, AttachmentExecuteFn execute)
{
    RGPass pass{};
    pass.name = name;
    pass.isAttachmentPass = true;
    pass.attachmentExecute = std::move(execute);
    m_Passes.PushBack(std::move(pass));

    RGPassBuilder builder(*this, m_Passes.back());
    if (setup)
        setup(builder);
}

void RenderGraph::AddPass(const char* name, SetupFn setup, GenericExecuteFn execute)
{
    RGPass pass{};
    pass.name = name;
    pass.isAttachmentPass = false;
    pass.genericExecute = std::move(execute);
    m_Passes.PushBack(std::move(pass));

    RGPassBuilder builder(*this, m_Passes.back());
    if (setup)
        setup(builder);
}

void RenderGraph::SetQueueCommandBuffer(RGQueue queue, agfx::CommandBuffer* commandBuffer)
{
    m_QueueCommandBuffers[(uint8)queue] = commandBuffer;
}

agfx::CommandBuffer& RenderGraph::ResolveQueueCommandBuffer(RGQueue queue)
{
    auto it = m_QueueCommandBuffers.Find((uint8)queue);
    assert(it != m_QueueCommandBuffers.End() && it->second && "RenderGraph pass declared a queue with no command buffer registered via SetQueueCommandBuffer");
    return *it->second;
}

void RenderGraph::Cull()
{
    TArray<bool> textureNeeded(m_Textures.Size(), false);
    TArray<bool> bufferNeeded(m_Buffers.Size(), false);

    for (int64 i = (int64)m_Passes.Size() - 1; i >= 0; --i) {
        RGPass& pass = m_Passes[(size_t)i];

        bool isSink = false;
        for (const RGResourceAccess& access : pass.accesses) {
            if (!access.isWrite)
                continue;

            if (access.type == RGResourceType::Texture) {
                const RGTextureDesc& tex = m_Textures[access.index];
                if (tex.isImported || tex.externallyRead || textureNeeded[access.index])
                    isSink = true;
            } else {
                const RGBufferDesc& buf = m_Buffers[access.index];
                if (buf.isImported || buf.externallyRead || bufferNeeded[access.index])
                    isSink = true;
            }
        }

        pass.kept = isSink || pass.forceKeep;
        if (pass.kept) {
            for (const RGResourceAccess& access : pass.accesses) {
                if (access.type == RGResourceType::Texture)
                    textureNeeded[access.index] = true;
                else
                    bufferNeeded[access.index] = true;
            }
        }
    }
}

void RenderGraph::ComputeLifetimes()
{
    for (uint32 i = 0; i < m_Passes.Size(); ++i) {
        RGPass& pass = m_Passes[i];
        if (!pass.kept)
            continue;

        for (const RGResourceAccess& access : pass.accesses) {
            if (access.type == RGResourceType::Texture) {
                RGTextureDesc& tex = m_Textures[access.index];
                if (tex.firstUsePass == UINT32_MAX)
                    tex.firstUsePass = i;
                tex.lastUsePass = i;
            } else {
                RGBufferDesc& buf = m_Buffers[access.index];
                if (buf.firstUsePass == UINT32_MAX)
                    buf.firstUsePass = i;
                buf.lastUsePass = i;
            }
        }
    }
}

void RenderGraph::DiffBarriers()
{
    for (uint32 i = 0; i < m_Passes.Size(); ++i) {
        RGPass& pass = m_Passes[i];
        if (!pass.kept)
            continue;

        for (const RGResourceAccess& access : pass.accesses) {
            if (access.type == RGResourceType::Texture) {
                RGTextureDesc& tex = m_Textures[access.index];
                bool isFirstAccess = (i == tex.firstUsePass);

                if (isFirstAccess && tex.aliasSource.IsValid()) {
                    agfx::ResourceState outgoing = tex.aliasSource.type == RGResourceType::Texture
                        ? m_Textures[tex.aliasSource.index].currentState
                        : m_Buffers[tex.aliasSource.index].currentState;
                    RGBarrierOp op{ RGResourceType::Texture, access.index, outgoing, access.requiredState, true, true };
                    pass.preBarriers.PushBack(op);
                    tex.currentState = access.requiredState;
                } else if (tex.currentState != access.requiredState) {
                    pass.preBarriers.PushBack({ RGResourceType::Texture, access.index, tex.currentState, access.requiredState, !tex.isSwapchainResource, false });
                    tex.currentState = access.requiredState;
                }

                bool hasPriorProducer = tex.isImported || tex.lastTouchedPass >= 0;
                if (hasPriorProducer && tex.currentQueue != pass.queue) {
                    m_CrossQueueEdges.PushBack({ tex.currentQueue, pass.queue,
                        tex.lastTouchedPass >= 0 ? (uint32)tex.lastTouchedPass : UINT32_MAX, i, RGResourceType::Texture, access.index });
                }
                tex.currentQueue = pass.queue;
                tex.lastTouchedPass = (int32)i;
            } else {
                RGBufferDesc& buf = m_Buffers[access.index];
                bool isFirstAccess = (i == buf.firstUsePass);

                agfx::ResourceState oldState = buf.currentState;
                if (isFirstAccess && buf.aliasSource.IsValid()) {
                    oldState = buf.aliasSource.type == RGResourceType::Texture
                        ? m_Textures[buf.aliasSource.index].currentState
                        : m_Buffers[buf.aliasSource.index].currentState;
                }
                if (oldState != access.requiredState) {
                    pass.preBarriers.PushBack({ RGResourceType::Buffer, access.index, oldState, access.requiredState, true, false });
                    buf.currentState = access.requiredState;
                }

                bool hasPriorProducer = buf.isImported || buf.lastTouchedPass >= 0;
                if (hasPriorProducer && buf.currentQueue != pass.queue) {
                    m_CrossQueueEdges.PushBack({ buf.currentQueue, pass.queue,
                        buf.lastTouchedPass >= 0 ? (uint32)buf.lastTouchedPass : UINT32_MAX, i, RGResourceType::Buffer, access.index });
                }
                buf.currentQueue = pass.queue;
                buf.lastTouchedPass = (int32)i;
            }
        }
    }

    for (uint32 i = 0; i < m_Textures.Size(); ++i) {
        RGTextureDesc& tex = m_Textures[i];
        if (tex.externallyRead && tex.currentState != tex.externallyReadState) {
            m_TrailingBarriers.PushBack({ RGResourceType::Texture, i, tex.currentState, tex.externallyReadState, !tex.isSwapchainResource, false });
            tex.currentState = tex.externallyReadState;
        }
    }
    for (uint32 i = 0; i < m_Buffers.Size(); ++i) {
        RGBufferDesc& buf = m_Buffers[i];
        if (buf.externallyRead && buf.currentState != buf.externallyReadState) {
            m_TrailingBarriers.PushBack({ RGResourceType::Buffer, i, buf.currentState, buf.externallyReadState, true, false });
            buf.currentState = buf.externallyReadState;
        }
    }
}

void RenderGraph::AllocateTransientResourcesCommitted()
{
    bool createdAny = false;

    for (RGTextureDesc& tex : m_Textures) {
        if (tex.isImported || tex.firstUsePass == UINT32_MAX)
            continue;
        tex.ownedTexture = m_Device->CreateTexture(tex.createInfo);
        tex.ownedTexture.SetName(tex.name.CStr());
        tex.allocationInfo = m_Device->GetTextureAllocationInfo(tex.createInfo);
        createdAny = true;
    }

    for (RGBufferDesc& buf : m_Buffers) {
        if (buf.isImported || buf.firstUsePass == UINT32_MAX)
            continue;
        buf.ownedBuffer = m_Device->CreateBuffer(buf.createInfo);
        buf.ownedBuffer.SetName(buf.name.CStr());
        buf.allocationInfo = m_Device->GetBufferAllocationInfo(buf.createInfo);
        createdAny = true;
    }

    if (createdAny)
        m_Device->MakeResourcesResident();
}

void RenderGraph::Compile()
{
    Cull();
    ComputeLifetimes();

    if (m_Allocator)
        m_Allocator->Allocate(m_Textures, m_Buffers);
    else
        AllocateTransientResourcesCommitted();

    DiffBarriers();
    PopulateDebugInfo();
}

void RenderGraph::EmitBarrier(agfx::CommandBuffer& commandBuffer, const RGBarrierOp& op)
{
    if (op.type == RGResourceType::Texture) {
        if (op.isAliasing)
            commandBuffer.AliasingBarrier(m_Textures[op.index].Resolve(), op.oldState, op.newState, op.agglomerate);
        else
            commandBuffer.TextureBarrier(m_Textures[op.index].Resolve(), op.oldState, op.newState, agfx::AllMips, agfx::AllLayers, op.agglomerate);
    } else {
        commandBuffer.MemoryBarrier(op.oldState, op.newState, op.agglomerate);
    }
}

RenderGraph::TextureSize RenderGraph::GetTextureSize(uint32 index) const
{
    const RGTextureDesc& tex = m_Textures[index];
    if (tex.isImported) {
        agfx::TextureCreateInfo info = tex.importedTexture->GetInfo();
        return { info.width, info.height };
    }
    return { tex.createInfo.width, tex.createInfo.height };
}

agfx::RenderTarget& RenderGraph::ResolveRenderTargetInternal(uint32 textureIndex, bool isDepth)
{
    uint32 key = textureIndex * 2 + (isDepth ? 1u : 0u);

    auto it = m_RenderTargetCache.Find(key);
    if (it != m_RenderTargetCache.End())
        return it->second;

    agfx::RenderTargetCreateInfo info{};
    info.SetTexture(m_Textures[textureIndex].Resolve()).SetIsDepth(isDepth);

    auto [inserted, ok] = m_RenderTargetCache.emplace(key, m_Device->CreateRenderTarget(info));
    return inserted->second;
}

void RenderGraph::Execute(agfx::CommandBuffer& commandBuffer)
{
    // The Graphics queue always uses the caller's directly-passed command buffer, whether or not it
    // was also registered via SetQueueCommandBuffer -- keeps every single-queue caller (every real
    // pass today) unaffected by the multi-queue plumbing below.
    m_QueueCommandBuffers[(uint8)RGQueue::Graphics] = &commandBuffer;

    for (uint32 i = 0; i < m_Passes.Size(); ++i) {
        RGPass& pass = m_Passes[i];
        if (!pass.kept)
            continue;

        agfx::CommandBuffer& queueCommandBuffer = ResolveQueueCommandBuffer(pass.queue);

        for (const RGBarrierOp& op : pass.preBarriers)
            EmitBarrier(queueCommandBuffer, op);

        RGResolveContext ctx(*this, i);

        if (pass.isAttachmentPass) {
            agfx::RenderPassCreateInfo passInfo{};
            passInfo.SetName(pass.name.CStr());

            TextureSize size{ 0, 0 };
            for (const RGAttachmentDesc& att : pass.colorAttachments) {
                agfx::RenderTarget& rt = ResolveRenderTargetInternal(att.texture.index, false);
                agfx::RenderPassAttachment a{};
                a.SetRenderTarget(rt).SetLoadOp(att.loadOp).SetStoreOp(att.storeOp);
                if (att.loadOp == agfx::LoadOp::Clear)
                    a.Clear(att.clearColor[0], att.clearColor[1], att.clearColor[2], att.clearColor[3]);
                passInfo.AddColorAttachment(a);
                if (size.width == 0)
                    size = GetTextureSize(att.texture.index);
            }
            if (pass.hasDepthAttachment) {
                agfx::RenderTarget& rt = ResolveRenderTargetInternal(pass.depthAttachment.texture.index, true);
                agfx::RenderPassAttachment a{};
                a.SetRenderTarget(rt).SetLoadOp(pass.depthAttachment.loadOp).SetStoreOp(pass.depthAttachment.storeOp);
                if (pass.depthAttachment.loadOp == agfx::LoadOp::Clear)
                    a.ClearDepth(pass.depthAttachment.clearDepth);
                passInfo.SetDepthAttachment(a);
                if (size.width == 0)
                    size = GetTextureSize(pass.depthAttachment.texture.index);
            }
            passInfo.SetSize(size.width, size.height);

            agfx::RenderPass renderPass = queueCommandBuffer.BeginRenderPass(passInfo);
            pass.attachmentExecute(renderPass, ctx);
            renderPass.End();
        } else {
            pass.genericExecute(queueCommandBuffer, ctx);
        }
    }

    // Trailing barriers (externally-read resources, e.g. scene-color/backbuffer) always run on the
    // Graphics queue -- every real externally-read resource is consumed outside the graph on the
    // graphics timeline (ImGui next frame, present), and dormant multi-queue passes don't change that.
    for (const RGBarrierOp& op : m_TrailingBarriers)
        EmitBarrier(commandBuffer, op);
}

void RenderGraph::PopulateDebugInfo()
{
    m_DebugInfo.passes.Clear();
    m_DebugInfo.resources.Clear();
    m_DebugInfo.edges.Clear();

    uint32 textureCount = (uint32)m_Textures.Size();

    TArray<int32> lastWriterPass(m_Textures.Size() + m_Buffers.Size(), -1);

    for (uint32 i = 0; i < m_Passes.Size(); ++i) {
        const RGPass& pass = m_Passes[i];

        RGDebugPassInfo info{};
        info.name = pass.name;
        info.queue = pass.queue;
        info.isAttachmentPass = pass.isAttachmentPass;
        info.wasCulled = !pass.kept;

        for (const RGAttachmentDesc& att : pass.colorAttachments)
            info.colorAttachmentResourceIndices.PushBack(att.texture.index);
        info.depthAttachmentResourceIndex = pass.hasDepthAttachment ? (int32)pass.depthAttachment.texture.index : -1;

        if (pass.kept) {
            for (const RGResourceAccess& access : pass.accesses) {
                uint32 mergedIndex = access.type == RGResourceType::Texture ? access.index : textureCount + access.index;

                if (access.isWrite) {
                    info.writeResourceIndices.PushBack(mergedIndex);
                    info.writeStates.PushBack(access.requiredState);
                    lastWriterPass[mergedIndex] = (int32)i;
                } else {
                    info.readResourceIndices.PushBack(mergedIndex);
                    info.readStates.PushBack(access.requiredState);

                    if (lastWriterPass[mergedIndex] >= 0) {
                        RGDebugEdgeInfo edge{};
                        edge.producerPassIndex = (uint32)lastWriterPass[mergedIndex];
                        edge.consumerPassIndex = i;
                        edge.resourceIndex = mergedIndex;
                        edge.isCrossQueue = m_Passes[(size_t)lastWriterPass[mergedIndex]].queue != pass.queue;
                        m_DebugInfo.edges.PushBack(edge);
                    }
                }
            }
        }

        m_DebugInfo.passes.PushBack(std::move(info));
    }

    for (const RGTextureDesc& tex : m_Textures) {
        RGDebugResourceInfo info{};
        info.name = tex.name;
        info.type = RGResourceType::Texture;
        info.isImported = tex.isImported;
        info.firstUsePass = tex.firstUsePass;
        info.lastUsePass = tex.lastUsePass;
        info.sizeBytes = tex.allocationInfo.size;
        info.heapAliasGroup = tex.heapAliasGroup;
        m_DebugInfo.resources.PushBack(std::move(info));
    }
    for (const RGBufferDesc& buf : m_Buffers) {
        RGDebugResourceInfo info{};
        info.name = buf.name;
        info.type = RGResourceType::Buffer;
        info.isImported = buf.isImported;
        info.firstUsePass = buf.firstUsePass;
        info.lastUsePass = buf.lastUsePass;
        info.sizeBytes = buf.allocationInfo.size;
        info.heapAliasGroup = buf.heapAliasGroup;
        m_DebugInfo.resources.PushBack(std::move(info));
    }
}
