#include "gbuffer/gbuffer_common.hlsl"

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float3 color : COLOR;
    [[vk::location(3)]] float2 texCoord : TEXCOORD0;
};

/**
 * @brief Vertex Shader dla statycznych obiektów geometrii w renderingu odroczonym (Deferred).
 * 
 * Transformuje wierzchołki statycznej siatki do przestrzeni projekcji ekranu 
 * oraz oblicza normalne i pozycje świata niezbędne do napełnienia G-Buffera.
 */
VertexOutput vertexMain(VertexInput input)
{
    float4 worldPos4 = mul(model, float4(input.position, 1.0));
    VertexOutput output;
    output.position = mul(proj, mul(view, worldPos4));
    output.worldPos = worldPos4.xyz;
    output.worldNormal = normalize(mul(inverseTranspose3x3(model), input.normal));
    output.color = input.color;
    output.texCoord = input.texCoord;
    return output;
}

/**
 * @brief Fragment Shader dla geometrii.
 * 
 * Przekazuje interpolowane atrybuty wierzchołków do metody `shadeGBuffer`
 * w celu zapisu do tekstur G-Buffera.
 */
GBufferOutput fragmentMain(VertexOutput input)
{
    return shadeGBuffer(input);
}
