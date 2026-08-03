/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 19:05:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include "Format.hpp"

#include <cgltf.h>

namespace CaramelAsset
{
    // One meshlet-culling ready index buffer for a single LOD level.
    struct MeshletLOD
    {
        TArray<MeshletDesc> meshlets;
        TArray<uint32> meshletVertices;
        TArray<uint8> meshletTriangles;
        TArray<MeshletCullData> bounds;
        float32 maxError = 0.0f;
    };

    // Output of compiling a single glTF primitive. All LODs share one vertex buffer --
    // meshopt_simplify only ever reduces the index buffer, never the vertex buffer.
    struct CompiledMesh
    {
        String name;
        int32 materialIndex = -1;
        int32 skinIndex = -1;
        glm::vec3 boundsMin{ 0.0f };
        glm::vec3 boundsMax{ 0.0f };
        bool hasTangent = false;
        bool hasSkin = false;
        TArray<Vertex> vertices;
        TArray<SkinVertex> skinVertices; // empty when !hasSkin
        MeshletLOD lods[kLodCount]; // coarse-to-fine: lods[0] is the coarsest, lods[kLodCount - 1] is full detail
    };

    class MeshCompressor
    {
    public:
        static CompiledMesh Compile(const cgltf_primitive& primitive, const cgltf_skin* skin, const String& name);

    private:
        static void ExtractAttributes(const cgltf_primitive& primitive, const cgltf_skin* skin, CompiledMesh& outMesh, TArray<uint32>& outIndices);
        static void GenerateTangents(TArray<Vertex>& vertices, const TArray<uint32>& indices);
        static MeshletLOD BuildMeshletLOD(const TArray<Vertex>& vertices, const TArray<uint32>& indices, float32 achievedError);
    };
}
