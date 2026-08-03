/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 09:00:36
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <AGFX/agfx.hpp>

class GPUTexture
{
public:
    GPUTexture(const agfx::TextureCreateInfo& info);
    ~GPUTexture() = default;

    agfx::Texture& GetTexture() { return m_Texture; }
    agfx::TextureView& GetTextureView() { return m_TextureView; }
private:
    agfx::Texture m_Texture;
    agfx::TextureView m_TextureView;
};
