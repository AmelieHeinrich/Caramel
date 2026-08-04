/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 00:01:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>

#include <functional>

class Renderer;
struct EditorContext;

class ViewportPanel
{
public:
    using DropFileCallback = std::function<void(const String&)>;

    static const char* const kTitle;

    void SetDropFileCallback(DropFileCallback callback) { m_OnDropFile = std::move(callback); }

    void Draw(EditorContext& context, Renderer& renderer);

private:
    DropFileCallback m_OnDropFile;
};
