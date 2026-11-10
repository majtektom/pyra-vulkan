
#include "shared/surface_lighting.hlsl"   // oświetlenie i atmosfera - wspólne z shaderami forward
#include "shared/globals.hlsl"   // UBO klatki, tablica tekstur bindless i jej sampler (wspólne dla wszystkich shaderów)


// Powiązanie tekstur G-Buffera (Set 1 / space1)
[[vk::binding(0, 1)]] Texture2D<float4> gbufferAlbedo;             // Albedo + metalness
[[vk::binding(0, 1)]] SamplerState gbufferSampler;
[[vk::binding(1, 1)]] Texture2D<float4> gbufferNormal;             // Normal + roughness
[[vk::binding(2, 1)]] Texture2D<float4> gbufferOcclusion;          // AO + emissive strength
[[vk::binding(3, 1)]] Texture2D<float4> gbufferWorldPos;           // World Position + depth mask
[[vk::binding(4, 1)]] Texture2DArray<float> shadowMapArray;        // Tablica map cieni słońca
[[vk::binding(4, 1)]] SamplerComparisonState shadowSampler;

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

/**
 * @brief Vertex Shader dla drugiego kroku (Lighting Pass) - Fullscreen Triangle.
 * 
 * Zamiast przesyłać bufor wierzchołków dla prostokąta, używamy sztuczki 
 * generowania jednego wielkiego trójkąta zasłaniającego cały ekran 
 * na podstawie indeksu wierzchołka (SV_VertexID):
 * - Wierzchołek 0: UV (0, 0), Pos (-1, -1)
 * - Wierzchołek 1: UV (2, 0), Pos (3, -1)
 * - Wierzchołek 2: UV (0, 2), Pos (-1, 3)
 * Karta graficzna automatycznie przycina trójkąt do obszaru ekranu.
 */
VertexOutput vertexMain(uint vid : SV_VertexID)
{
    VertexOutput o;
    o.uv = float2((vid << 1) & 2, vid & 2);
    o.position = float4(o.uv * float2(2.0, 2.0) + float2(-1.0, -1.0), 0.0, 1.0);
    return o;
}

/**
 * @brief Fragment Shader dla drugiego kroku (Lighting Pass).
 * 
 * Odczytuje parametry fizyczne pikseli z tekstur G-Buffera i wykonuje
 * obliczenia oświetlenia PBR oraz próbkowania cieni.
 */
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    // KROK 1: ODCZYT POWIERZCHNI Z G-BUFFERA (zapisanej przez packGBuffer)
    float4 albedoMetallic = gbufferAlbedo.Sample(gbufferSampler, input.uv);
    float4 normalRoughness = gbufferNormal.Sample(gbufferSampler, input.uv);
    float4 occlusionEmissive = gbufferOcclusion.Sample(gbufferSampler, input.uv);
    float4 worldPosData = gbufferWorldPos.Sample(gbufferSampler, input.uv);

    // W = 0: w tym pikselu nie ma geometrii (tło / niebo) - nic do oświetlenia.
    if (worldPosData.w < 0.5) discard;

    SurfaceData s;
    s.albedo = albedoMetallic.rgb;
    s.alpha = 1.0;
    s.metallic = albedoMetallic.a;
    s.roughness = normalRoughness.a;
    s.ao = occlusionEmissive.r;
    s.emissive = occlusionEmissive.gba;
    s.normal = normalize(normalRoughness.rgb * 2.0 - 1.0);   // z [0, 1] z powrotem na [-1, 1]
    s.worldPos = worldPosData.xyz;

    // KROK 2: OŚWIETLENIE (wszystkie światła, cienie, otoczenie) I ATMOSFERA (mgła, mgła wojny) - wspólne funkcje.
    float3 color = applyAtmosphere(shadeSurface(s, shadowMapArray, shadowSampler), s.worldPos);
    return float4(color, 1.0);
}
