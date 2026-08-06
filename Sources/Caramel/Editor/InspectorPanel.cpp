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
#include <cfloat>
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

bool InspectorPanel::DrawTransformRow(const char* label, glm::vec3& v, float speed, float resetTo)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    bool changed = false;

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
        if (ImGui::Button(kAxisNames[i], ImVec2(buttonWidth, buttonWidth))) {
            *components[i] = resetTo;
            changed = true;
        }
        ImGui::PopStyleColor(4);
        ImGui::SetItemTooltip("Reset %s", kAxisNames[i]);

        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(fieldWidth);
        if (ImGui::DragFloat("##value", components[i], speed, 0.0f, 0.0f, "%.2f"))
            changed = true;
        ImGui::PopID();
    }

    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    if (EditorTheme::IconButton(ICON_FA_ROTATE_LEFT, "Reset all axes")) {
        v = glm::vec3(resetTo);
        changed = true;
    }

    ImGui::PopID();
    return changed;
}

void InspectorPanel::DrawMeshTransformSection(EditorContext& context, StreamingManager& streaming)
{
    if (!context.SelectedMesh)
        return;

    const TArray<TShared<StreamingModel>>& models = streaming.GetModels();
    uint32 meshSlot = 0;
    bool foundSlot = false;
    for (uint32 slot = 0; slot < (uint32)context.SelectedEntity->meshIndices.Size(); ++slot) {
        if (models[context.SelectedEntity->meshIndices[slot]].get() == context.SelectedMesh) {
            meshSlot = slot;
            foundSlot = true;
            break;
        }
    }

    if (!foundSlot)
        return;

    EditorTheme::SectionHeader(ICON_FA_CUBE " Mesh Transform");
    ImGui::TextDisabled("Offset within the entity, shared by every instance");

    // Same reasoning as the instance transform: a mesh-scoped script rewrites this every frame, and
    // there is no undo.
    bool scriptDriven = context.Scripts && context.Scripts->IsMeshDrivenByScript(*context.SelectedEntity, meshSlot);
    if (scriptDriven)
        ImGui::BeginDisabled();

    // Reading through GetOrCreate would allocate an entry for every mesh the user merely clicks on,
    // so an untouched mesh is edited through a scratch copy that is only committed when it changes.
    const MeshTransform* existing = context.CurrentScene.FindMeshTransform(*context.SelectedEntity, meshSlot);
    MeshTransform edited = existing ? *existing : MeshTransform{};
    edited.meshSlot = meshSlot;

    if (EditorTheme::BeginProperties("InspectorMeshTransform")) {
        DrawTransformRow("Position", edited.position, 0.05f, 0.0f);
        DrawTransformRow("Rotation", edited.rotationEuler, 0.5f, 0.0f);
        DrawTransformRow("Scale", edited.scale, 0.01f, 1.0f);
        EditorTheme::EndProperties();
    }

    bool changed = !existing
        ? !edited.IsIdentity()
        : (edited.position != existing->position || edited.rotationEuler != existing->rotationEuler || edited.scale != existing->scale);

    if (changed && !scriptDriven) {
        MeshTransform& stored = context.CurrentScene.GetOrCreateMeshTransform(*context.SelectedEntity, meshSlot);
        stored.position = edited.position;
        stored.rotationEuler = edited.rotationEuler;
        stored.scale = edited.scale;
    }

    if (scriptDriven) {
        ImGui::EndDisabled();
        ImGui::TextDisabled(ICON_FA_SCROLL " Driven by script");
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

    bool changed = false;
    if (EditorTheme::BeginProperties("InspectorTransform")) {
        changed |= DrawTransformRow("Position", instance.position, 0.05f, 0.0f);
        changed |= DrawTransformRow("Rotation", instance.rotationEuler, 0.5f, 0.0f);

        // A light's direction is normalized and its extent is authored on the component, so scaling
        // one does nothing -- an editable control that silently has no effect is worse than no
        // control.
        if (context.SelectedEntity->type != ESceneNodeType::Light)
            changed |= DrawTransformRow("Scale", instance.scale, 0.01f, 1.0f);

        EditorTheme::EndProperties();
    }
    if (changed)
        context.CurrentScene.MarkRenderInstancesDirty();

    if (scriptDriven) {
        ImGui::EndDisabled();
        ImGui::TextDisabled(ICON_FA_SCROLL " Driven by script");
    }
}

void InspectorPanel::DrawLightSection(EditorContext& context)
{
    if (context.SelectedEntity->type != ESceneNodeType::Light)
        return;

    LightComponent& light = context.SelectedEntity->light;

    EditorTheme::SectionHeader(ICON_FA_LIGHTBULB " Light");

    // Nothing here marks the scene dirty: Scene::BuildLights rebuilds the whole list every frame
    // precisely so that every one of these widgets can just write into the component.
    if (EditorTheme::BeginProperties("InspectorLight")) {
        EditorTheme::PropertyLabel("Enabled");
        ImGui::Checkbox(EditorTheme::HiddenID("Enabled"), &light.enabled);

        EditorTheme::PropertyLabel("Type");
        int32 typeIndex = (int32)light.type;
        if (ImGui::Combo(EditorTheme::HiddenID("Type"), &typeIndex, "Directional\0Point\0Spot\0Area\0")) {
            light.type = (ELightType)typeIndex;
            // Intensity means a different physical unit per type, so keeping the number would turn
            // 5000 lumens into 5000 lux -- direct sunlight.
            light.intensity = LightDefaultIntensity(light.type);
        }

        EditorTheme::PropertyLabel("Color");
        ImGui::ColorEdit3(EditorTheme::HiddenID("Color"), &light.color.x);

        const char* intensityFormat = "%.0f lm";
        if (light.type == ELightType::Directional)
            intensityFormat = "%.0f lux";
        else if (light.type == ELightType::Area)
            intensityFormat = "%.0f nits";

        // Drag speed scales with the value, because the useful range spans several orders of
        // magnitude -- lux sits in the hundreds while lumens need six figures to carry across a
        // scene. A fixed speed makes one end of that range untouchable.
        EditorTheme::PropertyLabel("Intensity");
        float32 intensityStep = std::max(light.intensity * 0.01f, 1.0f);
        ImGui::DragFloat(EditorTheme::HiddenID("Intensity"), &light.intensity, intensityStep, 0.0f, FLT_MAX, intensityFormat);

        if (light.type != ELightType::Directional) {
            EditorTheme::PropertyLabel("Range");
            ImGui::DragFloat(EditorTheme::HiddenID("Range"), &light.range, 0.1f, 0.01f, FLT_MAX, "%.2f m");
        }

        if (light.type == ELightType::Spot) {
            EditorTheme::PropertyLabel("Inner Angle");
            ImGui::DragFloat(EditorTheme::HiddenID("InnerAngle"), &light.innerAngle, 0.5f, 0.0f, 89.0f, "%.1f deg");

            EditorTheme::PropertyLabel("Outer Angle");
            ImGui::DragFloat(EditorTheme::HiddenID("OuterAngle"), &light.outerAngle, 0.5f, 0.1f, 89.9f, "%.1f deg");

            light.outerAngle = std::max(light.outerAngle, 0.1f);
            light.innerAngle = std::min(light.innerAngle, light.outerAngle);
        }

        if (light.type == ELightType::Point || light.type == ELightType::Spot) {
            EditorTheme::PropertyLabel("Source Radius");
            ImGui::DragFloat(EditorTheme::HiddenID("SourceRadius"), &light.sourceRadius, 0.01f, 0.0f, FLT_MAX, "%.3f m");
        }

        if (light.type == ELightType::Area) {
            EditorTheme::PropertyLabel("Shape");
            int32 shapeIndex = (int32)light.shape;
            if (ImGui::Combo(EditorTheme::HiddenID("Shape"), &shapeIndex, "Rect\0Disk\0Tube\0"))
                light.shape = (EAreaShape)shapeIndex;

            // The two size fields mean different things per shape, and nits are per square metre, so
            // a mislabelled field silently changes how bright the light is.
            const char* firstLabel = "Width";
            const char* secondLabel = "Height";
            if (light.shape == EAreaShape::Disk) {
                firstLabel = "Radius";
                secondLabel = nullptr;
            } else if (light.shape == EAreaShape::Tube) {
                firstLabel = "Radius";
                secondLabel = "Length";
            }

            EditorTheme::PropertyLabel(firstLabel);
            ImGui::DragFloat(EditorTheme::HiddenID("SizeX"), &light.size.x, 0.01f, 0.0f, FLT_MAX, "%.3f m");

            if (secondLabel) {
                EditorTheme::PropertyLabel(secondLabel);
                ImGui::DragFloat(EditorTheme::HiddenID("SizeY"), &light.size.y, 0.01f, 0.0f, FLT_MAX, "%.3f m");
            }

            if (light.shape != EAreaShape::Tube) {
                EditorTheme::PropertyLabel("Two Sided");
                ImGui::Checkbox(EditorTheme::HiddenID("TwoSided"), &light.twoSided);
            }
        }

        EditorTheme::EndProperties();
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

    // Edits follow the selection: with a mesh picked they narrow to that mesh, otherwise they apply
    // to every mesh of the entity using this material.
    int32 editMeshSlot = MaterialOverride::kAllMeshes;
    if (context.SelectedMesh) {
        const TArray<TShared<StreamingModel>>& models = streaming.GetModels();
        for (uint32 slot = 0; slot < (uint32)context.SelectedEntity->meshIndices.Size(); ++slot) {
            if (models[context.SelectedEntity->meshIndices[slot]].get() == context.SelectedMesh) {
                editMeshSlot = (int32)slot;
                break;
            }
        }
    }

    // `material` is the pristine cooked material -- overrides are layered on by GPUScene at
    // buffer-write time and never written back here, so each widget has to seed itself from the
    // override when one is active. A mesh with no override of its own inherits the entity-wide one.
    const MaterialOverride* activeOverride = context.CurrentScene.FindMaterialOverride(*context.SelectedEntity, materialIndex, editMeshSlot);

    if (editMeshSlot != MaterialOverride::kAllMeshes)
        ImGui::TextDisabled(ICON_FA_CUBE " Editing this mesh only");
    else if (context.SelectedEntity->meshIndices.Size() > 1)
        ImGui::TextDisabled(ICON_FA_CUBES " Applies to every mesh of this entity");

    // One edit, applied to everything the current selection covers. With a mesh selected that is
    // just that mesh; with only the entity selected it is every distinct material the entity uses,
    // so picking a scheme on Sponza reaches all of it instead of only the first mesh's material.
    //
    // The re-resolve at the end is load-bearing: GetOrCreateMaterialOverride can push onto
    // materialOverrides, which reallocates and would leave activeOverride dangling for the widgets
    // further down this same frame.
    auto applyToSelection = [&](auto&& mutate) {
        if (editMeshSlot != MaterialOverride::kAllMeshes) {
            mutate(context.CurrentScene.GetOrCreateMaterialOverride(*context.SelectedEntity, materialIndex, editMeshSlot));
        } else {
            const TArray<TShared<StreamingModel>>& models = streaming.GetModels();
            TArray<int32> handled;

            for (uint32 meshIndex : context.SelectedEntity->meshIndices) {
                int32 candidate = models[meshIndex]->GetMesh().materialIndex;

                bool seen = false;
                for (int32 done : handled) {
                    if (done == candidate) {
                        seen = true;
                        break;
                    }
                }
                if (seen)
                    continue;

                handled.PushBack(candidate);
                mutate(context.CurrentScene.GetOrCreateMaterialOverride(*context.SelectedEntity, candidate, MaterialOverride::kAllMeshes));
            }
        }

        activeOverride = context.CurrentScene.FindMaterialOverride(*context.SelectedEntity, materialIndex, editMeshSlot);
    };

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
            applyToSelection([&](MaterialOverride& matOverride) {
                matOverride.overrideBaseColor = true;
                matOverride.baseColorFactor = baseColor;
            });
        }
        if (drawResetButton("resetBaseColor", baseColorOverridden))
            applyToSelection([](MaterialOverride& matOverride) { matOverride.overrideBaseColor = false; });

        bool metallicOverridden = activeOverride && activeOverride->overrideMetallic;
        float metallic = metallicOverridden ? activeOverride->metallicFactor : material.metallicFactor;
        EditorTheme::PropertyLabel("Metallic", true);
        if (ImGui::DragFloat("##Metallic", &metallic, 0.01f, 0.0f, 1.0f)) {
            applyToSelection([&](MaterialOverride& matOverride) {
                matOverride.overrideMetallic = true;
                matOverride.metallicFactor = metallic;
            });
        }
        if (drawResetButton("resetMetallic", metallicOverridden))
            applyToSelection([](MaterialOverride& matOverride) { matOverride.overrideMetallic = false; });

        bool roughnessOverridden = activeOverride && activeOverride->overrideRoughness;
        float roughness = roughnessOverridden ? activeOverride->roughnessFactor : material.roughnessFactor;
        EditorTheme::PropertyLabel("Roughness", true);
        if (ImGui::DragFloat("##Roughness", &roughness, 0.01f, 0.0f, 1.0f)) {
            applyToSelection([&](MaterialOverride& matOverride) {
                matOverride.overrideRoughness = true;
                matOverride.roughnessFactor = roughness;
            });
        }
        if (drawResetButton("resetRoughness", roughnessOverridden))
            applyToSelection([](MaterialOverride& matOverride) { matOverride.overrideRoughness = false; });

        bool emissiveOverridden = activeOverride && activeOverride->overrideEmissive;
        glm::vec3 emissive = emissiveOverridden ? activeOverride->emissiveFactor : material.emissiveFactor;
        EditorTheme::PropertyLabel("Emissive", true);
        if (ImGui::ColorEdit3("##Emissive", &emissive.x)) {
            applyToSelection([&](MaterialOverride& matOverride) {
                matOverride.overrideEmissive = true;
                matOverride.emissiveFactor = emissive;
            });
        }
        if (drawResetButton("resetEmissive", emissiveOverridden))
            applyToSelection([](MaterialOverride& matOverride) { matOverride.overrideEmissive = false; });

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
                    // The default scheme is stored as an empty name so that an entity which was never
                    // assigned a scheme and one explicitly set back to default serialize identically.
                    String chosen = (i == SchemeRegistry::kDefaultSchemeId) ? String() : candidate.name;
                    applyToSelection([&](MaterialOverride& matOverride) {
                        matOverride.schemeName = chosen;
                        matOverride.schemeParamValues.Clear();
                    });
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
                applyToSelection([&](MaterialOverride& matOverride) {
                    matOverride.schemeParamValues[param.name] = value;
                });
            }

            ImGui::PushID(param.name.CStr());
            if (drawResetButton("resetParam", isSet)) {
                applyToSelection([&](MaterialOverride& matOverride) {
                    matOverride.schemeParamValues.Erase(param.name);
                });
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
        else if (context.SelectedEntity->type == ESceneNodeType::Light)
            ImGui::TextDisabled("Light");
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
    DrawLightSection(context);
    DrawMeshTransformSection(context, streaming);
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
