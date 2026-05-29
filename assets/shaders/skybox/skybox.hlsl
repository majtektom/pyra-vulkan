cbuffer GlobalUniformBufferObject : register(b0)
{
    column_major float4x4 view;
    column_major float4x4 proj;
};

[[vk::push_constant]]
cbuffer SkyboxPushConstants
{
    float4 cameraPosition;
    float exposure;
    uint skyboxIndex;
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
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
};

struct FragmentInput
{
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
};

VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;

    float3 worldPosition = input.position;
    worldPosition.x += cameraPosition.x;
    worldPosition.y += cameraPosition.y;
    worldPosition.z += cameraPosition.z;

    output.position = mul(proj, mul(view, float4(worldPosition, 1.0)));
    output.texCoord = input.texCoord;
    return output;
}

float4 fragmentMain(FragmentInput input) : SV_Target0
{
    return textures[skyboxIndex].Sample(globalSampler, input.texCoord) * exposure;
}
