#include "gbuffer/gbuffer_common.hlsl"

// Bufor macierzy kości w przestrzeni 1 (Set 1 / space1)
cbuffer BoneBuffer : register(b0, space1)
{
    column_major float4x4 boneMatrices[MAX_BONES];
};

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float3 color : COLOR;
    [[vk::location(3)]] float2 texCoord : TEXCOORD0;
    [[vk::location(4)]] uint4 joints : JOINTS;       // Indeksy wpływających kości
    [[vk::location(5)]] float4 weights : WEIGHTS;     // Wagi wpływów kości
};

/**
 * @brief Vertex Shader dla animowanej szkieletowo geometrii w renderingu odroczonym (Deferred Skinned VS).
 * 
 * Wykonuje deformację pozycji wierzchołków i kierunków normalnych na podstawie kości,
 * a następnie przekształca je do przestrzeni rzutowania kamery.
 */
VertexOutput vertexMain(VertexInput input)
{
    // Zabezpieczenie zakresu indeksów
    uint4 clampedJoints = min(input.joints, (uint4)(MAX_BONES - 1));

    // Normalize weights
    float4 w = input.weights;
    float wSum = w.x + w.y + w.z + w.w;
    if (wSum > 0.0001) w /= wSum;
    else w = float4(1.0, 0.0, 0.0, 0.0);

    // LBS: weighted sum of 4 bone matrices
    float4x4 skinMat = boneMatrices[clampedJoints.x] * w.x
                     + boneMatrices[clampedJoints.y] * w.y
                     + boneMatrices[clampedJoints.z] * w.z
                     + boneMatrices[clampedJoints.w] * w.w;

    // Position: skinning then entity transform
    float4 skinnedPos = mul(skinMat, float4(input.position, 1.0));
    float4 worldPos4 = mul(model, skinnedPos);

    // Normal: use full 4x4 mul with w=0 (excludes translation).
    // This correctly handles column_major matrices unlike (float3x3) cast.
    float3 skinnedNormal = mul(skinMat, float4(input.normal, 0.0)).xyz;
    float3 worldNormal = mul(model, float4(skinnedNormal, 0.0)).xyz;

    VertexOutput output;
    output.position = mul(proj, mul(view, worldPos4));
    output.worldPos = worldPos4.xyz;
    output.worldNormal = normalize(worldNormal);
    output.color = input.color;
    output.texCoord = input.texCoord;
    return output;
}

/**
 * @brief Fragment Shader dla animowanej szkieletowo geometrii.
 */
GBufferOutput fragmentMain(VertexOutput input)
{
    return shadeGBuffer(input);
}