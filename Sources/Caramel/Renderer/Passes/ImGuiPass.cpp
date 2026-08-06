/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "ImGuiPass.hpp"

#include <Caramel/Renderer/ImGuiRenderer.hpp>

#include <imgui.h>

void ImGuiPass::Register(RenderGraph& graph, FrameContext& ctx)
{
    graph.AddAttachmentPass("Swapchain Pass",
        [&](RGPassBuilder& builder) {
            builder.MarkSwapchainEdge(ctx.backBuffer);
            builder.MarkAsExternallyRead(ctx.backBuffer, agfx::ResourceState::Present);

            RGAttachmentDesc colorAttachment{};
            colorAttachment.texture = ctx.backBuffer;
            colorAttachment.loadOp = agfx::LoadOp::Clear;
            colorAttachment.storeOp = agfx::StoreOp::Store;
            builder.AddColorAttachment(colorAttachment);
        },
        [this, &ctx](agfx::RenderPass& pass, RGResolveContext&) {
            m_ImGuiRenderer->RenderDrawData(ImGui::GetDrawData(), pass, ctx.windowWidth, ctx.windowHeight, ctx.frameIndex);
        });
}
