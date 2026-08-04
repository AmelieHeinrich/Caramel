/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Renderer/RenderGraph/RenderGraphTypes.hpp>

#include <AGFX/agfx.hpp>

// Either imported (non-owning, lifetime managed by the caller -- e.g. Renderer's persistent
// scene-color/depth textures, or the swap chain back buffer re-acquired every frame) or transient
// (described by createInfo, allocated during Compile() by RenderGraphAllocator -- heap-placed and
// possibly aliased where supported, committed otherwise).
struct RGTextureDesc
{
    String name;
    bool isImported = false;

    // Present<->RenderTarget transitions on this resource must keep agglomerate=false (Metal drops
    // the barrier otherwise) -- set via RGPassBuilder::MarkSwapchainEdge.
    bool isSwapchainResource = false;

    agfx::TextureCreateInfo createInfo;
    agfx::Texture* importedTexture = nullptr;

    agfx::Texture ownedTexture;
    agfxAllocationInfo allocationInfo{};

    uint32 firstUsePass = UINT32_MAX;
    uint32 lastUsePass = 0;

    // Populated by RenderGraphAllocator::Allocate() when heap-placed; -1 (committed) otherwise.
    int32 heapAliasGroup = -1;
    RGAliasSource aliasSource;
    uint64 pendingHeapOffset = 0;

    agfx::ResourceState currentState = agfx::ResourceState::Common;
    RGQueue currentQueue = RGQueue::Graphics;
    // Pass index of the most recent in-graph access, updated incrementally during DiffBarriers (unlike
    // firstUsePass/lastUsePass, which are the resource's full lifetime bounds computed up front). -1
    // until first touched in-graph -- used to tell a genuine cross-queue edge between two in-graph
    // passes apart from an imported resource's first in-graph touch (no in-graph producer to sync).
    int32 lastTouchedPass = -1;

    // Set via RGPassBuilder::MarkAsExternallyRead -- keeps this resource's producer pass(es) alive
    // through culling and appends a trailing barrier to `externallyReadState` after the last
    // in-graph write, even with no in-graph reader (e.g. scene-color, sampled by ImGui next frame
    // through a raw bindless handle the graph never sees).
    bool externallyRead = false;
    agfx::ResourceState externallyReadState = agfx::ResourceState::Common;

    agfx::Texture& Resolve() { return isImported ? *importedTexture : ownedTexture; }
};

struct RGBufferDesc
{
    String name;
    bool isImported = false;

    agfx::BufferCreateInfo createInfo;
    agfx::Buffer* importedBuffer = nullptr;

    agfx::Buffer ownedBuffer;
    agfxAllocationInfo allocationInfo{};

    uint32 firstUsePass = UINT32_MAX;
    uint32 lastUsePass = 0;

    int32 heapAliasGroup = -1;
    RGAliasSource aliasSource;
    uint64 pendingHeapOffset = 0;

    agfx::ResourceState currentState = agfx::ResourceState::Common;
    RGQueue currentQueue = RGQueue::Graphics;
    int32 lastTouchedPass = -1;

    bool externallyRead = false;
    agfx::ResourceState externallyReadState = agfx::ResourceState::Common;

    agfx::Buffer& Resolve() { return isImported ? *importedBuffer : ownedBuffer; }
};
