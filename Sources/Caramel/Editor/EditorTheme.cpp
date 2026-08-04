/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 10:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "EditorTheme.hpp"

#include <FontAwesome/FA.h>

#include <cfloat>
#include <cstdarg>
#include <cstdio>

namespace
{
    constexpr float kBaseFontSize = 17.0f;
    constexpr float kHeaderFontSize = 20.0f;

    ImFont* g_HeaderFont = nullptr;

    // The one saturated color in the editor. Everything that is *state* (checked, selected, being
    // dragged) is amber; everything that is merely chrome stays neutral.
    constexpr ImVec4 kAccent = ImVec4(0.95f, 0.75f, 0.15f, 1.00f);

    ImVec4 WithAlpha(const ImVec4& color, float alpha)
    {
        return ImVec4(color.x, color.y, color.z, alpha);
    }

    void ApplyStyle(ImGuiStyle& style)
    {
        ImVec4* colors = style.Colors;

        // --- 1. Sizing and spacing ---
        style.WindowPadding = ImVec2(8.0f, 8.0f);
        style.FramePadding = ImVec2(6.0f, 4.0f);
        style.CellPadding = ImVec2(6.0f, 4.0f);
        style.ItemSpacing = ImVec2(6.0f, 5.0f);
        style.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
        style.ScrollbarSize = 12.0f;
        style.GrabMinSize = 10.0f;
        style.IndentSpacing = 18.0f;

        // --- 2. Borders and rounding ---
        style.WindowRounding = 3.0f;
        style.ChildRounding = 3.0f;
        style.FrameRounding = 3.0f;
        style.PopupRounding = 3.0f;
        style.ScrollbarRounding = 3.0f;
        style.GrabRounding = 3.0f;
        style.TabRounding = 3.0f;

        style.WindowBorderSize = 1.0f;
        // A 1px outline on every drag field turns a dense inspector into a grid of boxes. The frame
        // background alone carries enough contrast to read as an input.
        style.FrameBorderSize = 0.0f;

        // --- 3. Fonts ---
        style.FontSizeBase = kBaseFontSize;
#if defined(CARAMEL_MACOS)
        // macOS renders type noticeably larger at the same nominal size.
        style.FontScaleMain = 0.75f;
#endif

        // --- 4. Section separators ---
        style.SeparatorTextBorderSize = 1.0f;
        style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
        style.SeparatorTextPadding = ImVec2(16.0f, style.FramePadding.y);

        // --- 5. Tree hierarchy ---
        // The hierarchy nests four levels deep (folder -> entity -> instance -> mesh); connecting
        // lines are what make that depth readable at a glance.
        style.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesToNodes;
        style.TreeLinesSize = 1.0f;

        // --- 6. Palette ---

        // Text stays neutral. Tinting every glyph amber removes the contrast that makes the accent
        // mean anything.
        colors[ImGuiCol_Text] = ImVec4(0.92f, 0.91f, 0.88f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.48f, 0.47f, 0.45f, 1.00f);

        colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.075f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.095f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.095f, 0.98f);

        colors[ImGuiCol_Border] = ImVec4(0.22f, 0.21f, 0.18f, 0.80f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        colors[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.17f, 0.16f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.23f, 0.22f, 0.19f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.26f, 0.20f, 1.00f);

        colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.09f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.14f, 0.12f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.07f, 0.07f, 0.06f, 1.00f);

        colors[ImGuiCol_MenuBarBg] = ImVec4(0.12f, 0.12f, 0.11f, 1.00f);

        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.06f, 0.06f, 0.055f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.26f, 0.25f, 0.22f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.34f, 0.32f, 0.26f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.44f, 0.40f, 0.28f, 1.00f);

        colors[ImGuiCol_Separator] = ImVec4(0.24f, 0.23f, 0.20f, 1.00f);
        colors[ImGuiCol_SeparatorHovered] = WithAlpha(kAccent, 0.60f);
        colors[ImGuiCol_SeparatorActive] = kAccent;

        colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.25f, 0.22f, 0.60f);
        colors[ImGuiCol_ResizeGripHovered] = WithAlpha(kAccent, 0.60f);
        colors[ImGuiCol_ResizeGripActive] = kAccent;

        // State: amber.
        colors[ImGuiCol_CheckMark] = kAccent;
        colors[ImGuiCol_SliderGrab] = ImVec4(0.68f, 0.54f, 0.14f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = kAccent;

        // Chrome: neutral, warming up on interaction.
        colors[ImGuiCol_Button] = ImVec4(0.20f, 0.19f, 0.17f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.31f, 0.27f, 0.16f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.43f, 0.36f, 0.16f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.25f, 0.23f, 0.16f, 1.00f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.35f, 0.30f, 0.17f, 1.00f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.45f, 0.38f, 0.18f, 1.00f);

        colors[ImGuiCol_Tab] = ImVec4(0.13f, 0.13f, 0.12f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.25f, 0.16f, 1.00f);
        colors[ImGuiCol_TabSelected] = ImVec4(0.19f, 0.18f, 0.15f, 1.00f);
        // Left unset, ImGui draws its default blue overline on top of the amber tabs.
        colors[ImGuiCol_TabSelectedOverline] = kAccent;
        colors[ImGuiCol_TabDimmed] = ImVec4(0.10f, 0.10f, 0.09f, 1.00f);
        colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.15f, 0.14f, 0.13f, 1.00f);
        colors[ImGuiCol_TabDimmedSelectedOverline] = WithAlpha(kAccent, 0.35f);

        colors[ImGuiCol_TableHeaderBg] = ImVec4(0.16f, 0.16f, 0.14f, 1.00f);
        colors[ImGuiCol_TableBorderStrong] = ImVec4(0.28f, 0.27f, 0.23f, 1.00f);
        colors[ImGuiCol_TableBorderLight] = ImVec4(0.20f, 0.19f, 0.17f, 1.00f);
        colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.025f);

        colors[ImGuiCol_TreeLines] = ImVec4(0.32f, 0.31f, 0.28f, 1.00f);
        colors[ImGuiCol_TextLink] = kAccent;
        colors[ImGuiCol_TextSelectedBg] = WithAlpha(kAccent, 0.25f);
        colors[ImGuiCol_DragDropTarget] = WithAlpha(kAccent, 0.90f);
        colors[ImGuiCol_NavCursor] = kAccent;

#ifdef IMGUI_HAS_DOCK
        colors[ImGuiCol_DockingPreview] = WithAlpha(kAccent, 0.40f);
        colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.08f, 0.08f, 0.075f, 1.00f);
#endif
    }
}

void EditorTheme::Initialize()
{
    ImGuiIO& io = ImGui::GetIO();

    io.FontDefault = io.Fonts->AddFontFromFileTTF("Content/Fonts/Ubuntu-Regular.ttf", kBaseFontSize);

    static const ImWchar kIconRanges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    ImFontConfig iconConfig;
    iconConfig.MergeMode = true;
    iconConfig.PixelSnapH = true;
    io.Fonts->AddFontFromFileTTF("Content/Fonts/FA.ttf", kBaseFontSize, &iconConfig, kIconRanges);

    g_HeaderFont = io.Fonts->AddFontFromFileTTF("Content/Fonts/Quicksand-Bold.ttf", kHeaderFontSize);

    // Section headers are drawn in this face and their labels carry icons, so the icon range has to
    // be merged here as well -- otherwise every header renders its glyph as a missing-character box.
    ImFontConfig headerIconConfig;
    headerIconConfig.MergeMode = true;
    headerIconConfig.PixelSnapH = true;
    io.Fonts->AddFontFromFileTTF("Content/Fonts/FA.ttf", kHeaderFontSize, &headerIconConfig, kIconRanges);

    ApplyStyle(ImGui::GetStyle());
}

ImFont* EditorTheme::HeaderFont()
{
    return g_HeaderFont;
}

void EditorTheme::SectionHeader(const char* label)
{
    ImGuiStyle& style = ImGui::GetStyle();

    if (g_HeaderFont)
        ImGui::PushFont(g_HeaderFont, style.FontSizeBase);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.97f, 0.86f, 0.52f, 1.00f));
    ImGui::SeparatorText(label);
    ImGui::PopStyleColor();
    if (g_HeaderFont)
        ImGui::PopFont();
}

bool EditorTheme::IconButton(const char* icon, const char* tooltip)
{
    // The tooltip doubles as the ID, so two buttons sharing an icon stay distinct.
    ImGui::PushID(tooltip);
    float size = ImGui::GetFrameHeight();
    bool pressed = ImGui::Button(icon, ImVec2(size, size));
    ImGui::PopID();

    ImGui::SetItemTooltip("%s", tooltip);
    return pressed;
}

bool EditorTheme::BeginProperties(const char* id)
{
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX))
        return false;

    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, Em(6.5f));
    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void EditorTheme::EndProperties()
{
    ImGui::EndTable();
}

void EditorTheme::PropertyLabel(const char* label, bool reserveTrailingButton)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);

    ImGui::TableSetColumnIndex(1);
    float reserved = reserveTrailingButton ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0.0f;
    ImGui::SetNextItemWidth(reserved > 0.0f ? -reserved : -FLT_MIN);
}

const char* EditorTheme::HiddenID(const char* label)
{
    // Consumed by the very next widget call, so a single rotating buffer is enough.
    static char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "##%s", label);
    return buffer;
}

void EditorTheme::StatRow(const char* label, const char* fmt, ...)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", label);

    ImGui::TableSetColumnIndex(1);
    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
}
