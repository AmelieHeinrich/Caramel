/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Renderer/RenderGraph/RenderGraphTypes.hpp>
#include <Caramel/Renderer/RenderGraph/RenderGraphResources.hpp>

#include <AGFX/agfx.hpp>

// Persistent (owned by Renderer, survives across frames -- unlike RenderGraph itself, which is
// rebuilt from scratch every Render() call) owner of the single placement heap backing every graph's
// transient resources. Packs resources with non-overlapping [firstUsePass, lastUsePass] lifetimes
// into aliased heap regions via greedy first-fit bump allocation: sort by firstUsePass (ties by size
// descending), reuse the first already-free, big-enough, alignment-compatible region or bump-allocate
// a new one. This is deliberately not a full graph-coloring allocator -- at Caramel's pass count
// (a handful today), greedy first-fit gets provably close to optimal and stays trivial to debug; a
// real allocator only pays for itself once graphs have dozens of overlapping transients.
//
// Falls back to committed (heap = nullptr) allocation entirely, for every transient resource, on
// adapters that don't support placement heaps (e.g. D3D12 resource heap tier 1) -- detected once, at
// construction, via a representative GetTextureAllocationInfo query returning {0, 0}.
class RenderGraphAllocator
{
public:
    explicit RenderGraphAllocator(agfx::Device& device);

    bool SupportsPlacementHeaps() const { return m_SupportsPlacementHeaps; }

    // Called once per RenderGraph::Compile(), after Cull() and lifetime computation but before
    // barrier diffing -- decides committed-vs-heap-placed and, for heap-placed resources, creates the
    // real agfx::Texture/Buffer and records which (if any) prior resource's region they alias via
    // `aliasSource` (consumed by RenderGraph::DiffBarriers to swap in an aliasing transition for that
    // resource's first access).
    void Allocate(TArray<RGTextureDesc>& textures, TArray<RGBufferDesc>& buffers);

private:
    struct HeapRegion
    {
        uint64 offset = 0;
        uint64 size = 0;
        uint32 freeAtPass = 0;
        int32 aliasGroup = -1;
        RGResourceType lastOccupantType = RGResourceType::Texture;
        int32 lastOccupantIndex = -1;
    };

    struct Request
    {
        RGResourceType type;
        uint32 index;
        uint64 size;
        uint64 alignment;
        uint32 firstUsePass;
        uint32 lastUsePass;
    };

    agfx::Device* m_Device;
    bool m_SupportsPlacementHeaps;

    agfx::Heap m_Heap;
    uint64 m_HeapCapacity = 0;

    void AllocateCommitted(TArray<RGTextureDesc>& textures, TArray<RGBufferDesc>& buffers);
    bool EnsureHeapCapacity(uint64 requiredSize);
    int32 FindReusableRegion(const TArray<HeapRegion>& regions, const Request& req) const;
};
