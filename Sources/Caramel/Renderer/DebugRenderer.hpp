/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 14:15:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Renderer/Common.hpp>
#include <Caramel/Renderer/Camera.hpp>
#include <Caramel/Renderer/StreamBuffer.hpp>

#include <AGFX/agfx.hpp>
#include <glm/glm.hpp>

struct DebugStyle
{
    glm::vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };

    bool filled = false;

    bool depthTest = true;

    float thickness = 2.0f;

    uint32 segments = 24;
};

class DebugRenderer
{
public:
    DebugRenderer(agfx::Device& device, agfx::TextureFormat colorFormat, agfx::TextureFormat depthFormat, uint32 framesInFlight);

    static DebugRenderer& Get() { return *s_Instance; }

    void Line(const glm::vec3& a, const glm::vec3& b, const DebugStyle& style = {});
    void Point(const glm::vec3& p, const DebugStyle& style = {});

    void Quad(const glm::mat4& transform, const glm::vec2& halfExtents, const DebugStyle& style = {});
    void Quad(const glm::vec3& center, const glm::vec3& right, const glm::vec3& up, const DebugStyle& style = {});

    void Triangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const DebugStyle& style = {});

    void Box(const glm::vec3& boundsMin, const glm::vec3& boundsMax, const DebugStyle& style = {});

    void Arrow(const glm::vec3& from, const glm::vec3& to, const DebugStyle& style = {}, float headLength = -1.0f, float headRadius = -1.0f);

    void Cylinder(const glm::vec3& base, const glm::vec3& axis, float radius, const DebugStyle& style = {});
    void Cylinder(const glm::mat4& transform, float radius, float height, const DebugStyle& style = {});

    void Capsule(const glm::vec3& a, const glm::vec3& b, float radius, const DebugStyle& style = {});
    void Capsule(const glm::mat4& transform, float radius, float cylinderHeight, const DebugStyle& style = {});

    void Cone(const glm::vec3& apex, const glm::vec3& dir, float height, float radius, const DebugStyle& style = {});
    void Cone(const glm::mat4& transform, float radius, float height, const DebugStyle& style = {});

    void SphereRings(const glm::vec3& center, float radius, const DebugStyle& style = {});

    void Sphere(const glm::vec3& center, float radius, const DebugStyle& style = {});

    void Axes(const glm::mat4& transform, float scale, const DebugStyle& style = {});

    void Frustum(const glm::mat4& viewProj, const DebugStyle& style = {});

    void Flush(agfx::CommandBuffer& commandBuffer, agfx::RenderTarget& color, agfx::RenderTarget& depth,
               const Camera& camera, uint32 width, uint32 height, uint32 frameIndex);

    uint32 GetSegmentCount() const { return (uint32)(m_Segments[0].Size() + m_Segments[1].Size()); }
    uint32 GetPointCount() const { return (uint32)(m_Points[0].Size() + m_Points[1].Size()); }
    uint32 GetTriangleCount() const { return (uint32)((m_Vertices[0].Size() + m_Vertices[1].Size()) / 3); }

private:
    static DebugRenderer* s_Instance;

    struct GPUSegment
    {
        glm::vec3 a;
        uint32 color;
        glm::vec3 b;
        float thickness;
    };

    struct GPUPoint
    {
        glm::vec3 p;
        uint32 color;
        float size;
        glm::vec3 pad;
    };

    struct GPUVertex
    {
        glm::vec3 p;
        uint32 color;
        glm::vec3 n;
        float pad;
    };

    struct GPUFrameConstants
    {
        glm::mat4 viewProj;
        glm::vec3 cameraPos;
        float pad;
        glm::vec2 viewport;
        glm::vec2 invViewport;
    };

    struct PushConstants
    {
        uint32 rFrameConstants;
        uint32 rPrims;
        uint32 uBaseIndex;
        uint32 uPad;
    };

    void EmitLine(const glm::vec3& a, const glm::vec3& b, const DebugStyle& style);
    void EmitTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const DebugStyle& style);
    void EmitTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                      const glm::vec3& na, const glm::vec3& nb, const glm::vec3& nc, const DebugStyle& style);
    void EmitQuad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const DebugStyle& style);

    void EmitRing(const glm::mat4& frame, float radius, float y, uint32 segments, const DebugStyle& style);
    void EmitDisc(const glm::mat4& frame, float radius, float y, float normalSign, uint32 segments, const DebugStyle& style);
    void EmitCylinderSurface(const glm::mat4& frame, float radius, float height, uint32 segments, uint32 wireMeridians, bool caps, const DebugStyle& style);
    void EmitConeSurface(const glm::mat4& frame, float radius, float height, uint32 segments, uint32 wireMeridians, const DebugStyle& style);
    void EmitSphereSurface(const glm::mat4& frame, float radius, float yOffset, uint32 sectors, uint32 stacks,
                           uint32 stackBegin, uint32 stackEnd, uint32 wireMeridians, const DebugStyle& style);

    void ClearBuckets();

    void DrawBucket(agfx::RenderPass& pass, const char* shaderPath, std::initializer_list<const char*> variants,
                    const StreamBuffer& stream, uint32 frameIndex, uint32 baseIndex, uint32 vertexCount);

    agfx::Device* m_Device;

    TArray<GPUSegment> m_Segments[2];
    TArray<GPUPoint> m_Points[2];
    TArray<GPUVertex> m_Vertices[2];

    StreamBuffer m_SegmentStream;
    StreamBuffer m_PointStream;
    StreamBuffer m_VertexStream;

    TArray<agfx::Buffer> m_FrameConstantBuffers;
    TArray<agfx::BufferView> m_FrameConstantViews;
};
