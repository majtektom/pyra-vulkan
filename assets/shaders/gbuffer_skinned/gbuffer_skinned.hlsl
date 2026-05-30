#define MAX_BONES 64

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

cbuffer BoneBuffer : register(b0, space1)
{
    column_major float4x4 boneMatrices[MAX_BONES];
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
    [[vk::location(4)]] uint4 joints : JOINTS;
    [[vk::location(5)]] float4 weights : WEIGHTS;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color : COLOR;
    [[vk::location(1)]] float2 texCoord : TEXCOORD0;
    [[vk::location(2)]] float3 worldNormal : TEXCOORD3;
    [[vk::location(3)]] float3 worldPos : TEXCOORD4;
};

struct GBufferOutput
{
    float4 albedoMetallic : SV_Target0;
    float4 normalRoughness : SV_Target1;
    float4 occlusionEmissive : SV_Target2;
    float4 worldPosition : SV_Target3;
};

float3x3 inverseTranspose3x3(float4x4 m)
{
    float3x3 a = float3x3(m[0].xyz, m[1].xyz, m[2].xyz);
    return a;
}

VertexOutput vertexMain(VertexInput input)
{
    // Clamp joint indices to valid range
    uint4 clampedJoints = min(input.joints, (uint4)(MAX_BONES-1));
    float4 clampedWeights = input.weights;
    float clampedTotalWeight = clampedWeights.x + clampedWeights.y + clampedWeights.z + clampedWeights.w;

    // TEST: skinMat = boneMatrices[clampedJoints.x] (bez wag)
    float4x4 skinMat = boneMatrices[clampedJoints.x];
    // skinMat = boneMatrices[0];

    float4 skinnedPos = mul(skinMat, float4(input.position, 1.0));
    float4 worldPos4 = mul(model, skinnedPos);
    float4 worldNormal4 = mul(model, mul(skinMat, float4(input.normal, 0.0)));

    VertexOutput output;
    output.position = mul(proj, mul(view, worldPos4));
    output.worldPos = worldPos4.xyz;
    output.worldNormal = normalize(worldNormal4.xyz);
    output.color = input.color;
    output.texCoord = input.texCoord;
    return output;
}

GBufferOutput fragmentMain(VertexOutput input)
{
    float4 baseColor = textures[baseColorTextureSet].Sample(globalSampler, input.texCoord) * float4(input.color, 1.0) * baseColorFactor;

    float3 N = normalize(input.worldNormal);

    float3 metallicRoughness = textures[physicalDescriptorTextureSet].Sample(globalSampler, input.texCoord).bga;
    float metallic = metallicRoughness.x * metallicFactor;
    float roughness = metallicRoughness.y * roughnessFactor;

    float ao = textures[occlusionTextureSet].Sample(globalSampler, input.texCoord).r;
    float emissiveStrength = dot(textures[emissiveTextureSet].Sample(globalSampler, input.texCoord).rgb, float3(0.333, 0.333, 0.333));

    GBufferOutput output;
    output.albedoMetallic = float4(baseColor.rgb, metallic);
    output.normalRoughness = float4(N * 0.5 + 0.5, roughness);
    output.occlusionEmissive = float4(ao, emissiveStrength, 0.0, 1.0);
    output.worldPosition = float4(input.worldPos, 1.0);
    return output;
}
