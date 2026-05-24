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
};

Texture2D texSampler : register(t1, space0);
SamplerState texSamplerState : register(s1, space0);
Texture2D normalSampler : register(t2, space0);
SamplerState normalSamplerState : register(s2, space0);

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

struct FragmentInput
{
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
    float4 worldPos4 = mul(model, float4(input.position, 1.0));
    VertexOutput output;
    output.position = mul(proj, mul(view, worldPos4));
    output.worldPos = worldPos4.xyz;
    output.worldNormal = normalize(mul(inverseTranspose3x3(model), input.normal));
    output.color = input.color;
    output.texCoord = input.texCoord;
    output.matColor = materialColor;
    return output;
}

float4 fragmentMain(FragmentInput input) : SV_Target0
{
    float4 albedo = texSampler.Sample(texSamplerState, input.texCoord) * float4(input.color, 1.0) * input.matColor;

    float3 N = normalize(input.worldNormal);
    float3 dp1 = ddx(input.worldPos);
    float3 dp2 = ddy(input.worldPos);
    float2 duv1 = ddx(input.texCoord);
    float2 duv2 = ddy(input.texCoord);

    float3 bumpedNormal = N;
    if (bumpIntensity > 0.001)
    {
        float r = 1.0 / (duv1.x * duv2.y - duv1.y * duv2.x + 0.0001);
        float3 T = (dp1 * duv2.y - dp2 * duv1.y) * r;
        T = normalize(T - N * dot(N, T));
        float3 B = cross(N, T);
        float3x3 TBN = float3x3(T, B, N);

        float3 normalTex = normalSampler.Sample(normalSamplerState, input.texCoord).xyz * 2.0 - 1.0;
        normalTex.xy *= bumpIntensity;
        normalTex = normalize(normalTex);
        bumpedNormal = normalize(mul(normalTex, TBN));
    }

    float3 L = normalize(lightDir.xyz);
    float NdotL = max(dot(bumpedNormal, L), 0.0);
    float3 H = normalize(L + float3(0.0, 0.0, 1.0));
    float spec = pow(max(dot(bumpedNormal, H), 0.0), 32.0);

    float ambient = lightDir.w;
    float3 lit = albedo.rgb * (ambient + NdotL) + spec * 0.5;
    return float4(lit, albedo.a);
}
