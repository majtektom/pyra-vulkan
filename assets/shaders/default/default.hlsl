#include "shared/light_eval.hlsl"

// Globalne bufory jednolite (UBO) - dane współdzielone dla całej klatki
cbuffer GlobalUniformBufferObject : register(b0, space0)
{
    column_major float4x4 view;
    column_major float4x4 proj;
    float4 camRight;
    float4 camUp;
    float4 cameraPos;
    float4 ambientColor;
    float ambientStrength;
    uint lightingEnabled;
    uint lightCount;
    uint shadowsEnabled;
    LightData lights[16]; // Dane oświetlenia (maksymalnie 16 dynamicznych świateł na scenie)
    float4 fogColor;
    float fogStart;
    float fogEnd;
    float fogDensity;
    uint fogEnabled;
};

// Szybki bufor Push Constants powiązany z konkretnym renderowanym obiektem
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
Texture2D textures[] : register(t1, space0); // Tablica bindless tekstur

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
    [[vk::location(0)]] float3 color : COLOR;
    [[vk::location(1)]] float2 texCoord : TEXCOORD0;
    [[vk::location(2)]] float4 matColor : TEXCOORD1;
    [[vk::location(3)]] float3 worldPos : TEXCOORD2;
    [[vk::location(4)]] float3 worldNormal : TEXCOORD3;
};

// Pomocnicza konwersja macierzy 4x4 na 3x3 do transformacji wektora normalnego
float3x3 inverseTranspose3x3(float4x4 m)
{
    float3x3 a = float3x3(m[0].xyz, m[1].xyz, m[2].xyz);
    return a;
}

/**
 * @brief Shader wierzchołków (Vertex Shader).
 * 
 * Transformuje pozycję wierzchołka do przestrzeni ekranu (SV_Position) oraz 
 * wylicza wektor normalny i pozycję w przestrzeni świata (World Space), niezbędne 
 * do prawidłowego wyliczenia kierunków światła w shaderze fragmentów.
 */
VertexOutput vertexMain(VertexInput input)
{
    float4 worldPos4 = mul(model, float4(input.position, 1.0));
    VertexOutput output;
    output.position = mul(proj, mul(view, worldPos4));
    output.worldPos = worldPos4.xyz;
    
    // Transformacja wektora normalnego do przestrzeni świata.
    // normal wejściowy mnożymy przez macierz modelu bez przesunięcia (tylko rotacja/skala)
    output.worldNormal = normalize(mul(inverseTranspose3x3(model), input.normal));
    output.color = input.color;
    output.texCoord = input.texCoord;
    output.matColor = baseColorFactor;
    return output;
}

/**
 * @brief Shader pikseli (Fragment/Pixel Shader).
 * 
 * Wykonuje pełny rendering materiału bazując na standardzie PBR:
 * - Pobiera tekstury wejściowe (Albedo, Normal Map, Physical Descriptor, AO, Emissive).
 * - Rekonstruuje przestrzeń TBN (Tangent, Bitangent, Normal) za pomocą pochodnych ekranowych (ddx/ddy).
 * - Realizuje test przezroczystości (Alpha Cutout) na podstawie maski.
 * - Iteruje po źródłach światła, wyliczając ich wpływ za pomocą Cook-Torrance BRDF.
 */
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    // KROK 1: POBRANIE KOLORU BAZOWEGO (Albedo)
    float4 baseColor = textures[baseColorTextureSet].Sample(globalSampler, input.texCoord) * float4(input.color, 1.0) * input.matColor;

    // Test alfy (Alpha Cutout) - jeśli piksel ma przezroczystość niższą niż próg, jest natychmiast odrzucany
    if (baseColor.a < alphaMaskCutoff)
        discard;

    float3 N = normalize(input.worldNormal);
    
    // KROK 2: DYNAMICZNA REKONSTRUKCJA PRZESTRZENI TBN NA GPU (Bez przesyłania tangentów w VBO)
    // ddx i ddy obliczają różniczkę (zmianę) pozycji świata i współrzędnych UV pikseli 
    // w poziomie i w pionie dla sąsiadujących wątków na procesorze graficznym.
    // Pozwala to w locie obliczyć wektory styczne (Tangent T, Bitangent B) bezpośrednio w shaderze.
    float3 dp1 = ddx(input.worldPos);
    float3 dp2 = ddy(input.worldPos);
    float2 duv1 = ddx(input.texCoord);
    float2 duv2 = ddy(input.texCoord);

    if (bumpIntensity > 0.001)
    {
        // Odczyt wektora normalnego z mapy normalnych i rozpakowanie go z zakresu [0, 1] na [-1, 1]
        float3 normalTex = textures[normalTextureSet].Sample(globalSampler, input.texCoord).xyz * 2.0 - 1.0;
        normalTex.xy *= bumpIntensity; // Modulacja siły wybojów (bump intensity)
        normalTex = normalize(normalTex);

        // Rekonstrukcja układu współrzędnych stycznych (TBN Matrix)
        float r = 1.0 / (duv1.x * duv2.y - duv1.y * duv2.x + 0.0001);
        float3 T_raw = (dp1 * duv2.y - dp2 * duv1.y) * r;
        float3 T_proj = T_raw - N * dot(N, T_raw);
        float lenSq = dot(T_proj, T_proj);
        if (lenSq > 0.00001)
        {
            float3 T = T_proj / sqrt(lenSq);
            float3 B = cross(N, T);
            float3x3 TBN = float3x3(T, B, N);
            // Przeliczenie normalnej z przestrzeni stycznej (tangent space) do przestrzeni świata
            N = normalize(mul(normalTex, TBN));
        }
    }

    float3 color;
    if (lightingEnabled == 0)
    {
        color = baseColor.rgb;
    }
    else
    {
        // KROK 3: ODCZYT PARAMETRÓW FIZYCZNYCH PBR (Metallic, Roughness, Ambient Occlusion, Emissive)
        // glTF przechowuje parametry metaliczności i szorstkości w jednej teksturze:
        // zielony (G) = roughness, niebieski (B) = metallic.
        float3 metallicRoughness = textures[physicalDescriptorTextureSet].Sample(globalSampler, input.texCoord).bga;
        float metallic = metallicRoughness.x * metallicFactor;
        float roughness = max(metallicRoughness.y * roughnessFactor, 0.04); // Zabezpieczenie przed zbyt idealną gładkością

        float ao = textures[occlusionTextureSet].Sample(globalSampler, input.texCoord).r;      // Maska ocienienia otoczenia (Ambient Occlusion)
        float3 emissive = textures[emissiveTextureSet].Sample(globalSampler, input.texCoord).rgb; // Kolor samozjarzenia (Emissive)

        float3 V = normalize(cameraPos.xyz - input.worldPos); // Wektor do oka obserwatora (View vector)
        float NdotV = max(dot(N, V), 0.0);

        // KROK 4: WYLICZENIE ODBICIA BAZOWEGO (F0)
        // Dla dielektryków (plastik, drewno) F0 wynosi średnio 0.04. Dla metali F0 jest równe kolorowi albedo.
        float3 F0 = float3(0.04, 0.04, 0.04);
        F0 = lerp(F0, baseColor.rgb, metallic);

        // KROK 5: SUMOWANIE OŚWIETLENIA DLA WSZYSTKICH ŹRÓDEŁ ŚWIATŁA
        float3 Lo = float3(0.0, 0.0, 0.0);
        uint count = min(lightCount, 16u);
        for (uint i = 0; i < count; i++)
        {
            float3 L;
            float3 radiance;
            uint valid;
            
            // Wyznaczenie natężenia i kierunku padania światła
            unpackDirectLight(lights[i], input.worldPos, L, radiance, valid);
            if (valid == 0u) continue;

            // Cook-Torrance specular BRDF
            Lo += pbrDirectLight(N, V, baseColor.rgb, metallic, roughness, NdotV, F0, L, radiance);
        }

        // KROK 6: DODANIE ŚWIATŁA OTOCZENIA I SAMOZJARZENIA
        float3 ambientContrib = baseColor.rgb * ambientStrength * ambientColor.rgb * ao;
        color = ambientContrib + Lo + emissive;

        color = saturate(color); // Obcięcie do bezpiecznego zakresu [0.0, 1.0]
    }

    if (fogEnabled != 0u)
    {
        float dist = distance(cameraPos.xyz, input.worldPos);
        float fogFactor = (fogEnd - dist) / (fogEnd - fogStart);
        fogFactor = saturate(fogFactor);
        color = lerp(fogColor.rgb, color, fogFactor);
    }

    return float4(color, baseColor.a);
}
