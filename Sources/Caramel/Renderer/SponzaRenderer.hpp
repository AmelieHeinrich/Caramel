/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 10:30:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Renderer/Common.hpp>
#include <Caramel/Renderer/Camera.hpp>
#include <Caramel/Asset/StreamingManager.hpp>

#include <AGFX/agfx.hpp>

// Draws every mesh tracked by a StreamingManager through a mesh-shader pipeline, always at
// whichever LOD is currently resident -- there is no distance/screen-space LOD selection here, on
// purpose: this is a showcase for watching mesh LOD streaming happen, the geometry equivalent of
// the Content Viewer's texture mip thumbnails.
class SponzaRenderer
{
public:
    SponzaRenderer(agfx::Device& device, agfx::TextureFormat colorFormat, agfx::TextureFormat depthFormat, uint32 framesInFlight);

    void Render(agfx::RenderPass& renderPass, StreamingManager& streamingManager, const Camera& camera, uint32 width, uint32 height, uint32 frameIndex);

private:
    agfx::Device* m_Device;

    agfx::Sampler m_Sampler;
    agfx::Texture m_FallbackTexture;
    agfx::TextureView m_FallbackTextureView;

    // One small {mat4 viewProj} constant buffer per frame-in-flight, updated every Render() call.
    agfx::Buffer m_CameraBuffers[FRAMES_IN_FLIGHT];
    agfx::BufferView m_CameraBufferViews[FRAMES_IN_FLIGHT];

    // One {mat4 model} entry per streamed mesh, built once the first time a new mesh shows up
    // (Sponza is static -- no skinning/animation -- so this is a one-shot write, not per-frame).
    agfx::Buffer m_InstanceBuffer;
    agfx::BufferView m_InstanceBufferView;
    uint32 m_InstanceCapacity = 0;
    uint32 m_InstanceCount = 0;

    void EnsureInstanceBuffer(StreamingManager& streamingManager);
};
