#include "shared/globals.hlsl"   // UBO klatki, tablica tekstur bindless i jej sampler (wspólne dla wszystkich shaderów)

// Szybki blok push constants przesyłany bezpośrednio przed rysowaniem skyboxa
[[vk::push_constant]]
cbuffer SkyboxPushConstants
{
    float4 cameraPosition; // Pozycja kamery (używana do przesunięcia geometrii skyboxa)
    float exposure;         // Ekspozycja (mnożnik jasności)
    uint skyboxIndex;       // Indeks tekstury nieba w tablicy tekstur bindless
};

// Globalne zasoby powiązane w Vulkan

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

/**
 * @brief Shader wierzchołków Skyboxa.
 * 
 * Aby uzyskać efekt, że niebo jest nieskończenie daleko (gracz nie może zbliżyć się do krawędzi nieba),
 * przesuwamy wierzchołki skyboxa o aktualną pozycję kamery w świecie. Dzięki temu skybox jest
 * zawsze idealnie wycentrowany na kamerze obserwatora.
 */
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;

    float3 worldPosition = input.position;
    // Przesunięcie pozycji wierzchołka o pozycję kamery w świecie
    worldPosition.x += cameraPosition.x;
    worldPosition.y += cameraPosition.y;
    worldPosition.z += cameraPosition.z;

    // Przekształcenie do przestrzeni ekranu
    output.position = mul(proj, mul(view, float4(worldPosition, 1.0)));
    output.texCoord = input.texCoord;
    return output;
}

/**
 * @brief Shader pikseli Skyboxa.
 * 
 * Pobiera piksel z tekstury nieba na podstawie indeksu `skyboxIndex` przekazanego z C++
 * i mnoży go przez współczynnik jasności (ekspozycji).
 */
float4 fragmentMain(FragmentInput input) : SV_Target0
{
    // Próbkowanie z tablicy bindless textures na podstawie indeksu
    return textures[skyboxIndex].Sample(globalSampler, input.texCoord) * exposure;
}
