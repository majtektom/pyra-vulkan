cbuffer GlobalUniformBufferObject : register(b0, space0)
{
    column_major float4x4 view;
    column_major float4x4 proj;
    float4 camRight;
    float4 camUp;
};

[[vk::push_constant]]
cbuffer PushConstants
{
    column_major float4x4 model;
    float4 materialColor;
    float4 lightDir;
    float4 bumpIntensityPad;
};

Texture2D texSampler : register(t1, space0);
SamplerState texSamplerState : register(s1, space0);

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float3 color : COLOR;
    [[vk::location(3)]] float2 texCoord : TEXCOORD0;
    [[vk::location(4)]] float4 instancePosSize : TEXCOORD1;
    [[vk::location(5)]] float4 instanceColor : TEXCOORD2;
    [[vk::location(6)]] float4 instanceUV : TEXCOORD3;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color : COLOR;
    [[vk::location(1)]] float2 texCoord : TEXCOORD0;
    [[vk::location(2)]] float4 matColor : TEXCOORD1;
    [[vk::location(3)]] float3 worldNormal : TEXCOORD2;
};

struct FragmentInput
{
    [[vk::location(0)]] float3 color : COLOR;
    [[vk::location(1)]] float2 texCoord : TEXCOORD0;
    [[vk::location(2)]] float4 matColor : TEXCOORD1;
    [[vk::location(3)]] float3 worldNormal : TEXCOORD2;
};

VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;

    float3 worldPos = input.instancePosSize.xyz
                    + input.position.x * input.instancePosSize.w * camRight.xyz
                    + input.position.y * input.instancePosSize.w * camUp.xyz;

    output.position = mul(proj, mul(view, float4(worldPos, 1.0)));
    output.worldNormal = normalize(input.normal);
    output.color = input.instanceColor.rgb;
    output.texCoord = float2(
        input.texCoord.x * input.instanceUV.z + input.instanceUV.x,
        input.texCoord.y * input.instanceUV.w + input.instanceUV.y
    );
    output.matColor = materialColor;
    return output;
}

float4 fragmentMain(FragmentInput input) : SV_Target0
{
    float4 albedo = texSampler.Sample(texSamplerState, input.texCoord) * float4(input.color, 1.0) * input.matColor;

    float3 N = normalize(input.worldNormal);
    float3 L = normalize(lightDir.xyz);
    float NdotL = max(dot(N, L), 0.0);
    float ambient = lightDir.w;
    float3 lit = albedo.rgb * (ambient + NdotL);
    return float4(lit, albedo.a);
}
