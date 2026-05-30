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

[[vk::binding(0, 1)]] Texture2D<float4> gbufferAlbedo;
[[vk::binding(0, 1)]] SamplerState gbufferSampler;
[[vk::binding(1, 1)]] Texture2D<float4> gbufferNormal;
[[vk::binding(2, 1)]] Texture2D<float4> gbufferOcclusion;
[[vk::binding(3, 1)]] Texture2D<float4> gbufferWorldPos;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
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
    float4 albedoMetallic = gbufferAlbedo.Sample(gbufferSampler, input.uv);
    float4 normalRoughness = gbufferNormal.Sample(gbufferSampler, input.uv);
    float4 occlusionEmissive = gbufferOcclusion.Sample(gbufferSampler, input.uv);
    float4 worldPosData = gbufferWorldPos.Sample(gbufferSampler, input.uv);

    float3 baseColor = albedoMetallic.rgb;
    float metallic = albedoMetallic.a;
    float roughness = max(normalRoughness.a, 0.04);
    float3 N = normalize(normalRoughness.rgb * 2.0 - 1.0);
    float ao = occlusionEmissive.r;
    float emissiveStrength = occlusionEmissive.g;

    float3 worldPos = worldPosData.xyz;

    // Discard pixels without GBuffer data (skybox regions)
    if (worldPosData.w < 0.5) discard;

    float4 baseColorOut = float4(baseColor, 1.0);

    if (lightingEnabled == 0)
    {
        return baseColorOut;
    }

    if (lightingEnabled == 0)
    {
        return baseColorOut;
    }

    float3 V = normalize(cameraPos.xyz - worldPos);
    float NdotV = max(dot(N, V), 0.0);

    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, baseColor, metallic);

    float3 Lo = float3(0.0, 0.0, 0.0);
    uint count = min(lightCount, 4u);

    for (uint i = 0; i < count; i++)
    {
        float4 pos = getLightPosition(i, lights_0_position, lights_1_position, lights_2_position, lights_3_position);
        float4 dir = getLightDirection(i, lights_0_direction, lights_1_direction, lights_2_direction, lights_3_direction);
        float4 col = getLightColor(i, lights_0_color, lights_1_color, lights_2_color, lights_3_color);
        float4 par = getLightParams(i, lights_0_params, lights_1_params, lights_2_params, lights_3_params);
        uint ltype = uint(dir.w);

        if (ltype == 1) // directional
        {
            float3 L = normalize(dir.xyz);
            float3 radiance = col.rgb * col.w;
            Lo += pbrDirectLight(N, V, baseColor, metallic, roughness, NdotV, F0, L, radiance);
        }
        else if (ltype == 2) // point
        {
            float3 Lvec = pos.xyz - worldPos;
            float dist = length(Lvec);
            float range = max(par.z, 0.001);
            float attenuation = 1.0 / (1.0 + (dist * dist) / (range * range));
            float3 L = Lvec / dist;
            float3 radiance = col.rgb * col.w * attenuation;
            Lo += pbrDirectLight(N, V, baseColor, metallic, roughness, NdotV, F0, L, radiance);
        }
        else if (ltype == 3) // spot
        {
            float3 Lvec = pos.xyz - worldPos;
            float dist = length(Lvec);
            float range = max(par.z, 0.001);
            float attenuation = 1.0 / (1.0 + (dist * dist) / (range * range));
            float3 L = Lvec / dist;

            float3 spotDir = normalize(dir.xyz);
            float theta = dot(-L, spotDir);
            float inner = max(par.x, 0.01);
            float outer = max(par.y, 0.0);
            float spotIntensity = smoothstep(outer, inner, theta);

            float3 radiance = col.rgb * col.w * attenuation * spotIntensity;
            Lo += pbrDirectLight(N, V, baseColor, metallic, roughness, NdotV, F0, L, radiance);
        }
    }

    float3 ambientContrib = baseColor * ambientStrength * ambientColor.rgb * ao;

    float3 color = ambientContrib + Lo + baseColor * emissiveStrength;

    color = saturate(color);

    return float4(color, baseColorOut.a);
}
