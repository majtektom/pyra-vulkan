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
float sampleShadowCascade(Texture2DArray<float> shadowMaps,
                          SamplerComparisonState shadowSampler,
                          LightData light,
                          float3 worldPos,
                          float3 N,
                          uint cascadeIndex)
{
    // Wyznaczenie kierunku od piksela do światła (L)
    float3 L = (light.direction.w == 1.0)
        ? normalize(light.direction.xyz)
        : normalize(light.position.xyz - worldPos);

    uint shadowWidth = 0;
    uint shadowHeight = 0;
    uint shadowLayers = 0;
    shadowMaps.GetDimensions(shadowWidth, shadowHeight, shadowLayers);

    float4x4 lightMatrix = light.lightViewProj[cascadeIndex];
    float ndotl = saturate(dot(N, L));
    float3 samplePos = worldPos;
    float bias;

    if (light.direction.w == 1.0)
    {
        // SŁOŃCE (rzut ortogonalny, kaskady).
        // Rozmiar teksela mapy cieni w jednostkach świata - odczytany z macierzy kaskady.
        // Wiersz 0 macierzy ortho*view to (1/r) * oś X światła, więc 1/|wiersz0| = r (połowa szerokości kaskady).
        // Dla kaskad dalekich teksel jest duży, dla bliskich mały - bias musi się z nim skalować.
        float rowLen = length(lightMatrix[0].xyz);
        float texelWorld = (rowLen > 1e-6) ? 2.0 / (rowLen * float(shadowWidth)) : 0.05;

        // Normal offset: próbkujemy mapę cieni z punktu odsuniętego od powierzchni wzdłuż normalnej
        // o 1,5-4 teksele (więcej dla powierzchni ustawionych bokiem do światła). To usuwa
        // "shadow acne" - paski, w których powierzchnia zacienia samą siebie.
        float normalOffset = texelWorld * (1.5 + 2.5 * (1.0 - ndotl));
        samplePos = worldPos + N * normalOffset;

        // Bias głębokości w jednostkach teksela (wiersz 2 = skala głębokości NDC na jednostkę świata).
        // Rośnie dla powierzchni ustawionych bokiem do światła, bo próbki PCF obok środka trafiają
        // wtedy w wyraźnie inną głębokość powierzchni.
        float slope = sqrt(max(1.0 - ndotl * ndotl, 0.0)) / max(ndotl, 0.2);   // tan(kąta padania), ograniczony
        bias = texelWorld * (0.6 + 0.6 * slope) * length(lightMatrix[2].xyz);
    }
    else
    {
        // Reflektor / światło punktowe (rzut perspektywiczny): stały bias zależny od kąta padania.
        bias = max(0.0008, 0.0025 * (1.0 - ndotl));
    }

    // Przeliczenie pozycji świata piksela na przestrzeń projekcji światła (Light Space)
    float4 shadowPos = mul(lightMatrix, float4(samplePos, 1.0));
    float3 projCoords = shadowPos.xyz / shadowPos.w; // Dzielenie przez w (perspektywa)
    
    // Transformacja współrzędnych NDC [-1.0, 1.0] na zakres tekstury [0.0, 1.0]
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // Obsługa punktów leżących poza stożkiem widoczności światła (brak cieniowania)
    if (projCoords.z > 1.0) return 1.0;
    if (projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) return 1.0;

    float shadow = 0.0;
    float totalWeight = 0.0;
    float2 texelSize = 1.0 / float2(shadowWidth, shadowHeight);
    float filterRadius = 1.0;   // odstęp próbek w tekselach (sampler sprzętowo uśrednia jeszcze 2x2)
    float layer;
    float2 uv = projCoords.xy;
    float2 uvMin = float2(0.0, 0.0);
    float2 uvMax = float2(1.0, 1.0);

    if (light.direction.w == 1.0)
    {
        // Słońce: każda kaskada ma własną warstwę (position.w = pierwsza warstwa).
        layer = light.position.w + cascadeIndex;
    }
    else
    {
        // Światło lokalne: ćwiartka atlasu (params.w = numer ćwiartki 0..3: kolumna = slot % 2, wiersz = slot / 2).
        layer = light.position.w;
        uint slot = (uint)round(light.params.w);
        float2 atlasOrigin = float2(float(slot % 2u), float(slot / 2u)) * 0.5;
        uv = atlasOrigin + projCoords.xy * 0.5;
        // Próbki PCF nie mogą wyjść poza ćwiartkę (tam jest cień innego światła).
        uvMin = atlasOrigin + texelSize;
        uvMax = atlasOrigin + 0.5 - texelSize;
    }

    // 3x3 tent PCF daje miększą krawędź bez dużego wzrostu kosztu.
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float2 offset = float2(x, y) * texelSize * filterRadius;
            float weight = (2.0 - abs((float)x)) * (2.0 - abs((float)y));
            shadow += shadowMaps.SampleCmpLevelZero(
                shadowSampler,
                float3(clamp(uv + offset, uvMin, uvMax), layer),
                projCoords.z - bias) * weight;
            totalWeight += weight;
        }
    }

    return shadow / max(totalWeight, 0.0001);
}

float sampleShadowPcf(Texture2DArray<float> shadowMaps,
                      SamplerComparisonState shadowSampler,
                      LightData light,
                      float3 worldPos,
                      float3 N)
{
    // Światło bez przydzielonej mapy cieni (za daleko lub poza limitem świateł z cieniem) - oświetla bez cienia.
    if (light.position.w < 0.0) return 1.0;

    uint cascadeIndex = 0u;
    // params.w: dla słońca liczba kaskad, dla świateł lokalnych numer ćwiartki atlasu (kaskada zawsze jedna).
    uint cascadeCount = (light.direction.w == 1.0) ? max(1u, (uint)round(light.params.w)) : 1u;
    float viewDepth = 0.0;

    if (light.direction.w == 1.0 && directionalShadowCascadeCount > 1u)
    {
        viewDepth = abs(mul(view, float4(worldPos, 1.0)).z);
        cascadeCount = min(cascadeCount, directionalShadowCascadeCount);
        [unroll]
        for (uint i = 0u; i + 1u < 4u; ++i)
        {
            if (i + 1u >= cascadeCount) break;
            cascadeIndex += (viewDepth > directionalShadowSplits[i]) ? 1u : 0u;
        }
        cascadeIndex = min(cascadeIndex, cascadeCount - 1u);
    }

    float shadow = sampleShadowCascade(
        shadowMaps, shadowSampler, light, worldPos, N, cascadeIndex);

    // Mieszanie ostatnich 10% kaskady usuwa widoczne przeskoki rozdzielczości.
    if (light.direction.w == 1.0 && cascadeIndex + 1u < cascadeCount)
    {
        float splitNear = cascadeIndex == 0u ? 0.0 : directionalShadowSplits[cascadeIndex - 1u];
        float splitFar = directionalShadowSplits[cascadeIndex];
        float blendStart = lerp(splitNear, splitFar, 0.9);
        float blend = saturate((viewDepth - blendStart) / max(splitFar - blendStart, 0.0001));
        if (blend > 0.0)
        {
            float nextShadow = sampleShadowCascade(
                shadowMaps, shadowSampler, light, worldPos, N, cascadeIndex + 1u);
            shadow = lerp(shadow, nextShadow, blend);
        }
    }

    return shadow;
}

#endif
