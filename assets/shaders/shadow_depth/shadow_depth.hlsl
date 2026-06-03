// Push Constants przekazywane przy rysowaniu obiektów do mapy cieni
[[vk::push_constant]]
cbuffer PushConstants
{
    column_major float4x4 model;         // Macierz transformacji obiektu
    column_major float4x4 lightViewProj; // Macierz widoku-projekcji światła rzucającego cień
    float alphaCutoff;                   // Próg przezroczystości (dla odrzucania liści itp.)
    uint baseColorTextureSet;            // Indeks tekstury albedo w tablicy bindless
    float2 _pad;
};

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(3)]] float2 texCoord : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
};

SamplerState globalSampler : register(s1, space0);
Texture2D textures[] : register(t1, space0);

/**
 * @brief Shader wierzchołków dla generowania mapy cieni (Shadow Pass VS).
 * 
 * Transformuje wierzchołki ze współrzędnych lokalnych obiektu bezpośrednio 
 * do przestrzeni rzutowania światła (Light Space).
 */
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    float4 worldPos = mul(model, float4(input.position, 1.0));
    output.position = mul(lightViewProj, worldPos);
    output.texCoord = input.texCoord;
    return output;
}

/**
 * @brief Shader pikseli dla generowania mapy cieni (Shadow Pass PS).
 * 
 * Normalnie, generowanie mapy głębokości (Depth Pass) nie wymaga shadera pikseli, 
 * ponieważ sprzęt automatycznie zapisuje wartość Z bufora. 
 * Jednak dla materiałów z maską przezroczystości (np. liście drzew, trawy, siatka płotu), 
 * musimy odczytać kanał alfa tekstury i odrzucić przezroczyste piksele (discard). 
 * W przeciwnym razie cień liści byłby litym, brzydkim kwadratem!
 */
void fragmentMain(VertexOutput input)
{
    if (alphaCutoff > 0.0)
    {
        // Pobranie wartości alfa z tekstury
        float alpha = textures[baseColorTextureSet].Sample(globalSampler, input.texCoord).a;
        
        // Odrzucenie piksela (brak zapisu głębokości do mapy cieni), jeśli jest przezroczysty
        if (alpha < alphaCutoff)
            discard;
    }
}
