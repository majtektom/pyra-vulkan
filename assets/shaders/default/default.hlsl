#include "shared/surface_lighting.hlsl"   // wspólne oświetlenie, cienie i atmosfera

#include "shared/globals.hlsl"   // UBO klatki, tablica tekstur bindless i jej sampler (wspólne dla wszystkich shaderów)

// Szybki bufor Push Constants powiązany z konkretnym renderowanym obiektem
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
};


struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float3 color : COLOR;
    [[vk::location(3)]] float2 texCoord : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color : COLOR;
    [[vk::location(1)]] float2 texCoord : TEXCOORD0;
    [[vk::location(2)]] float4 matColor : TEXCOORD1;
    [[vk::location(3)]] float3 worldPos : TEXCOORD2;
    [[vk::location(4)]] float3 worldNormal : TEXCOORD3;
};

// Pomocnicza konwersja macierzy 4x4 na 3x3 do transformacji wektora normalnego
float3x3 inverseTranspose3x3(float4x4 m)
{
    float3x3 a = float3x3(m[0].xyz, m[1].xyz, m[2].xyz);
    return a;
}

/**
 * @brief Shader wierzchołków (Vertex Shader).
 * 
 * Transformuje pozycję wierzchołka do przestrzeni ekranu (SV_Position) oraz 
 * wylicza wektor normalny i pozycję w przestrzeni świata (World Space), niezbędne 
 * do prawidłowego wyliczenia kierunków światła w shaderze fragmentów.
 */
VertexOutput vertexMain(VertexInput input)
{
    float4 worldPos4 = mul(model, float4(input.position, 1.0));
    VertexOutput output;
    output.position = mul(proj, mul(view, worldPos4));
    output.worldPos = worldPos4.xyz;
    
    // Transformacja wektora normalnego do przestrzeni świata.
    // normal wejściowy mnożymy przez macierz modelu bez przesunięcia (tylko rotacja/skala)
    output.worldNormal = normalize(mul(inverseTranspose3x3(model), input.normal));
    output.color = input.color;
    output.texCoord = input.texCoord;
    output.matColor = baseColorFactor;
    return output;
}

/**
 * @brief Shader pikseli (Fragment/Pixel Shader).
 * 
 * Wykonuje pełny rendering materiału bazując na standardzie PBR:
 * - Pobiera tekstury wejściowe (Albedo, Normal Map, Physical Descriptor, AO, Emissive).
 * - Rekonstruuje przestrzeń TBN (Tangent, Bitangent, Normal) za pomocą pochodnych ekranowych (ddx/ddy).
 * - Realizuje test przezroczystości (Alpha Cutout) na podstawie maski.
 * - Iteruje po źródłach światła, wyliczając ich wpływ za pomocą Cook-Torrance BRDF.
 */
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    // Kolor bazowy (tekstura x kolor wierzchołka x kolor materiału); poniżej progu alfy - piksel odrzucony.
    float4 baseColor = textures[baseColorTextureSet].Sample(globalSampler, input.texCoord) * float4(input.color, 1.0) * input.matColor;
    if (baseColor.a < alphaMaskCutoff)
        discard;

    // Powierzchnia: glTF trzyma metaliczność (B) i chropowatość (G) w jednej teksturze.
    SurfaceData s;
    s.albedo = baseColor.rgb;
    s.alpha = baseColor.a;
    float3 metallicRoughness = textures[physicalDescriptorTextureSet].Sample(globalSampler, input.texCoord).bga;
    s.metallic = metallicRoughness.x * metallicFactor;
    s.roughness = metallicRoughness.y * roughnessFactor;
    s.ao = textures[occlusionTextureSet].Sample(globalSampler, input.texCoord).r;
    s.emissive = textures[emissiveTextureSet].Sample(globalSampler, input.texCoord).rgb;
    s.normal = perturbNormal(normalize(input.worldNormal), input.worldPos, input.texCoord,
                             textures[normalTextureSet].Sample(globalSampler, input.texCoord).xyz, bumpIntensity);
    s.worldPos = input.worldPos;

    // Oświetlenie (z cieniami) i atmosfera - te same funkcje co w przebiegu deferred.
    return float4(shadeSurfaceForward(s), s.alpha);
}
