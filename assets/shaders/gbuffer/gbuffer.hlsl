#include "shared/pbr.hlsl"

cbuffer GlobalUniformBufferObject : register(b0, space0)
{
    column_major float4x4 view;
    column_major float4x4 proj;
    float4 camRight;
    float4 camUp;
    column_major float4x4 lightViewProj;
    float4 _padLightDir;
    uint shadowMapIndex;
    uint lightingEnabled;
    float ambientStrength;
    uint lightCount;
    float4 _padLightColor;
    float4 ambientColor;
    float4 cameraPos;
    float4 lights_0_position;
    float4 lights_0_direction;
    float4 lights_0_color;
    float4 lights_0_params;
    float4 lights_1_position;
    float4 lights_1_direction;
    float4 lights_1_color;
    float4 lights_1_params;
    float4 lights_2_position;
    float4 lights_2_direction;
    float4 lights_2_color;
    float4 lights_2_params;
    float4 lights_3_position;
    float4 lights_3_direction;
    float4 lights_3_color;
    float4 lights_3_params;
};

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

SamplerState globalSampler : register(s1, space0);
Texture2D textures[] : register(t1, space0);


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
    [[vk::location(2)]] float3 worldNormal : TEXCOORD3;
    [[vk::location(3)]] float3 worldPos : TEXCOORD4;
};

struct GBufferOutput
{
    float4 albedoMetallic : SV_Target0;
    float4 normalRoughness : SV_Target1;
    float4 occlusionEmissive : SV_Target2;
    float4 worldPosition : SV_Target3;
};

float3x3 inverseTranspose3x3(float4x4 m)
{
    float3x3 a = float3x3(m[0].xyz, m[1].xyz, m[2].xyz);
    return a;
}

VertexOutput vertexMain(VertexInput input)
{
    float4 worldPos4 = mul(model, float4(input.position, 1.0));
    VertexOutput output;
    output.position = mul(proj, mul(view, worldPos4));
    output.worldPos = worldPos4.xyz;
    output.worldNormal = normalize(mul(inverseTranspose3x3(model), input.normal));
    output.color = input.color;
    output.texCoord = input.texCoord;
    return output;
}

GBufferOutput fragmentMain(VertexOutput input)
{
    float4 baseColor = textures[baseColorTextureSet].Sample(globalSampler, input.texCoord) * float4(input.color, 1.0) * baseColorFactor;

    float3 N = normalize(input.worldNormal);

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
        float3 T = normalize((dp1 * duv2.y - dp2 * duv1.y) * re - N * dot(N, (dp1 * duv2.y - dp2 * duv1.y) * re));
        float3 B = cross(N, T);
        float3x3 TBN = float3x3(T, B, N);
        N = normalize(mul(normalTex, TBN));
    }

    float3 metallicRoughness = textures[physicalDescriptorTextureSet].Sample(globalSampler, input.texCoord).bga;
    float metallic = metallicRoughness.x * metallicFactor;
    float roughness = metallicRoughness.y * roughnessFactor;

    float ao = textures[occlusionTextureSet].Sample(globalSampler, input.texCoord).r;
    float emissiveStrength = dot(textures[emissiveTextureSet].Sample(globalSampler, input.texCoord).rgb, float3(0.333, 0.333, 0.333));

    GBufferOutput output;
    output.albedoMetallic = float4(baseColor.rgb, metallic);
    output.normalRoughness = float4(N * 0.5 + 0.5, roughness);
    output.occlusionEmissive = float4(ao, emissiveStrength, 0.0, 1.0);
    output.worldPosition = float4(input.worldPos, 1.0);
    return output;
}
