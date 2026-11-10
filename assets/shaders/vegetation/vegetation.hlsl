#include "shared/surface_lighting.hlsl"   // wspólne oświetlenie, cienie i atmosfera

#include "shared/globals.hlsl"   // UBO klatki, tablica tekstur bindless i jej sampler (wspólne dla wszystkich shaderów)

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

float3x3 inverseTranspose3x3(float4x4 m)
{
    float3x3 a = float3x3(m[0].xyz, m[1].xyz, m[2].xyz);
    return a;
}

VertexOutput vertexMain(VertexInput input)
{
    float3 pos = input.position;

    float wind = sin(windTime * 2.5 + pos.x * 0.3 + pos.z * 0.3 + input.texCoord.y * 5.0) * windStrength;
    pos.x += wind * pos.y * 0.15;
    pos.z += wind * pos.y * 0.08;

    float4 worldPos4 = mul(model, float4(pos, 1.0));
    VertexOutput output;
    output.position = mul(proj, mul(view, worldPos4));
    output.worldPos = worldPos4.xyz;

    output.worldNormal = normalize(mul(inverseTranspose3x3(model), input.normal));
    output.color = input.color;
    output.texCoord = input.texCoord;
    output.matColor = baseColorFactor;
    return output;
}

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
