/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 10:30:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Renderer/Common.hpp>
#include <Caramel/Renderer/Camera.hpp>
#include <Caramel/Scene/GPUScene.hpp>

#include <AGFX/agfx.hpp>

class SceneRenderer
{
public:
    SceneRenderer(agfx::Device& device, agfx::TextureFormat colorFormat, agfx::TextureFormat depthFormat, uint32 framesInFlight);

    void Render(agfx::RenderPass& renderPass, GPUScene& gpuScene,
                const Camera& camera, uint32 width, uint32 height, uint32 frameIndex);

private:
    agfx::Device* m_Device;

    agfx::Sampler m_Sampler;

    agfx::Buffer m_CameraBuffers[FRAMES_IN_FLIGHT];
    agfx::BufferView m_CameraBufferViews[FRAMES_IN_FLIGHT];
};
