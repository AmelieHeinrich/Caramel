/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 10:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <imgui.h>

// Owns everything that decides how the editor *looks*: the palette, the fonts, and the layout
// primitives the panels share. Panels should reach for these instead of hand-rolling separators,
// label alignment or pixel offsets, so a change here moves the whole editor at once.
namespace EditorTheme
{
    // Loads the fonts into the atlas and applies the palette. Must run after ImGui::CreateContext().
    void Initialize();

    ImFont* HeaderFont();

    // Font-relative spacing. Every hardcoded pixel offset in a panel is a bug waiting for the next
    // font size or DPI change, so distances are expressed as multiples of the current text height.
    inline float Em(float multiplier) { return ImGui::GetFontSize() * multiplier; }

    // Section title drawn in the header face, inline with its rule.
    void SectionHeader(const char* label);

    // Square icon-only button. The label survives as the tooltip, so toolbars stay narrow enough
    // for a docked side panel.
    bool IconButton(const char* icon, const char* tooltip);

    // Two-column label/value layout. Without it every widget starts at whatever x its label string
    // happened to end at, which is what makes a long inspector look ragged.
    bool BeginProperties(const char* id);
    void EndProperties();

    // Opens a row and leaves the cursor in the value column with the item width already set.
    // Pass reserveTrailingButton when a SameLine() button follows the widget.
    void PropertyLabel(const char* label, bool reserveTrailingButton = false);

    // "##label", for widgets whose visible label now lives in the property column.
    const char* HiddenID(const char* label);

    // label/value row for read-only telemetry (the overlay).
    void StatRow(const char* label, const char* fmt, ...) IM_FMTARGS(2);
}
