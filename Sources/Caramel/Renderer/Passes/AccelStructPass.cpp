/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "AccelStructPass.hpp"

#include <Caramel/Renderer/AccelerationStructureManager.hpp>

bool AccelStructPass::Enabled(const FrameContext& ctx) const
{
    return m_Manager->ShouldBuild();
}

void AccelStructPass::Register(RenderGraph& graph, FrameContext& ctx)
{
    graph.AddPass("Acceleration Structure Build",
        [&](RGPassBuilder& builder) {
            builder.SetQueue(RGQueue::Compute);
            // Its side effects are on BLAS/TLAS objects, which the graph models as neither texture
            // nor buffer -- without this there is no reachable-sink evidence to keep the pass.
            builder.AlwaysExecute();
        },
        [this, &ctx](agfx::CommandBuffer& cmd, RGResolveContext&) {
            m_Manager->RecordBuilds(cmd, *ctx.renderInstances);
        });
}
