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
    uint sceneDepthTexIndex; // głębia sceny (bindless "ocean_scene_depth") — obejmuje też obiekty forward
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
    // Load zamiast Sample: bez filtrowania głębi (nie każdy format depth wspiera filtr liniowy).
    float depth = textures[sceneDepthTexIndex].Load(int3(int2(input.position.xy), 0)).r;

    // Macierz projekcji odwraca już Y pod Vulkana: NDC = uv * 2 - 1 (jak w ocean.hlsl).
    float2 ndc = input.uv * 2.0 - 1.0;
    float maxDist = max(fogRadius, 0.001);

    // Niebo (depth = 1): mgła na całej długości wolumenu. Geometria: do odległości od kamery.
    float t = 1.0;
    if (depth < 0.99999)
    {
        float4 world = mul(invViewProj, float4(ndc, depth, 1.0));
        float3 geomPos = world.xyz / max(world.w, 0.00001);
        t = saturate(length(geomPos - cameraPos.xyz) / maxDist);
    }

    // rgb = światło rozproszone, a = transmitancja; blending: dst * a + rgb.
    float4 fogData = fogVolume.SampleLevel(volumeSampler, float3(input.uv, t), 0);
    return float4(fogData.rgb, 1.0 - fogData.a);
}
