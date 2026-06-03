#ifndef SHARED_SHADOW_PCF_HLSL
#define SHARED_SHADOW_PCF_HLSL

#include "shared/light_types.hlsl"

/**
 * @brief Próbkuje mapę cieni z filtrowaniem PCF (Percentage Closer Filtering).
 * 
 * PCF eliminuje ostre, schodkowane krawędzie cieni poprzez pobranie 9 próbek (siatka 3x3)
 * wokół docelowego punktu, a następnie wyciągnięcie średniej z wyników testu głębokości.
 * Wykorzystuje sprzętowy sampler porównujący (SamplerComparisonState) dla maksymalnej wydajności.
 * 
 * @param shadowMaps Tablica tekstur map cieni 2D (Texture2DArray).
 * @param shadowSampler Sampler porównujący (zwraca 1.0 jeśli punkt jest oświetlony, 0.0 jeśli w cieniu).
 * @param light Struktura danych źródła światła rzucającego cień.
 * @param worldPos Pozycja świata aktualnie cieniowanego piksela.
 * @param N Wektor normalny powierzchni w tym punkcie.
 * @return Współczynnik oświetlenia (1.0 = brak cienia, 0.0 = pełny cień, wartości pośrednie = półcień).
 */
float sampleShadowPcf(Texture2DArray<float> shadowMaps,
                      SamplerComparisonState shadowSampler,
                      LightData light,
                      float3 worldPos,
                      float3 N)
{
    // Wyznaczenie kierunku od piksela do światła (L)
    float3 L = (light.direction.w == 1.0)
        ? normalize(light.direction.xyz)
        : normalize(light.position.xyz - worldPos);

    // Dynamiczny bias (przesunięcie wzdłuż wektora normalnego) w celu eliminacji artefaktu "shadow acne"
    // (nakładania się cienia na geometrię z powodu skończonej precyzji zmiennoprzecinkowej bufora głębokości)
    float bias = max(0.01 * (1.0 - dot(N, L)), 0.002);
    
    // Przeliczenie pozycji świata piksela na przestrzeń projekcji światła (Light Space)
    float4 shadowPos = mul(light.lightViewProj, float4(worldPos + N * bias, 1.0));
    float3 projCoords = shadowPos.xyz / shadowPos.w; // Dzielenie przez w (perspektywa)
    
    // Transformacja współrzędnych NDC [-1.0, 1.0] na zakres tekstury [0.0, 1.0]
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // Obsługa punktów leżących poza stożkiem widoczności światła (brak cieniowania)
    if (projCoords.z > 1.0) return 1.0;
    if (projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) return 1.0;

    float shadow = 0.0;
    float2 texelSize = 1.0 / 2048.0; // Rozmiar teksela mapy cieni (dla tekstury o rozdzielczości 2048x2048)
    float layer = light.position.w;  // Indeks warstwy w tablicy tekstur przypisany do tego światła

    // Pętla pobierająca 9 próbek (siatka 3x3) wokół piksela docelowego
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            // SampleCmpLevelZero porównuje projCoords.z z wartością głębokości zapisaną w mapie cieni
            shadow += shadowMaps.SampleCmpLevelZero(
                shadowSampler,
                float3(projCoords.xy + float2(x, y) * texelSize, layer),
                projCoords.z);
        }
    }

    // Zwrócenie średniego współczynnika oświetlenia
    return shadow / 9.0;
}

#endif
