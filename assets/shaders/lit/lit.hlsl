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
    [[vk::location(2)]] float4 matColor : TEXCOORD1;
    [[vk::location(3)]] float3 worldPos : TEXCOORD2;
    [[vk::location(4)]] float3 worldNormal : TEXCOORD3;
    [[vk::location(5)]] float4 lightSpacePos : TEXCOORD4;
};

struct FragmentInput
{
    [[vk::location(0)]] float3 color : COLOR;
    [[vk::location(1)]] float2 texCoord : TEXCOORD0;
    [[vk::location(2)]] float4 matColor : TEXCOORD1;
    [[vk::location(3)]] float3 worldPos : TEXCOORD2;
    [[vk::location(4)]] float3 worldNormal : TEXCOORD3;
    [[vk::location(5)]] float4 lightSpacePos : TEXCOORD4;
};

static const float PI = 3.14159265359;

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
    output.matColor = baseColorFactor;
    output.lightSpacePos = mul(lightViewProj, worldPos4);
    return output;
}

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return nom / denom;
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float ggx1 = NdotV / (NdotV * (1.0 - k) + k);
    float ggx2 = NdotL / (NdotL * (1.0 - k) + k);
    return ggx1 * ggx2;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
}

float shadowFactor(float4 lightSpacePos, uint shadowIdx)
{
    float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0)
        return 1.0;

    float bias = 0.002;
    float currentDepth = projCoords.z - bias;

    float shadow = 0.0;
    float2 texelSize = float2(1.0 / 2048.0, 1.0 / 2048.0);
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            float2 uv = projCoords.xy + float2(x, y) * texelSize;
            float depth = textures[shadowIdx].SampleLevel(globalSampler, uv, 0).r;
            shadow += (currentDepth > depth) ? 0.3 : 1.0;
        }
    }
    return shadow / 9.0;
}

float4 getLightPosition(uint i)
{
    if (i == 0) return lights_0_position;
    if (i == 1) return lights_1_position;
    if (i == 2) return lights_2_position;
    return lights_3_position;
}

float4 getLightDirection(uint i)
{
    if (i == 0) return lights_0_direction;
    if (i == 1) return lights_1_direction;
    if (i == 2) return lights_2_direction;
    return lights_3_direction;
}

float4 getLightColor(uint i)
{
    if (i == 0) return lights_0_color;
    if (i == 1) return lights_1_color;
    if (i == 2) return lights_2_color;
    return lights_3_color;
}

float4 getLightParams(uint i)
{
    if (i == 0) return lights_0_params;
    if (i == 1) return lights_1_params;
    if (i == 2) return lights_2_params;
    return lights_3_params;
}

float3 pbrDirectLight(float3 N, float3 V, float3 baseColor, float metallic, float roughness,
                      float NdotV, float3 F0, float3 L, float3 radiance)
{
    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySmith(NdotV, NdotL, roughness);
    float3 F = FresnelSchlick(HdotV, F0);

    float3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);

    float3 kS = F;
    float3 kD = (float3(1.0, 1.0, 1.0) - kS) * (1.0 - metallic);

    return (kD * baseColor / PI + specular) * radiance * NdotL;
}

float4 fragmentMain(FragmentInput input) : SV_Target0
{
    float4 baseColor = textures[baseColorTextureSet].Sample(globalSampler, input.texCoord) * float4(input.color, 1.0) * input.matColor;

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
        float3 T = (dp1 * duv2.y - dp2 * duv1.y) * r;
        T = normalize(T - N * dot(N, T));
        float3 B = cross(N, T);
        float3x3 TBN = float3x3(T, B, N);
        N = normalize(mul(normalTex, TBN));
    }

    if (lightingEnabled == 0)
    {
        return baseColor;
    }

    float3 metallicRoughness = textures[physicalDescriptorTextureSet].Sample(globalSampler, input.texCoord).bga;
    float metallic = metallicRoughness.x * metallicFactor;
    float roughness = max(metallicRoughness.y * roughnessFactor, 0.04);

    float ao = textures[occlusionTextureSet].Sample(globalSampler, input.texCoord).r;

    float3 emissive = textures[emissiveTextureSet].Sample(globalSampler, input.texCoord).rgb;

    float3 V = normalize(cameraPos.xyz - input.worldPos);
    float NdotV = max(dot(N, V), 0.0);

    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, baseColor.rgb, metallic);

    float s = (shadowMapIndex > 0) ? shadowFactor(input.lightSpacePos, shadowMapIndex) : 1.0;

    float3 Lo = float3(0.0, 0.0, 0.0);

    uint count = min(lightCount, 4u);
    for (uint i = 0; i < count; i++)
    {
        float4 pos = getLightPosition(i);
        float4 dir = getLightDirection(i);
        float4 col = getLightColor(i);
        float4 par = getLightParams(i);
        uint ltype = uint(dir.w);

        if (ltype == 1) // directional
        {
            float3 L = normalize(dir.xyz);
            float3 radiance = col.rgb * col.w;
            Lo += pbrDirectLight(N, V, baseColor.rgb, metallic, roughness, NdotV, F0, L, radiance);
        }
        else if (ltype == 2) // point
        {
            float3 Lvec = pos.xyz - input.worldPos;
            float dist = length(Lvec);
            float range = max(par.z, 0.001);
            float attenuation = 1.0 / (1.0 + (dist * dist) / (range * range));
            float3 L = Lvec / dist;
            float3 radiance = col.rgb * col.w * attenuation;
            Lo += pbrDirectLight(N, V, baseColor.rgb, metallic, roughness, NdotV, F0, L, radiance);
        }
        else if (ltype == 3) // spot
        {
            float3 Lvec = pos.xyz - input.worldPos;
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
            Lo += pbrDirectLight(N, V, baseColor.rgb, metallic, roughness, NdotV, F0, L, radiance);
        }
    }

    Lo *= s;

    float3 ambientContrib = baseColor.rgb * ambientStrength * ambientColor.rgb * ao;

    float3 color = ambientContrib + Lo + emissive;

    color = saturate(color);

    return float4(color, baseColor.a);
}
