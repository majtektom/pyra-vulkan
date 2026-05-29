cbuffer GlobalUniformBufferObject : register(b0, space0)
{
    column_major float4x4 view;
    column_major float4x4 proj;
};

[[vk::push_constant]]
cbuffer PushConstants
{
    column_major float4x4 model;
    float4 materialColor;
    float4 lightDir;
    float bumpIntensity;
    int numLayers;
    float3 pad;
};

Texture2D diffTex0  : register(t1);
Texture2D diffTex1  : register(t2);
Texture2D diffTex2  : register(t3);
Texture2D diffTex3  : register(t4);
Texture2D diffTex4  : register(t5);
Texture2D diffTex5  : register(t6);
Texture2D diffTex6  : register(t7);
Texture2D diffTex7  : register(t8);
Texture2D diffTex8  : register(t9);
Texture2D diffTex9  : register(t10);
Texture2D diffTex10 : register(t11);
Texture2D diffTex11 : register(t12);
Texture2D maskTex0  : register(t13);
Texture2D maskTex1  : register(t14);
Texture2D maskTex2  : register(t15);
Texture2D maskTex3  : register(t16);
Texture2D maskTex4  : register(t17);
Texture2D maskTex5  : register(t18);
Texture2D maskTex6  : register(t19);
Texture2D maskTex7  : register(t20);
Texture2D maskTex8  : register(t21);
Texture2D maskTex9  : register(t22);
Texture2D maskTex10 : register(t23);
Texture2D maskTex11 : register(t24);

SamplerState texSampler : register(s1);

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
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
    [[vk::location(1)]] float3 worldPos : TEXCOORD2;
    [[vk::location(2)]] float3 worldNormal : TEXCOORD3;
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
    output.texCoord = input.texCoord;
    output.worldPos = worldPos4.xyz;
    output.worldNormal = normalize(mul(inverseTranspose3x3(model), input.normal));
    return output;
}

float3 sampleLayer(int i, float2 uv)  
{
    if (i == 0) return diffTex0.Sample(texSampler, uv).rgb;
    if (i == 1) return diffTex1.Sample(texSampler, uv).rgb;
    if (i == 2) return diffTex2.Sample(texSampler, uv).rgb;
    if (i == 3) return diffTex3.Sample(texSampler, uv).rgb;
    if (i == 4) return diffTex4.Sample(texSampler, uv).rgb;
    if (i == 5) return diffTex5.Sample(texSampler, uv).rgb;
    if (i == 6) return diffTex6.Sample(texSampler, uv).rgb;
    if (i == 7) return diffTex7.Sample(texSampler, uv).rgb;
    if (i == 8) return diffTex8.Sample(texSampler, uv).rgb;
    if (i == 9) return diffTex9.Sample(texSampler, uv).rgb;
    if (i == 10) return diffTex10.Sample(texSampler, uv).rgb;
    return diffTex11.Sample(texSampler, uv).rgb;
}

float sampleMask(int i, float2 worldUV)
{
    if (i == 0) return maskTex0.Sample(texSampler, worldUV).r;
    if (i == 1) return maskTex1.Sample(texSampler, worldUV).r;
    if (i == 2) return maskTex2.Sample(texSampler, worldUV).r;
    if (i == 3) return maskTex3.Sample(texSampler, worldUV).r;
    if (i == 4) return maskTex4.Sample(texSampler, worldUV).r;
    if (i == 5) return maskTex5.Sample(texSampler, worldUV).r;
    if (i == 6) return maskTex6.Sample(texSampler, worldUV).r;
    if (i == 7) return maskTex7.Sample(texSampler, worldUV).r;
    if (i == 8) return maskTex8.Sample(texSampler, worldUV).r;
    if (i == 9) return maskTex9.Sample(texSampler, worldUV).r;
    if (i == 10) return maskTex10.Sample(texSampler, worldUV).r;
    return maskTex11.Sample(texSampler, worldUV).r;
}

float4 fragmentMain(VertexOutput input) : SV_Target0
{
    float2 texUV = input.texCoord;
    float2 maskWorldUV = input.worldPos.xz / 2560.0;
    float3 N = normalize(input.worldNormal);
    float3 L = normalize(lightDir.xyz);
    float NdotL = max(dot(N, L), 0.0);
    float ambient = lightDir.w;

    int n = min(numLayers, 12);
    float3 lit = sampleLayer(0, texUV) * (ambient + NdotL) * materialColor.rgb;
    float3 result = lit;

    for (int i = 1; i < n; i++)
    {
        float maskVal = saturate(sampleMask(i, maskWorldUV));
        if (maskVal < 0.01) continue;
        float3 albedo = sampleLayer(i, texUV) * (ambient + NdotL) * materialColor.rgb;
        result = albedo * maskVal + result * (1.0 - maskVal);
    }

    return float4(result, 1.0);
}
