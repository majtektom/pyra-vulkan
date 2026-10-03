#include "shared/light_types.hlsl"

cbuffer GlobalUniformBufferObject : register(b0)
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

// Parametry oceanu przesyłane co klatkę
[[vk::push_constant]]
cbuffer OceanPushConstants
{
    float4 cameraPosition;
    float time;             // Czas animacji fal
    float waterLevel;       // Poziom bazowy wody (wysokość Y)
    float waveAmplitude;    // Amplituda fal (wysokość)
    float waveSpeed;        // Prędkość poruszania się fal
    float waveFrequency;    // Częstotliwość (gęstość falowania)
    float foamThreshold;    // Próg tworzenia się piany na grzbietach fal
    float sceneColorIndex;
    float sceneDepthIndex;
    float dudvMapIndex;
    float4 lightDir;        // Kierunek słońca (do odblasków specularity)
};

SamplerState globalSampler : register(s1, space0);
Texture2D textures[] : register(t1, space0);

float3 sceneWorldPosition(float2 uv, uint depthTextureIndex, out bool valid)
{
    float depth = textures[depthTextureIndex].SampleLevel(globalSampler, uv, 0).r;
    valid = depth < 0.99999;
    float2 ndc = uv * 2.0 - 1.0;
    float4 world = mul(invViewProj, float4(ndc, depth, 1.0));
    return world.xyz / max(world.w, 0.00001);
}

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
    [[vk::location(0)]] float3 worldPos : TEXCOORD0;
    [[vk::location(1)]] float3 normal : TEXCOORD1;
    [[vk::location(2)]] float height : TEXCOORD2;
    [[vk::location(3)]] float2 texCoord : TEXCOORD3;
};

struct FragmentInput
{
    [[vk::location(0)]] float3 worldPos : TEXCOORD0;
    [[vk::location(1)]] float3 normal : TEXCOORD1;
    [[vk::location(2)]] float height : TEXCOORD2;
    [[vk::location(3)]] float2 texCoord : TEXCOORD3;
};

/**
 * @brief Oblicza przesunięcie fali Gerstnera (Gerstner Wave).
 * 
 * Fale Gerstnera są fizycznie wierniejszym modelem fal morskich niż proste fale sinusoidalne.
 * W przeciwieństwie do sinusoid (które dają zaokrąglone grzbiety fal), fale Gerstnera 
 * przesuwają wierzchołki również poziomo w kierunku grzbietu. Powoduje to powstawanie 
 * charakterystycznych, ostrych i stromych grzbietów fal.
 * 
 * Funkcja aktualizuje wektory styczne (tangent i binormal) w celu rekonstrukcji normalnej fali na GPU.
 */
float3 gerstnerWave(float3 p, float2 dir, float steepness, float waveLen, float amp, float speed, inout float3 tangent, inout float3 binormal)
{
    float freq = 6.283185307 / waveLen; // Liczba falowa (k)
    float phase = freq * dot(dir, p.xz) + time * speed; // Faza fali
    float c = cos(phase);
    float s = sin(phase);

    // Wyliczenie pochodnych cząstkowych dla wektora stycznego (tangent) i dwustycznego (binormal)
    tangent += float3(-dir.x * dir.x * steepness * s, dir.x * steepness * c, -dir.x * dir.y * steepness * s);
    binormal += float3(-dir.x * dir.y * steepness * s, dir.y * steepness * c, -dir.y * dir.y * steepness * s);

    // Zwrócenie przesunięcia wierzchołka (X, Y, Z)
    return float3(dir.x * steepness * c, amp * s, dir.y * steepness * c);
}

/**
 * @brief Shader wierzchołków oceanu (Gerstner Waves displacement).
 * 
 * Nakłada na siebie 5 niezależnych fal Gerstnera o różnych kierunkach,
 * częstotliwościach i prędkościach, tworząc chaotyczną i naturalną powierzchnię morza.
 * Normalna fali jest wyliczana poprzez iloczyn wektorowy zaktualizowanych wektorów stycznych.
 */
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;

    float3 p = input.position;
    float3 tangent = float3(1, 0, 0);  // Kierunek styczny początkowy
    float3 binormal = float3(0, 0, 1); // Kierunek dwustyczny początkowy
    float3 displaced = float3(0, 0, 0);

    // Sumowanie 5 fal Gerstnera o malejących amplitudach i rosnących prędkościach (fraktalność fal)
    displaced += gerstnerWave(p, float2(0.7, 0.7), 0.3, 40.0 / waveFrequency, 1.5 * waveAmplitude, 0.6 * waveSpeed, tangent, binormal);
    displaced += gerstnerWave(p, float2(-0.3, 0.9), 0.2, 25.0 / waveFrequency, 1.0 * waveAmplitude, 0.8 * waveSpeed, tangent, binormal);
    displaced += gerstnerWave(p, float2(0.5, -0.5), 0.15, 15.0 / waveFrequency, 0.6 * waveAmplitude, 1.2 * waveSpeed, tangent, binormal);
    displaced += gerstnerWave(p, float2(-0.8, 0.2), 0.1, 10.0 / waveFrequency, 0.3 * waveAmplitude, 1.5 * waveSpeed, tangent, binormal);
    displaced += gerstnerWave(p, float2(0.2, 0.85), 0.05, 6.0 / waveFrequency, 0.15 * waveAmplitude, 2.0 * waveSpeed, tangent, binormal);

    p.y = waterLevel; // Ustawienie bazowego poziomu morza
    float3 worldPos = p + displaced; // Nałożenie przesunięć falowych
    
    output.position = mul(proj, mul(view, float4(worldPos, 1.0)));
    output.worldPos = worldPos;

    // Rekonstrukcja wektora normalnego (N) jako iloczyn wektorowy stycznej i dwustycznej
    float3 n = normalize(cross(binormal, tangent));
    output.normal = n;

    output.height = displaced.y; // Zapisujemy wysokość fali w celu wyliczenia piany w pixel shaderze
    output.texCoord = input.texCoord;

    return output;
}

/**
 * @brief Shader pikseli oceanu.
 * 
 * Oblicza optykę wody:
 * - Efekt Fresnela (woda oglądana pod płaskim kątem odbija niebo, z góry widać głębię).
 * - Odbicie nieba (Sky reflection).
 * - Odbicie słońca (Sun Specularity) - wysoki wykładnik potęgi daje ostre refleksy.
 * - Piana morska na szczytach fal (Foam).
 */
float4 fragmentMain(FragmentInput input) : SV_Target0
{
    float3 N = normalize(input.normal);
    float3 L = normalize(lightDir.xyz);
    float3 V = normalize(cameraPosition.xyz - input.worldPos);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    // KROK 1: EFEKT FRESNELA (Przejście między odbiciem nieba a przezroczystością toni wodnej)
    // Lekko podbity F0 względem wartości fizycznej daje czytelniejszą taflę
    // przy typowych kamerach growych, które patrzą na wodę z góry.
    float fresnel = 0.055 + 0.945 * pow(1.0 - NdotV, 5.0);

    // Screen UV of the current water pixel
    float4 clipPos = mul(proj, mul(view, float4(input.worldPos, 1.0)));
    float2 screenUV = clipPos.xy / clipPos.w * float2(0.5, 0.5) + 0.5;

    // KROK 2: SUMOWANIE KOLORU GŁĘBI I PŁYCIZNY (Water body absorption)
    uint sceneIdx = (uint)sceneColorIndex;
    uint depthIdx = (uint)sceneDepthIndex;
    uint dudvIdx = (uint)dudvMapIndex;

    // Animated dUdV distortion (two layers for complex wave motion)
    float2 dudvUV1 = input.texCoord * 0.06 + float2(time * 0.004, time * 0.002);
    float2 dudvUV2 = input.texCoord * 0.1 + float2(time * -0.003, time * 0.005);
    float2 dudv1 = textures[dudvIdx].Sample(globalSampler, dudvUV1).rg;
    float2 dudv2 = textures[dudvIdx].Sample(globalSampler, dudvUV2).rg;
    float2 dudvOffset = ((dudv1 + dudv2) * 0.5 - 0.5) * 0.035;
    dudvOffset += (N.xz * 0.012);

    // Refrakcja próbuje zakrzywiony UV, ale wraca do UV bez przesunięcia,
    // gdy próbka wychodzi ponad taflę (zapobiega wciąganiu brzegu pod wodę).
    float2 refractUV = saturate(screenUV + dudvOffset * (0.35 + 0.65 * (1.0 - NdotV)));
    bool validBottom;
    float3 bottomPos = sceneWorldPosition(refractUV, depthIdx, validBottom);
    validBottom = validBottom && bottomPos.y < waterLevel;
    if (!validBottom)
    {
        refractUV = screenUV;
        bottomPos = sceneWorldPosition(refractUV, depthIdx, validBottom);
        validBottom = validBottom && bottomPos.y < waterLevel;
    }

    float waterDepth = validBottom ? distance(input.worldPos, bottomPos) : 80.0;
    waterDepth = clamp(waterDepth, 0.0, 120.0);
    float3 refractColor = textures[sceneIdx].SampleLevel(globalSampler, refractUV, 0).rgb;
    float3 absorption = float3(0.018, 0.008, 0.004);
    float3 transmittance = exp(-absorption * waterDepth);
    float3 deepScatter = float3(0.008, 0.11, 0.16);
    float3 refractionFinal = refractColor * transmittance
        + deepScatter * (1.0 - transmittance);

    // Odbicie ekranowe: promień odbity jest porównywany bezpośrednio
    // z pozycją świata zapisaną w G-bufferze.
    float3 incident = normalize(input.worldPos - cameraPosition.xyz);
    float3 reflectionRay = normalize(reflect(incident, N));
    float2 reflectionUV = screenUV;
    float reflectionConfidence = 0.0;
    float3 previousRayPosition = input.worldPos + reflectionRay * 0.5;
    float previousDepthDelta = -100000.0;

    [loop] for (int step = 0; step < 72; ++step)
    {
        float distanceAlongRay = 2.0 + step * step * 0.22;
        float3 rayPosition = input.worldPos + reflectionRay * distanceAlongRay;
        float4 rayClip = mul(proj, mul(view, float4(rayPosition, 1.0)));
        if (rayClip.w <= 0.0) break;

        float2 rayUV = rayClip.xy / rayClip.w * 0.5 + 0.5;
        if (any(rayUV <= 0.002) || any(rayUV >= 0.998)) break;

        bool validScenePosition;
        float3 scenePosition = sceneWorldPosition(rayUV, depthIdx, validScenePosition);
        if (validScenePosition && scenePosition.y > waterLevel + waveAmplitude * 0.25)
        {
            float rayViewDepth = -mul(view, float4(rayPosition, 1.0)).z;
            float sceneViewDepth = -mul(view, float4(scenePosition, 1.0)).z;
            float depthDelta = rayViewDepth - sceneViewDepth;
            float hitThickness = 4.0 + distanceAlongRay * 0.035;
            if (depthDelta >= 0.0 && (previousDepthDelta < 0.0 || depthDelta < hitThickness))
            {
                float3 low = previousRayPosition;
                float3 high = rayPosition;
                [unroll] for (int refinement = 0; refinement < 7; ++refinement)
                {
                    float3 middle = (low + high) * 0.5;
                    float4 middleClip = mul(proj, mul(view, float4(middle, 1.0)));
                    float2 middleUV = middleClip.xy / middleClip.w * 0.5 + 0.5;
                    bool validMiddle;
                    float3 middleScene = sceneWorldPosition(middleUV, depthIdx, validMiddle);
                    float middleRayDepth = -mul(view, float4(middle, 1.0)).z;
                    float middleSceneDepth = -mul(view, float4(middleScene, 1.0)).z;
                    if (validMiddle && middleRayDepth >= middleSceneDepth)
                        high = middle;
                    else
                        low = middle;
                }

                float4 hitClip = mul(proj, mul(view, float4(high, 1.0)));
                float2 hitUV = hitClip.xy / hitClip.w * 0.5 + 0.5;
                bool validHit;
                float3 hitScene = sceneWorldPosition(hitUV, depthIdx, validHit);
                if (validHit && hitScene.y > waterLevel + waveAmplitude * 0.25)
                {
                    reflectionUV = hitUV + dudvOffset * 0.12;
                    float edge = max(abs(hitUV.x - 0.5), abs(hitUV.y - 0.5)) * 2.0;
                    reflectionConfidence = 1.0 - smoothstep(0.68, 0.98, edge);
                    break;
                }
            }
            previousDepthDelta = depthDelta;
        }
        previousRayPosition = rayPosition;
    }

    float2 fallbackReflectionUV = screenUV;
    float fallbackReflectionConfidence = 0.0;
    float4 fallbackClip = mul(proj, mul(view, float4(input.worldPos + reflectionRay * 90.0, 1.0)));
    if (fallbackClip.w > 0.0)
    {
        float2 projectedFallbackUV = fallbackClip.xy / fallbackClip.w * 0.5 + 0.5;
        bool uvInBounds = all(projectedFallbackUV > 0.01) && all(projectedFallbackUV < 0.99);
        if (uvInBounds)
        {
            bool validFallbackScene;
            float3 fallbackScenePos = sceneWorldPosition(projectedFallbackUV, depthIdx, validFallbackScene);
            if (validFallbackScene && fallbackScenePos.y > waterLevel - 0.5)
            {
                fallbackReflectionUV = projectedFallbackUV + dudvOffset * 0.08;
                float shorelineBoost = 1.0 - smoothstep(10.0, 42.0, waterDepth);
                fallbackReflectionConfidence = shorelineBoost * (1.0 - reflectionConfidence);
            }
        }
    }

    float3 skyReflection = lerp(
        ambientColor.rgb * 0.7,
        float3(0.32, 0.52, 0.72),
        saturate(reflectionRay.y));
    float3 reflectedScene = textures[sceneIdx].SampleLevel(
        globalSampler, saturate(reflectionUV), 0).rgb;
    float3 fallbackReflectedScene = textures[sceneIdx].SampleLevel(
        globalSampler, saturate(fallbackReflectionUV), 0).rgb;
    float3 sceneReflection = lerp(
        fallbackReflectedScene,
        reflectedScene,
        reflectionConfidence);
    float sceneReflectionConfidence = saturate(reflectionConfidence + fallbackReflectionConfidence);
    float3 reflectColor = lerp(skyReflection, sceneReflection, sceneReflectionConfidence);
    float reflectionAmount = saturate(0.12 + fresnel * 0.9 + reflectionConfidence * 0.2);
    float3 result = lerp(refractionFinal, reflectColor, reflectionAmount);

    // Specular highlights (sun reflections on wave facets)
    float3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    // Wykładnik 512.0 daje małe, ostre "iskierki" słońca na powierzchni fal
    float spec = pow(NdotH, 512.0) * 0.9 + pow(NdotH, 64.0) * 0.3;
    float3 sunSpec = float3(1.0, 0.9, 0.7) * spec * fresnel;

    // KROK 5: PODPOWIERZCHNIOWE ROZPRASZANIE ŚWIATŁA (Sub-surface scattering)
    float3 subSurf = deepScatter * 0.3 * NdotL * (1.0 - fresnel);

    // KROK 6: RYSOWANIE PIANY NA GRZBIETACH FAL (Foam rendering)
    // Jeśli fala jest odpowiednio wysoko (displaced.y > foamThreshold), nanosimy pianę
    float foam = smoothstep(foamThreshold, foamThreshold + 0.4, input.height);
    float3 foamColor = float3(0.92, 0.94, 0.96);

    // Kompozycja końcowa piksela wody
    result += sunSpec * 0.8;
    result += subSurf;
    result = lerp(result, foamColor, foam * 0.4); // Nałożenie piany na grzbiety fal

    if (fogEnabled != 0u)
    {
        float dist = distance(cameraPos.xyz, input.worldPos);
        float fogFactor = (fogEnd - dist) / (fogEnd - fogStart);
        fogFactor = saturate(fogFactor);
        result = lerp(fogColor.rgb, result, fogFactor);
    }

    return float4(result, 1.0);
}
