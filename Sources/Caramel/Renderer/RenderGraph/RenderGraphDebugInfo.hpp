/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Renderer/RenderGraph/RenderGraphTypes.hpp>

#include <AGFX/agfx.hpp>

// Read-only snapshot of a compiled/executed graph, captured after RenderGraph::Execute() and kept
// around after the graph itself is torn down (the graph is rebuilt from scratch every frame) so the
// resource dependency viewer panel can display last frame's pipeline. Resource indices here are into
// `resources` (textures first, then buffers), not into the live graph's internal per-type arrays.
struct RGDebugPassInfo
{
    String name;
    RGQueue queue = RGQueue::Graphics;
    bool isAttachmentPass = false;
    bool wasCulled = false;

    TArray<uint32> colorAttachmentResourceIndices;
    int32 depthAttachmentResourceIndex = -1;

    TArray<uint32> readResourceIndices;
    TArray<agfx::ResourceState> readStates;
    TArray<uint32> writeResourceIndices;
    TArray<agfx::ResourceState> writeStates;
};

struct RGDebugResourceInfo
{
    String name;
    RGResourceType type = RGResourceType::Texture;
    bool isImported = false;
    uint32 firstUsePass = UINT32_MAX;
    uint32 lastUsePass = 0;
    uint64 sizeBytes = 0;
    int32 heapAliasGroup = -1;
};

struct RGDebugEdgeInfo
{
    uint32 producerPassIndex = 0;
    uint32 consumerPassIndex = 0;
    uint32 resourceIndex = 0;
    bool isCrossQueue = false;
};

struct RenderGraphDebugInfo
{
    TArray<RGDebugPassInfo> passes;
    TArray<RGDebugResourceInfo> resources;
    TArray<RGDebugEdgeInfo> edges;
};

// One entry per pass timed by RenderGraph::Execute() last time its queue's frame-in-flight slot was
// read back -- see Renderer::GetLastPassTimings() (Graphics) and
// AccelerationStructureManager::GetLastPassTimings() (Compute).
struct RGPassTiming
{
    String name;
    float gpuTimeMs = 0.0f;
};
