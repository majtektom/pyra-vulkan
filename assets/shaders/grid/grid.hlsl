#include "shared/globals.hlsl"   // UBO klatki, tablica tekstur bindless i jej sampler (wspólne dla wszystkich shaderów)

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION; // Pozycja wierzchołka w przestrzeni świata
    // Lokacje zgodne z Vertex::attributeDescriptions(): 1 = normalna (nieużywana), 2 = kolor, 3 = UV.
    [[vk::location(2)]] float3 color : COLOR;        // Kolor linii siatki przekazany z CPU
    [[vk::location(3)]] float2 texCoord : TEXCOORD0; // Nieużywane współrzędne UV (dla zgodności z formatem Vertex)
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color : COLOR;
};

struct FragmentInput
{
    [[vk::location(0)]] float3 color : COLOR;
};

/**
 * @brief Shader wierzchołków dla siatki 3D.
 * 
 * Wykonuje standardowe przekształcenie współrzędnych wierzchołka z przestrzeni świata 
 * do przestrzeni projekcji ekranowej (Clip Space) za pomocą mnożenia macierzowego:
 * pos_clip = proj * view * pos_world
 */
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(proj, mul(view, float4(input.position, 1.0)));
    output.color = input.color;
    return output;
}

/**
 * @brief Shader pikseli siatki.
 * 
 * Zwraca interpolowany kolor linii siatki z pełną nieprzezroczystością (alfa = 1.0).
 */
float4 fragmentMain(FragmentInput input) : SV_Target0
{
    return float4(input.color, 1.0);
}