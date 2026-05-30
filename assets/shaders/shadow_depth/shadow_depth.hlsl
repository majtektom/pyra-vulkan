cbuffer GlobalUniformBufferObject : register(b0, space0)
{
    column_major float4x4 view;
    column_major float4x4 proj;
    float4 _pad0;
    float4 _pad1;
    column_major float4x4 lightViewProj;
};

[[vk::push_constant]]
cbuffer PushConstants
{
    column_major float4x4 model;
};

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
};

struct VertexOutput
{
    float4 position : SV_Position;
};

VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    float4 worldPos = mul(model, float4(input.position, 1.0));
    output.position = mul(lightViewProj, worldPos);
    return output;
}
