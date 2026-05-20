cbuffer GlobalUniformBufferObject : register(b0)
{
    column_major float4x4 view;
    column_major float4x4 proj;
};

[[vk::push_constant]]
cbuffer PushConstants
{
    column_major float4x4 model;
    float4 materialColor;
};

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
    [[vk::location(1)]] float4 matColor : TEXCOORD1;
};

struct FragmentInput
{
    [[vk::location(0)]] float3 color : COLOR;
    [[vk::location(1)]] float4 matColor : TEXCOORD1;
};

VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(proj, mul(view, mul(model, float4(input.position, 1.0))));
    output.color = input.color;
    output.matColor = materialColor;
    return output;
}

float4 fragmentMain(FragmentInput input) : SV_Target0
{
    return float4(input.color, 1.0) * input.matColor;
}
