/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Renderer/RenderPass.hpp>

class ImGuiRenderer;

// Draws the editor UI straight into the swap chain back buffer. Always last: it is the only pass that
// touches the back buffer, and MarkSwapchainEdge is what keeps the Present transition intact.
class ImGuiPass : public RenderPass
{
public:
    explicit ImGuiPass(ImGuiRenderer& imguiRenderer) : m_ImGuiRenderer(&imguiRenderer) {}

    const char* Name() const override { return "Swapchain Pass"; }

    void Register(RenderGraph& graph, FrameContext& ctx) override;

private:
    ImGuiRenderer* m_ImGuiRenderer;
};
