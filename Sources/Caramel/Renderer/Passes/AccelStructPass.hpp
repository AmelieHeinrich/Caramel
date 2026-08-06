/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Renderer/RenderPass.hpp>

class AccelerationStructureManager;

// BLAS/TLAS builds for newly resident models, on the async compute queue.
//
// Unlike every other pass, this one cannot be fully self-contained: RenderGraph deliberately does not
// own multi-queue submission (see SetQueueCommandBuffer), so Renderer still drives the compute command
// buffer's Begin/End/Submit and the fence bridging around it. This class owns the graph declaration;
// Renderer owns the queue plumbing.
class AccelStructPass : public RenderPass
{
public:
    explicit AccelStructPass(AccelerationStructureManager& manager) : m_Manager(&manager) {}

    const char* Name() const override { return "Acceleration Structure Build"; }

    bool Enabled(const FrameContext& ctx) const override;

    void Register(RenderGraph& graph, FrameContext& ctx) override;

private:
    AccelerationStructureManager* m_Manager;
};
