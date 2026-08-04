/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:05:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

class ContentDrawerPanel
{
public:
    void Draw();

private:
    void DrawContentDirectory(const String& directory);

    bool m_Open = false;
    float m_AnimTime = 1.0f;
};
