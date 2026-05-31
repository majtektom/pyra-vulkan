[[vk::push_constant]]
cbuffer PushConstants
{
    column_major float4x4 model;
    column_major float4x4 lightViewProj;
    float alphaCutoff;
    uint baseColorTextureSet;
    float2 _pad;
};

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(3)]] float2 texCoord : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
};

SamplerState globalSampler : register(s1, space0);
Texture2D textures[] : register(t1, space0);

VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    float4 worldPos = mul(model, float4(input.position, 1.0));
    output.position = mul(lightViewProj, worldPos);
    output.texCoord = input.texCoord;
    return output;
}

void fragmentMain(VertexOutput input)
{
    if (alphaCutoff > 0.0)
    {
        float alpha = textures[baseColorTextureSet].Sample(globalSampler, input.texCoord).a;
        if (alpha < alphaCutoff)
            discard;
    }
}
