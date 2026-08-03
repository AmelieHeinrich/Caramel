/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 09:01:51
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "GPUTexture.hpp"

#include <Caramel/Renderer/Renderer.hpp>

GPUTexture::GPUTexture(const agfx::TextureCreateInfo& info)
{
    agfx::Device& device = Renderer::Get().GetDevice();
    m_Texture = device.CreateTexture(info);

    agfx::TextureViewCreateInfo textureViewCreateInfo = agfx::TextureViewCreateInfo().SetTexture(m_Texture)
                                                                                     .SetFormat(static_cast<agfx::TextureFormat>(info.format))
                                                                                     .SetMipRange(0, info.mipLevels)
                                                                                     .SetWriteable(false);
    m_TextureView = device.CreateTextureView(textureViewCreateInfo);
}
