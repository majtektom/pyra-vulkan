#include "shared/light_eval.hlsl"

// Globalny bufor jednolity (UBO) - dane widoku i światła
cbuffer GlobalUniformBufferObject : register(b0, space0)
{
    column_major float4x4 view;
    column_major float4x4 proj;
    column_major float4x4 invViewProj;
    float4 camRight;
    float4 camUp;
    float4 cameraPos;
    float4 ambientColor;
    float ambientStrength;
    uint lightingEnabled;
    uint lightCount;
    uint shadowsEnabled;
    LightData lights[16];
    float4 directionalShadowSplits;
    uint directionalShadowCascadeCount;
    float3 shadowPadding;
    float4 fogColor;
    float fogStart;
    float fogEnd;
    float fogDensity;
    uint fogEnabled;
};

#include "shared/shadow_pcf.hlsl"

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
    // KROK 1: ODCZYT PARAMETRÓW GEOMETRII Z G-BUFFERA
    float4 albedoMetallic = gbufferAlbedo.Sample(gbufferSampler, input.uv);
    float4 normalRoughness = gbufferNormal.Sample(gbufferSampler, input.uv);
    float4 occlusionEmissive = gbufferOcclusion.Sample(gbufferSampler, input.uv);
    float4 worldPosData = gbufferWorldPos.Sample(gbufferSampler, input.uv);

    float3 baseColor = albedoMetallic.rgb;
    float metallic = albedoMetallic.a;
    float roughness = max(normalRoughness.a, 0.04);
    
    // Przywrócenie wektora normalnego z zakresu [0.0, 1.0] z powrotem na [-1.0, 1.0]
    float3 N = normalize(normalRoughness.rgb * 2.0 - 1.0);
    
    float ao = occlusionEmissive.r;
    float3 emissive = occlusionEmissive.gba;
    float3 worldPos = worldPosData.xyz;

    // Jeżeli kanał W pozycji świata jest mniejszy niż 0.5, oznacza to, 
    // że w tym pikselu nie ma żadnego obiektu (tło/skybox). Odrzucamy ten fragment,
    // ponieważ nie wymaga on kalkulacji oświetlenia.
    if (worldPosData.w < 0.5) discard;

    float3 color;
    if (lightingEnabled == 0)
    {
        color = baseColor;
        if (shadowsEnabled != 0u && lightCount > 0u)
        {
            // Bez oświetlenia: cień tylko przyciemnia teksturę, a jego siła zależy od siły światła
            // (w nocy słońce ma siłę 0 - nie ma cieni).
            float shadow = sampleShadowPcf(shadowMapArray, shadowSampler, lights[0], worldPos, N);
            float strength = saturate(lights[0].color.w * 0.5);
            color = baseColor * lerp(1.0, lerp(0.35, 1.0, shadow), strength);
        }
    }
    else
    {
        float3 V = normalize(cameraPos.xyz - worldPos);
        float NdotV = max(dot(N, V), 0.0);
        float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);

        float3 Lo = float3(0.0, 0.0, 0.0);
        uint count = min(lightCount, 16u);

        // KROK 2: OBLICZANIE WPŁYWU DLA WSZYSTKICH ŹRÓDEŁ ŚWIATŁA (w jednym shaderze!)
        // W odróżnieniu od forward renderingu, nie ma tu problemu z overdrawem (wielokrotnym
        // uruchamianiem cieniowania pikseli dla nakładającej się geometrii).
        for (uint i = 0; i < count; i++)
        {
            float3 L;
            float3 radiance;
            uint valid;
            
            unpackDirectLight(lights[i], worldPos, L, radiance, valid);
            if (valid == 0u) continue;

            // Próbkowanie mapy cieni z PCF
            float shadow = (shadowsEnabled != 0) ? sampleShadowPcf(shadowMapArray, shadowSampler, lights[i], worldPos, N) : 1.0;
            
            // Sumowanie oświetlenia Cook-Torrance
            Lo += pbrDirectLight(N, V, baseColor, metallic, roughness, NdotV, F0, L, radiance) * shadow;
        }

        // KROK 3: DODANIE ŚWIATŁA OTOCZENIA I SAMOZJARZENIA
        float3 ambientContrib = baseColor * ambientStrength * ambientColor.rgb * ao;
        color = ambientContrib + Lo + emissive;
        color = saturate(color);
    }

    if (fogEnabled != 0u)
    {
        float dist = distance(cameraPos.xyz, worldPos);
        float fogFactor = (fogEnd - dist) / (fogEnd - fogStart);
        fogFactor = saturate(fogFactor);
        color = lerp(fogColor.rgb, color, fogFactor);
    }

    return float4(color, 1.0);
}
