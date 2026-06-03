cbuffer GlobalUniformBufferObject : register(b0, space0)
{
    column_major float4x4 view;
    column_major float4x4 proj;
    float4 camRight; // Prawy wektor kamery w przestrzeni świata (Camera Right Vector)
    float4 camUp;    // Górny wektor kamery w przestrzeni świata (Camera Up Vector)
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
    // Lokalna geometria quada cząsteczki (np. [-0.5, 0.5])
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float3 color : COLOR;
    [[vk::location(3)]] float2 texCoord : TEXCOORD0;
    
    // Dane instancji (unikalne dla każdej cząsteczki pobierane z bufora instancji):
    [[vk::location(4)]] float4 instancePosSize : TEXCOORD1; // xyz = pozycja świata, w = skala rozmiaru
    [[vk::location(5)]] float4 instanceColor : TEXCOORD2;   // rgb = kolor, a = alfa (zanikanie w czasie)
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color : COLOR;
    [[vk::location(1)]] float2 texCoord : TEXCOORD0;
};

/**
 * @brief Shader wierzchołków cząsteczek (GPU Instanced Billboarding).
 * 
 * Zamiast obliczać obroty quada na CPU (co zabija wydajność procesora), rozszerzamy
 * wierzchołki płaskiego quada jednostkowego w przestrzeni świata wzdłuż osi kamery:
 * Right (prawa) oraz Up (góra). Sprawia to, że cząsteczka niezależnie od pozycji
 * sferycznej zawsze idealnie "patrzy" w stronę oka obiektywu (Billboarding).
 */
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    
    // Przeliczenie billboardu:
    // wektor_swiata = centrum_swiata + (lokalne_x * rozmiar * camRight) + (lokalne_y * rozmiar * camUp)
    float3 worldPos = input.instancePosSize.xyz
                    + input.position.x * input.instancePosSize.w * camRight.xyz
                    + input.position.y * input.instancePosSize.w * camUp.xyz;
                    
    output.position = mul(proj, mul(view, float4(worldPos, 1.0)));
    output.color = input.instanceColor.rgb;
    output.texCoord = input.texCoord;
    return output;
}

/**
 * @brief Shader pikseli cząsteczek.
 * 
 * Próbkuje teksturę cząsteczki (np. z przejściem tonalnym chmury dymu/iskry) 
 * i moduluje ją kolorem wyliczonym z czasu życia cząsteczki.
 */
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    float4 tex = textures[baseColorTextureSet].Sample(globalSampler, input.texCoord);
    return float4(tex.rgb * input.color, tex.a);
}
