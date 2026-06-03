/**
 * @brief Struktura danych wejściowych shadera wierzchołków dla tekstu.
 */
struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;  // Pozycja w przestrzeni ekranu (NDC) z głębokością Z
    [[vk::location(1)]] float2 texCoord : TEXCOORD0; // Współrzędne UV określające pozycję glifu w atlasie czcionki
    [[vk::location(2)]] float4 color : COLOR;        // Kolor tekstu przekazany z wierzchołka
};

/**
 * @brief Dane przekazywane z shadera wierzchołków do shadera pikseli.
 */
struct VertexOutput
{
    float4 position : SV_Position;                   // Pozycja wyjściowa dla rasteryzatora Vulkana
    [[vk::location(0)]] float2 texCoord : TEXCOORD0; // Przekazane UV glifu
    [[vk::location(1)]] float4 color : COLOR;        // Przekazany kolor tekstu
};

struct FragmentInput
{
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
    [[vk::location(1)]] float4 color : COLOR;
};

// Zasoby tekstury i samplera powiązane w Vulkanie
Texture2D fontAtlasSampler : register(t0, space0);         // Atlas czcionki (zawiera maskę glifów)
SamplerState fontAtlasSamplerState : register(s0, space0); // Stan samplera (filtracja liniowa)

/**
 * @brief Shader wierzchołków (Vertex Shader).
 * 
 * Ponieważ współrzędne wierzchołków napisu są już obliczone na CPU w przestrzeni 
 * znormalizowanych współrzędnych ekranu NDC (Normalized Device Coordinates),
 * shader nie musi wykonywać żadnych transformacji macierzowych i przekazuje dane dalej.
 */
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position, 1.0); // X, Y, Z, W
    output.texCoord = input.texCoord;
    output.color = input.color;
    return output;
}

/**
 * @brief Shader pikseli (Fragment/Pixel Shader).
 * 
 * Odpowiada za pokolorowanie pikseli tekstu. Glify są zapisane w atlasie czcionek 
 * jako czarno-biała maska pokrycia (red channel). Odczytana wartość 'coverage' 
 * modyfikuje przezroczystość (kanał alfa) wyjściowego koloru tekstu.
 */
float4 fragmentMain(FragmentInput input) : SV_Target0
{
    // Odczytanie stopnia pokrycia piksela przez glif z kanału czerwonego (R)
    float coverage = fontAtlasSampler.Sample(fontAtlasSamplerState, input.texCoord).r;
    
    // Zwrócenie docelowego koloru z modulacją alfy
    return float4(input.color.rgb, input.color.a * coverage);
}
