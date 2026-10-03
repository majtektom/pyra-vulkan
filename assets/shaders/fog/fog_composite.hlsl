#include "shared/light_types.hlsl"

cbuffer GlobalUniformBufferObject : register(b0, space0)
{
    column_major float4x4 view;
    column_major float4x4 proj;
    column_major float4x4 invViewProj;
    float4 camRight;
    float4 camUp;
    float4 cameraPos;
    float4 ambientColor;
    float ambientStrength;
    uint lightingEnabled;
    uint lightCount;
    uint shadowsEnabled;
    LightData lights[16];
    float4 directionalShadowSplits;
    uint directionalShadowCascadeCount;
    float3 shadowPadding;
    float4 fogColor;
    float fogStart;
    float fogEnd;
    float fogDensity;
    uint fogEnabled;
};

SamplerState globalSampler : register(s1, space0);
Texture2D<float4> textures[] : register(t1, space0);

[[vk::binding(0, 1)]] Texture3D<float4> fogVolume;
[[vk::binding(0, 1)]] SamplerState volumeSampler;

[[vk::push_constant]]
cbuffer PushConstants
{
    uint worldPosTexIndex;
    float fogRadius;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VertexOutput vertexMain(uint vid : SV_VertexID)
{
    VertexOutput o;
    o.uv = float2((vid << 1) & 2, vid & 2);
    o.position = float4(o.uv * float2(2.0, 2.0) + float2(-1.0, -1.0), 0.0, 1.0);
    return o;
}

float4 fragmentMain(VertexOutput input) : SV_Target0
{
    // Sample world position directly from G-buffer (avoid depth-clear issue in overlay)
    float3 geomPos = textures[worldPosTexIndex].SampleLevel(globalSampler, input.uv, 0).xyz;

    // NDC coordinate (Vulkan: Y-down, so flip Y)
    float2 ndc = input.uv * float2(2.0, -2.0) + float2(-1.0, 1.0);

    // Reconstruct world position on the far plane
    float4 farClip = mul(invViewProj, float4(ndc, 1.0, 1.0));
    float3 farPos = farClip.xyz / farClip.w;

    // Normalized slice coordinate within the fog radius
    float viewDist = length(farPos - cameraPos.xyz);
    float geomDist = length(geomPos - cameraPos.xyz);
    float maxDist = max(fogRadius, 0.001);

    float t;
    float farFade;
    if (geomDist < 0.001) {
        t = 1.0;
        farFade = 0.0;
    } else {
        t = saturate(geomDist / maxDist);
        farFade = 1.0;
    }

    // Sample accumulated fog from the 3D volume
    float3 volUV = float3(input.uv, t);
    float4 fogData = fogVolume.SampleLevel(volumeSampler, volUV, 0);

    float3 fogScattered = fogData.rgb * farFade;
    float fogFactor = (1.0 - fogData.a) * farFade;

    return float4(fogScattered, fogFactor);
}
