#define MAX_BONES 64

// Push Constants dla cieniowania obiektu animowanego szkieletowo
[[vk::push_constant]]
cbuffer PushConstants
{
    column_major float4x4 model;
    column_major float4x4 lightViewProj;
    float alphaCutoff;
    uint baseColorTextureSet;
    float2 _pad;
};

// Bufor macierzy transformacji kości (Set 1 / space1)
cbuffer BoneBuffer : register(b0, space1)
{
    column_major float4x4 boneMatrices[MAX_BONES];
};

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(3)]] float2 texCoord : TEXCOORD0;
    [[vk::location(4)]] uint4 joints : JOINTS;       // Indeksy kości wpływających na ten wierzchołek
    [[vk::location(5)]] float4 weights : WEIGHTS;     // Wagi (siła wpływu) poszczególnych kości
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
};

SamplerState globalSampler : register(s1, space0);
Texture2D textures[] : register(t1, space0);

/**
 * @brief Shader wierzchołków dla cieni obiektów animowanych szkieletowo (Skinned Shadow VS).
 * 
 * Transformuje wierzchołki na podstawie przypisanych kości (Skeletal Skinning) 
 * przed ich projekcją do przestrzeni rzutowania światła. 
 * Zapobiega to powstawaniu cieni w pozycji statycznej (tzw. T-pose), sprawiając, 
 * że cienie poruszają się synchronicznie wraz z animowaną postacią.
 */
VertexOutput vertexMain(VertexInput input)
{
    // Zabezpieczenie przed wyjściem indeksu kości poza dopuszczalny zakres tablicy
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

    // Deformacja pozycji wierzchołka kośćmi (Skinning)
    float4 skinnedPos = mul(skinMat, float4(input.position, 1.0));
    float4 worldPos = mul(model, skinnedPos);

    VertexOutput output;
    output.position = mul(lightViewProj, worldPos);
    output.texCoord = input.texCoord;
    return output;
}

/**
 * @brief Shader pikseli (Shadow Pass Cutout PS).
 */
void fragmentMain(VertexOutput input)
{
    if (alphaCutoff > 0.0)
    {
        float alpha = textures[baseColorTextureSet].Sample(globalSampler, input.texCoord).a;
        if (alpha < alphaCutoff)
            discard;
    }
}
