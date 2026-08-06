/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-06 12:00:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Analytic barycentrics + derivatives from clip-space triangle corners, after "The Visibility
// Buffer" (Burns/Hunt) / Wicked Engine's derivation. Packing matches VisBuffer.hlsl: G channel is
// meshletIndex << 7 | triangleIndex (kMeshletMaxTriangles = 124 < 128).

#ifndef CARAMEL_VISIBILITYBUFFER_HLSL
#define CARAMEL_VISIBILITYBUFFER_HLSL

uint VisMeshlet(uint g) { return g >> 7; }
uint VisTriangle(uint g) { return g & 0x7Fu; }
uint VisPack(uint meshletIndex, uint triangleIndex) { return (meshletIndex << 7) | triangleIndex; }

struct BarycentricDeriv
{
    float3 m_lambda;
    float3 m_ddx;
    float3 m_ddy;
};

BarycentricDeriv CalcFullBary(float4 pt0, float4 pt1, float4 pt2, float2 pixelNdc, float2 winSize)
{
    BarycentricDeriv ret = (BarycentricDeriv)0;

    float3 invW = rcp(float3(pt0.w, pt1.w, pt2.w));

    float2 ndc0 = pt0.xy * invW.x;
    float2 ndc1 = pt1.xy * invW.y;
    float2 ndc2 = pt2.xy * invW.z;

    float invDet = rcp(determinant(float2x2(ndc2 - ndc1, ndc0 - ndc1)));
    ret.m_ddx = float3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet * invW;
    ret.m_ddy = float3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet * invW;
    float ddxSum = dot(ret.m_ddx, float3(1,1,1));
    float ddySum = dot(ret.m_ddy, float3(1,1,1));

    float2 deltaVec = pixelNdc - ndc0;
    float interpInvW = invW.x + deltaVec.x*ddxSum + deltaVec.y*ddySum;
    float interpW = rcp(interpInvW);

    ret.m_lambda.x = interpW * (invW[0] + deltaVec.x*ret.m_ddx.x + deltaVec.y*ret.m_ddy.x);
    ret.m_lambda.y = interpW * (0.0f    + deltaVec.x*ret.m_ddx.y + deltaVec.y*ret.m_ddy.y);
    ret.m_lambda.z = interpW * (0.0f    + deltaVec.x*ret.m_ddx.z + deltaVec.y*ret.m_ddy.z);

    ret.m_ddx *= (2.0f/winSize.x);
    ret.m_ddy *= (2.0f/winSize.y);
    ddxSum    *= (2.0f/winSize.x);
    ddySum    *= (2.0f/winSize.y);

    ret.m_ddy *= -1.0f;
    ddySum    *= -1.0f;

    float interpW_ddx = 1.0f / (interpInvW + ddxSum);
    float interpW_ddy = 1.0f / (interpInvW + ddySum);

    ret.m_ddx = interpW_ddx*(ret.m_lambda*interpInvW + ret.m_ddx) - ret.m_lambda;
    ret.m_ddy = interpW_ddy*(ret.m_lambda*interpInvW + ret.m_ddy) - ret.m_lambda;

    return ret;
}

float3 BaryInterpolate(BarycentricDeriv deriv, float v0, float v1, float v2)
{
    float3 mergedV = float3(v0, v1, v2);
    return float3(dot(mergedV, deriv.m_lambda), dot(mergedV, deriv.m_ddx), dot(mergedV, deriv.m_ddy));
}

float2 BaryInterpolate2(BarycentricDeriv deriv, float2 v0, float2 v1, float2 v2)
{
    return v0 * deriv.m_lambda.x + v1 * deriv.m_lambda.y + v2 * deriv.m_lambda.z;
}

float3 BaryInterpolate3(BarycentricDeriv deriv, float3 v0, float3 v1, float3 v2)
{
    return v0 * deriv.m_lambda.x + v1 * deriv.m_lambda.y + v2 * deriv.m_lambda.z;
}

float4 BaryInterpolate4(BarycentricDeriv deriv, float4 v0, float4 v1, float4 v2)
{
    return v0 * deriv.m_lambda.x + v1 * deriv.m_lambda.y + v2 * deriv.m_lambda.z;
}

void BaryInterpolateUV(BarycentricDeriv deriv, float2 uv0, float2 uv1, float2 uv2,
                       out float2 outUV, out float2 outDdx, out float2 outDdy)
{
    float3 u = BaryInterpolate(deriv, uv0.x, uv1.x, uv2.x);
    float3 v = BaryInterpolate(deriv, uv0.y, uv1.y, uv2.y);
    outUV = float2(u.x, v.x);
    outDdx = float2(u.y, v.y);
    outDdy = float2(u.z, v.z);
}

#endif
