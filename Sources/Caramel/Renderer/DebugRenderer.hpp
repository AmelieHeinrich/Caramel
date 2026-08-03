/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-03 14:15:00
 * @ Copyright: Day III Digital - All rights reserved
 */

#pragma once

#include <Caramel/Core/Common.hpp>
#include <Caramel/Renderer/Common.hpp>
#include <Caramel/Renderer/Camera.hpp>

#include <AGFX/agfx.hpp>
#include <glm/glm.hpp>

/// @brief Per-shape appearance. Every draw call takes one; the defaults give a thin white
/// wireframe that is occluded by scene geometry.
struct DebugStyle
{
    glm::vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };

    /// Solid triangles (shaded with a headlight term) instead of a wireframe outline.
    bool filled = false;

    /// Depth-tested against the scene depth buffer. False draws the shape on top of everything.
    bool depthTest = true;

    /// Wireframe line width and point size, in pixels -- constant on screen regardless of distance,
    /// since lines and points are expanded to quads in the vertex shader.
    float thickness = 2.0f;

    /// Radial tessellation for curved shapes (cylinder, capsule, cone, sphere). Clamped to [3, 128].
    uint32 segments = 24;
};

/// @brief Immediate-mode debug geometry.
///
/// Draw calls append to CPU-side buckets and are consumed and cleared by Flush() once per frame,
/// so nothing persists: a shape is visible exactly on the frames its draw call runs. Shapes are
/// split into a depth-tested bucket and an always-on-top bucket by DebugStyle::depthTest, and
/// Flush() emits one render pass for each (the depth pass loads the scene depth buffer, so it
/// must run after the scene pass).
///
/// Not thread-safe: submit only from the main thread, between frames' Flush() calls.
class DebugRenderer
{
public:
    DebugRenderer(agfx::Device& device, agfx::TextureFormat colorFormat, agfx::TextureFormat depthFormat, uint32 framesInFlight);

    static DebugRenderer& Get() { return *s_Instance; }

    // -- Primitives --------------------------------------------------------------------------

    void Line(const glm::vec3& a, const glm::vec3& b, const DebugStyle& style = {});
    void Point(const glm::vec3& p, const DebugStyle& style = {});

    // -- Shapes ------------------------------------------------------------------------------
    // Every shape has a transform-based form (the transform's rotation/scale apply to a canonical
    // unit shape at the origin) and a convenience form built from explicit vectors.

    /// Unit quad in the transform's XY plane, scaled by `halfExtents`.
    void Quad(const glm::mat4& transform, const glm::vec2& halfExtents, const DebugStyle& style = {});
    void Quad(const glm::vec3& center, const glm::vec3& right, const glm::vec3& up, const DebugStyle& style = {});

    void Triangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const DebugStyle& style = {});

    /// Shaft from `from` to `to` capped with a cone head. Negative head values pick a size
    /// proportional to the shaft length (20%) and head length (40%) respectively.
    void Arrow(const glm::vec3& from, const glm::vec3& to, const DebugStyle& style = {}, float headLength = -1.0f, float headRadius = -1.0f);

    /// `axis` spans base to top -- its length is the cylinder's height.
    void Cylinder(const glm::vec3& base, const glm::vec3& axis, float radius, const DebugStyle& style = {});
    void Cylinder(const glm::mat4& transform, float radius, float height, const DebugStyle& style = {});

    /// `a` and `b` are the centres of the two hemispherical caps, so total length is |b-a| + 2r.
    void Capsule(const glm::vec3& a, const glm::vec3& b, float radius, const DebugStyle& style = {});
    void Capsule(const glm::mat4& transform, float radius, float cylinderHeight, const DebugStyle& style = {});

    /// Cone with its tip at `apex`, opening along `dir` for `height`.
    void Cone(const glm::vec3& apex, const glm::vec3& dir, float height, float radius, const DebugStyle& style = {});
    void Cone(const glm::mat4& transform, float radius, float height, const DebugStyle& style = {});

    /// Cheap sphere: three great circles. Much lighter than Sphere() for things like bounding
    /// volumes drawn in bulk. Forwards to Sphere() if `style.filled` is set (rings have no fill).
    void SphereRings(const glm::vec3& center, float radius, const DebugStyle& style = {});

    /// Fully tessellated UV sphere -- wireframe draws every edge, filled draws every triangle.
    void Sphere(const glm::vec3& center, float radius, const DebugStyle& style = {});

    /// Three arrows along the transform's basis vectors, forced to red/green/blue for X/Y/Z
    /// (`style.color` is ignored, everything else applies).
    void Axes(const glm::mat4& transform, float scale, const DebugStyle& style = {});

    /// The volume that `viewProj` projects to the unit cube, recovered by unprojecting the eight
    /// NDC corners. Pass any camera's view-projection matrix.
    void Frustum(const glm::mat4& viewProj, const DebugStyle& style = {});

    // -- Frame ------------------------------------------------------------------------------

    /// @brief Uploads and draws everything submitted this frame, then clears the buckets.
    /// Records up to two render passes on `commandBuffer` (both load and store `color`; the
    /// depth-tested one also loads `depth` and tests without writing). Call after the scene pass
    /// -- `depth` must already hold the scene's depth -- and before the ImGui pass.
    void Flush(agfx::CommandBuffer& commandBuffer, agfx::RenderTarget& color, agfx::RenderTarget& depth,
               const Camera& camera, uint32 width, uint32 height, uint32 frameIndex);

    /// Number of primitives submitted so far this frame, for debug UI.
    uint32 GetSegmentCount() const { return (uint32)(m_Segments[0].Size() + m_Segments[1].Size()); }
    uint32 GetPointCount() const { return (uint32)(m_Points[0].Size() + m_Points[1].Size()); }
    uint32 GetTriangleCount() const { return (uint32)((m_Vertices[0].Size() + m_Vertices[1].Size()) / 3); }

private:
    static DebugRenderer* s_Instance;

    // Mirror Content/Shaders/Common/DebugDraw.hlsli field-for-field. Colors are packed RGBA8 in
    // the same little-endian order ImGui uses, so the shader's unpack math is shared.
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

    /// One grow-on-demand CPU-visible buffer per frame-in-flight, holding the depth-tested block
    /// followed by the overlay block. Same pattern as ImGuiRenderer's vertex buffer.
    struct StreamBuffer
    {
        TArray<agfx::Buffer> buffers;
        TArray<agfx::BufferView> views;
        TArray<uint64> capacities;

        void Init(uint32 framesInFlight);
        /// Uploads `first` then `second` contiguously, growing if needed. Returns false if there
        /// is nothing to draw.
        bool Upload(agfx::Device& device, uint32 frameIndex, const char* name, uint32 stride,
                    const void* first, uint64 firstBytes, const void* second, uint64 secondBytes);
    };

    void EmitLine(const glm::vec3& a, const glm::vec3& b, const DebugStyle& style);
    void EmitTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const DebugStyle& style);
    void EmitTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                      const glm::vec3& na, const glm::vec3& nb, const glm::vec3& nc, const DebugStyle& style);
    /// Emits a quad as two triangles (filled) or four segments (wireframe), in `a b c d` ring order.
    void EmitQuad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const DebugStyle& style);

    // Surface emitters shared between shapes. `frame` maps a canonical shape (local +Y is the
    // axis, origin is the base/centre) into world space. `wireMeridians` is how many longitudinal
    // lines the wireframe form draws -- kept below `segments` so composite shapes stay readable.
    void EmitRing(const glm::mat4& frame, float radius, float y, uint32 segments, const DebugStyle& style);
    void EmitDisc(const glm::mat4& frame, float radius, float y, float normalSign, uint32 segments, const DebugStyle& style);
    void EmitCylinderSurface(const glm::mat4& frame, float radius, float height, uint32 segments, uint32 wireMeridians, bool caps, const DebugStyle& style);
    void EmitConeSurface(const glm::mat4& frame, float radius, float height, uint32 segments, uint32 wireMeridians, const DebugStyle& style);
    /// Stacks run from +Y (index 0) to -Y (index `stacks`); `stackBegin`/`stackEnd` carve out a
    /// hemisphere for capsule caps. `yOffset` shifts the sphere centre along the local axis.
    void EmitSphereSurface(const glm::mat4& frame, float radius, float yOffset, uint32 sectors, uint32 stacks,
                           uint32 stackBegin, uint32 stackEnd, uint32 wireMeridians, const DebugStyle& style);

    void ClearBuckets();

    void DrawBucket(agfx::RenderPass& pass, const char* shaderPath, std::initializer_list<const char*> variants,
                    const StreamBuffer& stream, uint32 frameIndex, uint32 baseIndex, uint32 vertexCount);

    agfx::Device* m_Device;

    // Index 0 = depth-tested, index 1 = overlay. DebugStyle::depthTest selects the bucket.
    TArray<GPUSegment> m_Segments[2];
    TArray<GPUPoint> m_Points[2];
    TArray<GPUVertex> m_Vertices[2];

    StreamBuffer m_SegmentStream;
    StreamBuffer m_PointStream;
    StreamBuffer m_VertexStream;

    TArray<agfx::Buffer> m_FrameConstantBuffers;
    TArray<agfx::BufferView> m_FrameConstantViews;
};
