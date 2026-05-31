#define MAX_BONES 64

[[vk::push_constant]]
cbuffer PushConstants
{
    column_major float4x4 model;
    column_major float4x4 lightViewProj;
    float alphaCutoff;
    uint baseColorTextureSet;
    float2 _pad;
};

cbuffer BoneBuffer : register(b0, space1)
{
    column_major float4x4 boneMatrices[MAX_BONES];
};

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(3)]] float2 texCoord : TEXCOORD0;
    [[vk::location(4)]] uint4 joints : JOINTS;
    [[vk::location(5)]] float4 weights : WEIGHTS;
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
    uint4 clampedJoints = min(input.joints, (uint4)(MAX_BONES - 1));

    float4x4 skinMat = boneMatrices[clampedJoints.x];

    float4 skinnedPos = mul(skinMat, float4(input.position, 1.0));
    float4 worldPos = mul(model, skinnedPos);

    VertexOutput output;
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
