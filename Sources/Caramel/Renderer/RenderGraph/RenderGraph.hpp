/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Renderer/RenderGraph/RenderGraphTypes.hpp>
#include <Caramel/Renderer/RenderGraph/RenderGraphResources.hpp>
#include <Caramel/Renderer/RenderGraph/RenderGraphBuilder.hpp>
#include <Caramel/Renderer/RenderGraph/RenderGraphDebugInfo.hpp>
#include <Caramel/Renderer/RenderGraph/RenderGraphAllocator.hpp>

#include <AGFX/agfx.hpp>

#include <functional>

class RenderGraph;
class RGResolveContext;

struct RGResourceAccess
{
    RGResourceType type = RGResourceType::Texture;
    uint32 index = 0;
    agfx::ResourceState requiredState = agfx::ResourceState::Common;
    bool isWrite = false;
};

struct RGBarrierOp
{
    RGResourceType type = RGResourceType::Texture;
    uint32 index = 0;
    agfx::ResourceState oldState = agfx::ResourceState::Common;
    agfx::ResourceState newState = agfx::ResourceState::Common;
    bool agglomerate = true;
    // Textures only: emit agfxCommandBufferAliasingBarrier instead of a plain TextureBarrier. Buffers
    // need no such distinction -- MemoryBarrier is used for both plain and aliasing transitions,
    // AGFX has no separate buffer-aliasing entry point (see RenderGraphAllocator's header comment).
    bool isAliasing = false;
};

// A pass on `consumerQueue` accesses a resource whose most recent in-graph touch (or, for an imported
// resource with no in-graph producer yet, its caller-declared starting queue) was on `producerQueue`.
// GPU-side ordering across the two needs a real fence signal/wait, not just the state barrier that's
// diffed regardless -- see RenderGraph::SetQueueCommandBuffer's header comment for the full pattern.
// producerPassIndex is UINT32_MAX when the producer is external (an imported resource's first
// in-graph touch): there is no in-graph pass to hang a Signal off, the caller's own prior submission
// already established the ordering the caller is responsible for bridging.
struct RGCrossQueueEdge
{
    RGQueue producerQueue = RGQueue::Graphics;
    RGQueue consumerQueue = RGQueue::Graphics;
    uint32 producerPassIndex = UINT32_MAX;
    uint32 consumerPassIndex = 0;
    RGResourceType resourceType = RGResourceType::Texture;
    uint32 resourceIndex = 0;
};

struct RGPass
{
    String name;
    RGQueue queue = RGQueue::Graphics;
    bool isAttachmentPass = false;
    bool kept = true;
    bool forceKeep = false;

    TArray<RGResourceAccess> accesses;

    TArray<RGAttachmentDesc> colorAttachments;
    bool hasDepthAttachment = false;
    RGAttachmentDesc depthAttachment;

    TArray<RGBarrierOp> preBarriers;

    std::function<void(agfx::RenderPass&, RGResolveContext&)> attachmentExecute;
    std::function<void(agfx::CommandBuffer&, RGResolveContext&)> genericExecute;
};

// Maps a virtual handle to the real AGFX object inside a pass's execute callback. Handed to every
// execute callback; never stored beyond the callback's own scope.
class RGResolveContext
{
public:
    RGResolveContext(RenderGraph& graph, uint32 passIndex) : m_Graph(graph), m_PassIndex(passIndex) {}

    agfx::Texture& ResolveTexture(RGTextureHandle handle);
    agfx::Buffer& ResolveBuffer(RGBufferHandle handle);
    agfx::RenderTarget& ResolveRenderTarget(RGTextureHandle handle, bool isDepth = false);
    // Bindless handle for a whole-texture, mip-0 view. `writeable` picks a UAV over an SRV, which is
    // what lets a compute pass write a graph-owned transient -- the texture must carry
    // TextureUsage::Storage for that. Read-only and writeable views are cached separately, so asking
    // for both on one texture is fine.
    uint64 ResolveBindlessTexture(RGTextureHandle handle, bool writeable = false);

private:
    RenderGraph& m_Graph;
    uint32 m_PassIndex;
};

// Declarative render graph: passes declare reads/writes/attachments/queue against virtual resource
// handles (setup callbacks run immediately at registration time), Compile() culls unused passes and
// derives every barrier from the declared access pattern, Execute() plays the result back into a
// real command buffer. Rebuilt from scratch every frame -- no state persists across Renderer::Render()
// calls except what the caller explicitly re-imports (see Renderer's cross-frame imported-state cache).
class RenderGraph
{
public:
    using AttachmentExecuteFn = std::function<void(agfx::RenderPass&, RGResolveContext&)>;
    using GenericExecuteFn = std::function<void(agfx::CommandBuffer&, RGResolveContext&)>;
    using SetupFn = std::function<void(RGPassBuilder&)>;

    // `allocator` is optional: pass one (persistent across frames, owned by the caller -- see
    // Renderer's m_RenderGraphAllocator) to route transient resources through heap-placed aliasing;
    // omit it to force every transient resource to committed allocation, e.g. for isolated tests.
    explicit RenderGraph(agfx::Device& device, RenderGraphAllocator* allocator = nullptr);

    // Hands every resource this graph created (transient textures/buffers, and the view + render
    // target caches built during Execute) to the allocator's retirement queue, which frees them once
    // the fence proves the GPU is done with this frame slot. Without an allocator there is nowhere to
    // defer to and they are destroyed here and now -- safe only for the isolated-test case the
    // allocator-less constructor exists for, never for a graph whose work was actually submitted.
    ~RenderGraph();

    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    // Graphics-queue passes only, timestamped in a caller-owned, frame-in-flight-sized QueryPool --
    // see Renderer::m_TimingQueryPools for why the pool itself can't live here (it must persist
    // across frames, RenderGraph doesn't).
    static constexpr uint32 kMaxTimedPasses = 32;

    RGTextureHandle ImportTexture(const char* name, agfx::Texture& texture, agfx::ResourceState currentState, RGQueue currentQueue = RGQueue::Graphics);
    RGTextureHandle CreateTexture(const char* name, const agfx::TextureCreateInfo& info);
    RGBufferHandle ImportBuffer(const char* name, agfx::Buffer& buffer, agfx::ResourceState currentState, RGQueue currentQueue = RGQueue::Graphics);
    RGBufferHandle CreateBuffer(const char* name, const agfx::BufferCreateInfo& info);

    // Framework opens the render pass and hands the execute callback an open agfx::RenderPass& --
    // for consumers that expect to draw into an already-open pass (e.g. SceneRenderer::Render).
    void AddAttachmentPass(const char* name, SetupFn setup, AttachmentExecuteFn execute);

    // Framework hands the execute callback the raw agfx::CommandBuffer&, fully unmanaged -- for
    // consumers that open their own pass(es) internally (e.g. DebugRenderer::Flush) or pure
    // compute/copy work that never opens a render pass at all.
    void AddPass(const char* name, SetupFn setup, GenericExecuteFn execute);

    // Registers the command buffer a queue's passes record into. The Graphics queue always falls
    // back to Execute()'s own `commandBuffer` parameter if not registered here, so single-queue
    // callers (every real pass today) never need to call this. A non-Graphics pass with no registered
    // command buffer for its queue is a caller bug -- see RGPassBuilder::SetQueue.
    //
    // Multi-queue submission is deliberately NOT owned by RenderGraph: Execute() only records barriers
    // and pass work into each queue's command buffer, exactly as it does for the single-queue case --
    // the caller still owns Begin/End/Submit and, for any edge in GetCrossQueueEdges(), the
    // Signal/Wait pair bridging them (producer queue Submit -> Signal(fence, N), consumer queue
    // Wait(fence, N) issued before its own Submit -- CPU issue order, not GPU completion order,
    // is what makes this correct; see agfx::CommandQueue::Signal/Wait).
    void SetQueueCommandBuffer(RGQueue queue, agfx::CommandBuffer* commandBuffer);

    const TArray<RGCrossQueueEdge>& GetCrossQueueEdges() const { return m_CrossQueueEdges; }

    void Compile();
    // Both query pools are optional and independent: pass one to bracket every kept pass on that
    // queue with GPU timestamps (resolved into the pool once, after all passes on that queue) --
    // omit either to skip timing that queue. Each pool must have been created against that queue
    // (its timestamp frequency is queue-pinned, see agfx::Device::CreateQueryPool) and sized at least
    // kMaxTimedPasses * 2.
    void Execute(agfx::CommandBuffer& commandBuffer, agfx::QueryPool* graphicsQueryPool = nullptr, agfx::QueryPool* computeQueryPool = nullptr);

    // Names of the passes actually timestamped this Execute(), in query-pair order (pass N's
    // timestamps are at indices 2N/2N+1) -- empty if Execute() was called without that queue's pool.
    const TArray<String>& GetTimedPassNames() const { return m_TimedPassNames; }
    const TArray<String>& GetTimedComputePassNames() const { return m_TimedComputePassNames; }

    // Final state of an imported resource after Execute(), for the caller's cross-frame state cache
    // (imported resources are re-declared fresh every frame, but their physical GPU state persists).
    agfx::ResourceState GetFinalState(RGTextureHandle handle) const { return m_Textures[handle.index].currentState; }
    agfx::ResourceState GetFinalState(RGBufferHandle handle) const { return m_Buffers[handle.index].currentState; }

    const RenderGraphDebugInfo& GetDebugInfo() const { return m_DebugInfo; }

private:
    friend class RGPassBuilder;
    friend class RGResolveContext;

    struct TextureSize { uint32 width; uint32 height; };

    agfx::Device* m_Device;
    RenderGraphAllocator* m_Allocator;

    TArray<RGTextureDesc> m_Textures;
    TArray<RGBufferDesc> m_Buffers;
    TArray<RGPass> m_Passes;
    TArray<RGBarrierOp> m_TrailingBarriers;
    TArray<RGCrossQueueEdge> m_CrossQueueEdges;
    TArray<String> m_TimedPassNames;
    TArray<String> m_TimedComputePassNames;

    TDictionary<uint8, agfx::CommandBuffer*> m_QueueCommandBuffers;

    TDictionary<uint32, agfx::RenderTarget> m_RenderTargetCache;
    TDictionary<uint32, agfx::TextureView> m_BindlessViewCache;

    RenderGraphDebugInfo m_DebugInfo;

    void Cull();
    void ComputeLifetimes();
    void DiffBarriers();
    void AllocateTransientResourcesCommitted();
    void EmitBarrier(agfx::CommandBuffer& commandBuffer, const RGBarrierOp& op);
    agfx::CommandBuffer& ResolveQueueCommandBuffer(RGQueue queue);
    TextureSize GetTextureSize(uint32 index) const;
    agfx::RenderTarget& ResolveRenderTargetInternal(uint32 textureIndex, bool isDepth);
    void PopulateDebugInfo();
};
