/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "Model.hpp"

#include <Caramel/Core/FileStream.hpp>
#include <Caramel/Core/Logger.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace
{
    ModelLOD ParseLOD(const nlohmann::json& j)
    {
        ModelLOD lod;
        lod.maxError = j.value("maxError", 0.0f);
        lod.meshletCount = j.value("meshletCount", 0u);
        lod.meshletBufferOffset = j.value("meshletBufferOffset", uint64(0));
        lod.meshletBufferLength = j.value("meshletBufferLength", uint64(0));
        lod.meshletVertexBufferOffset = j.value("meshletVertexBufferOffset", uint64(0));
        lod.meshletVertexBufferLength = j.value("meshletVertexBufferLength", uint64(0));
        lod.meshletTriangleBufferOffset = j.value("meshletTriangleBufferOffset", uint64(0));
        lod.meshletTriangleBufferLength = j.value("meshletTriangleBufferLength", uint64(0));
        lod.meshletBoundsOffset = j.value("meshletBoundsOffset", uint64(0));
        lod.meshletBoundsLength = j.value("meshletBoundsLength", uint64(0));
        return lod;
    }

    ModelMesh ParseMesh(const nlohmann::json& j)
    {
        ModelMesh mesh;
        mesh.name = j.value("name", "");
        mesh.materialIndex = j.value("materialIndex", -1);
        mesh.skinIndex = j.value("skinIndex", -1);

        auto boundsMin = j.value("boundsMin", std::vector<float32>{ 0.0f, 0.0f, 0.0f });
        auto boundsMax = j.value("boundsMax", std::vector<float32>{ 0.0f, 0.0f, 0.0f });
        mesh.boundsMin = glm::vec3(boundsMin[0], boundsMin[1], boundsMin[2]);
        mesh.boundsMax = glm::vec3(boundsMax[0], boundsMax[1], boundsMax[2]);

        mesh.hasTangent = j.value("hasTangent", false);
        mesh.hasSkin = j.value("hasSkin", false);
        mesh.vertexCount = j.value("vertexCount", 0u);
        mesh.vertexBufferOffset = j.value("vertexBufferOffset", uint64(0));
        mesh.vertexBufferLength = j.value("vertexBufferLength", uint64(0));
        mesh.skinBufferOffset = j.value("skinBufferOffset", uint64(0));
        mesh.skinBufferLength = j.value("skinBufferLength", uint64(0));

        const auto& lodsJson = j.at("lods");
        for (uint32 i = 0; i < CaramelAsset::kLodCount && i < lodsJson.size(); i++)
            mesh.lods[i] = ParseLOD(lodsJson[i]);

        return mesh;
    }

    ModelNode ParseNode(const nlohmann::json& j)
    {
        ModelNode node;
        node.name = j.value("name", "");
        node.parentIndex = j.value("parentIndex", -1);

        auto t = j.value("translation", std::vector<float32>{ 0.0f, 0.0f, 0.0f });
        node.translation = glm::vec3(t[0], t[1], t[2]);

        // glTF/Compressor.cpp store rotation as [x, y, z, w]; glm::quat's constructor takes (w, x, y, z).
        auto r = j.value("rotation", std::vector<float32>{ 0.0f, 0.0f, 0.0f, 1.0f });
        node.rotation = glm::quat(r[3], r[0], r[1], r[2]);

        auto s = j.value("scale", std::vector<float32>{ 1.0f, 1.0f, 1.0f });
        node.scale = glm::vec3(s[0], s[1], s[2]);

        if (j.contains("meshIndices"))
            for (const auto& index : j["meshIndices"])
                node.meshIndices.PushBack(index.get<int32>());

        if (j.contains("children"))
            for (const auto& index : j["children"])
                node.children.PushBack(index.get<int32>());

        return node;
    }

    ModelMaterial ParseMaterial(const std::filesystem::path& materialPath, const std::filesystem::path& baseDirectory)
    {
        ModelMaterial material;

        std::ifstream file(materialPath);
        if (!file)
        {
            CARAMEL_WARN("CPUModel: failed to open material '{}'", materialPath.string());
            return material;
        }

        nlohmann::json j;
        file >> j;

        material.name = j.value("name", "");

        auto baseColorFactor = j.value("baseColorFactor", std::vector<float32>{ 1.0f, 1.0f, 1.0f, 1.0f });
        material.baseColorFactor = glm::vec4(baseColorFactor[0], baseColorFactor[1], baseColorFactor[2], baseColorFactor[3]);

        auto emissiveFactor = j.value("emissiveFactor", std::vector<float32>{ 0.0f, 0.0f, 0.0f });
        material.emissiveFactor = glm::vec3(emissiveFactor[0], emissiveFactor[1], emissiveFactor[2]);

        material.metallicFactor = j.value("metallicFactor", 1.0f);
        material.roughnessFactor = j.value("roughnessFactor", 1.0f);
        material.alphaCutoff = j.value("alphaCutoff", 0.5f);
        material.alphaMode = j.value("alphaMode", "Opaque");
        material.doubleSided = j.value("doubleSided", false);

        auto resolveTexture = [&](const char* key) -> String
        {
            std::string relative = j.value(key, "");
            return relative.empty() ? String() : String((baseDirectory / relative).string());
        };

        material.baseColorTexture = resolveTexture("baseColorTexture");
        material.normalTexture = resolveTexture("normalTexture");
        material.metallicRoughnessTexture = resolveTexture("metallicRoughnessTexture");
        material.occlusionTexture = resolveTexture("occlusionTexture");
        material.emissiveTexture = resolveTexture("emissiveTexture");

        return material;
    }
}

bool CPUModel::Load(const String& path)
{
    m_Path = path;
    m_BinaryChunkStart = 0;
    m_BinaryChunkSize = 0;
    m_Meshes.Clear();
    m_Nodes.Clear();
    m_Materials.Clear();

    FileStream stream(path);

    CaramelAsset::CmdlHeader header;
    if (stream.Read(header) != sizeof(header))
    {
        CARAMEL_ERROR("CPUModel: failed to open or read header from '{}'", path.CStr());
        return false;
    }

    if (header.magic != CaramelAsset::kCmdlMagic || header.version != CaramelAsset::kFormatVersion)
    {
        CARAMEL_ERROR("CPUModel: '{}' is not a valid .cmdl file (or a version mismatch)", path.CStr());
        return false;
    }

    TArray<char> jsonBytes(header.jsonChunkSize);
    if (stream.Read(jsonBytes.Data(), header.jsonChunkSize) != header.jsonChunkSize)
    {
        CARAMEL_ERROR("CPUModel: '{}' has a truncated JSON chunk", path.CStr());
        return false;
    }

    m_BinaryChunkStart = stream.Tell();
    m_BinaryChunkSize = header.binChunkSize;

    nlohmann::json json = nlohmann::json::parse(jsonBytes.Begin(), jsonBytes.End());

    std::filesystem::path baseDirectory = std::filesystem::path(path.CStr()).parent_path();

    for (const auto& meshJson : json.value("meshes", nlohmann::json::array()))
        m_Meshes.PushBack(ParseMesh(meshJson));

    for (const auto& nodeJson : json.value("nodes", nlohmann::json::array()))
        m_Nodes.PushBack(ParseNode(nodeJson));

    for (const auto& materialFile : json.value("materials", nlohmann::json::array()))
        m_Materials.PushBack(ParseMaterial(baseDirectory / materialFile.get<std::string>(), baseDirectory));

    return true;
}

uint64 CPUModel::ReadBinaryRange(uint64 offset, uint64 length, void* destination) const
{
    if (length == 0)
        return 0;

    if (offset + length > m_BinaryChunkSize)
    {
        CARAMEL_ERROR("CPUModel: read range [{}, {}) is out of bounds for '{}'", offset, offset + length, m_Path.CStr());
        return 0;
    }

    FileStream stream(m_Path);
    stream.Seek(m_BinaryChunkStart + offset);
    return stream.Read(destination, length);
}

uint64 CPUModel::LoadVertexBuffer(uint32 meshIndex, void* destination) const
{
    const ModelMesh& mesh = m_Meshes[meshIndex];
    return ReadBinaryRange(mesh.vertexBufferOffset, mesh.vertexBufferLength, destination);
}

uint64 CPUModel::LoadSkinBuffer(uint32 meshIndex, void* destination) const
{
    const ModelMesh& mesh = m_Meshes[meshIndex];
    return ReadBinaryRange(mesh.skinBufferOffset, mesh.skinBufferLength, destination);
}

uint64 CPUModel::LoadLOD(uint32 meshIndex, uint32 lodIndex, void* destination) const
{
    const ModelLOD& lod = m_Meshes[meshIndex].lods[lodIndex];
    return ReadBinaryRange(lod.GetByteOffset(), lod.GetByteLength(), destination);
}
