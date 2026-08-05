/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-05 00:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

// Every registered CVar (Core/CVar.hpp), grouped by category into a collapsible section with a
// widget matching its type. Systems don't add anything here directly -- declaring a CVar anywhere
// in the engine is enough for it to show up.
class SettingsPanel
{
public:
    void Draw();
};
