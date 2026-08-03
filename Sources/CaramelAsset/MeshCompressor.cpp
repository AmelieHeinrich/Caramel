/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 18:54:33
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "MeshCompressor.hpp"

#include <meshoptimizer.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>

namespace CaramelAsset
{
    static glm::vec3 OrthogonalVector(const glm::vec3& n)
    {
        return (std::fabs(n.x) > std::fabs(n.z))
            ? glm::normalize(glm::vec3(-n.y, n.x, 0.0f))
            : glm::normalize(glm::vec3(0.0f, -n.z, n.y));
    }

    void MeshCompressor::ExtractAttributes(const cgltf_primitive& primitive, const cgltf_skin* skin, CompiledMesh& outMesh, TArray<uint32>& outIndices)
    {
        const cgltf_accessor* positionAccessor = nullptr;
        const cgltf_accessor* normalAccessor = nullptr;
        const cgltf_accessor* tangentAccessor = nullptr;
        const cgltf_accessor* uvAccessor = nullptr;
        const cgltf_accessor* jointsAccessor = nullptr;
        const cgltf_accessor* weightsAccessor = nullptr;

        for (cgltf_size i = 0; i < primitive.attributes_count; i++)
        {
            const cgltf_attribute& attribute = primitive.attributes[i];
            switch (attribute.type)
            {
                case cgltf_attribute_type_position: positionAccessor = attribute.data; break;
                case cgltf_attribute_type_normal: normalAccessor = attribute.data; break;
                case cgltf_attribute_type_tangent: tangentAccessor = attribute.data; break;
                case cgltf_attribute_type_texcoord: if (attribute.index == 0) uvAccessor = attribute.data; break;
                case cgltf_attribute_type_joints: if (attribute.index == 0) jointsAccessor = attribute.data; break;
                case cgltf_attribute_type_weights: if (attribute.index == 0) weightsAccessor = attribute.data; break;
                default: break;
            }
        }

        if (!positionAccessor)
            return;

        outMesh.hasTangent = tangentAccessor != nullptr;
        outMesh.hasSkin = skin != nullptr && jointsAccessor != nullptr && weightsAccessor != nullptr;

        const cgltf_size vertexCount = positionAccessor->count;
        outMesh.vertices.Resize(vertexCount);
        if (outMesh.hasSkin)
            outMesh.skinVertices.Resize(vertexCount);

        for (cgltf_size i = 0; i < vertexCount; i++)
        {
            Vertex& vertex = outMesh.vertices[i];

            float32 f[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

            cgltf_accessor_read_float(positionAccessor, i, f, 3);
            vertex.position = glm::vec3(f[0], f[1], f[2]);

            if (normalAccessor)
            {
                cgltf_accessor_read_float(normalAccessor, i, f, 3);
                vertex.normal = glm::normalize(glm::vec3(f[0], f[1], f[2]));
            }
            else
            {
                vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }

            if (uvAccessor)
            {
                cgltf_accessor_read_float(uvAccessor, i, f, 2);
                vertex.uv = glm::vec2(f[0], f[1]);
            }
            else
            {
                vertex.uv = glm::vec2(0.0f, 0.0f);
            }

            if (tangentAccessor)
            {
                cgltf_accessor_read_float(tangentAccessor, i, f, 4);
                vertex.tangent = glm::vec4(f[0], f[1], f[2], f[3]);
            }
            else
            {
                vertex.tangent = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // regenerated below by the caller
            }

            if (outMesh.hasSkin)
            {
                SkinVertex& skinVertex = outMesh.skinVertices[i];

                cgltf_uint joints[4] = { 0, 0, 0, 0 };
                cgltf_accessor_read_uint(jointsAccessor, i, joints, 4);

                float32 weights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                cgltf_accessor_read_float(weightsAccessor, i, weights, 4);

                for (int32 c = 0; c < 4; c++)
                {
                    skinVertex.jointIndices[c] = static_cast<uint16>(joints[c]);
                    skinVertex.jointWeights[c] = weights[c];
                }
            }
        }

        if (primitive.indices)
        {
            outIndices.Resize(primitive.indices->count);
            for (cgltf_size i = 0; i < primitive.indices->count; i++)
                outIndices[i] = static_cast<uint32>(cgltf_accessor_read_index(primitive.indices, i));
        }
        else
        {
            outIndices.Resize(vertexCount);
            for (cgltf_size i = 0; i < vertexCount; i++)
                outIndices[i] = static_cast<uint32>(i);
        }
    }

    void MeshCompressor::GenerateTangents(TArray<Vertex>& vertices, const TArray<uint32>& indices)
    {
        TArray<glm::vec3> tan1(vertices.Size(), glm::vec3(0.0f));
        TArray<glm::vec3> tan2(vertices.Size(), glm::vec3(0.0f));

        for (size_t i = 0; i + 2 < indices.Size(); i += 3)
        {
            uint32 i0 = indices[i];
            uint32 i1 = indices[i + 1];
            uint32 i2 = indices[i + 2];

            const Vertex& v0 = vertices[i0];
            const Vertex& v1 = vertices[i1];
            const Vertex& v2 = vertices[i2];

            glm::vec3 e1 = v1.position - v0.position;
            glm::vec3 e2 = v2.position - v0.position;
            glm::vec2 duv1 = v1.uv - v0.uv;
            glm::vec2 duv2 = v2.uv - v0.uv;

            float32 denom = duv1.x * duv2.y - duv2.x * duv1.y;
            float32 r = (std::fabs(denom) < 1e-8f) ? 0.0f : (1.0f / denom);

            glm::vec3 sdir = (e1 * duv2.y - e2 * duv1.y) * r;
            glm::vec3 tdir = (e2 * duv1.x - e1 * duv2.x) * r;

            tan1[i0] += sdir; tan1[i1] += sdir; tan1[i2] += sdir;
            tan2[i0] += tdir; tan2[i1] += tdir; tan2[i2] += tdir;
        }

        for (size_t i = 0; i < vertices.Size(); i++)
        {
            const glm::vec3& n = vertices[i].normal;
            const glm::vec3& t = tan1[i];

            glm::vec3 tangent = t - n * glm::dot(n, t);
            float32 len = glm::length(tangent);
            tangent = (len > 1e-8f) ? (tangent / len) : OrthogonalVector(n);

            float32 handedness = (glm::dot(glm::cross(n, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;
            vertices[i].tangent = glm::vec4(tangent, handedness);
        }
    }

    MeshletLOD MeshCompressor::BuildMeshletLOD(const TArray<Vertex>& vertices, const TArray<uint32>& indices, float32 achievedError)
    {
        MeshletLOD lod;
        lod.maxError = achievedError;

        if (indices.IsEmpty() || vertices.IsEmpty())
            return lod;

        const float32* positions = reinterpret_cast<const float32*>(vertices.Data());

        size_t maxMeshlets = meshopt_buildMeshletsBound(indices.Size(), kMeshletMaxVertices, kMeshletMaxTriangles);

        TArray<meshopt_Meshlet> rawMeshlets(maxMeshlets);
        TArray<uint32> meshletVertices(maxMeshlets * kMeshletMaxVertices);
        TArray<uint8> meshletTriangles(maxMeshlets * kMeshletMaxTriangles * 3);

        size_t meshletCount = meshopt_buildMeshlets(
            rawMeshlets.Data(), meshletVertices.Data(), meshletTriangles.Data(),
            indices.Data(), indices.Size(),
            positions, vertices.Size(), sizeof(Vertex),
            kMeshletMaxVertices, kMeshletMaxTriangles, kMeshletConeWeight);

        rawMeshlets.Resize(meshletCount);

        if (meshletCount > 0)
        {
            const meshopt_Meshlet& last = rawMeshlets[meshletCount - 1];
            meshletVertices.Resize(last.vertex_offset + last.vertex_count);
            meshletTriangles.Resize(last.triangle_offset + last.triangle_count * 3);
        }
        else
        {
            meshletVertices.Clear();
            meshletTriangles.Clear();
        }

        lod.meshlets.Reserve(meshletCount);
        lod.bounds.Reserve(meshletCount);

        for (size_t i = 0; i < meshletCount; i++)
        {
            const meshopt_Meshlet& m = rawMeshlets[i];

            meshopt_optimizeMeshlet(&meshletVertices[m.vertex_offset], &meshletTriangles[m.triangle_offset], m.triangle_count, m.vertex_count);

            lod.meshlets.PushBack(MeshletDesc{ m.vertex_offset, m.triangle_offset, m.vertex_count, m.triangle_count });

            meshopt_Bounds bounds = meshopt_computeMeshletBounds(
                &meshletVertices[m.vertex_offset], &meshletTriangles[m.triangle_offset], m.triangle_count,
                positions, vertices.Size(), sizeof(Vertex));

            MeshletCullData cull;
            cull.center = glm::make_vec3(bounds.center);
            cull.radius = bounds.radius;
            cull.coneApex = glm::make_vec3(bounds.cone_apex);
            cull.coneAxis = glm::make_vec3(bounds.cone_axis);
            cull.coneCutoff = bounds.cone_cutoff;
            lod.bounds.PushBack(cull);
        }

        lod.meshletVertices = std::move(meshletVertices);
        lod.meshletTriangles = std::move(meshletTriangles);

        return lod;
    }

    CompiledMesh MeshCompressor::Compile(const cgltf_primitive& primitive, const cgltf_skin* skin, const String& name)
    {
        CompiledMesh mesh;
        mesh.name = name;

        TArray<uint32> rawIndices;
        ExtractAttributes(primitive, skin, mesh, rawIndices);

        if (mesh.vertices.IsEmpty() || rawIndices.IsEmpty())
            return mesh;

        // Deduplicate binary-equivalent vertices (identity determined by position/normal/tangent/uv only).
        TArray<uint32> remap(mesh.vertices.Size());
        size_t uniqueCount = meshopt_generateVertexRemap(remap.Data(), rawIndices.Data(), rawIndices.Size(), mesh.vertices.Data(), mesh.vertices.Size(), sizeof(Vertex));

        TArray<uint32> indices(rawIndices.Size());
        meshopt_remapIndexBuffer(indices.Data(), rawIndices.Data(), rawIndices.Size(), remap.Data());

        TArray<Vertex> vertices(uniqueCount);
        meshopt_remapVertexBuffer(vertices.Data(), mesh.vertices.Data(), mesh.vertices.Size(), sizeof(Vertex), remap.Data());

        TArray<SkinVertex> skinVertices;
        if (mesh.hasSkin)
        {
            skinVertices.Resize(uniqueCount);
            meshopt_remapVertexBuffer(skinVertices.Data(), mesh.skinVertices.Data(), mesh.skinVertices.Size(), sizeof(SkinVertex), remap.Data());
        }

        if (!mesh.hasTangent)
            GenerateTangents(vertices, indices);

        // Vertex-cache-optimize LOD0, then fetch-optimize once to reorder the buffer *all* LODs will share.
        meshopt_optimizeVertexCache(indices.Data(), indices.Data(), indices.Size(), vertices.Size());

        TArray<uint32> fetchRemap(vertices.Size());
        size_t fetchedCount = meshopt_optimizeVertexFetchRemap(fetchRemap.Data(), indices.Data(), indices.Size(), vertices.Size());

        TArray<uint32> lod0Indices(indices.Size());
        meshopt_remapIndexBuffer(lod0Indices.Data(), indices.Data(), indices.Size(), fetchRemap.Data());

        TArray<Vertex> finalVertices(fetchedCount);
        meshopt_remapVertexBuffer(finalVertices.Data(), vertices.Data(), vertices.Size(), sizeof(Vertex), fetchRemap.Data());

        mesh.vertices = std::move(finalVertices);
        if (mesh.hasSkin)
        {
            TArray<SkinVertex> finalSkin(fetchedCount);
            meshopt_remapVertexBuffer(finalSkin.Data(), skinVertices.Data(), skinVertices.Size(), sizeof(SkinVertex), fetchRemap.Data());
            mesh.skinVertices = std::move(finalSkin);
        }

        mesh.boundsMin = mesh.vertices[0].position;
        mesh.boundsMax = mesh.vertices[0].position;
        for (const Vertex& v : mesh.vertices)
        {
            mesh.boundsMin = glm::min(mesh.boundsMin, v.position);
            mesh.boundsMax = glm::max(mesh.boundsMax, v.position);
        }

        const float32* positions = reinterpret_cast<const float32*>(mesh.vertices.Data());

        // Ratios/errors are indexed fine-to-coarse (generation always simplifies down from the full-res
        // base); mesh.lods[] is stored coarse-to-fine (index 0 = coarsest) so streaming can load it in order.
        static constexpr float32 kLodRatios[kLodCount] = { 1.0f, 0.5f, 0.25f, 0.12f, 0.06f };
        static constexpr float32 kLodTargetErrors[kLodCount] = { 0.0f, 0.01f, 0.03f, 0.06f, 0.12f };

        mesh.lods[kLodCount - 1] = BuildMeshletLOD(mesh.vertices, lod0Indices, 0.0f);

        for (uint32 lodIndex = 1; lodIndex < kLodCount; lodIndex++)
        {
            size_t targetIndexCount = static_cast<size_t>(static_cast<float32>(lod0Indices.Size()) * kLodRatios[lodIndex]);
            targetIndexCount -= targetIndexCount % 3;
            targetIndexCount = std::max<size_t>(targetIndexCount, 3);

            TArray<uint32> lodIndices(lod0Indices.Size());
            float32 resultError = 0.0f;
            size_t newIndexCount = meshopt_simplify(
                lodIndices.Data(), lod0Indices.Data(), lod0Indices.Size(),
                positions, mesh.vertices.Size(), sizeof(Vertex),
                targetIndexCount, kLodTargetErrors[lodIndex], meshopt_SimplifyLockBorder, &resultError);
            lodIndices.Resize(newIndexCount);

            if (newIndexCount > targetIndexCount * 2 && newIndexCount < lod0Indices.Size())
            {
                // meshopt_simplify couldn't hit the target ratio without exceeding the error bound;
                // fall back to the sloppy simplifier which ignores topology to reach the target size.
                TArray<uint32> sloppyIndices(lod0Indices.Size());
                size_t sloppyCount = meshopt_simplifySloppy(
                    sloppyIndices.Data(), lod0Indices.Data(), lod0Indices.Size(),
                    positions, mesh.vertices.Size(), sizeof(Vertex),
                    targetIndexCount, kLodTargetErrors[lodIndex] * 4.0f, &resultError);
                if (sloppyCount > 0 && sloppyCount < newIndexCount)
                {
                    sloppyIndices.Resize(sloppyCount);
                    lodIndices = std::move(sloppyIndices);
                }
            }

            meshopt_optimizeVertexCache(lodIndices.Data(), lodIndices.Data(), lodIndices.Size(), mesh.vertices.Size());

            mesh.lods[kLodCount - 1 - lodIndex] = BuildMeshletLOD(mesh.vertices, lodIndices, resultError);
        }

        return mesh;
    }
}
