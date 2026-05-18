[[vk::push_constant]]
cbuffer TextPushConstants
{
    float2 screenSize;
};

Texture2D fontAtlasSampler : register(t0, space0);

SamplerState fontAtlasSamplerState : register(s0, space0);

struct VertexInput
{
    [[vk::location(0)]] float2 position : POSITION;
    [[vk::location(1)]] float2 texCoord : TEXCOORD0;
    [[vk::location(2)]] float4 color : COLOR;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
    [[vk::location(1)]] float4 color : COLOR;
};

struct FragmentInput
{
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
    [[vk::location(1)]] float4 color : COLOR;
};

VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    float2 normalized = float2(
        (input.position.x / screenSize.x) * 2.0 - 1.0,
        (input.position.y / screenSize.y) * 2.0 - 1.0
    );

    output.position = float4(normalized, 0.0, 1.0);
    output.texCoord = input.texCoord;
    output.color = input.color;
    return output;
}

float4 fragmentMain(FragmentInput input) : SV_Target0
{
    float coverage = fontAtlasSampler.Sample(fontAtlasSamplerState, input.texCoord).r;
    return float4(input.color.rgb, input.color.a * coverage);
}