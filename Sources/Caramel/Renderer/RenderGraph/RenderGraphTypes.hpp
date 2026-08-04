/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

enum class RGResourceType : uint8
{
    Texture,
    Buffer,
};

enum class RGQueue : uint8
{
    Graphics,
    Compute,
    Transfer,
};

struct RGTextureHandle
{
    static constexpr uint32 kInvalidIndex = 0xFFFFFFFFu;
    uint32 index = kInvalidIndex;

    bool IsValid() const { return index != kInvalidIndex; }
    bool operator==(const RGTextureHandle& other) const { return index == other.index; }
    bool operator!=(const RGTextureHandle& other) const { return index != other.index; }
};

struct RGBufferHandle
{
    static constexpr uint32 kInvalidIndex = 0xFFFFFFFFu;
    uint32 index = kInvalidIndex;

    bool IsValid() const { return index != kInvalidIndex; }
    bool operator==(const RGBufferHandle& other) const { return index == other.index; }
    bool operator!=(const RGBufferHandle& other) const { return index != other.index; }
};

// Identifies the resource that used to occupy a heap-placed transient resource's memory, so barrier
// diffing can source the correct `outgoingState` for its aliasing transition. Invalid (index == -1)
// for a region's first-ever occupant, which needs no aliasing barrier -- just the normal Common ->
// requiredState transition every fresh resource gets.
struct RGAliasSource
{
    RGResourceType type = RGResourceType::Texture;
    int32 index = -1;

    bool IsValid() const { return index >= 0; }
};
