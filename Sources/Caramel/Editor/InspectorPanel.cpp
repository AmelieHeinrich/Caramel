/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:04:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "InspectorPanel.hpp"

#include <Caramel/Editor/EditorContext.hpp>
#include <Caramel/Editor/EditorTheme.hpp>
#include <Caramel/Scene/Scene.hpp>
#include <Caramel/Asset/StreamingManager.hpp>
#include <Caramel/Asset/StreamingModel.hpp>
#include <Caramel/Asset/StreamingTexture.hpp>
#include <Caramel/Renderer/MaterialScheme.hpp>
#include <Caramel/Renderer/Renderer.hpp>
#include <Caramel/Script/ScriptEngine.hpp>
#include <Caramel/Script/ScriptSystem.hpp>
#include <Caramel/Editor/ScriptMenu.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <FontAwesome/FA.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdio>

namespace
{
    const ImVec4 kAxisColors[3] = {
        ImVec4(0.66f, 0.22f, 0.26f, 1.0f),
        ImVec4(0.26f, 0.55f, 0.28f, 1.0f),
        ImVec4(0.22f, 0.42f, 0.72f, 1.0f),
    };
    const ImVec4 kAxisColorsHovered[3] = {
        ImVec4(0.80f, 0.28f, 0.32f, 1.0f),
        ImVec4(0.32f, 0.68f, 0.34f, 1.0f),
        ImVec4(0.28f, 0.52f, 0.88f, 1.0f),
    };
    const char* const kAxisNames[3] = { "X", "Y", "Z" };
}

const char* const InspectorPanel::kTitle = ICON_FA_CIRCLE " Inspector";

void InspectorPanel::DrawScriptSection(EditorContext& context, StreamingManager& streaming)
{
    if (!context.Scripts || !context.SelectedEntity)
        return;

    SceneNode& node = *context.SelectedEntity;
    ScriptEngine& engine = context.Scripts->GetEngine();

    EditorTheme::SectionHeader(ICON_FA_SCROLL " Scripts");

    // Scope follows whatever the inspector is currently showing, so the button attaches to the same
    // thing the panel above it is describing.
    if (ImGui::Button(ICON_FA_PLUS " Add Script"))
        ImGui::OpenPopup("AddScriptPopup");

    if (ImGui::BeginPopup("AddScriptPopup")) {
        if (ImGui::BeginMenu(ICON_FA_CUBES " On entity"))  {
            DrawAddScriptMenuItems(context, node, EScriptScope::Node, 0);
            ImGui::EndMenu();
        }

        if (SceneNodeTypeHasInstances(node.type) && ImGui::BeginMenu(ICON_FA_LOCATION_ARROW " On this instance")) {
            DrawAddScriptMenuItems(context, node, EScriptScope::Instance, context.SelectedInstance);
            ImGui::EndMenu();
        }

        if (context.SelectedMesh) {
            const TArray<TShared<StreamingModel>>& models = streaming.GetModels();
            uint32 meshSlot = 0;
            bool foundSlot = false;
            for (uint32 slot = 0; slot < (uint32)node.meshIndices.Size(); ++slot) {
                if (models[node.meshIndices[slot]].get() == context.SelectedMesh) {
                    meshSlot = slot;
                    foundSlot = true;
                    break;
                }
            }

            if (foundSlot && ImGui::BeginMenu(ICON_FA_CUBE " On this mesh")) {
                DrawAddScriptMenuItems(context, node, EScriptScope::Mesh, meshSlot);
                ImGui::EndMenu();
            }
        }

        ImGui::EndPopup();
    }

    if (node.scripts.IsEmpty()) {
        ImGui::TextDisabled("None attached");
        return;
    }

    int32 pendingRemove = -1;

    for (uint32 i = 0; i < (uint32)node.scripts.Size(); ++i) {
        ScriptComponent& component = node.scripts[i];

        // Instance- and mesh-scoped components only belong to the row currently selected.
        if (component.scope == EScriptScope::Instance && component.targetIndex != context.SelectedInstance)
            continue;
        if (component.scope == EScriptScope::Mesh) {
            if (!context.SelectedMesh || component.targetIndex >= node.meshIndices.Size())
                continue;
            if (streaming.GetModels()[node.meshIndices[component.targetIndex]].get() != context.SelectedMesh)
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
            } else if (EditorTheme::BeginProperties("ScriptProperties")) {
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
                        EditorTheme::PropertyLabel(property.name.CStr());
                        if (ImGui::InputText(EditorTheme::HiddenID(property.name.CStr()), buffer, sizeof(buffer))) {
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

                    // Width is reserved for the revert button whether or not it is showing, so the
                    // fields stay in one column as values get overridden.
                    EditorTheme::PropertyLabel(property.name.CStr(), true);
                    const char* id = EditorTheme::HiddenID(property.name.CStr());

                    bool edited = false;
                    switch (property.type) {
                        case EScriptPropertyType::Bool: {
                            bool asBool = value.x != 0.0f;
                            edited = ImGui::Checkbox(id, &asBool);
                            if (edited)
                                value.x = asBool ? 1.0f : 0.0f;
                            break;
                        }
                        case EScriptPropertyType::Int: {
                            int32 asInt = (int32)value.x;
                            edited = ImGui::DragInt(id, &asInt, 0.1f, (int32)minValue, (int32)maxValue);
                            if (edited)
                                value.x = (float32)asInt;
                            break;
                        }
                        case EScriptPropertyType::Float:
                            edited = ImGui::DragFloat(id, &value.x, 0.01f, minValue, maxValue);
                            break;
                        case EScriptPropertyType::Float2:
                            edited = ImGui::DragFloat2(id, &value.x, 0.01f, minValue, maxValue);
                            break;
                        case EScriptPropertyType::Float3:
                            edited = property.isColor ? ImGui::ColorEdit3(id, &value.x)
                                                      : ImGui::DragFloat3(id, &value.x, 0.01f, minValue, maxValue);
                            break;
                        case EScriptPropertyType::Float4:
                            edited = property.isColor ? ImGui::ColorEdit4(id, &value.x)
                                                      : ImGui::DragFloat4(id, &value.x, 0.01f, minValue, maxValue);
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
                        if (EditorTheme::IconButton(ICON_FA_ROTATE_LEFT, "Revert to default")) {
                            component.propertyValues.Erase(property.name);
                            context.Scripts->ApplyPropertyToLive(node, i, property);
                        }
                        ImGui::PopID();
                    }
                }

                EditorTheme::EndProperties();
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

void InspectorPanel::DrawTransformSection(EditorContext& context)
{
    Instance& instance = context.SelectedEntity->instances[context.SelectedInstance];

    EditorTheme::SectionHeader(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " Transform");

    // A script that owns this instance rewrites the transform every frame, so editing it here would
    // silently lose the edit -- and the editor has no undo.
    bool scriptDriven = context.Scripts && context.Scripts->IsInstanceDrivenByScript(*context.SelectedEntity, context.SelectedInstance);
    if (scriptDriven)
        ImGui::BeginDisabled();

    auto transformRow = [](const char* label, glm::vec3& v, float speed, float resetTo) {
        const ImGuiStyle& style = ImGui::GetStyle();

        ImGui::PushID(label);
        EditorTheme::PropertyLabel(label);

        // Three [axis tag][field] groups plus the trailing reset button, all sized off the frame
        // height so the row survives a font or DPI change.
        float buttonWidth = ImGui::GetFrameHeight();
        float fieldWidth = (ImGui::GetContentRegionAvail().x - 4.0f * buttonWidth - 6.0f * style.ItemInnerSpacing.x) / 3.0f;
        fieldWidth = std::max(fieldWidth, EditorTheme::Em(1.5f));

        float* components[3] = { &v.x, &v.y, &v.z };
        for (int i = 0; i < 3; ++i) {
            if (i > 0)
                ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
            ImGui::PushID(i);

            // Coloring the tag rather than the number keeps the value itself readable.
            ImGui::PushStyleColor(ImGuiCol_Button, kAxisColors[i]);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAxisColorsHovered[i]);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAxisColorsHovered[i]);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            if (ImGui::Button(kAxisNames[i], ImVec2(buttonWidth, buttonWidth)))
                *components[i] = resetTo;
            ImGui::PopStyleColor(4);
            ImGui::SetItemTooltip("Reset %s", kAxisNames[i]);

            ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
            ImGui::SetNextItemWidth(fieldWidth);
            ImGui::DragFloat("##value", components[i], speed, 0.0f, 0.0f, "%.2f");
            ImGui::PopID();
        }

        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        if (EditorTheme::IconButton(ICON_FA_ROTATE_LEFT, "Reset all axes"))
            v = glm::vec3(resetTo);

        ImGui::PopID();
    };

    if (EditorTheme::BeginProperties("InspectorTransform")) {
        transformRow("Position", instance.position, 0.05f, 0.0f);
        transformRow("Rotation", instance.rotationEuler, 0.5f, 0.0f);
        transformRow("Scale", instance.scale, 0.01f, 1.0f);
        EditorTheme::EndProperties();
    }

    if (scriptDriven) {
        ImGui::EndDisabled();
        ImGui::TextDisabled(ICON_FA_SCROLL " Driven by script");
    }
}

void InspectorPanel::DrawMaterialSection(EditorContext& context, StreamingManager& streaming)
{
    EditorTheme::SectionHeader(ICON_FA_PALETTE " Material");

    StreamingModel* materialSource = context.SelectedMesh;
    if (!materialSource && !context.SelectedEntity->meshIndices.IsEmpty())
        materialSource = streaming.GetModels()[context.SelectedEntity->meshIndices[0]].get();

    if (!materialSource) {
        ImGui::TextDisabled("No material on this entity");
        return;
    }

    const ModelMaterial& material = materialSource->GetMaterial();
    uint32 requestId = materialSource->GetRequestId();
    int32 materialIndex = materialSource->GetMesh().materialIndex;

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
        bool reset = EditorTheme::IconButton(ICON_FA_ROTATE_LEFT, "Revert to cooked value");
        ImGui::PopID();
        return reset;
    };

    if (EditorTheme::BeginProperties("InspectorMaterial")) {
        EditorTheme::PropertyLabel("Name");
        ImGui::TextUnformatted(material.name.CStr());

        bool baseColorOverridden = activeOverride && activeOverride->overrideBaseColor;
        glm::vec4 baseColor = baseColorOverridden ? activeOverride->baseColorFactor : material.baseColorFactor;
        EditorTheme::PropertyLabel("Base Color", true);
        if (ImGui::ColorEdit4("##BaseColor", &baseColor.x)) {
            MaterialOverride& matOverride = context.CurrentScene.GetOrCreateMaterialOverride(*context.SelectedEntity, materialIndex);
            matOverride.overrideBaseColor = true;
            matOverride.baseColorFactor = baseColor;
        }
        if (drawResetButton("resetBaseColor", baseColorOverridden))
            context.CurrentScene.GetOrCreateMaterialOverride(*context.SelectedEntity, materialIndex).overrideBaseColor = false;

        bool metallicOverridden = activeOverride && activeOverride->overrideMetallic;
        float metallic = metallicOverridden ? activeOverride->metallicFactor : material.metallicFactor;
        EditorTheme::PropertyLabel("Metallic", true);
        if (ImGui::DragFloat("##Metallic", &metallic, 0.01f, 0.0f, 1.0f)) {
            MaterialOverride& matOverride = context.CurrentScene.GetOrCreateMaterialOverride(*context.SelectedEntity, materialIndex);
            matOverride.overrideMetallic = true;
            matOverride.metallicFactor = metallic;
        }
        if (drawResetButton("resetMetallic", metallicOverridden))
            context.CurrentScene.GetOrCreateMaterialOverride(*context.SelectedEntity, materialIndex).overrideMetallic = false;

        bool roughnessOverridden = activeOverride && activeOverride->overrideRoughness;
        float roughness = roughnessOverridden ? activeOverride->roughnessFactor : material.roughnessFactor;
        EditorTheme::PropertyLabel("Roughness", true);
        if (ImGui::DragFloat("##Roughness", &roughness, 0.01f, 0.0f, 1.0f)) {
            MaterialOverride& matOverride = context.CurrentScene.GetOrCreateMaterialOverride(*context.SelectedEntity, materialIndex);
            matOverride.overrideRoughness = true;
            matOverride.roughnessFactor = roughness;
        }
        if (drawResetButton("resetRoughness", roughnessOverridden))
            context.CurrentScene.GetOrCreateMaterialOverride(*context.SelectedEntity, materialIndex).overrideRoughness = false;

        bool emissiveOverridden = activeOverride && activeOverride->overrideEmissive;
        glm::vec3 emissive = emissiveOverridden ? activeOverride->emissiveFactor : material.emissiveFactor;
        EditorTheme::PropertyLabel("Emissive", true);
        if (ImGui::ColorEdit3("##Emissive", &emissive.x)) {
            MaterialOverride& matOverride = context.CurrentScene.GetOrCreateMaterialOverride(*context.SelectedEntity, materialIndex);
            matOverride.overrideEmissive = true;
            matOverride.emissiveFactor = emissive;
        }
        if (drawResetButton("resetEmissive", emissiveOverridden))
            context.CurrentScene.GetOrCreateMaterialOverride(*context.SelectedEntity, materialIndex).overrideEmissive = false;

        EditorTheme::PropertyLabel("Alpha Mode");
        ImGui::TextUnformatted(material.alphaMode.CStr());

        EditorTheme::PropertyLabel("Double-Sided");
        ImGui::TextUnformatted(material.doubleSided ? "Yes" : "No");

        EditorTheme::EndProperties();
    }

    EditorTheme::SectionHeader(ICON_FA_LAYER_GROUP " Shading");

    const SchemeRegistry& schemes = Renderer::Get().GetSchemeRegistry();
    uint32 currentSchemeId = schemes.FindId(activeOverride ? activeOverride->schemeName : String());

    if (EditorTheme::BeginProperties("InspectorShading")) {
        EditorTheme::PropertyLabel("Scheme");
        if (ImGui::BeginCombo("##Scheme", schemes.Get(currentSchemeId).name.CStr())) {
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

            EditorTheme::PropertyLabel(param.name.CStr(), true);
            const char* id = EditorTheme::HiddenID(param.name.CStr());

            bool edited = false;
            switch (param.type) {
                case ESchemeParamType::Float:
                    edited = ImGui::DragFloat(id, &value.x, 0.01f, param.minValue, param.maxValue);
                    break;
                case ESchemeParamType::Int:
                case ESchemeParamType::UInt: {
                    int32 asInt = (int32)value.x;
                    edited = ImGui::DragInt(id, &asInt, 0.1f, (int32)param.minValue, (int32)param.maxValue);
                    if (edited)
                        value.x = (float32)asInt;
                    break;
                }
                case ESchemeParamType::Float2:
                    edited = ImGui::DragFloat2(id, &value.x, 0.01f, param.minValue, param.maxValue);
                    break;
                case ESchemeParamType::Float3:
                    edited = param.isColor ? ImGui::ColorEdit3(id, &value.x)
                                           : ImGui::DragFloat3(id, &value.x, 0.01f, param.minValue, param.maxValue);
                    break;
                case ESchemeParamType::Float4:
                    edited = param.isColor ? ImGui::ColorEdit4(id, &value.x)
                                           : ImGui::DragFloat4(id, &value.x, 0.01f, param.minValue, param.maxValue);
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

        EditorTheme::EndProperties();
    }

    DrawTextureGrid(streaming, material, requestId, materialIndex);
}

void InspectorPanel::DrawTextureGrid(StreamingManager& streaming, const ModelMaterial& material, uint32 requestId, int32 materialIndex)
{
    struct TextureSlotEntry
    {
        const char* label;
        const String* path;
        MaterialTextureSlot slot;
    };

    const TextureSlotEntry slots[] = {
        { "Base Color", &material.baseColorTexture, MaterialTextureSlot::BaseColor },
        { "Normal", &material.normalTexture, MaterialTextureSlot::Normal },
        { "Metallic/Roughness", &material.metallicRoughnessTexture, MaterialTextureSlot::MetallicRoughness },
        { "Occlusion", &material.occlusionTexture, MaterialTextureSlot::Occlusion },
        { "Emissive", &material.emissiveTexture, MaterialTextureSlot::Emissive },
    };

    // Collapsed by default: five stacked full-size previews pushed everything below them off the
    // panel. The names live in the tooltips instead of on their own lines.
    if (!ImGui::CollapsingHeader(ICON_FA_IMAGE " Textures"))
        return;

    float tileSize = EditorTheme::Em(4.0f);
    float rowWidth = ImGui::GetContentRegionAvail().x;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float usedWidth = 0.0f;
    bool drewAny = false;

    for (const TextureSlotEntry& entry : slots) {
        if (entry.path->Empty())
            continue;

        if (drewAny && usedWidth + tileSize <= rowWidth) {
            ImGui::SameLine();
        } else {
            usedWidth = 0.0f;
        }
        usedWidth += tileSize + spacing;
        drewAny = true;

        ImGui::BeginGroup();
        TShared<StreamingTexture> texture = streaming.GetMaterialTexture(requestId, materialIndex, entry.slot);
        if (!texture || texture->GetDisplayTexID() == ImTextureID_Invalid) {
            ImGui::Dummy(ImVec2(tileSize, tileSize));
            ImGui::SetItemTooltip("%s\nStreaming...", entry.label);
        } else {
            float aspect = texture->GetWidth() > 0 ? (float)texture->GetHeight() / (float)texture->GetWidth() : 1.0f;
            ImGui::Image(texture->GetDisplayTexID(), ImVec2(tileSize, tileSize * aspect));
            ImGui::SetItemTooltip("%s\n%s\n%ux%u", entry.label, entry.path->CStr(), texture->GetWidth(), texture->GetHeight());
        }
        ImGui::EndGroup();
    }

    if (!drewAny)
        ImGui::TextDisabled("No textures on this material");
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

    EditorTheme::SectionHeader(ICON_FA_CUBES " Entity");
    if (EditorTheme::BeginProperties("InspectorEntity")) {
        EditorTheme::PropertyLabel("Entity");
        ImGui::TextUnformatted(context.SelectedEntity->name.CStr());

        EditorTheme::PropertyLabel("Asset");
        if (context.SelectedEntity->type == ESceneNodeType::Entity)
            ImGui::TextUnformatted(context.SelectedEntity->cmdlPath.CStr());
        else
            ImGui::TextDisabled("Empty entity");

        if (context.SelectedMesh) {
            EditorTheme::PropertyLabel("Mesh");
            ImGui::TextUnformatted(context.SelectedMesh->GetMesh().name.CStr());
        }

        char nameBuffer[256];
        std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", instance.name.CStr());
        EditorTheme::PropertyLabel("Name");
        if (ImGui::InputText("##InstanceName", nameBuffer, sizeof(nameBuffer)))
            instance.name = nameBuffer;

        EditorTheme::EndProperties();
    }

    DrawTransformSection(context);
    DrawScriptSection(context, streaming);
    DrawMaterialSection(context, streaming);

    EditorTheme::SectionHeader(ICON_FA_LAYER_GROUP " Meshes");
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
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.15f, 1.0f), ICON_FA_CUBE " %s", mesh.GetMesh().name.CStr());
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

    ImGui::End();
}
