/*****************************************************************************
 MIT License

 Copyright(c) 2022 Alexander Veselov

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this softwareand associated documentation files(the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions :

 The above copyright noticeand this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 *****************************************************************************/

#ifndef MATERIAL_H
#define MATERIAL_H

#include "src/kernels/bxdf.h"
#include "src/kernels/utils.h"
#include "src/kernels/shared_structures.h"

#ifdef GLSL
struct Material
#else
typedef struct
#endif
{
    float3 diffuse_albedo;
    float  roughness;
    float3 specular_albedo;
    float  metalness;
    float3 emission;
    float  ior;
    float  transparency;
}
#ifndef GLSL
Material
#endif
;

float3 SampleDiffuse(float2 s, float3 albedo, float3 f0, float3 normal,
    float3 incoming,
#ifdef GLSL
    out float3 outgoing, out float pdf
#else
    float3* outgoing, float* pdf
#endif
)
{
    float3 tbn_outgoing = SampleHemisphereCosine(s, pdf);
    OUT(outgoing) = TangentToWorld(tbn_outgoing, normal);

    return albedo * INV_PI;
}

float3 SampleSpecular(float2 s, float3 f0, float alpha,
    float3 normal, float3 incoming,
#ifdef GLSL
    out float3 outgoing, out float pdf
#else
    float3* outgoing, float* pdf
#endif
)
{
    if (alpha <= 1e-4f)
    {
        OUT(outgoing) = reflect(incoming, normal);
        OUT(pdf) = 1.0;
        float n_dot_o = dot(OUT(outgoing), normal);
        // Don't apply fresnel here, it's applied in the external function
        return 1.0f / n_dot_o;
    }
    else
    {
        float3 wh = GGX_Sample(s, normal, alpha);
        OUT(outgoing) = reflect(incoming, wh);

        float n_dot_o = dot(normal, OUT(outgoing));
        float n_dot_h = dot(normal, wh);
        float n_dot_i = dot(normal, incoming);
        float h_dot_o = dot(OUT(outgoing), wh);

        float D = GGX_D(alpha, n_dot_h);
        // Don't apply fresnel here, it's applied in the external function
        //float3 F = FresnelSchlick(f0, h_dot_o);
        float G = V_SmithGGXCorrelated(n_dot_i, n_dot_o, alpha);

        *pdf = D * n_dot_h / (4.0f * dot(wh, OUT(outgoing)));

        return D /* F */ * G;
    }
}

float3 SampleTransparency(float2 s, float3 normal, float3 incoming,
#ifdef GLSL
    out float3 outgoing, out float pdf
#else
    float3* outgoing, float* pdf
#endif
)
{
    OUT(pdf) = 1.0f;
    OUT(outgoing) = -incoming;
    return 1.0f;
}

float3 EvaluateSpecular(float alpha, float n_dot_i, float n_dot_o, float n_dot_h)
{
    float D = GGX_D(alpha, n_dot_h);
    float G = V_SmithGGXCorrelated(n_dot_i, n_dot_o, alpha);

    return D * G;
}

float3 EvaluateDiffuse(float3 albedo)
{
    return albedo * Lambert();
}

float3 EvaluateMaterial(Material material, float3 normal, float3 incoming, float3 outgoing)
{
    if (material.transparency < 0.5)
    {
        return make_float3(0.0f, 0.0f, 0.0f);
    }

    float3 half_vec = normalize(incoming + outgoing);
    float n_dot_i = max(dot(normal, incoming), EPS);
    float n_dot_o = max(dot(normal, outgoing), EPS);
    float n_dot_h = max(dot(normal, half_vec), EPS);
    float h_dot_o = max(dot(half_vec, outgoing), EPS);

    // Perceptual roughness remapping
    float roughness = material.roughness;
    float alpha = roughness * roughness;

    // Compute f0 values for metals and dielectrics
    float3 f0_dielectric = IorToF0(1.0f, material.ior);
    float3 f0_metal = material.specular_albedo.xyz;

    // Blend f0 values based on metalness
    float3 f0 = mix(f0_dielectric, f0_metal, material.metalness);

    // Since metals don't have the diffuse term, fade it to zero
    //@TODO: precompute it?
    float3 diffuse_color = (1.0f - material.metalness) * material.diffuse_albedo.xyz;

    // This is the scaling value for specular bxdf
    float3 specular_albedo = mix(material.specular_albedo.xyz, 1.0f, material.metalness);

    float3 fresnel = FresnelSchlick(f0, h_dot_o);

    float3 specular = EvaluateSpecular(alpha, n_dot_i, n_dot_o, n_dot_h);
    float3 diffuse = EvaluateDiffuse(diffuse_color);

    return fresnel * specular + (1.0f - fresnel) * diffuse;
}

float3 SampleBxdf(float s1, float2 s, Material material, float3 normal,
    float3 incoming,
#ifdef GLSL
    out float3 outgoing, out float pdf, out float offset
#else
    float3* outgoing, float* pdf, float* offset
#endif
)
{
#ifdef ENABLE_WHITE_FURNACE
    material.diffuse_albedo.xyz = 1.0f;
    material.specular_albedo.xyz = 1.0f;
#endif // ENABLE_WHITE_FURNACE

    // Perceptual roughness remapping
    float roughness = material.roughness;
    float alpha = roughness * roughness;

    // Compute f0 values for metals and dielectrics
    float3 f0_dielectric = IorToF0(1.0f, material.ior);
    float3 f0_metal = material.specular_albedo.xyz;

    // Blend f0 values based on metalness
    float3 f0 = mix(f0_dielectric, f0_metal, material.metalness);

    // Since metals don't have the diffuse term, fade it to zero
    //@TODO: precompute it?
    float3 diffuse_albedo = (1.0f - material.metalness) * material.diffuse_albedo.xyz;

    // This is the scaling value for specular bxdf
    float3 specular_albedo = mix(material.specular_albedo.xyz, 1.0, material.metalness);

    // This is not an actual fresnel value, because we need to use half vector instead of normal here
    // it's a "heuristic" used for better layer importance sampling and energy conservation
    float3 fresnel = FresnelSchlick(f0, dot(normal, incoming)) * specular_albedo;

    float specular_weight = Luma(specular_albedo * fresnel);
    float diffuse_weight = Luma(diffuse_albedo * (1.0f - fresnel));
    float weight_sum = diffuse_weight + specular_weight;

    float specular_sampling_pdf = specular_weight / weight_sum;
    float diffuse_sampling_pdf = diffuse_weight / weight_sum;

    //@TODO: reduce diffuse intensity where the specular value is high
    //@TODO: evaluate all layers at once?

    float3 bxdf = 0.0f;
    OUT(offset) = 1.0f;

    if (material.transparency < 0.5)
    {
        bxdf = SampleTransparency(s, normal, incoming, outgoing, pdf);
        OUT(offset) = -1.0f;
        return bxdf;
    }

    if (s1 <= specular_sampling_pdf)
    {
        // Sample specular
        bxdf = fresnel * SampleSpecular(s, f0, alpha, normal, incoming, outgoing, pdf) * max(dot(OUT(outgoing), normal), 0.0f);
        OUT(pdf) *= specular_sampling_pdf;
    }
    else
    {
        // Sample diffuse
        bxdf = (1.0f - fresnel) * SampleDiffuse(s, diffuse_albedo, f0, normal, incoming, outgoing, pdf) * max(dot(OUT(outgoing), normal), 0.0f);
        OUT(pdf) *= diffuse_sampling_pdf;
    }

    return bxdf;
}

#ifndef GLSL
float3 SampleTexture(Texture texture, float2 uv, __global uint* texture_data)
{
    // Wrap coords
    uv -= floor(uv);
    uv.y = 1.f - uv.y;

    int texel_x = clamp((int)(uv.x * texture.width), 0, texture.width - 1);
    int texel_y = clamp((int)(uv.y * texture.height), 0, texture.height - 1);
    int texel_addr = texture.data_start + texel_y * texture.width + texel_x;

    float4 color = UnpackRGBA8(texture_data[texel_addr]);

    return clamp(color.xyz, 0.0f, 1.0f);
}

void ApplyTextures(PackedMaterial in_material, Material* out_material, float2 uv,
    __global Texture* textures, __global uint* texture_data)
{
    uint diffuse_albedo_idx;
    out_material->diffuse_albedo = UnpackRGBTex(in_material.diffuse_albedo, &diffuse_albedo_idx);

    if (diffuse_albedo_idx != INVALID_TEXTURE_IDX)
    {
        out_material->diffuse_albedo = pow(SampleTexture(textures[diffuse_albedo_idx], uv, texture_data), 2.2f);
    }

    uint specular_albedo_idx;
    out_material->specular_albedo = UnpackRGBTex(in_material.specular_albedo, &specular_albedo_idx);

    if (specular_albedo_idx != INVALID_TEXTURE_IDX)
    {
        out_material->specular_albedo = pow(SampleTexture(textures[specular_albedo_idx], uv, texture_data), 2.2f);
    }

    out_material->emission = UnpackRGBE(in_material.emission);

    uint roughness_idx;
    uint metalness_idx;
    UnpackRoughnessMetalness(in_material.roughness_metalness, &out_material->roughness, &roughness_idx,
        &out_material->metalness, &metalness_idx);

    if (roughness_idx != INVALID_TEXTURE_IDX)
    {
        out_material->roughness = SampleTexture(textures[roughness_idx], uv, texture_data).x;
    }

    if (metalness_idx != INVALID_TEXTURE_IDX)
    {
        out_material->metalness = SampleTexture(textures[metalness_idx], uv, texture_data).x;
    }

    uint emission_idx;
    uint transparency_idx;
    UnpackIorEmissionIdxTransparency(in_material.ior_emission_idx_transparency,
        &out_material->ior, &emission_idx, &out_material->transparency, &transparency_idx);

    if (emission_idx != INVALID_TEXTURE_IDX)
    {
        out_material->emission *= pow(SampleTexture(textures[emission_idx], uv, texture_data), 2.2f);
    }

    if (transparency_idx != INVALID_TEXTURE_IDX)
    {
        out_material->transparency *= SampleTexture(textures[transparency_idx], uv, texture_data).x;
    }
}
#endif

#endif // MATERIAL_H
