#define FROXEL_Z 64

[[vk::push_constant]]
cbuffer FogPushConstants
{
    float4x4 invViewProj;
    float4 cameraPos;
    float4 fogColor;
    float4 lightDir;
    float fogDensity;
    float fogHeightFalloff;
    float seaLevel;
    float fogRadius;
};

// RWTexture3D<float4> kompiluje się do formatu Rgba32f — obraz po stronie C++ musi mieć R32G32B32A32_SFLOAT.
[[vk::binding(0, 0)]] RWTexture3D<float4> fogVolume;

struct LocalFogVolume
{
    float4 centerDensity;
    float4 halfExtentsEdgeFade;
};

[[vk::binding(1, 0)]] StructuredBuffer<LocalFogVolume> localFogVolumes;

float localFogDensity(float3 worldPos, uint volumeCount)
{
    float density = 0.0;
    [loop] for (uint i = 0; i < volumeCount; ++i)
    {
        LocalFogVolume volume = localFogVolumes[i];
        float3 normalizedOffset = abs(worldPos - volume.centerDensity.xyz)
            / max(volume.halfExtentsEdgeFade.xyz, 0.001);
        float boundaryDistance = length(normalizedOffset);
        float fadeWidth = max(volume.halfExtentsEdgeFade.w, 0.01);
        float weight = 1.0 - smoothstep(1.0 - fadeWidth, 1.0, boundaryDistance);
        density += volume.centerDensity.w * weight;
    }
    return density;
}

groupshared float densities[FROXEL_Z];
groupshared float3 lights[FROXEL_Z];
groupshared float distances[FROXEL_Z];

[numthreads(1, 1, FROXEL_Z)]
void main(uint3 id : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID)
{
    uint width, height, slices;
    fogVolume.GetDimensions(width, height, slices);

    uint x = id.x;
    uint y = id.y;
    uint z = gtid.z;

    float2 screenUV = (float2(x, y) + 0.5) / float2(width, height);
    float2 ndc = screenUV * 2.0 - 1.0;
    ndc.y = -ndc.y;

    // Reconstruct world position on the far plane (clip-space depth = 1.0)
    float4 farClip = float4(ndc, 1.0, 1.0);
    float4 farWorld4 = mul(invViewProj, farClip);
    float3 farWorld = farWorld4.xyz / farWorld4.w;

    // Normalized world-space distance for this slice [0, 1]
    float t = (z + 0.5) / slices;

    // World position at uniform world-space distance from camera
    float3 viewDir = normalize(farWorld - cameraPos.xyz);
    float viewDist = length(farWorld - cameraPos.xyz);
    float maxDist = min(viewDist, fogRadius);
    float3 worldPos = cameraPos.xyz + viewDir * t * maxDist;

    // Store world-space distance for step-size computation
    distances[z] = t * maxDist;

    uint localVolumeCount = min((uint)round(lightDir.w), 32u);
    float h = worldPos.y;
    float density = localVolumeCount > 0u
        ? localFogDensity(worldPos, localVolumeCount)
        : fogDensity * exp(-fogHeightFalloff * max(0.0, h - seaLevel));

    float noise = 0.5 + 0.5 * sin(worldPos.x * 0.015 + worldPos.y * 0.025 + worldPos.z * 0.015);
    density *= (0.6 + 0.4 * noise);

    float NdotL = dot(normalize(worldPos - cameraPos.xyz), -lightDir.xyz);
    float phase = 0.15 + 0.85 * max(0.0, NdotL);
    float3 scatteredLight = density * fogColor.rgb * phase;

    densities[z] = density;
    lights[z] = scatteredLight;

    GroupMemoryBarrierWithGroupSync();

    float extinction = 0.0;
    float3 accumulatedLight = 0.0;

    [unroll] for (uint s = 0; s <= z; s++)
    {
        float stepSize = (s == 0)
            ? distances[0]
            : distances[s] - distances[s - 1];
        stepSize = max(stepSize, 0.001);

        float d = densities[s];
        float stepT = exp(-d * stepSize);

        accumulatedLight = accumulatedLight * stepT + lights[s] * (1.0 - stepT);
        extinction += d * stepSize;
    }

    float totalTransmittance = exp(-extinction);
    fogVolume[uint3(x, y, z)] = float4(accumulatedLight, totalTransmittance);
}
