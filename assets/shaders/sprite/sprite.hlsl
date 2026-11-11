#include "shared/globals.hlsl"   // UBO klatki, tablica tekstur bindless i jej sampler (wspólne dla wszystkich shaderów)

#include "shared/material_push.hlsl"   // blok push constants (układ jak w push_constants.h)


struct VertexInput
{
    // Lokalna geometria quada [-0.5, 0.5]
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float3 color : COLOR;
    [[vk::location(3)]] float2 texCoord : TEXCOORD0;
    
    // Dane instancji (z bufora instancji sprite'ów):
    [[vk::location(4)]] float4 instancePosSize : TEXCOORD1; // xyz = pozycja świata, w = rozmiar (skala)
    [[vk::location(5)]] float4 instanceColor : TEXCOORD2;   // Kolor modulacji (rgba)
    [[vk::location(6)]] float4 instanceUV : TEXCOORD3;      // x,y = przesunięcie UV (Offset), z,w = szerokość/wysokość klatki (Scale) w atlasie
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR;
    [[vk::location(1)]] float2 texCoord : TEXCOORD0;
};

/**
 * @brief Shader wierzchołków dla Sprite'ów 2D w świecie 3D.
 * 
 * Podobnie jak w cząsteczkach, wektory kamery Right/Up są używane do ułożenia quada
 * przodem do kamery (billboarding).
 * Dodatkowo, funkcja modyfikuje współrzędne UV na podstawie wektora `instanceUV`
 * w celu wycięcia klatki animacji z większego atlasu tekstur (Sprite Sheet):
 * UV_klatki = UV_wierzcholka * rozmiar_klatki + offset_klatki
 */
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    
    // Billboarding w przestrzeni świata
    float3 worldPos = input.instancePosSize.xyz
                    + input.position.x * input.instancePosSize.w * camRight.xyz
                    + input.position.y * input.instancePosSize.w * camUp.xyz;
                    
    output.position = mul(proj, mul(view, float4(worldPos, 1.0)));
    output.color = input.instanceColor;
    
    // Skalowanie i przesunięcie UV do odczytania właściwej klatki z atlasu
    output.texCoord = float2(
        input.texCoord.x * input.instanceUV.z + input.instanceUV.x,
        input.texCoord.y * input.instanceUV.w + input.instanceUV.y
    );
    return output;
}

/**
 * @brief Shader pikseli Sprite'ów.
 */
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    float4 tex = textures[baseColorTextureSet].Sample(globalSampler, input.texCoord);
    return float4(tex.rgb * input.color.rgb, tex.a * input.color.a);
}
