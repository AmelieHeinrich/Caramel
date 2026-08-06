/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Renderer/FrameContext.hpp>
#include <Caramel/Renderer/RenderGraph/RenderGraph.hpp>

// One rendering technique, owning its own pipelines and persistent buffers and registering its own
// work against the frame's graph. Renderer holds an ordered list of these and does nothing per pass
// beyond `if (pass->Enabled(ctx)) pass->Register(graph, ctx)`, so adding a technique is a new file
// plus one line in that list -- no Renderer members, no create/resize/import/state-writeback edits.
//
// Registration order is execution order. The graph derives every barrier from declared reads/writes,
// but it does not reorder passes, so the list in Renderer is the authoritative frame outline.
class RenderPass
{
public:
    virtual ~RenderPass() = default;

    /// @brief Display name, used for the graph debug panel and GPU timing rows.
    virtual const char* Name() const = 0;

    /// @brief Viewport changed. Only override to resize resources the pass owns itself; graph
    /// transients are sized from FrameContext at registration time and need nothing here.
    virtual void Resize(uint32 width, uint32 height) {}

    /// @brief Runs before the frame's graph is built, for work that must not be deferred into a pass
    /// callback -- e.g. uploading constants that a later pass's setup reads.
    virtual void BeginFrame(FrameContext& ctx) {}

    /// @brief Skip this pass entirely this frame. A disabled pass registers nothing, so anything it
    /// would have published to the blackboard is absent rather than stale -- consumers must handle
    /// an invalid handle from FrameContext::Get.
    virtual bool Enabled(const FrameContext& ctx) const { return true; }

    /// @brief Declare and record. Call graph.AddPass/AddAttachmentPass here, publishing any output
    /// other passes consume via ctx.Set().
    virtual void Register(RenderGraph& graph, FrameContext& ctx) = 0;
};
