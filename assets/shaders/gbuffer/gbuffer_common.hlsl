#ifndef GBUFFER_COMMON_HLSL
#define GBUFFER_COMMON_HLSL

#define MAX_BONES 64

#include "shared/light_types.hlsl"

#include "shared/globals.hlsl"   // UBO klatki, tablica tekstur bindless i jej sampler (wspólne dla wszystkich shaderów)

#include "shared/material_push.hlsl"   // blok push constants (układ jak w push_constants.h)


struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color : COLOR;
    [[vk::location(1)]] float2 texCoord : TEXCOORD0;
    [[vk::location(2)]] float3 worldNormal : TEXCOORD3;
    [[vk::location(3)]] float3 worldPos : TEXCOORD4;
};

#include "shared/gbuffer_output.hlsl"   // GBufferOutput i packGBuffer (wspólne z terenem)

float3x3 inverseTranspose3x3(float4x4 m)
{
    float3x3 a = float3x3(m[0].xyz, m[1].xyz, m[2].xyz);
    return a;
}

uint getLayerBaseTextureSet(uint index)
{
    uint group = index / 4u;
    uint lane = index % 4u;
    if (group == 0u) return layerBaseTextureSets[0][lane];
    if (group == 1u) return layerBaseTextureSets[1][lane];
    return layerBaseTextureSets[2][lane];
}

uint getLayerMaskTextureSet(uint index)
{
    uint group = index / 4u;
    uint lane = index % 4u;
    if (group == 0u) return layerMaskTextureSets[0][lane];
    if (group == 1u) return layerMaskTextureSets[1][lane];
    return layerMaskTextureSets[2][lane];
}

/**
 * @brief Funkcja pomocnicza cieniująca i zapisująca dane w strukturze G-Buffera.
 */
GBufferOutput shadeGBuffer(VertexOutput input)
{
    // 1. Odczyt koloru podstawowego i splattingu warstw terenu (jeśli włączony)
    float4 baseColor = textures[baseColorTextureSet].Sample(globalSampler, input.texCoord) * float4(input.color, 1.0) * baseColorFactor;

    if (layerBlendEnabled != 0 && layerCount > 0)
    {
        float invWorldSize = (layerWorldSize > 0.0001) ? (1.0 / layerWorldSize) : 0.0;
        float2 maskUv = (input.worldPos.xz - model[3].xz) * invWorldSize;

        uint n = min(layerCount, 12u);
        float4 result = textures[getLayerBaseTextureSet(0u)].Sample(globalSampler, input.texCoord);
        for (uint i = 1; i < n; ++i)
        {
            float4 layerColor = textures[getLayerBaseTextureSet(i)].Sample(globalSampler, input.texCoord);
            float maskVal = saturate(textures[getLayerMaskTextureSet(i)].Sample(globalSampler, maskUv).r);
            result = lerp(result, layerColor, maskVal);
        }
        baseColor = result * float4(input.color, 1.0) * baseColorFactor;
    }

    if (baseColor.a < alphaMaskCutoff)
        discard;

    // 2. Powierzchnia: normalna (z mapą normalnych) i parametry PBR - zapis wspólną funkcją packGBuffer.
    SurfaceData surf;
    surf.albedo = baseColor.rgb;
    surf.alpha = 1.0;
    float3 metallicRoughness = textures[physicalDescriptorTextureSet].Sample(globalSampler, input.texCoord).bga;
    surf.metallic = metallicRoughness.x * metallicFactor;
    surf.roughness = metallicRoughness.y * roughnessFactor;
    surf.ao = textures[occlusionTextureSet].Sample(globalSampler, input.texCoord).r;
    surf.emissive = textures[emissiveTextureSet].Sample(globalSampler, input.texCoord).rgb;
    surf.normal = perturbNormal(normalize(input.worldNormal), input.worldPos, input.texCoord,
                                textures[normalTextureSet].Sample(globalSampler, input.texCoord).xyz, bumpIntensity);
    surf.worldPos = input.worldPos;
    return packGBuffer(surf);
}

#endif
