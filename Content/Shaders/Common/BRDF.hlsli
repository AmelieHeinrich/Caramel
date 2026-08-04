/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-04 12:10:00
 * @ Copyright: Day III Digital - All rights reserved
 */

// Cook-Torrance specular (GGX distribution, Smith geometry, Schlick Fresnel) plus a Burley/Disney
// diffuse term. All angles are passed in as precomputed dot products so callers can share them
// between the diffuse and specular terms.

#ifndef CARAMEL_BRDF_HLSL
#define CARAMEL_BRDF_HLSL

static const float kPi = 3.14159265359f;

float DistributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (NdotH * NdotH) * (a2 - 1.0f) + 1.0f;
    return a2 / max(kPi * d * d, 1e-6f);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f; // direct-lighting remap
    return NdotV / max(NdotV * (1.0f - k) + k, 1e-6f);
}

float GeometrySmith(float NdotV, float NdotL, float roughness) {
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

// Disney/Burley diffuse: Lambert with a retro-reflective falloff at grazing angles, driven by
// roughness. Falls back to Lambert as roughness -> 0.
float3 DiffuseBurley(float NdotV, float NdotL, float LdotH, float roughness, float3 albedo) {
    float fd90 = 0.5f + 2.0f * roughness * LdotH * LdotH;
    float lightScatter = 1.0f + (fd90 - 1.0f) * pow(saturate(1.0f - NdotL), 5.0f);
    float viewScatter = 1.0f + (fd90 - 1.0f) * pow(saturate(1.0f - NdotV), 5.0f);
    return albedo * (lightScatter * viewScatter / kPi);
}

// Full direct-light contribution for a single light of the given NdotL, already multiplied by
// NdotL -- callers just need to multiply by the light's radiance.
float3 CookTorrance(float3 N, float3 V, float3 L, float3 albedo, float metallic, float roughness) {
    float3 H = normalize(V + L);

    float NdotV = max(dot(N, V), 1e-4f);
    float NdotL = max(dot(N, L), 0.0f);
    float NdotH = max(dot(N, H), 0.0f);
    float LdotH = max(dot(L, H), 0.0f);

    if (NdotL <= 0.0f) {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 F = FresnelSchlick(LdotH, F0);

    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySmith(NdotV, NdotL, roughness);
    float3 specular = (D * G * F) / max(4.0f * NdotV * NdotL, 1e-4f);

    float3 diffuseAlbedo = albedo * (1.0f - metallic);
    float3 diffuse = DiffuseBurley(NdotV, NdotL, LdotH, roughness, diffuseAlbedo) * (1.0f - F);

    return (diffuse + specular) * NdotL;
}

#endif
