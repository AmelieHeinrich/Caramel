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

// One LOD's byte range within the .cmdl binary chunk. meshletBuffer/meshletVertexBuffer/
// meshletTriangleBuffer/bounds are appended contiguously by the compiler (see
// Compressor.cpp::SerializeMesh -- nothing pads between them), so the whole LOD is one contiguous
// span from meshletBufferOffset to meshletBoundsOffset+meshletBoundsLength; GetByteOffset()/
// GetByteLength() give that span directly. lods[] is coarse-to-fine: index 0 is the coarsest.
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
    uint64 skinBufferOffset = 0, skinBufferLength = 0; // both 0 when !hasSkin
    uint64 colliderOffset = 0, colliderLength = 0; // both 0 when the compiler couldn't cook a collider

    ModelLOD lods[CaramelAsset::kLodCount]; // coarse-to-fine
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

// Resolved description of a Materials/*.json entry: scalar factors plus each texture slot's path,
// already joined with the .cmdl's directory (empty string = slot unused). Parsed eagerly in
// CPUModel::Load rather than scheduled -- material JSON is a few hundred bytes, there's nothing
// worth deferring a read for. The textures it names are NOT loaded here; resolve each path with a
// CPUTexture when you actually need it.
struct ModelMaterial
{
    String name;
    glm::vec4 baseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
    glm::vec3 emissiveFactor{ 0.0f, 0.0f, 0.0f };
    float32 metallicFactor = 1.0f;
    float32 roughnessFactor = 1.0f;
    float32 alphaCutoff = 0.5f;
    String alphaMode; // "Opaque" | "Mask" | "Blend", kept verbatim from the JSON
    bool doubleSided = false;

    String baseColorTexture;
    String normalTexture;
    String metallicRoughnessTexture;
    String occlusionTexture;
    String emissiveTexture;
};

// Identifies one of ModelMaterial's texture slots -- used to key streamed textures per (model,
// material, slot) instead of just (model, material), since a material has up to 5 independent ones.
enum class MaterialTextureSlot : uint8
{
    BaseColor = 0,
    Normal,
    MetallicRoughness,
    Occlusion,
    Emissive,
    Count
};

// Parses a compiled .cmdl file's header and JSON chunk up front -- cheap, at most a few MB of
// text -- and exposes on-demand, per-range reads of the binary chunk, which can be hundreds of MB.
// Each Load*() call is self-contained (opens its own file handle) so it's safe to fire off from
// any thread, e.g. one JobSystem job per call, letting a caller schedule exactly the bytes it
// currently wants instead of loading the whole file. This class does no GPU work and tracks no
// completion/readiness state -- that's the streaming layer built on top of it.
class CPUModel
{
public:
    CPUModel() = default;
    // Convenience constructor equivalent to CPUModel{} + Load(path); check IsValid() to see
    // whether it succeeded.
    explicit CPUModel(const String& path) { Load(path); }

    // Opens `path` and parses the header + JSON chunk (including every referenced Materials/*.json).
    // Returns false, leaving the asset empty, on a missing file or a bad magic/version.
    bool Load(const String& path);

    bool IsValid() const { return m_BinaryChunkSize > 0; }

    const TArray<ModelMesh>& GetMeshes() const { return m_Meshes; }
    const TArray<ModelNode>& GetNodes() const { return m_Nodes; }
    const TArray<ModelMaterial>& GetMaterials() const { return m_Materials; }
    // Mutable access for scene-local material overrides (Scene::ApplyMaterialOverrides) -- each
    // Scene entity gets its own private CPUModel instance (ExecuteModelLoad parses a fresh one per
    // LoadModel() call, never shared/cached across entities), so mutating in place here never leaks
    // into other entities or scenes that happen to reference the same .cmdl path.
    TArray<ModelMaterial>& GetMaterials() { return m_Materials; }

    // Reads mesh[meshIndex]'s Vertex[]/SkinVertex[] range into `destination`, which must be at
    // least vertexBufferLength/skinBufferLength bytes. Returns the number of bytes actually read
    // (0 on failure/out-of-range).
    uint64 LoadVertexBuffer(uint32 meshIndex, void* destination) const;
    uint64 LoadSkinBuffer(uint32 meshIndex, void* destination) const;

    // Reads mesh[meshIndex].lods[lodIndex]'s whole contiguous span (meshlets + meshlet vertices +
    // meshlet triangles + bounds) into `destination` in one read; must be at least
    // lods[lodIndex].GetByteLength() bytes. Returns the number of bytes actually read.
    uint64 LoadLOD(uint32 meshIndex, uint32 lodIndex, void* destination) const;

    // Reads mesh[meshIndex]'s cooked JPH::Shape::SaveBinaryState() bytes into `destination`, which
    // must be at least colliderLength bytes. Returns 0 (no-op) if the mesh has no collider.
    uint64 LoadCollider(uint32 meshIndex, void* destination) const;

private:
    uint64 ReadBinaryRange(uint64 offset, uint64 length, void* destination) const;

    String m_Path;
    uint64 m_BinaryChunkStart = 0; // absolute byte offset of the binary chunk within the file
    uint64 m_BinaryChunkSize = 0;

    TArray<ModelMesh> m_Meshes;
    TArray<ModelNode> m_Nodes;
    TArray<ModelMaterial> m_Materials;
};
