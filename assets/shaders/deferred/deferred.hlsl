#include "shared/pbr.hlsl"

struct LightData {
    column_major float4x4 lightViewProj;
    float4 position;    // xyz = world position, w = shadow map layer index
    float4 direction;   // xyz = light direction, w = type: 1=dir, 2=point, 3=spot
    float4 color;       // rgb = color, w = intensity
    float4 params;      // x=innerCos, y=outerCos, z=range, w=unused
};

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
};

[[vk::binding(0, 1)]] Texture2D<float4> gbufferAlbedo;
[[vk::binding(0, 1)]] SamplerState gbufferSampler;
[[vk::binding(1, 1)]] Texture2D<float4> gbufferNormal;
[[vk::binding(2, 1)]] Texture2D<float4> gbufferOcclusion;
[[vk::binding(3, 1)]] Texture2D<float4> gbufferWorldPos;
[[vk::binding(4, 1)]] Texture2DArray<float> shadowMapArray;
[[vk::binding(4, 1)]] SamplerComparisonState shadowSampler;

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

float sampleShadow(uint lightIndex, float3 worldPos, float3 N)
{
    float3 L = (lights[lightIndex].direction.w == 1.0) ? 
               normalize(lights[lightIndex].direction.xyz) : 
               normalize(lights[lightIndex].position.xyz - worldPos);

    // Dynamic bias based on normal and light direction
    float bias = max(0.01 * (1.0 - dot(N, L)), 0.002);
    float4 shadowPos = mul(lights[lightIndex].lightViewProj, float4(worldPos + N * bias, 1.0));
    float3 projCoords = shadowPos.xyz / shadowPos.w;
    
    // Transform to [0,1] range
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    
    if (projCoords.z > 1.0) return 1.0;
    if (projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) return 1.0;

    float shadow = 0.0;
    float2 texelSize = 1.0 / 2048.0;
    float layer = lights[lightIndex].position.w;

    // PCF 3x3
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            shadow += shadowMapArray.SampleCmpLevelZero(shadowSampler, float3(projCoords.xy + float2(x, y) * texelSize, layer), projCoords.z);
        }
    }
    
    return shadow / 9.0;
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
    if (worldPosData.w < 0.5) discard;

    if (lightingEnabled == 0) return float4(baseColor, 1.0);

    float3 V = normalize(cameraPos.xyz - worldPos);
    float NdotV = max(dot(N, V), 0.0);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);

    float3 Lo = float3(0.0, 0.0, 0.0);
    uint count = min(lightCount, 16u);

    for (uint i = 0; i < count; i++)
    {
        float3 pos = lights[i].position.xyz;
        float3 dir = lights[i].direction.xyz;
        float3 col = lights[i].color.rgb;
        float intensity = lights[i].color.w;
        float4 par = lights[i].params;
        uint ltype = uint(lights[i].direction.w);

        float3 L;
        float3 radiance;
        
        if (ltype == 1) // directional
        {
            L = normalize(dir);
            radiance = col * intensity;
        }
        else if (ltype == 2 || ltype == 3) // point or spot
        {
            float3 Lvec = pos - worldPos;
            float dist = length(Lvec);
            float range = max(par.z, 0.001);
            float attenuation = 1.0 / (1.0 + (dist * dist) / (range * range));
            L = Lvec / dist;
            radiance = col * intensity * attenuation;
            
            if (ltype == 3) // spot
            {
                float theta = dot(-L, normalize(dir));
                radiance *= smoothstep(par.y, par.x, theta);
            }
        }
        else continue;

        float shadow = (shadowsEnabled != 0) ? sampleShadow(i, worldPos, N) : 1.0;
        Lo += pbrDirectLight(N, V, baseColor, metallic, roughness, NdotV, F0, L, radiance) * shadow;
    }

    float3 ambientContrib = baseColor * ambientStrength * ambientColor.rgb * ao;
    float3 color = ambientContrib + Lo + baseColor * emissiveStrength;
    color = saturate(color);

    return float4(color, 1.0);
}