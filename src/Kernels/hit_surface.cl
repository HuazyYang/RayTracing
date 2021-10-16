#include "shared_structures.h"
#include "material.h"
#include "sampling.h"
#include "light.h"

__kernel void HitSurface
(
    // Input
    __global Ray*      incoming_rays,
    __global uint*     incoming_ray_counter,
    __global uint*     incoming_pixel_indices,
    __global Hit*      hits,
    __global Triangle* triangles,
    __global Light*    analytic_lights,
    __global uint*     emissive_indices,
    __global Material* materials,
    uint bounce,
    uint width,
    uint height,
    __global uint* sample_counter,
    SceneInfo scene_info,
    // Blue noise sampler
    __global int* sobol_256spp_256d,
    __global int* scramblingTile,
    __global int* rankingTile,
    // Output
    __global float3* throughputs,
    __global Ray*    outgoing_rays,
    __global uint*   outgoing_ray_counter,
    __global uint*   outgoing_pixel_indices,
    __global Ray*    shadow_rays,
    __global uint*   shadow_ray_counter,
    __global uint*   shadow_pixel_indices,
    __global float3* direct_light_samples,
    __global float4* result_radiance
)
{
    uint incoming_ray_idx = get_global_id(0);
    uint num_incoming_rays = incoming_ray_counter[0];

    if (incoming_ray_idx >= num_incoming_rays)
    {
        return;
    }

    Hit hit = hits[incoming_ray_idx];

    if (hit.primitive_id == INVALID_ID)
    {
        return;
    }

    Ray incoming_ray = incoming_rays[incoming_ray_idx];
    float3 incoming = -incoming_ray.direction.xyz;

    uint pixel_idx = incoming_pixel_indices[incoming_ray_idx];
    uint sample_idx = sample_counter[0];

    int x = pixel_idx % width;
    int y = pixel_idx / width;

    Triangle triangle = triangles[hit.primitive_id];

    float3 position = InterpolateAttributes(triangle.v1.position,
        triangle.v2.position, triangle.v3.position, hit.bc);

    float2 texcoord = InterpolateAttributes2(triangle.v1.texcoord.xy,
        triangle.v2.texcoord.xy, triangle.v3.texcoord.xy, hit.bc);

    float3 normal = normalize(InterpolateAttributes(triangle.v1.normal,
        triangle.v2.normal, triangle.v3.normal, hit.bc));

    Material material = materials[triangle.mtlIndex];

    float3 hit_throughput = throughputs[pixel_idx];

#ifndef ENABLE_WHITE_FURNACE
    if (dot(material.emission, (float3)(1.0f, 1.0f, 1.0f)) > 0.0f)
    {
        result_radiance[pixel_idx].xyz += hit_throughput * material.emission;
    }
#endif // ENABLE_WHITE_FURNACE

    // Direct lighting
    {
        float s_light = SampleRandom(x, y, sample_idx, bounce, SAMPLE_TYPE_LIGHT, BLUE_NOISE_BUFFERS);
        float3 outgoing;
        float pdf;
        float3 light_radiance = Light_Sample(analytic_lights, scene_info, position, normal, s_light, &outgoing, &pdf);

        float distance_to_light = length(outgoing);
        outgoing = normalize(outgoing);

        float3 brdf = EvaluateMaterial(material, normal, incoming, outgoing);
        float3 light_sample = light_radiance * hit_throughput * brdf / pdf * max(dot(outgoing, normal), 0.0f);

        bool spawn_shadow_ray = (pdf > 0.0f) && (dot(light_sample, light_sample) > 0.0f);

        if (spawn_shadow_ray)
        {
            Ray shadow_ray;
            shadow_ray.origin.xyz = position + normal * EPS;
            shadow_ray.origin.w = 0.0f;
            shadow_ray.direction.xyz = outgoing;
            shadow_ray.direction.w = distance_to_light;

            ///@TODO: use LDS
            uint shadow_ray_idx = atomic_add(shadow_ray_counter, 1);

            // Store to the memory
            shadow_rays[shadow_ray_idx] = shadow_ray;
            shadow_pixel_indices[shadow_ray_idx] = pixel_idx;
            direct_light_samples[shadow_ray_idx] = light_sample;
        }
    }

    // Indirect lighting
    {
        // Sample bxdf
        float2 s;
        s.x = SampleRandom(x, y, sample_idx, bounce, SAMPLE_TYPE_BXDF_U, BLUE_NOISE_BUFFERS);
        s.y = SampleRandom(x, y, sample_idx, bounce, SAMPLE_TYPE_BXDF_V, BLUE_NOISE_BUFFERS);
        float s1 = SampleRandom(x, y, sample_idx, bounce, SAMPLE_TYPE_BXDF_LAYER, BLUE_NOISE_BUFFERS);

        float pdf = 0.0f;
        float3 throughput = 0.0f;
        float3 outgoing;
        float3 bxdf = SampleBxdf(s1, s, material, normal, incoming, &outgoing, &pdf);

        if (pdf > 0.0)
        {
            throughput = bxdf / pdf * max(dot(outgoing, normal), 0.0f);
        }

        throughputs[pixel_idx] *= throughput;

        bool spawn_outgoing_ray = (pdf > 0.0);

        if (spawn_outgoing_ray)
        {
            ///@TODO: use LDS
            uint outgoing_ray_idx = atomic_add(outgoing_ray_counter, 1);

            Ray outgoing_ray;
            outgoing_ray.origin.xyz = position + normal * EPS;
            outgoing_ray.origin.w = 0.0f;
            outgoing_ray.direction.xyz = outgoing;
            outgoing_ray.direction.w = MAX_RENDER_DIST;

            outgoing_rays[outgoing_ray_idx] = outgoing_ray;
            outgoing_pixel_indices[outgoing_ray_idx] = pixel_idx;
        }
    }

}
