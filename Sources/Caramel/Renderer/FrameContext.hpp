/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Renderer/Common.hpp>
#include <Caramel/Renderer/Camera.hpp>
#include <Caramel/Renderer/RenderGraph/RenderGraphTypes.hpp>
#include <Caramel/Scene/RenderInstance.hpp>

#include <AGFX/agfx.hpp>

class GPUScene;
class SchemeRegistry;

// Everything a render pass needs to register itself, assembled once per frame by Renderer before any
// pass runs. Replaces the per-pass bespoke argument lists (`(cmd, gpuScene, hzb, frameIndex)`,
// `(pass, gpuScene, visHandle, depthHandle, w, h, frameIndex)`, ...) with one object, so adding a
// pass never means inventing a new call signature or threading another parameter through Renderer.
//
// The named fields are the targets essentially every pass wants and that Renderer guarantees exist.
// Anything a pass invents for itself -- a bloom chain, an SSAO buffer -- goes through Set()/Get()
// instead, which is what keeps a new technique from having to touch this struct at all.
struct FrameContext
{
    agfx::Device* device = nullptr;
    GPUScene* gpuScene = nullptr;
    const SchemeRegistry* schemes = nullptr;
    const Camera* camera = nullptr;
    const TArray<RenderInstance>* renderInstances = nullptr;

    // Viewport, i.e. the size of every target below except backBuffer, which is the window's.
    uint32 width = 0;
    uint32 height = 0;
    uint32 windowWidth = 0;
    uint32 windowHeight = 0;

    // Frame-in-flight slot, not a monotonic frame counter -- indexes every per-slot resource.
    uint32 frameIndex = 0;

    RGTextureHandle depth;
    RGTextureHandle sceneColor;
    RGTextureHandle sceneLighting;
    RGTextureHandle visibility;
    RGTextureHandle hzb;
    RGTextureHandle backBuffer;
    RGTextureHandle gbuffer[kGBufferTextureCount];

    // Persistent per-mip HZB views, which have no render-graph equivalent (a graph handle resolves to
    // whole-texture mip-0 views only). Owned by Renderer.
    const HZBResources* hzbResources = nullptr;

    void Set(const String& name, RGTextureHandle handle) { m_Blackboard[name] = handle; }

    // Returns an invalid handle when nothing has published `name` yet, so a consumer can degrade
    // rather than crash when its producer pass is disabled.
    RGTextureHandle Get(const String& name) const
    {
        auto it = m_Blackboard.Find(name);
        return it != m_Blackboard.End() ? it->second : RGTextureHandle{};
    }

    void ClearBlackboard() { m_Blackboard.clear(); }

private:
    TDictionary<String, RGTextureHandle> m_Blackboard;
};
