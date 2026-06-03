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

    // Odczyt macierzy kości (uproszczone mapowanie single-joint)
    float4x4 skinMat = boneMatrices[clampedJoints.x];

    // Deformacja pozycji wierzchołka
    float4 skinnedPos = mul(skinMat, float4(input.position, 1.0));
    float4 worldPos4 = mul(model, skinnedPos);
    
    // Deformacja wektora normalnego. 
    // UWAGA: wektor normalny reprezentuje kierunek, dlatego rozszerzamy go do float4 z W = 0.0.
    // Dzięki temu transformacje translacji (przesunięcia kości) nie zniekształcają kierunku normalnej.
    float4 worldNormal4 = mul(model, mul(skinMat, float4(input.normal, 0.0)));

    VertexOutput output;
    output.position = mul(proj, mul(view, worldPos4));
    output.worldPos = worldPos4.xyz;
    output.worldNormal = normalize(worldNormal4.xyz);
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