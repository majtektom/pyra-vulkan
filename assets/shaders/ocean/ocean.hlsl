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
    float fresnel = pow(1.0 - NdotV, 4.0);

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
    float2 dudvOffset = ((dudv1 + dudv2) * 0.5 - 0.5) * 0.06;
    dudvOffset += ((dudv1 * 2.0 - 1.0) * 0.02 + (dudv2 * 2.0 - 1.0) * 0.015) * input.height * 0.2;

    // --- REFLECTION: SSR with fallback ---
    // Default reflection: perspective-correct fallback using virtual reflection plane
    float3 viewDir = normalize(input.worldPos - cameraPos.xyz);
    float3 R_flat = reflect(viewDir, float3(0.0, 1.0, 0.0));
    
    float H_cam = max(0.1, cameraPos.y - waterLevel);
    float H_refl = clamp(H_cam * 0.15, .0, 8.0);
    
    float t_refl = (waterLevel + H_refl - input.worldPos.y) / max(0.0001, R_flat.y);
    float3 P_refl = input.worldPos + max(0.0, t_refl) * R_flat;
    
    float4 reflClip = mul(proj, mul(view, float4(P_refl, 1.0)));
    float2 reflectUV = reflClip.xy / max(0.0001, reflClip.w) * float2(0.5, 0.5) + 0.5;
    
    reflectUV += dudvOffset * 0.3;
    
    float3 reflectColor = textures[sceneIdx].Sample(globalSampler, saturate(reflectUV)).rgb;

    // SSR: corrects the reflection where the reflected ray hits scene geometry
    {
        float rawDepth = textures[depthIdx].SampleLevel(globalSampler, screenUV, 0).r;
        // screenUV has Y flipped (from clipPos * float2(0.5, -0.5)), undo for correct NDC
        float2 ndc = screenUV * float2(2.0, -2.0) + float2(-1.0, 1.0);
        float4 clipPos = float4(ndc, rawDepth, 1.0);
        float4 wp = mul(invViewProj, clipPos);
        float3 worldPos_depth = wp.xyz / wp.w;

        // View vector should be camera-to-water surface pixel, not bottom of the water
        float3 V = normalize(input.worldPos - cameraPos.xyz);
        float3 R = normalize(reflect(V, N));

        const int kMarchSteps = 48;
        const int kBinarySteps = 8;
        const float kMaxDist = 800.0;
        float stepSize = kMaxDist / kMarchSteps;

        float3 rayPos = input.worldPos;
        bool found = false;
        float3 hitPos = (float3)0;

        [loop] for (int i = 0; i < kMarchSteps; i++) {
            rayPos += R * stepSize;
            float4 pp = mul(proj, mul(view, float4(rayPos, 1.0)));
            float2 ru = pp.xy / pp.w * float2(0.5, 0.5) + 0.5;
            if (ru.x < 0 || ru.x > 1 || ru.y < 0 || ru.y > 1) break;

            float sd = textures[depthIdx].SampleLevel(globalSampler, ru, 0).r;
            // Vulkan NDC depth is [0, 1], not OpenGL [-1, 1]
            float rd = pp.z / pp.w;

            if (rd > sd + 0.0005) {
                float3 hi = rayPos;
                float3 lo = rayPos - R * stepSize;
                [loop] for (int j = 0; j < kBinarySteps; j++) {
                    float3 mid = (lo + hi) * 0.5;
                    float4 mp = mul(proj, mul(view, float4(mid, 1.0)));
                    float2 mu = mp.xy / mp.w * float2(0.5, 0.5) + 0.5;
                    if (mu.x < 0 || mu.x > 1 || mu.y < 0 || mu.y > 1) { hi = mid; continue; }
                    float md = textures[depthIdx].SampleLevel(globalSampler, mu, 0).r;
                    // Vulkan NDC depth is [0, 1]
                    float mr = mp.z / mp.w;
                    if (mr > md) hi = mid; else lo = mid;
                }
                hitPos = hi;
                found = true;
                break;
            }
        }

        if (found) {
            float4 fp = mul(proj, mul(view, float4(hitPos, 1.0)));
            float2 fu = fp.xy / fp.w * float2(0.5, 0.5) + 0.5;
            if (fu.x >= 0 && fu.x <= 1 && fu.y >= 0 && fu.y <= 1) {
                float3 ssrColor = textures[sceneIdx].SampleLevel(globalSampler, fu, 0).rgb;
                float ssrConf = saturate(1.0 - max(abs(fu.x - 0.5), abs(fu.y - 0.5)) * 2.0);
                reflectColor = lerp(reflectColor, ssrColor, lerp(0.5, 1.0, ssrConf));
            }
        }
    }

    // --- REFRACTION ---
    float2 refractUV = screenUV + dudvOffset * 0.6;
    float3 refractColor = textures[sceneIdx].Sample(globalSampler, saturate(refractUV)).rgb;

    // Water body color
    float3 deepColor   = float3(0.01, 0.05, 0.15);
    float3 shallowColor = float3(0.04, 0.25, 0.45);
    float depthFactor = saturate((input.worldPos.y - waterLevel) / 15.0 + 0.5);
    float3 waterBody = lerp(shallowColor, deepColor, depthFactor);

    // Refraction absorbs into water color with depth
    float waterOpacity = lerp(0.05, 0.5, depthFactor);
    float3 refractionFinal = lerp(refractColor, waterBody, waterOpacity);
    refractionFinal = lerp(refractionFinal, waterBody, fresnel * 0.2);

    float3 result = lerp(refractionFinal, reflectColor, fresnel);

    // Specular highlights (sun reflections on wave facets)
    float3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    // Wykładnik 512.0 daje małe, ostre "iskierki" słońca na powierzchni fal
    float spec = pow(NdotH, 512.0) * 0.9 + pow(NdotH, 64.0) * 0.3;
    float3 sunSpec = float3(1.0, 0.9, 0.7) * spec * fresnel;

    // KROK 5: PODPOWIERZCHNIOWE ROZPRASZANIE ŚWIATŁA (Sub-surface scattering)
    float3 subSurf = deepColor * 0.3 * NdotL * (1.0 - fresnel);

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

    float alpha = lerp(0.75, 0.95, fresnel);
    return float4(result, alpha);
}
