/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:04:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "InspectorPanel.hpp"

#include <Caramel/Editor/EditorContext.hpp>
#include <Caramel/Scene/Scene.hpp>
#include <Caramel/Asset/StreamingManager.hpp>
#include <Caramel/Asset/StreamingModel.hpp>
#include <Caramel/Asset/StreamingTexture.hpp>
#include <Caramel/Renderer/MaterialScheme.hpp>
#include <Caramel/Renderer/Renderer.hpp>
#include <Caramel/Script/ScriptEngine.hpp>
#include <Caramel/Script/ScriptSystem.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <FontAwesome/FA.h>

#include <glm/glm.hpp>

#include <cstdio>

const char* const InspectorPanel::kTitle = ICON_FA_CIRCLE " Inspector";

void InspectorPanel::DrawScriptSection(EditorContext& context)
{
    if (!context.Scripts || !context.SelectedEntity)
        return;

    SceneNode& node = *context.SelectedEntity;
    ScriptEngine& engine = context.Scripts->GetEngine();

    ImGui::Separator();
    ImGui::TextUnformatted(ICON_FA_SCROLL " Scripts");

    if (node.scripts.IsEmpty()) {
        ImGui::TextDisabled("None -- right-click in the Hierarchy to add one");
        return;
    }

    int32 pendingRemove = -1;

    for (uint32 i = 0; i < (uint32)node.scripts.Size(); ++i) {
        ScriptComponent& component = node.scripts[i];

        // Instance- and mesh-scoped components only belong to the row currently selected.
        if (component.scope == EScriptScope::Instance && component.targetIndex != context.SelectedInstance)
            continue;
        if (component.scope == EScriptScope::Mesh) {
            if (!context.SelectedMesh)
                continue;
            if (component.targetIndex >= node.meshIndices.Size())
                continue;
        }

        ImGui::PushID((int)i);

        const ScriptClassInfo* classInfo = engine.FindClass(component.scriptPath, component.className);

        char header[320];
        std::snprintf(header, sizeof(header), "%s %s (%s)", ICON_FA_SCROLL, component.className.CStr(),
                      ScriptScopeToString(component.scope));

        if (ImGui::TreeNodeEx(header, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
            ImGui::TextDisabled("%s", component.scriptPath.CStr());

            if (ImGui::Checkbox("Enabled", &component.enabled))
                context.Scripts->OnComponentAdded(node, i);

            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_TRASH " Remove"))
                pendingRemove = (int32)i;

            if (classInfo && classInfo->runOnce) {
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_PLAY " Run"))
                    context.Scripts->RunComponentOnce(node, i);
            }

            if (!classInfo) {
                ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " Not compiled -- see log");
            } else {
                // Widgets are generated from the script's declared metadata, the same way the scheme
                // parameter loop below is generated from the scheme JSON.
                for (const ScriptProperty& property : classInfo->properties) {
                    if (property.hidden)
                        continue;

                    if (property.type == EScriptPropertyType::Text) {
                        auto existing = component.textPropertyValues.Find(property.name);
                        String text = existing != component.textPropertyValues.End() ? existing->second : property.defaultText;

                        char buffer[512];
                        std::snprintf(buffer, sizeof(buffer), "%s", text.CStr());
                        if (ImGui::InputText(property.name.CStr(), buffer, sizeof(buffer))) {
                            component.textPropertyValues[property.name] = String(buffer);
                            context.Scripts->ApplyPropertyToLive(node, i, property);
                        }
                        if (!property.tooltip.Empty())
                            ImGui::SetItemTooltip("%s", property.tooltip.CStr());
                        continue;
                    }

                    auto existing = component.propertyValues.Find(property.name);
                    bool isSet = existing != component.propertyValues.End();
                    glm::vec4 value = isSet ? existing->second : property.defaultValue;

                    float32 minValue = property.hasRange ? property.minValue : 0.0f;
                    float32 maxValue = property.hasRange ? property.maxValue : 0.0f;

                    bool edited = false;
                    switch (property.type) {
                        case EScriptPropertyType::Bool: {
                            bool asBool = value.x != 0.0f;
                            edited = ImGui::Checkbox(property.name.CStr(), &asBool);
                            if (edited)
                                value.x = asBool ? 1.0f : 0.0f;
                            break;
                        }
                        case EScriptPropertyType::Int: {
                            int32 asInt = (int32)value.x;
                            edited = ImGui::DragInt(property.name.CStr(), &asInt, 0.1f, (int32)minValue, (int32)maxValue);
                            if (edited)
                                value.x = (float32)asInt;
                            break;
                        }
                        case EScriptPropertyType::Float:
                            edited = ImGui::DragFloat(property.name.CStr(), &value.x, 0.01f, minValue, maxValue);
                            break;
                        case EScriptPropertyType::Float2:
                            edited = ImGui::DragFloat2(property.name.CStr(), &value.x, 0.01f, minValue, maxValue);
                            break;
                        case EScriptPropertyType::Float3:
                            edited = property.isColor ? ImGui::ColorEdit3(property.name.CStr(), &value.x)
                                                      : ImGui::DragFloat3(property.name.CStr(), &value.x, 0.01f, minValue, maxValue);
                            break;
                        case EScriptPropertyType::Float4:
                            edited = property.isColor ? ImGui::ColorEdit4(property.name.CStr(), &value.x)
                                                      : ImGui::DragFloat4(property.name.CStr(), &value.x, 0.01f, minValue, maxValue);
                            break;
                        default:
                            break;
                    }

                    if (!property.tooltip.Empty())
                        ImGui::SetItemTooltip("%s", property.tooltip.CStr());

                    if (edited) {
                        component.propertyValues[property.name] = value;
                        context.Scripts->ApplyPropertyToLive(node, i, property);
                    }

                    if (isSet) {
                        ImGui::SameLine();
                        ImGui::PushID(property.name.CStr());
                        if (ImGui::SmallButton(ICON_FA_ROTATE_LEFT)) {
                            component.propertyValues.Erase(property.name);
                            context.Scripts->ApplyPropertyToLive(node, i, property);
                        }
                        ImGui::PopID();
                    }
                }
            }

            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    if (pendingRemove >= 0) {
        node.scripts.Erase((size_t)pendingRemove);
        context.Scripts->OnComponentRemoved(node, (uint32)pendingRemove);
    }
}

void InspectorPanel::Draw(EditorContext& context, StreamingManager& streaming)
{
    ImGui::SetNextWindowSize(ImVec2(360, 480), ImGuiCond_FirstUseEver);
    ImGui::Begin(kTitle);

    if (!context.SelectedEntity || context.SelectedInstance >= context.SelectedEntity->instances.Size()) {
        ImGui::TextDisabled("Nothing selected");
        ImGui::End();
        return;
    }

    Instance& instance = context.SelectedEntity->instances[context.SelectedInstance];

    ImGui::Text(ICON_FA_CUBES " Entity: %s", context.SelectedEntity->name.CStr());
    if (context.SelectedEntity->type == ESceneNodeType::Entity)
        ImGui::Text(ICON_FA_FILE " Asset: %s", context.SelectedEntity->cmdlPath.CStr());
    else
        ImGui::TextDisabled(ICON_FA_CIRCLE_NOTCH " Empty entity");
    if (context.SelectedMesh)
        ImGui::Text(ICON_FA_CUBE " Mesh: %s", context.SelectedMesh->GetMesh().name.CStr());

    char nameBuffer[256];
    std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", instance.name.CStr());
    if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
        instance.name = nameBuffer;

    ImGui::Separator();
    ImGui::TextUnformatted(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " Transform");

    static const ImVec4 kAxisColors[3] = {
        ImVec4(0.80f, 0.25f, 0.30f, 1.0f),
        ImVec4(0.30f, 0.70f, 0.30f, 1.0f),
        ImVec4(0.25f, 0.50f, 0.90f, 1.0f),
    };
    static const char* kAxisNames[3] = { "X", "Y", "Z" };

    auto transformRow = [&](const char* label, glm::vec3& v, float speed, float resetTo) {
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        ImGui::SameLine(90.0f);

        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth() - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
        float* components[3] = { &v.x, &v.y, &v.z };
        for (int i = 0; i < 3; ++i) {
            if (i > 0)
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::PushID(i);
            ImGui::PushStyleColor(ImGuiCol_Text, kAxisColors[i]);
            ImGui::DragFloat(kAxisNames[i], components[i], speed, 0.0f, 0.0f, "%.2f");
            ImGui::PopStyleColor();
            ImGui::PopItemWidth();
            ImGui::PopID();
        }

        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        if (ImGui::Button(ICON_FA_ROTATE_LEFT))
            v = glm::vec3(resetTo);
        ImGui::PopID();
    };

    // A script that owns this instance rewrites the transform every frame, so editing it here would
    // silently lose the edit -- and the editor has no undo.
    bool scriptDriven = context.Scripts && context.Scripts->IsInstanceDrivenByScript(*context.SelectedEntity, context.SelectedInstance);
    if (scriptDriven)
        ImGui::BeginDisabled();

    transformRow("Position", instance.position, 0.05f, 0.0f);
    transformRow("Rotation", instance.rotationEuler, 0.5f, 0.0f);
    transformRow("Scale", instance.scale, 0.01f, 1.0f);

    if (scriptDriven) {
        ImGui::EndDisabled();
        ImGui::TextDisabled(ICON_FA_SCROLL " Driven by script");
    }

    DrawScriptSection(context);

    ImGui::Separator();
    ImGui::TextUnformatted(ICON_FA_PALETTE " Material");
    StreamingModel* materialSource = context.SelectedMesh;
    if (!materialSource && !context.SelectedEntity->meshIndices.IsEmpty())
        materialSource = streaming.GetModels()[context.SelectedEntity->meshIndices[0]].get();

    if (materialSource) {
        const ModelMaterial& material = materialSource->GetMaterial();
        uint32 requestId = materialSource->GetRequestId();
        int32 materialIndex = materialSource->GetMesh().materialIndex;

        ImGui::Text("Name: %s", material.name.CStr());

        // `material` is the pristine cooked material -- overrides are layered on by GPUScene at
        // buffer-write time and never written back here, so each widget has to seed itself from the
        // override when one is active.
        const MaterialOverride* activeOverride = nullptr;
        for (const MaterialOverride& candidate : context.SelectedEntity->materialOverrides) {
            if (candidate.materialIndex == materialIndex) {
                activeOverride = &candidate;
                break;
            }
        }

        // Draws a "revert to cooked value" button for an active override. Without this there is no
        // way to undo an override, which is what made the old in-place mutation unrecoverable.
        auto drawResetButton = [&](const char* id, bool isOverridden) -> bool {
            if (!isOverridden)
                return false;
            ImGui::SameLine();
            ImGui::PushID(id);
            bool reset = ImGui::SmallButton(ICON_FA_ROTATE_LEFT);
            ImGui::PopID();
            return reset;
        };

        bool baseColorOverridden = activeOverride && activeOverride->overrideBaseColor;
        glm::vec4 baseColor = baseColorOverridden ? activeOverride->baseColorFactor : material.baseColorFactor;
        if (ImGui::ColorEdit4("Base Color", &baseColor.x)) {
            MaterialOverride& matOverride = context.CurrentScene.GetOrCreateMaterialOverride(*context.SelectedEntity, materialIndex);
            matOverride.overrideBaseColor = true;
            matOverride.baseColorFactor = baseColor;
        }
        if (drawResetButton("resetBaseColor", baseColorOverridden))
            context.CurrentScene.GetOrCreateMaterialOverride(*context.SelectedEntity, materialIndex).overrideBaseColor = false;

        bool metallicOverridden = activeOverride && activeOverride->overrideMetallic;
        float metallic = metallicOverridden ? activeOverride->metallicFactor : material.metallicFactor;
        if (ImGui::DragFloat("Metallic", &metallic, 0.01f, 0.0f, 1.0f)) {
            MaterialOverride& matOverride = context.CurrentScene.GetOrCreateMaterialOverride(*context.SelectedEntity, materialIndex);
            matOverride.overrideMetallic = true;
            matOverride.metallicFactor = metallic;
        }
        if (drawResetButton("resetMetallic", metallicOverridden))
            context.CurrentScene.GetOrCreateMaterialOverride(*context.SelectedEntity, materialIndex).overrideMetallic = false;

        bool roughnessOverridden = activeOverride && activeOverride->overrideRoughness;
        float roughness = roughnessOverridden ? activeOverride->roughnessFactor : material.roughnessFactor;
        if (ImGui::DragFloat("Roughness", &roughness, 0.01f, 0.0f, 1.0f)) {
            MaterialOverride& matOverride = context.CurrentScene.GetOrCreateMaterialOverride(*context.SelectedEntity, materialIndex);
            matOverride.overrideRoughness = true;
            matOverride.roughnessFactor = roughness;
        }
        if (drawResetButton("resetRoughness", roughnessOverridden))
            context.CurrentScene.GetOrCreateMaterialOverride(*context.SelectedEntity, materialIndex).overrideRoughness = false;

        bool emissiveOverridden = activeOverride && activeOverride->overrideEmissive;
        glm::vec3 emissive = emissiveOverridden ? activeOverride->emissiveFactor : material.emissiveFactor;
        if (ImGui::ColorEdit3("Emissive", &emissive.x)) {
            MaterialOverride& matOverride = context.CurrentScene.GetOrCreateMaterialOverride(*context.SelectedEntity, materialIndex);
            matOverride.overrideEmissive = true;
            matOverride.emissiveFactor = emissive;
        }
        if (drawResetButton("resetEmissive", emissiveOverridden))
            context.CurrentScene.GetOrCreateMaterialOverride(*context.SelectedEntity, materialIndex).overrideEmissive = false;

        ImGui::Text("Alpha Mode: %s  Double-Sided: %s", material.alphaMode.CStr(), material.doubleSided ? "Yes" : "No");

        ImGui::Separator();
        ImGui::TextUnformatted(ICON_FA_LAYER_GROUP " Shading");

        const SchemeRegistry& schemes = Renderer::Get().GetSchemeRegistry();
        uint32 currentSchemeId = schemes.FindId(activeOverride ? activeOverride->schemeName : String());

        if (ImGui::BeginCombo("Scheme", schemes.Get(currentSchemeId).name.CStr())) {
            for (uint32 i = 0; i < schemes.Count(); ++i) {
                const MaterialScheme& candidate = schemes.Get(i);
                if (ImGui::Selectable(candidate.name.CStr(), i == currentSchemeId)) {
                    MaterialOverride& matOverride = context.CurrentScene.GetOrCreateMaterialOverride(*context.SelectedEntity, materialIndex);
                    // The default scheme is stored as an empty name so that an entity which was never
                    // assigned a scheme and one explicitly set back to default serialize identically.
                    matOverride.schemeName = (i == SchemeRegistry::kDefaultSchemeId) ? String() : candidate.name;
                    matOverride.schemeParamValues.Clear();
                }
            }
            ImGui::EndCombo();
        }

        // Parameter widgets are generated from the scheme's declared schema -- adding a parameter to
        // the JSON is enough to make it editable here.
        const MaterialScheme& scheme = schemes.Get(currentSchemeId);
        for (const SchemeParam& param : scheme.params) {
            glm::vec4 value = param.defaultValue;
            bool isSet = false;
            if (activeOverride) {
                auto existing = activeOverride->schemeParamValues.Find(param.name);
                if (existing != activeOverride->schemeParamValues.End()) {
                    value = existing->second;
                    isSet = true;
                }
            }

            bool edited = false;
            switch (param.type) {
                case ESchemeParamType::Float:
                    edited = ImGui::DragFloat(param.name.CStr(), &value.x, 0.01f, param.minValue, param.maxValue);
                    break;
                case ESchemeParamType::Int:
                case ESchemeParamType::UInt: {
                    int32 asInt = (int32)value.x;
                    edited = ImGui::DragInt(param.name.CStr(), &asInt, 0.1f, (int32)param.minValue, (int32)param.maxValue);
                    if (edited)
                        value.x = (float32)asInt;
                    break;
                }
                case ESchemeParamType::Float2:
                    edited = ImGui::DragFloat2(param.name.CStr(), &value.x, 0.01f, param.minValue, param.maxValue);
                    break;
                case ESchemeParamType::Float3:
                    edited = param.isColor ? ImGui::ColorEdit3(param.name.CStr(), &value.x)
                                           : ImGui::DragFloat3(param.name.CStr(), &value.x, 0.01f, param.minValue, param.maxValue);
                    break;
                case ESchemeParamType::Float4:
                    edited = param.isColor ? ImGui::ColorEdit4(param.name.CStr(), &value.x)
                                           : ImGui::DragFloat4(param.name.CStr(), &value.x, 0.01f, param.minValue, param.maxValue);
                    break;
            }

            if (edited) {
                MaterialOverride& matOverride = context.CurrentScene.GetOrCreateMaterialOverride(*context.SelectedEntity, materialIndex);
                matOverride.schemeParamValues[param.name] = value;
            }

            ImGui::PushID(param.name.CStr());
            if (drawResetButton("resetParam", isSet)) {
                MaterialOverride& matOverride = context.CurrentScene.GetOrCreateMaterialOverride(*context.SelectedEntity, materialIndex);
                matOverride.schemeParamValues.Erase(param.name);
            }
            ImGui::PopID();
        }

        auto drawTextureSlot = [&](const char* label, const String& path, MaterialTextureSlot slot) {
            ImGui::Text("%s: %s", label, path.Empty() ? "-" : path.CStr());
            if (path.Empty())
                return;

            TShared<StreamingTexture> texture = streaming.GetMaterialTexture(requestId, materialIndex, slot);
            if (!texture || texture->GetDisplayTexID() == ImTextureID_Invalid) {
                ImGui::TextDisabled("Streaming...");
                return;
            }

            float aspect = texture->GetWidth() > 0 ? (float)texture->GetHeight() / (float)texture->GetWidth() : 1.0f;
            ImGui::Image(texture->GetDisplayTexID(), ImVec2(96.0f, 96.0f * aspect));
        };

        drawTextureSlot("Base Color Tex", material.baseColorTexture, MaterialTextureSlot::BaseColor);
        drawTextureSlot("Normal Tex", material.normalTexture, MaterialTextureSlot::Normal);
        drawTextureSlot("Metallic/Roughness Tex", material.metallicRoughnessTexture, MaterialTextureSlot::MetallicRoughness);
        drawTextureSlot("Occlusion Tex", material.occlusionTexture, MaterialTextureSlot::Occlusion);
        drawTextureSlot("Emissive Tex", material.emissiveTexture, MaterialTextureSlot::Emissive);
    }

    ImGui::Separator();
    if (ImGui::TreeNodeEx(ICON_FA_LAYER_GROUP " Meshes", ImGuiTreeNodeFlags_SpanAvailWidth)) {
        if (ImGui::BeginTable("InspectorMeshTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Mesh");
            ImGui::TableSetupColumn("Resident LOD");
            ImGui::TableSetupColumn("Meshlets");
            ImGui::TableHeadersRow();

            const TArray<TShared<StreamingModel>>& models = streaming.GetModels();
            for (uint32 meshIndex : context.SelectedEntity->meshIndices) {
                StreamingModel& mesh = *models[meshIndex];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (&mesh == context.SelectedMesh)
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.1f, 1.0f), ICON_FA_CUBE " %s", mesh.GetMesh().name.CStr());
                else
                    ImGui::TextUnformatted(mesh.GetMesh().name.CStr());

                ImGui::TableSetColumnIndex(1);
                uint32 residentLOD = mesh.SnapshotResidentLOD();
                if (residentLOD != StreamingModel::kNoResidentLOD)
                    ImGui::Text("%u/%u", residentLOD, (uint32)CaramelAsset::kLodCount - 1);
                else
                    ImGui::TextUnformatted("Loading...");

                ImGui::TableSetColumnIndex(2);
                if (residentLOD != StreamingModel::kNoResidentLOD)
                    ImGui::Text("%u", mesh.GetMeshletCount(residentLOD));
                else
                    ImGui::TextUnformatted("-");
            }

            ImGui::EndTable();
        }

        ImGui::TreePop();
    }

    ImGui::End();
}
