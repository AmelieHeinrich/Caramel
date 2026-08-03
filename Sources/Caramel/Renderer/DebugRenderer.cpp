/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 14:15:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "DebugRenderer.hpp"

#include <Caramel/Renderer/Shader/ShaderServer.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstring>

namespace
{
    constexpr const char* kDepthShaderPath = "Content/Shaders/DebugDrawDepth.hlsl";
    constexpr const char* kOverlayShaderPath = "Content/Shaders/DebugDrawOverlay.hlsl";

    constexpr float kPi = 3.14159265358979323846f;

    // glm::perspective's NDC depth range depends on this define, and Frustum() unprojects the NDC
    // cube -- so read it here rather than assuming a convention.
#if defined(GLM_FORCE_DEPTH_ZERO_TO_ONE)
    constexpr float kNdcNearZ = 0.0f;
#else
    constexpr float kNdcNearZ = -1.0f;
#endif

    /// RGBA8 in the byte order Content/Shaders/Common/DebugDraw.hlsli unpacks (and ImGui packs).
    uint32 PackColor(const glm::vec4& color)
    {
        glm::vec4 c = glm::clamp(color, 0.0f, 1.0f) * 255.0f;
        return (uint32)(c.r + 0.5f) | ((uint32)(c.g + 0.5f) << 8) | ((uint32)(c.b + 0.5f) << 16) | ((uint32)(c.a + 0.5f) << 24);
    }

    uint32 ClampSegments(uint32 segments)
    {
        return segments < 3 ? 3 : (segments > 128 ? 128 : segments);
    }

    /// Rigid frame placing the canonical shape's local +Y along `axis`, with its origin at `origin`.
    /// The remaining two basis vectors are arbitrary but stable for a given axis.
    glm::mat4 MakeAxisFrame(const glm::vec3& origin, const glm::vec3& axis)
    {
        float length = glm::length(axis);
        glm::vec3 y = length > 1e-8f ? axis / length : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 reference = std::abs(y.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 x = glm::normalize(glm::cross(reference, y));
        glm::vec3 z = glm::cross(y, x);
        return glm::mat4(glm::vec4(x, 0.0f), glm::vec4(y, 0.0f), glm::vec4(z, 0.0f), glm::vec4(origin, 1.0f));
    }

    glm::vec3 TransformPoint(const glm::mat4& m, const glm::vec3& p)
    {
        return glm::vec3(m * glm::vec4(p, 1.0f));
    }

    // All frames here are rigid (or uniformly scaled), so the upper 3x3 rotates normals correctly
    // without an inverse-transpose.
    glm::vec3 TransformNormal(const glm::mat4& m, const glm::vec3& n)
    {
        glm::vec3 result = glm::mat3(m) * n;
        float length = glm::length(result);
        return length > 1e-8f ? result / length : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

DebugRenderer* DebugRenderer::s_Instance = nullptr;

// ---------------------------------------------------------------------------------------------
// StreamBuffer
// ---------------------------------------------------------------------------------------------

void DebugRenderer::StreamBuffer::Init(uint32 framesInFlight)
{
    buffers.Resize(framesInFlight);
    views.Resize(framesInFlight);
    capacities.Resize(framesInFlight, 0);
}

bool DebugRenderer::StreamBuffer::Upload(agfx::Device& device, uint32 frameIndex, const char* name, uint32 stride,
                                         const void* first, uint64 firstBytes, const void* second, uint64 secondBytes)
{
    uint64 totalBytes = firstBytes + secondBytes;
    if (totalBytes == 0)
        return false;

    if (totalBytes > capacities[frameIndex]) {
        // Same over-allocate-and-keep policy as ImGuiRenderer: debug draw counts swing frame to
        // frame, and recreating a bindless view every frame would churn descriptors.
        uint64 newCapacity = totalBytes + totalBytes / 2 + (uint64)stride * 256;

        agfx::BufferCreateInfo bufferInfo;
        bufferInfo.SetSize(newCapacity).SetStride(stride).SetUsage(agfx::BufferUsage::ShaderRead).SetMemoryType(agfx::BufferMemoryType::CPUToGPU);
        buffers[frameIndex] = device.CreateBuffer(bufferInfo);
        buffers[frameIndex].SetName(name);

        agfx::BufferViewCreateInfo viewInfo;
        viewInfo.SetBuffer(buffers[frameIndex].Get()).SetType(agfx::BufferViewType::Structured).SetOffset(0).SetWriteable(false);
        views[frameIndex] = device.CreateBufferView(viewInfo);

        capacities[frameIndex] = newCapacity;
        device.MakeResourcesResident();
    }

    // The depth-tested block is written first and the overlay block right behind it; the overlay
    // draw reaches its half via the uBaseIndex push constant.
    agfx::MappedBuffer mapped(buffers[frameIndex]);
    uint8* dst = mapped.As<uint8>();
    if (firstBytes > 0)
        std::memcpy(dst, first, firstBytes);
    if (secondBytes > 0)
        std::memcpy(dst + firstBytes, second, secondBytes);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------------------------

DebugRenderer::DebugRenderer(agfx::Device& device, agfx::TextureFormat colorFormat, agfx::TextureFormat depthFormat, uint32 framesInFlight)
    : m_Device(&device)
{
    s_Instance = this;

    // Two templates, identical apart from depth state. Nothing is culled (debug shapes are viewed
    // from every side) and everything alpha blends. The depth-tested pass writes depth too, so
    // overlapping debug shapes occlude each other correctly instead of just painting in draw
    // order; nothing runs after this pass that reads the depth buffer, so there's nothing for the
    // writes to corrupt.
    agfx::RenderPipelineCreateInfo depthPipelineInfo;
    depthPipelineInfo.SetName("Debug Draw Pipeline (Depth Tested)")
                     .SetCullMode(agfx::CullMode::None)
                     .SetTopology(agfx::Topology::Triangles)
                     .SetDepthState(true, true, agfx::ComparisonFunction::Less)
                     .SetDepthFormat(depthFormat)
                     .AddColorAttachment(colorFormat)
                     .SetAlphaBlend();
    ShaderServer::RegisterRenderPipeline(depthPipelineInfo, kDepthShaderPath);

    agfx::RenderPipelineCreateInfo overlayPipelineInfo;
    overlayPipelineInfo.SetName("Debug Draw Pipeline (Overlay)")
                       .SetCullMode(agfx::CullMode::None)
                       .SetTopology(agfx::Topology::Triangles)
                       .SetDepthState(false, false)
                       .AddColorAttachment(colorFormat)
                       .SetAlphaBlend();
    ShaderServer::RegisterRenderPipeline(overlayPipelineInfo, kOverlayShaderPath);

    m_SegmentStream.Init(framesInFlight);
    m_PointStream.Init(framesInFlight);
    m_VertexStream.Init(framesInFlight);

    m_FrameConstantBuffers.Resize(framesInFlight);
    m_FrameConstantViews.Resize(framesInFlight);
    for (uint32 i = 0; i < framesInFlight; ++i) {
        // Read as a 1-element AGFXStructuredBuffer rather than a real cbuffer, matching the
        // convention SponzaRenderer established (AGFX.hlsli has no bindless-cbuffer wrapper).
        agfx::BufferCreateInfo bufferInfo;
        bufferInfo.SetSize(sizeof(GPUFrameConstants)).SetStride(sizeof(GPUFrameConstants)).SetUsage(agfx::BufferUsage::ShaderRead).SetMemoryType(agfx::BufferMemoryType::CPUToGPU);
        m_FrameConstantBuffers[i] = m_Device->CreateBuffer(bufferInfo);
        m_FrameConstantBuffers[i].SetName("Debug Draw Frame Constants");

        agfx::BufferViewCreateInfo viewInfo;
        viewInfo.SetBuffer(m_FrameConstantBuffers[i].Get()).SetType(agfx::BufferViewType::Structured).SetOffset(0).SetWriteable(false);
        m_FrameConstantViews[i] = m_Device->CreateBufferView(viewInfo);
    }
}

// ---------------------------------------------------------------------------------------------
// Emitters
// ---------------------------------------------------------------------------------------------

void DebugRenderer::EmitLine(const glm::vec3& a, const glm::vec3& b, const DebugStyle& style)
{
    GPUSegment segment;
    segment.a = a;
    segment.color = PackColor(style.color);
    segment.b = b;
    segment.thickness = style.thickness;
    m_Segments[style.depthTest ? 0 : 1].PushBack(segment);
}

void DebugRenderer::EmitTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const DebugStyle& style)
{
    glm::vec3 edge0 = b - a;
    glm::vec3 edge1 = c - a;
    glm::vec3 normal = glm::cross(edge0, edge1);
    float length = glm::length(normal);
    normal = length > 1e-12f ? normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
    EmitTriangle(a, b, c, normal, normal, normal, style);
}

void DebugRenderer::EmitTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                                 const glm::vec3& na, const glm::vec3& nb, const glm::vec3& nc, const DebugStyle& style)
{
    TArray<GPUVertex>& bucket = m_Vertices[style.depthTest ? 0 : 1];
    uint32 color = PackColor(style.color);
    bucket.PushBack({ a, color, na, 0.0f });
    bucket.PushBack({ b, color, nb, 0.0f });
    bucket.PushBack({ c, color, nc, 0.0f });
}

void DebugRenderer::EmitQuad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const DebugStyle& style)
{
    if (style.filled) {
        EmitTriangle(a, b, c, style);
        EmitTriangle(a, c, d, style);
    } else {
        EmitLine(a, b, style);
        EmitLine(b, c, style);
        EmitLine(c, d, style);
        EmitLine(d, a, style);
    }
}

void DebugRenderer::EmitRing(const glm::mat4& frame, float radius, float y, uint32 segments, const DebugStyle& style)
{
    glm::vec3 previous = TransformPoint(frame, glm::vec3(radius, y, 0.0f));
    for (uint32 i = 1; i <= segments; ++i) {
        float theta = (2.0f * kPi * (float)i) / (float)segments;
        glm::vec3 current = TransformPoint(frame, glm::vec3(radius * std::cos(theta), y, radius * std::sin(theta)));
        EmitLine(previous, current, style);
        previous = current;
    }
}

void DebugRenderer::EmitDisc(const glm::mat4& frame, float radius, float y, float normalSign, uint32 segments, const DebugStyle& style)
{
    glm::vec3 center = TransformPoint(frame, glm::vec3(0.0f, y, 0.0f));
    glm::vec3 normal = TransformNormal(frame, glm::vec3(0.0f, normalSign, 0.0f));
    glm::vec3 previous = TransformPoint(frame, glm::vec3(radius, y, 0.0f));
    for (uint32 i = 1; i <= segments; ++i) {
        float theta = (2.0f * kPi * (float)i) / (float)segments;
        glm::vec3 current = TransformPoint(frame, glm::vec3(radius * std::cos(theta), y, radius * std::sin(theta)));
        EmitTriangle(center, previous, current, normal, normal, normal, style);
        previous = current;
    }
}

void DebugRenderer::EmitCylinderSurface(const glm::mat4& frame, float radius, float height, uint32 segments, uint32 wireMeridians, bool caps, const DebugStyle& style)
{
    if (style.filled) {
        for (uint32 i = 0; i < segments; ++i) {
            float theta0 = (2.0f * kPi * (float)i) / (float)segments;
            float theta1 = (2.0f * kPi * (float)(i + 1)) / (float)segments;
            glm::vec2 dir0(std::cos(theta0), std::sin(theta0));
            glm::vec2 dir1(std::cos(theta1), std::sin(theta1));

            glm::vec3 bottom0 = TransformPoint(frame, glm::vec3(radius * dir0.x, 0.0f, radius * dir0.y));
            glm::vec3 bottom1 = TransformPoint(frame, glm::vec3(radius * dir1.x, 0.0f, radius * dir1.y));
            glm::vec3 top0 = TransformPoint(frame, glm::vec3(radius * dir0.x, height, radius * dir0.y));
            glm::vec3 top1 = TransformPoint(frame, glm::vec3(radius * dir1.x, height, radius * dir1.y));

            // Analytic side normals rather than face normals, so the headlight shading is smooth
            // around the barrel instead of faceted.
            glm::vec3 normal0 = TransformNormal(frame, glm::vec3(dir0.x, 0.0f, dir0.y));
            glm::vec3 normal1 = TransformNormal(frame, glm::vec3(dir1.x, 0.0f, dir1.y));

            EmitTriangle(bottom0, bottom1, top1, normal0, normal1, normal1, style);
            EmitTriangle(bottom0, top1, top0, normal0, normal1, normal0, style);
        }
        if (caps) {
            EmitDisc(frame, radius, 0.0f, -1.0f, segments, style);
            EmitDisc(frame, radius, height, 1.0f, segments, style);
        }
    } else {
        EmitRing(frame, radius, 0.0f, segments, style);
        EmitRing(frame, radius, height, segments, style);
        for (uint32 i = 0; i < wireMeridians; ++i) {
            float theta = (2.0f * kPi * (float)i) / (float)wireMeridians;
            glm::vec3 offset(radius * std::cos(theta), 0.0f, radius * std::sin(theta));
            EmitLine(TransformPoint(frame, offset), TransformPoint(frame, offset + glm::vec3(0.0f, height, 0.0f)), style);
        }
    }
}

void DebugRenderer::EmitConeSurface(const glm::mat4& frame, float radius, float height, uint32 segments, uint32 wireMeridians, const DebugStyle& style)
{
    // Canonical cone: apex at the local origin, base ring at local y = height.
    glm::vec3 apex = TransformPoint(frame, glm::vec3(0.0f, 0.0f, 0.0f));

    if (style.filled) {
        for (uint32 i = 0; i < segments; ++i) {
            float theta0 = (2.0f * kPi * (float)i) / (float)segments;
            float theta1 = (2.0f * kPi * (float)(i + 1)) / (float)segments;
            glm::vec2 dir0(std::cos(theta0), std::sin(theta0));
            glm::vec2 dir1(std::cos(theta1), std::sin(theta1));

            glm::vec3 base0 = TransformPoint(frame, glm::vec3(radius * dir0.x, height, radius * dir0.y));
            glm::vec3 base1 = TransformPoint(frame, glm::vec3(radius * dir1.x, height, radius * dir1.y));

            // Outward normal of the slanted surface: perpendicular to both the slant and the
            // tangent, which works out to (cos*h, -r, sin*h).
            glm::vec3 normal0 = TransformNormal(frame, glm::vec3(dir0.x * height, -radius, dir0.y * height));
            glm::vec3 normal1 = TransformNormal(frame, glm::vec3(dir1.x * height, -radius, dir1.y * height));

            EmitTriangle(apex, base0, base1, glm::normalize(normal0 + normal1), normal0, normal1, style);
        }
        EmitDisc(frame, radius, height, 1.0f, segments, style);
    } else {
        EmitRing(frame, radius, height, segments, style);
        for (uint32 i = 0; i < wireMeridians; ++i) {
            float theta = (2.0f * kPi * (float)i) / (float)wireMeridians;
            EmitLine(apex, TransformPoint(frame, glm::vec3(radius * std::cos(theta), height, radius * std::sin(theta))), style);
        }
    }
}

void DebugRenderer::EmitSphereSurface(const glm::mat4& frame, float radius, float yOffset, uint32 sectors, uint32 stacks,
                                      uint32 stackBegin, uint32 stackEnd, uint32 wireMeridians, const DebugStyle& style)
{
    // Unit direction on the sphere for stack/sector indices; stack 0 is +Y, stack `stacks` is -Y.
    auto direction = [&](uint32 stack, uint32 sector) {
        float phi = (kPi * (float)stack) / (float)stacks;
        float theta = (2.0f * kPi * (float)sector) / (float)sectors;
        float ringRadius = std::sin(phi);
        return glm::vec3(ringRadius * std::cos(theta), std::cos(phi), ringRadius * std::sin(theta));
    };
    auto position = [&](const glm::vec3& d) {
        return TransformPoint(frame, d * radius + glm::vec3(0.0f, yOffset, 0.0f));
    };

    if (style.filled) {
        for (uint32 stack = stackBegin; stack < stackEnd; ++stack) {
            for (uint32 sector = 0; sector < sectors; ++sector) {
                glm::vec3 d00 = direction(stack, sector);
                glm::vec3 d01 = direction(stack, sector + 1);
                glm::vec3 d10 = direction(stack + 1, sector);
                glm::vec3 d11 = direction(stack + 1, sector + 1);

                glm::vec3 n00 = TransformNormal(frame, d00);
                glm::vec3 n01 = TransformNormal(frame, d01);
                glm::vec3 n10 = TransformNormal(frame, d10);
                glm::vec3 n11 = TransformNormal(frame, d11);

                // The two polar stacks degenerate to triangles -- skip the collapsed half.
                if (stack != 0)
                    EmitTriangle(position(d00), position(d10), position(d11), n00, n10, n11, style);
                if (stack + 1 != stacks)
                    EmitTriangle(position(d00), position(d11), position(d01), n00, n11, n01, style);
            }
        }
    } else {
        for (uint32 stack = stackBegin; stack <= stackEnd; ++stack) {
            if (stack == 0 || stack == stacks)
                continue; // Pole: the "ring" is a single point.
            glm::vec3 previous = position(direction(stack, 0));
            for (uint32 sector = 1; sector <= sectors; ++sector) {
                glm::vec3 current = position(direction(stack, sector));
                EmitLine(previous, current, style);
                previous = current;
            }
        }
        for (uint32 i = 0; i < wireMeridians; ++i) {
            // Meridians are sampled on the full sector grid so they line up with the rings above.
            uint32 sector = (i * sectors) / wireMeridians;
            glm::vec3 previous = position(direction(stackBegin, sector));
            for (uint32 stack = stackBegin + 1; stack <= stackEnd; ++stack) {
                glm::vec3 current = position(direction(stack, sector));
                EmitLine(previous, current, style);
                previous = current;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Public shape API
// ---------------------------------------------------------------------------------------------

void DebugRenderer::Line(const glm::vec3& a, const glm::vec3& b, const DebugStyle& style)
{
    EmitLine(a, b, style);
}

void DebugRenderer::Point(const glm::vec3& p, const DebugStyle& style)
{
    GPUPoint point;
    point.p = p;
    point.color = PackColor(style.color);
    point.size = style.thickness;
    point.pad = glm::vec3(0.0f);
    m_Points[style.depthTest ? 0 : 1].PushBack(point);
}

void DebugRenderer::Quad(const glm::mat4& transform, const glm::vec2& halfExtents, const DebugStyle& style)
{
    EmitQuad(TransformPoint(transform, glm::vec3(-halfExtents.x, -halfExtents.y, 0.0f)),
             TransformPoint(transform, glm::vec3(halfExtents.x, -halfExtents.y, 0.0f)),
             TransformPoint(transform, glm::vec3(halfExtents.x, halfExtents.y, 0.0f)),
             TransformPoint(transform, glm::vec3(-halfExtents.x, halfExtents.y, 0.0f)),
             style);
}

void DebugRenderer::Quad(const glm::vec3& center, const glm::vec3& right, const glm::vec3& up, const DebugStyle& style)
{
    EmitQuad(center - right - up, center + right - up, center + right + up, center - right + up, style);
}

void DebugRenderer::Triangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const DebugStyle& style)
{
    if (style.filled) {
        EmitTriangle(a, b, c, style);
    } else {
        EmitLine(a, b, style);
        EmitLine(b, c, style);
        EmitLine(c, a, style);
    }
}

void DebugRenderer::Box(const glm::vec3& boundsMin, const glm::vec3& boundsMax, const DebugStyle& style)
{
    glm::vec3 corners[8] = {
        { boundsMin.x, boundsMin.y, boundsMin.z }, { boundsMax.x, boundsMin.y, boundsMin.z },
        { boundsMax.x, boundsMax.y, boundsMin.z }, { boundsMin.x, boundsMax.y, boundsMin.z },
        { boundsMin.x, boundsMin.y, boundsMax.z }, { boundsMax.x, boundsMin.y, boundsMax.z },
        { boundsMax.x, boundsMax.y, boundsMax.z }, { boundsMin.x, boundsMax.y, boundsMax.z },
    };

    if (style.filled) {
        EmitQuad(corners[0], corners[3], corners[2], corners[1], style); // -Z
        EmitQuad(corners[4], corners[5], corners[6], corners[7], style); // +Z
        EmitQuad(corners[0], corners[1], corners[5], corners[4], style); // -Y
        EmitQuad(corners[3], corners[7], corners[6], corners[2], style); // +Y
        EmitQuad(corners[0], corners[4], corners[7], corners[3], style); // -X
        EmitQuad(corners[1], corners[2], corners[6], corners[5], style); // +X
        return;
    }

    for (int i = 0; i < 4; ++i) {
        int next = (i + 1) % 4;
        EmitLine(corners[i], corners[next], style);
        EmitLine(corners[4 + i], corners[4 + next], style);
        EmitLine(corners[i], corners[4 + i], style);
    }
}

void DebugRenderer::Arrow(const glm::vec3& from, const glm::vec3& to, const DebugStyle& style, float headLength, float headRadius)
{
    glm::vec3 shaft = to - from;
    float length = glm::length(shaft);
    if (length < 1e-6f)
        return;
    glm::vec3 direction = shaft / length;

    if (headLength <= 0.0f)
        headLength = length * 0.2f;
    headLength = glm::min(headLength, length * 0.9f);
    if (headRadius <= 0.0f)
        headRadius = headLength * 0.4f;

    glm::vec3 shaftEnd = to - direction * headLength;
    uint32 segments = ClampSegments(style.segments);

    if (style.filled)
        EmitCylinderSurface(MakeAxisFrame(from, direction), headRadius * 0.35f, length - headLength, segments, 4, true, style);
    else
        EmitLine(from, shaftEnd, style);

    // The head is a cone with its tip at `to` opening backwards onto the end of the shaft.
    EmitConeSurface(MakeAxisFrame(to, -direction), headRadius, headLength, segments, 4, style);
}

void DebugRenderer::Cylinder(const glm::vec3& base, const glm::vec3& axis, float radius, const DebugStyle& style)
{
    float height = glm::length(axis);
    if (height < 1e-6f)
        return;
    EmitCylinderSurface(MakeAxisFrame(base, axis), radius, height, ClampSegments(style.segments), 4, true, style);
}

void DebugRenderer::Cylinder(const glm::mat4& transform, float radius, float height, const DebugStyle& style)
{
    EmitCylinderSurface(transform, radius, height, ClampSegments(style.segments), 4, true, style);
}

void DebugRenderer::Capsule(const glm::vec3& a, const glm::vec3& b, float radius, const DebugStyle& style)
{
    glm::vec3 axis = b - a;
    float height = glm::length(axis);
    glm::mat4 frame = MakeAxisFrame(a, height > 1e-6f ? axis : glm::vec3(0.0f, 1.0f, 0.0f));

    uint32 segments = ClampSegments(style.segments);
    // Even stack count so the equator falls exactly on a stack boundary and the two hemispheres
    // meet the barrel cleanly.
    uint32 stacks = glm::max(2u, (segments / 2u) & ~1u);

    if (height > 1e-6f)
        EmitCylinderSurface(frame, radius, height, segments, 4, false, style);

    EmitSphereSurface(frame, radius, height, segments, stacks, 0, stacks / 2, 4, style);         // Top cap.
    EmitSphereSurface(frame, radius, 0.0f, segments, stacks, stacks / 2, stacks, 4, style);      // Bottom cap.
}

void DebugRenderer::Capsule(const glm::mat4& transform, float radius, float cylinderHeight, const DebugStyle& style)
{
    Capsule(TransformPoint(transform, glm::vec3(0.0f)),
            TransformPoint(transform, glm::vec3(0.0f, cylinderHeight, 0.0f)),
            radius, style);
}

void DebugRenderer::Cone(const glm::vec3& apex, const glm::vec3& dir, float height, float radius, const DebugStyle& style)
{
    EmitConeSurface(MakeAxisFrame(apex, dir), radius, height, ClampSegments(style.segments), 6, style);
}

void DebugRenderer::Cone(const glm::mat4& transform, float radius, float height, const DebugStyle& style)
{
    EmitConeSurface(transform, radius, height, ClampSegments(style.segments), 6, style);
}

void DebugRenderer::SphereRings(const glm::vec3& center, float radius, const DebugStyle& style)
{
    if (style.filled) {
        Sphere(center, radius, style); // Three circles have nothing to fill.
        return;
    }

    uint32 segments = ClampSegments(style.segments);
    glm::mat4 frame = glm::translate(glm::mat4(1.0f), center);

    // Three great circles, one per basis plane: the XZ ring comes straight out of EmitRing, the
    // other two are the same ring rotated onto the XY and YZ planes.
    EmitRing(frame, radius, 0.0f, segments, style);
    EmitRing(glm::rotate(frame, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)), radius, 0.0f, segments, style);
    EmitRing(glm::rotate(frame, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)), radius, 0.0f, segments, style);
}

void DebugRenderer::Sphere(const glm::vec3& center, float radius, const DebugStyle& style)
{
    uint32 sectors = ClampSegments(style.segments);
    uint32 stacks = glm::max(2u, sectors / 2u);
    // Wireframe draws the full tessellation -- that's what distinguishes this from SphereRings().
    EmitSphereSurface(glm::translate(glm::mat4(1.0f), center), radius, 0.0f, sectors, stacks, 0, stacks, sectors, style);
}

void DebugRenderer::Axes(const glm::mat4& transform, float scale, const DebugStyle& style)
{
    glm::vec3 origin = TransformPoint(transform, glm::vec3(0.0f));

    DebugStyle axisStyle = style;
    const glm::vec3 kLocalAxes[3] = { glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f) };
    const glm::vec4 kAxisColors[3] = { glm::vec4(1.0f, 0.2f, 0.2f, 1.0f), glm::vec4(0.2f, 1.0f, 0.2f, 1.0f), glm::vec4(0.3f, 0.45f, 1.0f, 1.0f) };

    for (int i = 0; i < 3; ++i) {
        // Keep the caller's alpha so a gizmo can still be faded out, but the hue is fixed by
        // convention -- an axis gizmo whose X isn't red is worse than useless.
        axisStyle.color = glm::vec4(glm::vec3(kAxisColors[i]), style.color.a);
        Arrow(origin, origin + TransformNormal(transform, kLocalAxes[i]) * scale, axisStyle);
    }
}

void DebugRenderer::Frustum(const glm::mat4& viewProj, const DebugStyle& style)
{
    glm::mat4 inverseViewProj = glm::inverse(viewProj);

    // Ring order around each cap, so consecutive indices share an edge.
    const glm::vec2 kCorners[4] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f } };
    glm::vec3 corners[8];
    for (int i = 0; i < 4; ++i) {
        for (int cap = 0; cap < 2; ++cap) {
            glm::vec4 ndc(kCorners[i].x, kCorners[i].y, cap == 0 ? kNdcNearZ : 1.0f, 1.0f);
            glm::vec4 world = inverseViewProj * ndc;
            if (std::abs(world.w) < 1e-9f)
                return; // Degenerate/non-invertible projection; nothing meaningful to draw.
            corners[cap * 4 + i] = glm::vec3(world) / world.w;
        }
    }

    if (style.filled) {
        EmitQuad(corners[0], corners[1], corners[2], corners[3], style); // Near.
        EmitQuad(corners[4], corners[7], corners[6], corners[5], style); // Far.
        for (int i = 0; i < 4; ++i) {
            int next = (i + 1) % 4;
            EmitQuad(corners[i], corners[next], corners[4 + next], corners[4 + i], style);
        }
    } else {
        for (int i = 0; i < 4; ++i) {
            int next = (i + 1) % 4;
            EmitLine(corners[i], corners[next], style);
            EmitLine(corners[4 + i], corners[4 + next], style);
            EmitLine(corners[i], corners[4 + i], style);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Flush
// ---------------------------------------------------------------------------------------------

void DebugRenderer::DrawBucket(agfx::RenderPass& pass, const char* shaderPath, std::initializer_list<const char*> variants,
                               const StreamBuffer& stream, uint32 frameIndex, uint32 baseIndex, uint32 vertexCount)
{
    if (vertexCount == 0)
        return;

    agfx::RenderPipeline* pipeline = ShaderServer::GetPipeline(shaderPath, variants);
    if (!pipeline)
        return;

    pass.SetPipeline(*pipeline);

    PushConstants pc{};
    pc.rFrameConstants = (uint32)m_FrameConstantViews[frameIndex].GetHandle();
    pc.rPrims = (uint32)stream.views[frameIndex].GetHandle();
    pc.uBaseIndex = baseIndex;
    pass.PushConstants(pc);

    pass.Draw(vertexCount);
}

void DebugRenderer::Flush(agfx::CommandBuffer& commandBuffer, agfx::RenderTarget& color, agfx::RenderTarget& depth,
                          const Camera& camera, uint32 width, uint32 height, uint32 frameIndex)
{
    uint32 depthSegments = (uint32)m_Segments[0].Size();
    uint32 depthPoints = (uint32)m_Points[0].Size();
    uint32 depthVertices = (uint32)m_Vertices[0].Size();
    uint32 overlaySegments = (uint32)m_Segments[1].Size();
    uint32 overlayPoints = (uint32)m_Points[1].Size();
    uint32 overlayVertices = (uint32)m_Vertices[1].Size();

    bool hasDepthWork = depthSegments + depthPoints + depthVertices > 0;
    bool hasOverlayWork = overlaySegments + overlayPoints + overlayVertices > 0;
    if ((!hasDepthWork && !hasOverlayWork) || width == 0 || height == 0) {
        ClearBuckets();
        return;
    }

    {
        GPUFrameConstants constants{};
        constants.viewProj = camera.GetViewProjection((float)width / (float)height);
        constants.cameraPos = camera.GetPosition();
        constants.viewport = glm::vec2((float)width, (float)height);
        constants.invViewport = glm::vec2(1.0f / (float)width, 1.0f / (float)height);

        agfx::MappedBuffer mapped(m_FrameConstantBuffers[frameIndex]);
        std::memcpy(mapped.Get(), &constants, sizeof(constants));
    }

    bool hasSegments = m_SegmentStream.Upload(*m_Device, frameIndex, "Debug Draw Segment Buffer", sizeof(GPUSegment),
                                              m_Segments[0].Data(), m_Segments[0].Size() * sizeof(GPUSegment),
                                              m_Segments[1].Data(), m_Segments[1].Size() * sizeof(GPUSegment));
    bool hasPoints = m_PointStream.Upload(*m_Device, frameIndex, "Debug Draw Point Buffer", sizeof(GPUPoint),
                                          m_Points[0].Data(), m_Points[0].Size() * sizeof(GPUPoint),
                                          m_Points[1].Data(), m_Points[1].Size() * sizeof(GPUPoint));
    bool hasVertices = m_VertexStream.Upload(*m_Device, frameIndex, "Debug Draw Vertex Buffer", sizeof(GPUVertex),
                                             m_Vertices[0].Data(), m_Vertices[0].Size() * sizeof(GPUVertex),
                                             m_Vertices[1].Data(), m_Vertices[1].Size() * sizeof(GPUVertex));

    // Solid first, then lines, then points -- so wireframe overlays read on top of its own fill.
    if (hasDepthWork) {
        agfx::RenderPassCreateInfo passInfo{};
        passInfo.colorAttachmentCount = 1;
        passInfo.colorAttachments[0].renderTarget = color;
        passInfo.colorAttachments[0].loadOp = AGFX_LOAD_OPERATION_LOAD;
        passInfo.colorAttachments[0].storeOp = AGFX_STORE_OPERATION_STORE;
        passInfo.hasDepthAttachment = 1;
        passInfo.depthAttachment.renderTarget = depth;
        passInfo.depthAttachment.loadOp = AGFX_LOAD_OPERATION_LOAD;
        passInfo.depthAttachment.storeOp = AGFX_STORE_OPERATION_STORE;
        passInfo.name = "Debug Draw Pass (Depth Tested)";
        passInfo.width = width;
        passInfo.height = height;

        agfx::RenderPass pass = commandBuffer.BeginRenderPass(passInfo);
        pass.SetViewport(0.0f, 0.0f, (float)width, (float)height);
        pass.SetScissor(0, 0, width, height);
        if (hasVertices) DrawBucket(pass, kDepthShaderPath, {}, m_VertexStream, frameIndex, 0, depthVertices);
        if (hasSegments) DrawBucket(pass, kDepthShaderPath, { "DEBUG_LINE" }, m_SegmentStream, frameIndex, 0, depthSegments * 6);
        if (hasPoints)   DrawBucket(pass, kDepthShaderPath, { "DEBUG_POINT" }, m_PointStream, frameIndex, 0, depthPoints * 6);
        pass.End();
    }

    if (hasOverlayWork) {
        agfx::RenderPassCreateInfo passInfo{};
        passInfo.colorAttachmentCount = 1;
        passInfo.colorAttachments[0].renderTarget = color;
        passInfo.colorAttachments[0].loadOp = AGFX_LOAD_OPERATION_LOAD;
        passInfo.colorAttachments[0].storeOp = AGFX_STORE_OPERATION_STORE;
        passInfo.name = "Debug Draw Pass (Overlay)";
        passInfo.width = width;
        passInfo.height = height;

        agfx::RenderPass pass = commandBuffer.BeginRenderPass(passInfo);
        pass.SetViewport(0.0f, 0.0f, (float)width, (float)height);
        pass.SetScissor(0, 0, width, height);
        // The overlay block sits behind the depth-tested block in the same buffer.
        if (hasVertices) DrawBucket(pass, kOverlayShaderPath, {}, m_VertexStream, frameIndex, depthVertices, overlayVertices);
        if (hasSegments) DrawBucket(pass, kOverlayShaderPath, { "DEBUG_LINE" }, m_SegmentStream, frameIndex, depthSegments, overlaySegments * 6);
        if (hasPoints)   DrawBucket(pass, kOverlayShaderPath, { "DEBUG_POINT" }, m_PointStream, frameIndex, depthPoints, overlayPoints * 6);
        pass.End();
    }

    ClearBuckets();
}

void DebugRenderer::ClearBuckets()
{
    for (int i = 0; i < 2; ++i) {
        m_Segments[i].Clear();
        m_Points[i].Clear();
        m_Vertices[i].Clear();
    }
}
