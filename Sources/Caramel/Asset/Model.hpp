/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <CaramelAsset/Format.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

struct ModelLOD
{
    float32 maxError = 0.0f;
    uint32 meshletCount = 0;

    uint64 meshletBufferOffset = 0, meshletBufferLength = 0;
    uint64 meshletVertexBufferOffset = 0, meshletVertexBufferLength = 0;
    uint64 meshletTriangleBufferOffset = 0, meshletTriangleBufferLength = 0;
    uint64 meshletBoundsOffset = 0, meshletBoundsLength = 0;

    uint64 GetByteOffset() const { return meshletBufferOffset; }
    uint64 GetByteLength() const { return (meshletBoundsOffset + meshletBoundsLength) - meshletBufferOffset; }
};

struct ModelMesh
{
    String name;
    int32 materialIndex = -1;
    int32 skinIndex = -1;
    glm::vec3 boundsMin{ 0.0f };
    glm::vec3 boundsMax{ 0.0f };
    bool hasTangent = false;
    bool hasSkin = false;
    uint32 vertexCount = 0;

    uint64 vertexBufferOffset = 0, vertexBufferLength = 0;
    uint64 skinBufferOffset = 0, skinBufferLength = 0;
    uint64 colliderOffset = 0, colliderLength = 0;

    ModelLOD lods[CaramelAsset::kLodCount];
};

struct ModelNode
{
    String name;
    int32 parentIndex = -1;
    glm::vec3 translation{ 0.0f };
    glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 scale{ 1.0f };
    TArray<int32> meshIndices;
    TArray<int32> children;
};

struct ModelMaterial
{
    String name;
    glm::vec4 baseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
    glm::vec3 emissiveFactor{ 0.0f, 0.0f, 0.0f };
    float32 metallicFactor = 1.0f;
    float32 roughnessFactor = 1.0f;
    float32 alphaCutoff = 0.5f;
    String alphaMode;
    bool doubleSided = false;

    String baseColorTexture;
    String normalTexture;
    String metallicRoughnessTexture;
    String occlusionTexture;
    String emissiveTexture;
};

enum class MaterialTextureSlot : uint8
{
    BaseColor = 0,
    Normal,
    MetallicRoughness,
    Occlusion,
    Emissive,
    Count
};

class CPUModel
{
public:
    CPUModel() = default;
    explicit CPUModel(const String& path) { Load(path); }

    bool Load(const String& path);

    bool IsValid() const { return m_BinaryChunkSize > 0; }

    const TArray<ModelMesh>& GetMeshes() const { return m_Meshes; }
    const TArray<ModelNode>& GetNodes() const { return m_Nodes; }
    const TArray<ModelMaterial>& GetMaterials() const { return m_Materials; }
    TArray<ModelMaterial>& GetMaterials() { return m_Materials; }

    uint64 LoadVertexBuffer(uint32 meshIndex, void* destination) const;
    uint64 LoadSkinBuffer(uint32 meshIndex, void* destination) const;

    uint64 LoadLOD(uint32 meshIndex, uint32 lodIndex, void* destination) const;

    uint64 LoadCollider(uint32 meshIndex, void* destination) const;

private:
    uint64 ReadBinaryRange(uint64 offset, uint64 length, void* destination) const;

    String m_Path;
    uint64 m_BinaryChunkStart = 0;
    uint64 m_BinaryChunkSize = 0;

    TArray<ModelMesh> m_Meshes;
    TArray<ModelNode> m_Nodes;
    TArray<ModelMaterial> m_Materials;
};
