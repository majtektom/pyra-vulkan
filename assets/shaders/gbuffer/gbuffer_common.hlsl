#ifndef GBUFFER_COMMON_HLSL
#define GBUFFER_COMMON_HLSL

#define MAX_BONES 64

#include "shared/light_types.hlsl"

// UBO globalne do renderingu geometry pass
cbuffer GlobalUniformBufferObject : register(b0, space0)
{
    column_major float4x4 view;
    column_major float4x4 proj;
    float4 camRight;
    float4 camUp;
    float4 cameraPos;
    float4 ambientColor;
    float ambientStrength;
    uint lightingEnabled;
    uint lightCount;
    uint shadowsEnabled;
    LightData lights[16];
    float4 fogColor;
    float fogStart;
    float fogEnd;
    float fogDensity;
    uint fogEnabled;
};

// Parametry push constants pojedynczego obiektu
[[vk::push_constant]]
cbuffer PushConstants
{
    column_major float4x4 model;
    float4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float bumpIntensity;
    uint baseColorTextureSet;
    uint physicalDescriptorTextureSet;
    uint normalTextureSet;
    uint occlusionTextureSet;
    uint emissiveTextureSet;
    float alphaMaskCutoff;
    uint layerBlendEnabled;
    uint layerCount;
    float layerWorldSize;
    uint4 layerBaseTextureSets[3];          // Spakowane indeksy tekstur warstwowych (3 * 4 = 12 warstw)
    uint4 layerMaskTextureSets[3];          // Spakowane indeksy tekstur masek przejść (12 masek)
};

SamplerState globalSampler : register(s1, space0);
Texture2D textures[] : register(t1, space0);

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color : COLOR;
    [[vk::location(1)]] float2 texCoord : TEXCOORD0;
    [[vk::location(2)]] float3 worldNormal : TEXCOORD3;
    [[vk::location(3)]] float3 worldPos : TEXCOORD4;
};

/**
 * @brief Struktura wyjściowa G-Buffera (Geometry Buffer Multi-Render Target).
 * 
 * Zamiast bezpośrednio oświetlać piksele na ekranie (Forward Rendering), 
 * w renderingu odroczonym (Deferred Rendering) zapisujemy parametry fizyczne 
 * powierzchni geometrycznych do zestawu tekstur (G-Buffer) o tym samym rozmiarze co ekran.
 * Dopiero w kolejnym kroku (Lighting Pass) cieniujemy scenę na podstawie tych tekstur.
 */
struct GBufferOutput
{
    // Target 0: RGB = Kolor Albedo, A = Współczynnik metaliczności (metallic)
    float4 albedoMetallic : SV_Target0;
    
    // Target 1: RGB = Wektor normalny w przestrzeni świata zmapowany na [0.0, 1.0], A = Szorstkość (roughness)
    float4 normalRoughness : SV_Target1;
    
    // Target 2: R = Ocienienie (AO), GBA = Kolor samozjarzenia (emissive RGB)
    float4 occlusionEmissive : SV_Target2;
    
    // Target 3: RGB = Pozycja świata w trójwymiarze (XYZ), A = 1.0 (maska głębi geometrycznej)
    float4 worldPosition : SV_Target3;
};

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

    float3 N = normalize(input.worldNormal);

    // 2. Normal mapping na podstawie pochodnych ekranowych
    if (bumpIntensity > 0.001)
    {
        float3 normalTex = textures[normalTextureSet].Sample(globalSampler, input.texCoord).xyz * 2.0 - 1.0;
        float3 dp1 = ddx(input.worldPos);
        float3 dp2 = ddy(input.worldPos);
        float2 duv1 = ddx(input.texCoord);
        float2 duv2 = ddy(input.texCoord);
        normalTex.xy *= bumpIntensity;
        normalTex = normalize(normalTex);
        float re = 1.0 / (duv1.x * duv2.y - duv1.y * duv2.x + 0.0001);
        float3 T_raw = (dp1 * duv2.y - dp2 * duv1.y) * re;
        float3 T_proj = T_raw - N * dot(N, T_raw);
        float lenSq = dot(T_proj, T_proj);
        if (lenSq > 0.00001)
        {
            float3 T = T_proj / sqrt(lenSq);
            float3 B = cross(N, T);
            float3x3 TBN = float3x3(T, B, N);
            N = normalize(mul(normalTex, TBN));
        }
    }

    // 3. Odczyt parametrów PBR
    float3 metallicRoughness = textures[physicalDescriptorTextureSet].Sample(globalSampler, input.texCoord).bga;
    float metallic = metallicRoughness.x * metallicFactor;
    float roughness = metallicRoughness.y * roughnessFactor;

    float ao = textures[occlusionTextureSet].Sample(globalSampler, input.texCoord).r;
    float3 emissive = textures[emissiveTextureSet].Sample(globalSampler, input.texCoord).rgb;

    // KROK 4: WYPEŁNIENIE KANAŁÓW WYJŚCIOWYCH G-BUFFERA
    GBufferOutput output;
    output.albedoMetallic = float4(baseColor.rgb, metallic);
    
    // Normalne są mapowane z zakresu [-1.0, 1.0] na [0.0, 1.0], ponieważ standardowe formaty tekstur buforu koloru (np. UNORM)
    // nie wspierają bezpośrednio zapisu wartości ujemnych. Re-mapowanie to: N_mapped = N * 0.5 + 0.5.
    output.normalRoughness = float4(N * 0.5 + 0.5, roughness);
    output.occlusionEmissive = float4(ao, emissive.r, emissive.g, emissive.b);
    output.worldPosition = float4(input.worldPos, 1.0);
    
    return output;
}

#endif