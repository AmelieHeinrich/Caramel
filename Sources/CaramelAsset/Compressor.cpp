/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-02 18:54:14
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "Compressor.hpp"
#include "Format.hpp"
#include "MeshCompressor.hpp"
#include "Parallel.hpp"
#include "TextureCompressor.hpp"

#include <cgltf.h>
#include <stb_image.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <glm/gtc/type_ptr.hpp>

#include <filesystem>
#include <fstream>
#include <cctype>
#include <cstring>

namespace CaramelAsset
{
    namespace
    {
        struct TextureJob
        {
            const cgltf_image* image = nullptr;
            ETextureRole role = ETextureRole::Generic;
            std::string fileName;
            bool succeeded = false;
        };

        struct MeshJob
        {
            const cgltf_mesh* mesh = nullptr;
            const cgltf_primitive* primitive = nullptr;
            const cgltf_skin* skin = nullptr;
            std::string name;
            CompiledMesh compiled;
        };

        struct CompileContext
        {
            cgltf_data* gltf = nullptr;
            std::string sourceDirectory;
            std::string outputDirectory;
            bool verbose = false;
            bool skipTextures = false;

            nlohmann::json json;
            TArray<uint8> binary;

            TDictionary<const cgltf_image*, size_t> compiledTextures;
            TArray<TextureJob> textureJobs;
            TArray<nlohmann::json> materialJsons;
        };

        void LogVerbose(const CompileContext& ctx, const std::string& message)
        {
            if (ctx.verbose)
                spdlog::info("[CaramelAssetCompiler] {}", message);
        }

        uint64 AppendBinary(CompileContext& ctx, const void* data, size_t size)
        {
            uint64 offset = ctx.binary.Size();
            size_t oldSize = ctx.binary.Size();
            ctx.binary.Resize(oldSize + size);
            if (size > 0)
                std::memcpy(ctx.binary.Data() + oldSize, data, size);
            return offset;
        }

        template <typename T>
        uint64 AppendBinary(CompileContext& ctx, const TArray<T>& array)
        {
            return AppendBinary(ctx, array.Data(), array.Size() * sizeof(T));
        }

        std::string SanitizeFileName(const std::string& name)
        {
            std::string result = name.empty() ? "Texture" : name;
            for (char& c : result)
            {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-')
                    c = '_';
            }
            return result;
        }

        bool DecodeImageRGBA8(const CompileContext& ctx, const cgltf_image& image, TArray<uint8>& outPixels, uint32& outWidth, uint32& outHeight)
        {
            int width = 0, height = 0, channels = 0;
            stbi_uc* pixels = nullptr;

            if (image.buffer_view && image.buffer_view->buffer && image.buffer_view->buffer->data)
            {
                const uint8* data = reinterpret_cast<const uint8*>(image.buffer_view->buffer->data) + image.buffer_view->offset;
                pixels = stbi_load_from_memory(data, static_cast<int>(image.buffer_view->size), &width, &height, &channels, 4);
            }
            else if (image.uri)
            {
                if (std::strncmp(image.uri, "data:", 5) == 0)
                {
                    spdlog::warn("CaramelAssetCompiler: data-URI embedded images are not supported (image '{}')", image.name ? image.name : "?");
                    return false;
                }

                TArray<char> decodedUri(image.uri, image.uri + std::strlen(image.uri) + 1);
                cgltf_decode_uri(decodedUri.Data());

                std::filesystem::path path = std::filesystem::path(ctx.sourceDirectory) / decodedUri.Data();
                pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
            }

            if (!pixels)
                return false;

            outWidth = static_cast<uint32>(width);
            outHeight = static_cast<uint32>(height);
            outPixels.Resize(static_cast<size_t>(width) * height * 4);
            std::memcpy(outPixels.Data(), pixels, outPixels.Size());
            stbi_image_free(pixels);
            return true;
        }

        bool WriteCtex(const CompiledTexture& texture, const std::filesystem::path& path)
        {
            CtexHeader header;
            header.width = texture.width;
            header.height = texture.height;
            header.format = texture.format;
            header.mipCount = static_cast<uint32>(texture.mips.Size());
            header.arrayLayers = 1;
            header.flags = texture.isSRGB ? kCtexFlagSRGB : 0;

            TArray<CtexMipEntry> mipTable(texture.mips.Size());
            uint64 offset = sizeof(CtexHeader) + sizeof(CtexMipEntry) * texture.mips.Size();
            for (size_t i = 0; i < texture.mips.Size(); i++)
            {
                offset = (offset + 3) & ~static_cast<uint64>(3);
                mipTable[i].offset = offset;
                mipTable[i].size = texture.mips[i].Size();
                offset += texture.mips[i].Size();
            }

            std::ofstream file(path, std::ios::binary);
            if (!file)
                return false;

            file.write(reinterpret_cast<const char*>(&header), sizeof(header));
            file.write(reinterpret_cast<const char*>(mipTable.Data()), sizeof(CtexMipEntry) * mipTable.Size());

            uint64 written = sizeof(CtexHeader) + sizeof(CtexMipEntry) * mipTable.Size();
            for (size_t i = 0; i < texture.mips.Size(); i++)
            {
                while (written < mipTable[i].offset)
                {
                    char zero = 0;
                    file.write(&zero, 1);
                    written++;
                }
                file.write(reinterpret_cast<const char*>(texture.mips[i].Data()), texture.mips[i].Size());
                written += texture.mips[i].Size();
            }

            return file.good();
        }

        std::string ReserveTextureForRole(CompileContext& ctx, const cgltf_texture_view& view, ETextureRole role)
        {
            if (!view.texture || !view.texture->image)
                return "";

            const cgltf_image* image = view.texture->image;

            auto it = ctx.compiledTextures.Find(image);
            if (it != ctx.compiledTextures.End())
                return ctx.textureJobs[it->second].fileName;

            TextureJob job;
            job.image = image;
            job.role = role;
            job.fileName = "Textures/" + SanitizeFileName(image->name ? image->name : "Texture") + "_" + std::to_string(ctx.compiledTextures.Size()) + ".ctex";

            ctx.compiledTextures.Insert(image, ctx.textureJobs.Size());
            ctx.textureJobs.PushBack(std::move(job));
            return ctx.textureJobs[ctx.textureJobs.Size() - 1].fileName;
        }

        void CompileTextures(CompileContext& ctx)
        {
            if (ctx.skipTextures && !ctx.textureJobs.IsEmpty())
                spdlog::warn("CaramelAssetCompiler: --skip-textures set, reusing .ctex files from a prior full bake for {} texture(s)", ctx.textureJobs.Size());

            ParallelFor(ctx.textureJobs.Size(), [&ctx](size_t index)
            {
                TextureJob& job = ctx.textureJobs[index];

                if (ctx.skipTextures)
                {
                    job.succeeded = true;
                    return;
                }

                TArray<uint8> pixels;
                uint32 width = 0, height = 0;
                if (!DecodeImageRGBA8(ctx, *job.image, pixels, width, height))
                {
                    spdlog::warn("CaramelAssetCompiler: failed to decode texture '{}'",
                        job.image->name ? job.image->name : job.image->uri ? job.image->uri : "?");
                    return;
                }

                if (job.role == ETextureRole::MetallicRoughness)
                {
                    for (size_t i = 0; i < static_cast<size_t>(width) * height; i++)
                    {
                        uint8 g = pixels[i * 4 + 1];
                        uint8 b = pixels[i * 4 + 2];
                        pixels[i * 4 + 0] = g;
                        pixels[i * 4 + 1] = b;
                    }
                }

                CompiledTexture compiled = TextureCompressor::Compile(pixels.Data(), width, height, job.role);

                if (!WriteCtex(compiled, std::filesystem::path(ctx.outputDirectory) / job.fileName))
                {
                    spdlog::warn("CaramelAssetCompiler: failed to write '{}'", job.fileName);
                    return;
                }

                LogVerbose(ctx, "compiled texture " + job.fileName);
                job.succeeded = true;
            });
        }

        std::string WriteMaterialFile(const CompileContext& ctx, const nlohmann::json& materialJson, size_t index, const char* name)
        {
            std::string fileName = "Materials/" + SanitizeFileName(name && *name ? name : "Material") + "_" + std::to_string(index) + ".json";

            std::ofstream file(std::filesystem::path(ctx.outputDirectory) / fileName);
            if (!file)
                return "";

            file << materialJson.dump(4);
            return fileName;
        }

        void CompileMaterials(CompileContext& ctx)
        {
            ctx.materialJsons.Reserve(ctx.gltf->materials_count);

            for (cgltf_size i = 0; i < ctx.gltf->materials_count; i++)
            {
                const cgltf_material& material = ctx.gltf->materials[i];
                nlohmann::json j;
                j["name"] = material.name ? material.name : "";

                std::string baseColor, normal, metallicRoughness, occlusion, emissive;

                if (material.has_pbr_metallic_roughness)
                {
                    const cgltf_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;

                    baseColor = ReserveTextureForRole(ctx, pbr.base_color_texture, ETextureRole::BaseColor);
                    j["baseColorFactor"] = { pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3] };
                    j["metallicFactor"] = pbr.metallic_factor;
                    j["roughnessFactor"] = pbr.roughness_factor;

                    bool sharedWithOcclusion = pbr.metallic_roughness_texture.texture && material.occlusion_texture.texture &&
                        pbr.metallic_roughness_texture.texture->image == material.occlusion_texture.texture->image;

                    if (sharedWithOcclusion)
                    {
                        metallicRoughness = ReserveTextureForRole(ctx, pbr.metallic_roughness_texture, ETextureRole::ORM);
                        occlusion = metallicRoughness;
                    }
                    else
                    {
                        metallicRoughness = ReserveTextureForRole(ctx, pbr.metallic_roughness_texture, ETextureRole::MetallicRoughness);
                    }
                }
                else
                {
                    j["baseColorFactor"] = { 1.0f, 1.0f, 1.0f, 1.0f };
                    j["metallicFactor"] = 1.0f;
                    j["roughnessFactor"] = 1.0f;
                }

                normal = ReserveTextureForRole(ctx, material.normal_texture, ETextureRole::Normal);
                emissive = ReserveTextureForRole(ctx, material.emissive_texture, ETextureRole::Emissive);
                if (occlusion.empty() && material.occlusion_texture.texture)
                    occlusion = ReserveTextureForRole(ctx, material.occlusion_texture, ETextureRole::Occlusion);

                j["baseColorTexture"] = baseColor;
                j["normalTexture"] = normal;
                j["metallicRoughnessTexture"] = metallicRoughness;
                j["occlusionTexture"] = occlusion;
                j["emissiveTexture"] = emissive;
                j["emissiveFactor"] = { material.emissive_factor[0], material.emissive_factor[1], material.emissive_factor[2] };
                j["alphaMode"] = material.alpha_mode == cgltf_alpha_mode_mask ? "Mask" : material.alpha_mode == cgltf_alpha_mode_blend ? "Blend" : "Opaque";
                j["alphaCutoff"] = material.alpha_cutoff;
                j["doubleSided"] = static_cast<bool>(material.double_sided);

                ctx.materialJsons.PushBack(std::move(j));
            }
        }

        void WriteMaterialFiles(CompileContext& ctx)
        {
            TDictionary<std::string, bool> failedNames;
            for (const TextureJob& job : ctx.textureJobs)
                if (!job.succeeded)
                    failedNames.Insert(job.fileName, true);

            static const char* kTextureKeys[] = {
                "baseColorTexture", "normalTexture", "metallicRoughnessTexture", "occlusionTexture", "emissiveTexture"
            };

            auto& materialsJson = ctx.json["materials"];
            materialsJson = nlohmann::json::array();

            for (size_t i = 0; i < ctx.materialJsons.Size(); i++)
            {
                nlohmann::json& j = ctx.materialJsons[i];
                if (failedNames.Size() > 0)
                {
                    for (const char* key : kTextureKeys)
                    {
                        const std::string& name = j[key].get_ref<const std::string&>();
                        if (!name.empty() && failedNames.Contains(name))
                            j[key] = "";
                    }
                }

                const char* materialName = ctx.gltf->materials[i].name;
                std::string materialFile = WriteMaterialFile(ctx, j, i, materialName);
                if (materialFile.empty())
                    spdlog::warn("CaramelAssetCompiler: failed to write material file for material {}", i);

                LogVerbose(ctx, "compiled material " + materialFile);
                materialsJson.push_back(materialFile);
            }
        }

        TDictionary<const cgltf_mesh*, const cgltf_skin*> BuildMeshSkinMap(const CompileContext& ctx)
        {
            TDictionary<const cgltf_mesh*, const cgltf_skin*> map;
            for (cgltf_size i = 0; i < ctx.gltf->nodes_count; i++)
            {
                const cgltf_node& node = ctx.gltf->nodes[i];
                if (node.mesh && node.skin && !map.Contains(node.mesh))
                    map.Insert(node.mesh, node.skin);
            }
            return map;
        }

        nlohmann::json SerializeMesh(CompileContext& ctx, const CompiledMesh& mesh)
        {
            nlohmann::json j;
            j["name"] = mesh.name.CStr();
            j["materialIndex"] = mesh.materialIndex;
            j["skinIndex"] = mesh.skinIndex;
            j["boundsMin"] = { mesh.boundsMin.x, mesh.boundsMin.y, mesh.boundsMin.z };
            j["boundsMax"] = { mesh.boundsMax.x, mesh.boundsMax.y, mesh.boundsMax.z };
            j["hasTangent"] = mesh.hasTangent;
            j["hasSkin"] = mesh.hasSkin;
            j["vertexCount"] = mesh.vertices.Size();

            uint64 vbOffset = AppendBinary(ctx, mesh.vertices);
            j["vertexBufferOffset"] = vbOffset;
            j["vertexBufferLength"] = mesh.vertices.Size() * sizeof(Vertex);

            if (mesh.hasSkin)
            {
                uint64 sbOffset = AppendBinary(ctx, mesh.skinVertices);
                j["skinBufferOffset"] = sbOffset;
                j["skinBufferLength"] = mesh.skinVertices.Size() * sizeof(SkinVertex);
            }
            else
            {
                j["skinBufferOffset"] = 0;
                j["skinBufferLength"] = 0;
            }

            if (!mesh.colliderData.IsEmpty())
            {
                uint64 colliderOffset = AppendBinary(ctx, mesh.colliderData);
                j["colliderOffset"] = colliderOffset;
                j["colliderLength"] = mesh.colliderData.Size();
            }
            else
            {
                j["colliderOffset"] = 0;
                j["colliderLength"] = 0;
            }

            nlohmann::json lodsJson = nlohmann::json::array();
            for (uint32 i = 0; i < kLodCount; i++)
            {
                const MeshletLOD& lod = mesh.lods[i];
                nlohmann::json lj;
                lj["maxError"] = lod.maxError;
                lj["meshletCount"] = lod.meshlets.Size();

                uint64 meshletOffset = AppendBinary(ctx, lod.meshlets);
                lj["meshletBufferOffset"] = meshletOffset;
                lj["meshletBufferLength"] = lod.meshlets.Size() * sizeof(MeshletDesc);

                uint64 mvOffset = AppendBinary(ctx, lod.meshletVertices);
                lj["meshletVertexBufferOffset"] = mvOffset;
                lj["meshletVertexBufferLength"] = lod.meshletVertices.Size() * sizeof(uint32);

                uint64 mtOffset = AppendBinary(ctx, lod.meshletTriangles);
                lj["meshletTriangleBufferOffset"] = mtOffset;
                lj["meshletTriangleBufferLength"] = lod.meshletTriangles.Size();

                uint64 boundsOffset = AppendBinary(ctx, lod.bounds);
                lj["meshletBoundsOffset"] = boundsOffset;
                lj["meshletBoundsLength"] = lod.bounds.Size() * sizeof(MeshletCullData);

                uint64 flatIndexOffset = AppendBinary(ctx, lod.flatIndices);
                lj["flatIndexBufferOffset"] = flatIndexOffset;
                lj["flatIndexBufferLength"] = lod.flatIndices.Size() * sizeof(uint32);

                lodsJson.push_back(lj);
            }
            j["lods"] = lodsJson;

            return j;
        }

        TDictionary<const cgltf_mesh*, TArray<int32>> CompileMeshes(CompileContext& ctx, const TDictionary<const cgltf_mesh*, const cgltf_skin*>& meshSkinMap)
        {
            TDictionary<const cgltf_mesh*, TArray<int32>> meshIndexMap;

            auto& meshesJson = ctx.json["meshes"];
            meshesJson = nlohmann::json::array();

            TArray<MeshJob> jobs;
            for (cgltf_size mi = 0; mi < ctx.gltf->meshes_count; mi++)
            {
                const cgltf_mesh& mesh = ctx.gltf->meshes[mi];

                auto skinIt = meshSkinMap.Find(&mesh);
                const cgltf_skin* skin = (skinIt != meshSkinMap.End()) ? skinIt->second : nullptr;

                for (cgltf_size pi = 0; pi < mesh.primitives_count; pi++)
                {
                    const cgltf_primitive& primitive = mesh.primitives[pi];
                    if (primitive.type != cgltf_primitive_type_triangles)
                    {
                        LogVerbose(ctx, "skipping non-triangle-list primitive");
                        continue;
                    }

                    std::string baseName = mesh.name ? mesh.name : "Mesh";

                    MeshJob job;
                    job.mesh = &mesh;
                    job.primitive = &primitive;
                    job.skin = skin;
                    job.name = mesh.primitives_count > 1 ? (baseName + "_" + std::to_string(pi)) : baseName;
                    jobs.PushBack(std::move(job));
                }

                meshIndexMap.Insert(&mesh, TArray<int32>());
            }

            ParallelFor(jobs.Size(), [&ctx, &jobs](size_t index)
            {
                MeshJob& job = jobs[index];
                LogVerbose(ctx, "compiling mesh " + job.name);

                job.compiled = MeshCompressor::Compile(*job.primitive, job.skin, String(job.name));
                job.compiled.materialIndex = job.primitive->material ? static_cast<int32>(job.primitive->material - ctx.gltf->materials) : -1;
                job.compiled.skinIndex = job.skin ? static_cast<int32>(job.skin - ctx.gltf->skins) : -1;
            });

            for (MeshJob& job : jobs)
            {
                if (job.compiled.vertices.IsEmpty())
                {
                    spdlog::warn("CaramelAssetCompiler: primitive '{}' produced no vertices, skipping", job.name);
                    continue;
                }

                nlohmann::json meshJson = SerializeMesh(ctx, job.compiled);
                meshIndexMap.Find(job.mesh)->second.PushBack(static_cast<int32>(meshesJson.size()));
                meshesJson.push_back(meshJson);
            }

            return meshIndexMap;
        }

        void CompileNodes(CompileContext& ctx, const TDictionary<const cgltf_mesh*, TArray<int32>>& meshIndexMap)
        {
            auto& nodesJson = ctx.json["nodes"];
            nodesJson = nlohmann::json::array();

            for (cgltf_size i = 0; i < ctx.gltf->nodes_count; i++)
            {
                const cgltf_node& node = ctx.gltf->nodes[i];
                nlohmann::json j;
                j["name"] = node.name ? node.name : "";
                j["parentIndex"] = node.parent ? static_cast<int32>(node.parent - ctx.gltf->nodes) : -1;

                nlohmann::json meshIndices = nlohmann::json::array();
                if (node.mesh)
                {
                    auto it = meshIndexMap.Find(node.mesh);
                    if (it != meshIndexMap.End())
                        for (int32 idx : it->second)
                            meshIndices.push_back(idx);
                }
                j["meshIndices"] = meshIndices;

                if (node.has_matrix && !node.has_translation && !node.has_rotation && !node.has_scale)
                    spdlog::warn("CaramelAssetCompiler: node '{}' uses a raw matrix transform, which isn't decomposed in v1 -- exporting as identity", node.name ? node.name : "?");

                j["translation"] = node.has_translation
                    ? nlohmann::json{ node.translation[0], node.translation[1], node.translation[2] }
                    : nlohmann::json{ 0.0f, 0.0f, 0.0f };
                j["rotation"] = node.has_rotation
                    ? nlohmann::json{ node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3] }
                    : nlohmann::json{ 0.0f, 0.0f, 0.0f, 1.0f };
                j["scale"] = node.has_scale
                    ? nlohmann::json{ node.scale[0], node.scale[1], node.scale[2] }
                    : nlohmann::json{ 1.0f, 1.0f, 1.0f };

                nlohmann::json children = nlohmann::json::array();
                for (cgltf_size c = 0; c < node.children_count; c++)
                    children.push_back(static_cast<int32>(node.children[c] - ctx.gltf->nodes));
                j["children"] = children;

                nodesJson.push_back(j);
            }
        }

        void CompileSkeletons(CompileContext& ctx)
        {
            auto& skeletonsJson = ctx.json["skeletons"];
            skeletonsJson = nlohmann::json::array();

            for (cgltf_size si = 0; si < ctx.gltf->skins_count; si++)
            {
                const cgltf_skin& skin = ctx.gltf->skins[si];
                nlohmann::json j;

                nlohmann::json jointsJson = nlohmann::json::array();
                for (cgltf_size ji = 0; ji < skin.joints_count; ji++)
                {
                    const cgltf_node* jointNode = skin.joints[ji];
                    nlohmann::json jj;
                    jj["name"] = jointNode->name ? jointNode->name : "";
                    jj["nodeIndex"] = static_cast<int32>(jointNode - ctx.gltf->nodes);

                    glm::mat4 inverseBind(1.0f);
                    if (skin.inverse_bind_matrices)
                    {
                        float32 m[16];
                        cgltf_accessor_read_float(skin.inverse_bind_matrices, ji, m, 16);
                        inverseBind = glm::make_mat4(m);
                    }

                    nlohmann::json ibmJson = nlohmann::json::array();
                    const float32* ibmPtr = glm::value_ptr(inverseBind);
                    for (int k = 0; k < 16; k++)
                        ibmJson.push_back(ibmPtr[k]);
                    jj["inverseBindMatrix"] = ibmJson;

                    jointsJson.push_back(jj);
                }
                j["joints"] = jointsJson;

                int32 rootJoint = 0;
                for (cgltf_size ji = 0; ji < skin.joints_count; ji++)
                {
                    bool parentIsJoint = false;
                    for (cgltf_size pj = 0; pj < skin.joints_count; pj++)
                    {
                        if (skin.joints[pj] == skin.joints[ji]->parent)
                        {
                            parentIsJoint = true;
                            break;
                        }
                    }
                    if (!parentIsJoint)
                    {
                        rootJoint = static_cast<int32>(ji);
                        break;
                    }
                }
                j["rootJoint"] = rootJoint;

                skeletonsJson.push_back(j);
            }
        }

        void CompileAnimations(CompileContext& ctx)
        {
            auto& animationsJson = ctx.json["animations"];
            animationsJson = nlohmann::json::array();

            for (cgltf_size ai = 0; ai < ctx.gltf->animations_count; ai++)
            {
                const cgltf_animation& animation = ctx.gltf->animations[ai];
                nlohmann::json j;
                j["name"] = animation.name ? animation.name : "";

                float32 duration = 0.0f;
                nlohmann::json tracksJson = nlohmann::json::array();

                for (cgltf_size ci = 0; ci < animation.channels_count; ci++)
                {
                    const cgltf_animation_channel& channel = animation.channels[ci];
                    if (!channel.target_node || !channel.sampler)
                        continue;
                    if (channel.target_path != cgltf_animation_path_type_translation &&
                        channel.target_path != cgltf_animation_path_type_rotation &&
                        channel.target_path != cgltf_animation_path_type_scale)
                        continue;

                    const cgltf_animation_sampler& sampler = *channel.sampler;
                    cgltf_size keyCount = sampler.input->count;

                    TArray<float32> times(keyCount);
                    for (cgltf_size k = 0; k < keyCount; k++)
                        cgltf_accessor_read_float(sampler.input, k, &times[k], 1);

                    uint32 componentsPerKey = (channel.target_path == cgltf_animation_path_type_rotation) ? 4 : 3;
                    cgltf_size valueMultiplier = (sampler.interpolation == cgltf_interpolation_type_cubic_spline) ? 3 : 1;
                    cgltf_size elementCount = keyCount * valueMultiplier;

                    TArray<float32> values(elementCount * componentsPerKey);
                    for (cgltf_size e = 0; e < elementCount; e++)
                        cgltf_accessor_read_float(sampler.output, e, &values[e * componentsPerKey], componentsPerKey);

                    if (keyCount > 0)
                        duration = std::max(duration, times[keyCount - 1]);

                    nlohmann::json tj;
                    tj["nodeIndex"] = static_cast<int32>(channel.target_node - ctx.gltf->nodes);
                    tj["property"] = channel.target_path == cgltf_animation_path_type_translation ? "Translation"
                        : channel.target_path == cgltf_animation_path_type_rotation ? "Rotation" : "Scale";
                    tj["interpolation"] = sampler.interpolation == cgltf_interpolation_type_step ? "Step"
                        : sampler.interpolation == cgltf_interpolation_type_cubic_spline ? "CubicSpline" : "Linear";
                    tj["keyframeCount"] = keyCount;

                    uint64 timesOffset = AppendBinary(ctx, times);
                    tj["timesOffset"] = timesOffset;
                    tj["timesLength"] = times.Size() * sizeof(float32);

                    uint64 valuesOffset = AppendBinary(ctx, values);
                    tj["valuesOffset"] = valuesOffset;
                    tj["valuesLength"] = values.Size() * sizeof(float32);

                    tracksJson.push_back(tj);
                }

                j["duration"] = duration;
                j["tracks"] = tracksJson;
                animationsJson.push_back(j);
            }
        }

        bool WriteCmdl(CompileContext& ctx, const std::string& baseName)
        {
            ctx.json["asset"] = {
                { "version", kFormatVersion },
                { "generator", "CaramelAssetCompiler" },
                { "sourceFile", baseName + ".gltf" },
            };

            std::string jsonString = ctx.json.dump();
            while (jsonString.size() % 4 != 0)
                jsonString.push_back(' ');

            while (ctx.binary.Size() % 16 != 0)
                ctx.binary.PushBack(0);

            CmdlHeader header;
            header.jsonChunkSize = static_cast<uint32>(jsonString.size());
            header.binChunkSize = static_cast<uint32>(ctx.binary.Size());

            std::filesystem::path outPath = std::filesystem::path(ctx.outputDirectory) / (baseName + ".cmdl");
            std::ofstream file(outPath, std::ios::binary);
            if (!file)
                return false;

            file.write(reinterpret_cast<const char*>(&header), sizeof(header));
            file.write(jsonString.data(), static_cast<std::streamsize>(jsonString.size()));
            file.write(reinterpret_cast<const char*>(ctx.binary.Data()), static_cast<std::streamsize>(ctx.binary.Size()));

            return file.good();
        }

        nlohmann::json LoadCacheManifest(const std::filesystem::path& manifestPath)
        {
            if (std::filesystem::exists(manifestPath))
            {
                std::ifstream file(manifestPath);
                nlohmann::json manifest;
                if (file && (file >> manifest, !file.fail()) && manifest.is_object())
                    return manifest;
            }

            nlohmann::json manifest;
            manifest["version"] = 1;
            manifest["entries"] = nlohmann::json::object();
            return manifest;
        }

        void SaveCacheManifest(const std::filesystem::path& manifestPath, const nlohmann::json& manifest)
        {
            std::ofstream file(manifestPath);
            file << manifest.dump(4);
        }

        std::string ResolveCacheFolder(const nlohmann::json& manifest, const std::string& sourceKey, const std::string& stem)
        {
            const auto& entries = manifest["entries"];

            auto existing = entries.find(sourceKey);
            if (existing != entries.end())
                return existing->value("folder", stem);

            std::string folder = stem;
            for (int suffix = 2; ; suffix++)
            {
                bool taken = false;
                for (auto it = entries.begin(); it != entries.end(); ++it)
                {
                    if (it.key() != sourceKey && it.value().value("folder", "") == folder)
                    {
                        taken = true;
                        break;
                    }
                }
                if (!taken)
                    return folder;
                folder = stem + "_" + std::to_string(suffix);
            }
        }
    }

    bool Compressor::Compile(const String& inputGltfPath, const CompilerOptions& options)
    {
        std::filesystem::path inputPath(inputGltfPath.CStr());
        if (!std::filesystem::exists(inputPath))
        {
            spdlog::error("CaramelAssetCompiler: input file not found: {}", inputGltfPath.CStr());
            return false;
        }

        CompileContext ctx;
        ctx.sourceDirectory = inputPath.parent_path().string();
        ctx.verbose = options.verbose;
        ctx.skipTextures = options.skipTextures;

        const bool useCache = options.outputDirectory.Empty();
        std::filesystem::path manifestPath = std::filesystem::path("Content") / "Cache" / "cache.json";
        nlohmann::json manifest;
        std::string sourceKey;
        std::string cacheFolder;

        if (useCache)
        {
            std::error_code ec;
            std::filesystem::create_directories(manifestPath.parent_path(), ec);

            manifest = LoadCacheManifest(manifestPath);
            sourceKey = std::filesystem::relative(inputPath, manifestPath.parent_path()).generic_string();
            cacheFolder = ResolveCacheFolder(manifest, sourceKey, inputPath.stem().string());
            ctx.outputDirectory = (manifestPath.parent_path() / cacheFolder).string();
        }
        else
        {
            ctx.outputDirectory = std::string(options.outputDirectory.CStr());
        }

        std::error_code ec;
        std::filesystem::create_directories(ctx.outputDirectory, ec);
        std::filesystem::create_directories(std::filesystem::path(ctx.outputDirectory) / "Textures", ec);
        std::filesystem::create_directories(std::filesystem::path(ctx.outputDirectory) / "Materials", ec);

        cgltf_options parseOptions{};
        cgltf_data* data = nullptr;
        if (cgltf_parse_file(&parseOptions, inputGltfPath.CStr(), &data) != cgltf_result_success)
        {
            spdlog::error("CaramelAssetCompiler: failed to parse '{}'", inputGltfPath.CStr());
            return false;
        }
        if (cgltf_load_buffers(&parseOptions, data, inputGltfPath.CStr()) != cgltf_result_success)
        {
            spdlog::error("CaramelAssetCompiler: failed to load buffers for '{}'", inputGltfPath.CStr());
            cgltf_free(data);
            return false;
        }
        ctx.gltf = data;

        spdlog::info("Compiling '{}' -> '{}'", inputGltfPath.CStr(), ctx.outputDirectory);

        CompileMaterials(ctx);
        CompileTextures(ctx);
        WriteMaterialFiles(ctx);

        TDictionary<const cgltf_mesh*, const cgltf_skin*> meshSkinMap = BuildMeshSkinMap(ctx);
        TDictionary<const cgltf_mesh*, TArray<int32>> meshIndexMap = CompileMeshes(ctx, meshSkinMap);

        CompileNodes(ctx, meshIndexMap);
        CompileSkeletons(ctx);
        CompileAnimations(ctx);

        cgltf_size materialCount = ctx.gltf->materials_count;
        size_t meshCount = ctx.json["meshes"].size();
        size_t textureCount = ctx.compiledTextures.Size();

        std::string stem = inputPath.stem().string();
        bool success = WriteCmdl(ctx, stem);

        cgltf_free(data);

        if (success)
        {
            spdlog::info("Done: {} materials, {} meshes, {} textures", materialCount, meshCount, textureCount);

            if (useCache)
            {
                manifest["entries"][sourceKey] = {
                    { "folder", cacheFolder },
                    { "cmdl", cacheFolder + "/" + stem + ".cmdl" },
                };
                SaveCacheManifest(manifestPath, manifest);
            }
        }
        else
        {
            spdlog::error("CaramelAssetCompiler: failed to write output for '{}'", inputGltfPath.CStr());
        }

        return success;
    }
}
