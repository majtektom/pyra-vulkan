#include "shared/light_eval.hlsl"

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
    float windTime;
    float windStrength;
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
    float4 baseColor = textures[baseColorTextureSet].Sample(globalSampler, input.texCoord) * float4(input.color, 1.0) * input.matColor;

    if (baseColor.a < alphaMaskCutoff)
        discard;

    float3 N = normalize(input.worldNormal);

    float3 dp1 = ddx(input.worldPos);
    float3 dp2 = ddy(input.worldPos);
    float2 duv1 = ddx(input.texCoord);
    float2 duv2 = ddy(input.texCoord);

    if (bumpIntensity > 0.001)
    {
        float3 normalTex = textures[normalTextureSet].Sample(globalSampler, input.texCoord).xyz * 2.0 - 1.0;
        normalTex.xy *= bumpIntensity;
        normalTex = normalize(normalTex);

        float r = 1.0 / (duv1.x * duv2.y - duv1.y * duv2.x + 0.0001);
        float3 T_raw = (dp1 * duv2.y - dp2 * duv1.y) * r;
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

    float3 color;
    if (lightingEnabled == 0)
    {
        color = baseColor.rgb;
    }
    else
    {
        float3 metallicRoughness = textures[physicalDescriptorTextureSet].Sample(globalSampler, input.texCoord).bga;
        float metallic = metallicRoughness.x * metallicFactor;
        float roughness = max(metallicRoughness.y * roughnessFactor, 0.04);

        float ao = textures[occlusionTextureSet].Sample(globalSampler, input.texCoord).r;
        float3 emissive = textures[emissiveTextureSet].Sample(globalSampler, input.texCoord).rgb;

        float3 V = normalize(cameraPos.xyz - input.worldPos);
        float NdotV = max(dot(N, V), 0.0);

        float3 F0 = float3(0.04, 0.04, 0.04);
        F0 = lerp(F0, baseColor.rgb, metallic);

        float3 Lo = float3(0.0, 0.0, 0.0);
        uint count = min(lightCount, 16u);
        for (uint i = 0; i < count; i++)
        {
            float3 L;
            float3 radiance;
            uint valid;

            unpackDirectLight(lights[i], input.worldPos, L, radiance, valid);
            if (valid == 0u) continue;

            Lo += pbrDirectLight(N, V, baseColor.rgb, metallic, roughness, NdotV, F0, L, radiance);
        }

        float3 ambientContrib = baseColor.rgb * ambientStrength * ambientColor.rgb * ao;
        color = ambientContrib + Lo + emissive;
        color = saturate(color);
    }

    if (fogEnabled != 0u)
    {
        float dist = distance(cameraPos.xyz, input.worldPos);
        float fogFactor = (fogEnd - dist) / (fogEnd - fogStart);
        fogFactor = saturate(fogFactor);
        color = lerp(fogColor.rgb, color, fogFactor);
    }

    return float4(color, baseColor.a);
}