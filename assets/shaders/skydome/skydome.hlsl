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

// Parametry przesyłane co klatkę określające stan atmosfery
[[vk::push_constant]]
cbuffer SkydomePushConstants
{
    float4 cameraPosition; // Pozycja kamery
    float time;             // Czas gry (animacja chmur, migotanie gwiazd)
    float domeScale;        // Skala rozciągnięcia kopuły
    float cloudCoverage;    // Zachmurzenie [0.0 (czyste niebo) - 1.0 (całkowite zachmurzenie)]
    float cloudSpeed0;      // Prędkość wiatru warstwy chmur 1
    float cloudSpeed1;      // Prędkość wiatru warstwy chmur 2
    float cloudScale0;      // Skala chmur warstwy 1
    float cloudScale1;      // Skala chmur warstwy 2
    float sunDirX;          // Kierunek X do słońca
    float sunDirY;          // Kierunek Y do słońca
    float sunDirZ;          // Kierunek Z do słońca
    float sunSize;          // Rozmiar tarczy słonecznej (odwrotność rozmycia glow)
    float padX;
    float4 skyTopColor;     // Kolor nieba w zenicie
    float4 skyHorizonColor; // Kolor nieba na horyzoncie
    float4 sunColor;        // Kolor słońca (RGB) + intensywność/widoczność słońca (Alpha)
};

// Pomocnicza funkcja generująca pseudolosową wartość float [0.0, 1.0] na podstawie pozycji 2D.
float hash21(float2 p)
{
    p = frac(p * float2(443.8975, 397.2973));
    p += dot(p, p.yx + 19.19);
    return frac(p.x * p.y);
}

// Pomocniczy generator pseudolosowego wektora 2D
float hash22(float2 p)
{
    float2 r = frac(p * float2(123.34, 345.45) + dot(p, p.yx + 21.42));
    return frac(r.x * r.y + dot(r, r.yx + 17.31));
}

// Dwuwymiarowy szum interpolowany (Value Noise)
float noise(float2 p)
{
    float2 i = floor(p); // Współrzędne całkowite siatki
    float2 f = frac(p);  // Część ułamkowa (pozycja wewnątrz komórki)
    f = f * f * (3.0 - 2.0 * f); // Gładka interpolacja Hermite'a (Smoothstep)

    // Haszowanie wartości w czterech rogach siatki
    float a = hash21(i);
    float b = hash21(i + float2(1.0, 0.0));
    float c = hash21(i + float2(0.0, 1.0));
    float d = hash21(i + float2(1.0, 1.0));

    // Biliniowa interpolacja wartości z rogów
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

/**
 * @brief Szum FBM (Fractal Brownian Motion).
 * 
 * Generuje szum fraktalny poprzez nakładanie na siebie wielu oktaw szumu Value Noise.
 * Każda kolejna oktawa ma wyższą częstotliwość (mniejsze detale) i mniejszą amplitudę (siłę wpływu).
 * Pozwala to uzyskać strukturę przypominającą naturalne chmury lub ukształtowanie terenu.
 */
float fbm(float2 p)
{
    float2x2 rot = float2x2(0.8, 0.6, -0.6, 0.8); // Macierz rotacji zapobiegająca pokrywaniu się osi kolejnych oktaw
    float val = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    float lacunarity = 2.3; // Mnożnik częstotliwości kolejnej oktawy
    
    // Łączenie 6 oktaw szumu
    for (int i = 0; i < 6; i++)
    {
        val += amp * noise(p * freq);
        freq *= lacunarity;
        amp *= 0.5;
        p = mul(p, rot) * 1.3;
    }
    return val;
}

// Generator zniekształconego szumu FBM (Domain Warping) dla ciekawszych struktur chmur
float cloudFbm(float2 p)
{
    float2 q0 = p;
    // Domain warping: przesunięcie współrzędnych wejściowych (p) o wynik innego szumu
    float2 q1 = p + float2(fbm(p * 2.0 + 0.5) * 0.7, fbm(p * 2.0 + 2.7) * 0.7);
    return fbm(q1 * 1.1) * 0.65 + fbm(q0 * 0.5 + 1.2) * 0.35;
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
    [[vk::location(0)]] float2 tc : TEXCOORD0;       // Współrzędne UV dla mapowania chmur
    [[vk::location(1)]] float heightFrac : TEXCOORD1; // Wysokość wierzchołka [0.0 - 1.0] (dla gradientu nieba)
    [[vk::location(2)]] float3 worldPos : TEXCOORD2;  // Pozycja w świecie do obliczeń wektora słońca
};

struct FragmentInput
{
    [[vk::location(0)]] float2 tc : TEXCOORD0;
    [[vk::location(1)]] float heightFrac : TEXCOORD1;
    [[vk::location(2)]] float3 worldPos : TEXCOORD2;
};

VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;

    // Przesunięcie kopuły wraz z pozycją kamery (podobnie jak w Skyboxie, gracz nigdy nie ucieknie z kopuły)
    float3 localPos = float3(
        input.position.x * domeScale,
        input.position.y,
        input.position.z * domeScale);
    // Obniżamy niebo o 1500 jednostek w pionie, aby dół kopuły schodził pod horyzont (unikamy czarnej dziury przy wchodzeniu na góry)
    float3 worldPos = localPos + float3(cameraPosition.x, -500.0, cameraPosition.z);

    output.position = mul(proj, mul(view, float4(worldPos, 1.0)));

    // Rzutowanie współrzędnych w płaszczyźnie poziomej (XZ) dla chmur
    float distX = input.position.x / max(cloudScale0, 1.0);
    float distZ = input.position.z / max(cloudScale0, 1.0);
    output.tc = float2(distX, distZ);

    // Zmapowanie wysokości w pionie w celu wyznaczenia gradientu horyzont-zenit.
    // Początek nieba (gradientu) ma być dokładnie na Y = 0 w świecie.
    // Skoro worldPos.y = localPos.y - 1500.0, to worldPos.y = 0 oznacza localPos.y = 1500.0.
    // Od worldPos.y = 0 (Y=0 w świecie) gradient rośnie do worldPos.y = 3500.0 (top kopuły, local Y = 5000.0).
    output.heightFrac = saturate(worldPos.y / 5000.0);
    output.worldPos = worldPos;

    return output;
}

/**
 * @brief Shader pikseli Skydome (Kompozycja Nieba).
 * 
 * 1. Tworzy gradient pionowy tła nieba (horyzont -> zenit).
 * 2. Oblicza dwuwarstwowy, animowany wiatrem szum chmur FBM.
 * 3. Rysuje tarczę słoneczną oraz poświatę (Glow) za pomocą iloczynu skalarnego.
 * 4. Generuje proceduralne gwiazdy (twinkling stars) na podstawie podziału siatki komórkowej,
 *    widoczne tylko w nocy.
 */
float4 fragmentMain(FragmentInput input) : SV_Target0
{
    float3 sunDir = normalize(float3(sunDirX, sunDirY, sunDirZ));
    float3 viewDir = normalize(float3(
        input.worldPos.x - cameraPosition.x,
        input.worldPos.y - cameraPosition.y,
        input.worldPos.z - cameraPosition.z));
    float sunDot = dot(viewDir, sunDir);

    // KROK 1: GRADIENT NIEBA (Zlokalizowany na horyzoncie)
    float3 coolHorizon = lerp(float3(0.015, 0.015, 0.08), float3(0.48, 0.65, 0.90), saturate(sunColor.a * 0.6));
    float3 warmHorizon = skyHorizonColor.rgb;
    
    // Mieszamy je w zależności od kąta do słońca (sunDot)
    float horizonBlend = pow(max(sunDot, 0.0), 6.0);
    float3 finalHorizon = lerp(coolHorizon, warmHorizon, horizonBlend);

    float3 skyGradient = lerp(finalHorizon, skyTopColor.rgb,
                               smoothstep(0.0, 0.55, input.heightFrac));

    // KROK 2: DYNAMICZNE CHMURY (Wiatr przesuwa współrzędne w czasie)
    float2 tc0 = float2(input.tc.x + time * cloudSpeed0 * 0.015, input.tc.y);
    float2 tc1 = float2(input.tc.x + time * cloudSpeed1 * 0.015, input.tc.y);

    float noise0 = cloudFbm(tc0 * 2.8);
    float noise1 = cloudFbm(tc1 * 4.2);
    float cloudDensity = noise0 * 0.6 + noise1 * 0.4; // Połączenie dwóch warstw chmur

    // Próg odcięcia gęstości chmur na podstawie parametru cloudCoverage
    float threshold = 0.82 - cloudCoverage * 0.77;
    float cloudFactor = smoothstep(threshold - 0.06, threshold + 0.06, cloudDensity);

    // Drobny szum na krawędziach chmur dla realizmu (turbulencje)
    float detailNoise = fbm(tc0 * 8.0 + time * 0.005) * 0.15;
    cloudFactor = saturate(cloudFactor + detailNoise * cloudCoverage);

    // Cieniowanie spodu chmur (im większe zachmurzenie, tym ciemniejsze chmury)
    float darkness = pow(saturate(cloudCoverage), 2.5);
    float3 cloudLight = lerp(float3(0.03, 0.03, 0.06), float3(0.94, 0.92, 0.90), saturate(sunColor.a * 1.5));
    float3 cloudDark  = lerp(float3(0.01, 0.01, 0.02), float3(0.16, 0.15, 0.14), saturate(sunColor.a * 1.5));
    float3 cloudBase = lerp(cloudLight, cloudDark, darkness);

    float cloudVar = (cloudDensity - 0.5) * 0.35 * darkness;
    cloudBase = saturate(cloudBase + cloudVar);

    // Przyciemnianie chmur bliżej horyzontu (perspektywa powietrzna)
    float heightShade = 1.0 - smoothstep(0.25, 0.75, input.heightFrac) * 0.25 * darkness;
    cloudBase *= heightShade;

    // Połączenie nieba i chmur
    float3 result = lerp(skyGradient, cloudBase, cloudFactor);

    // KROK 3: RYSOWANIE SŁOŃCA I POŚWIATY (Sun Disk & Glow)
    // sunDir, viewDir i sunDot obliczone powyżej
    
    // Potęgowanie eksponencjalne tworzy gładką, nieliniową poświatę (glow) wokół słońca
    float sunGlow = exp((sunDot - 1.0) * sunSize) * sunColor.a;
    // Ostra krawędź samej tarczy słonecznej
    float sunDisc = smoothstep(0.9993, 0.9999, sunDot);

    result += sunColor.rgb * (sunDisc + sunGlow * 0.12);

    // KROK 4: PROCEDURALNE GWIAZDY (Aktywne w nocy: starVis > 0)
    float starVis = saturate(1.0 - sunColor.a * 5.0); // Widoczność gwiazd zależy od zachodu słońca
    if (starVis > 0.01)
    {
        // Podział nieba na małe komórki (Siatka gwiazd)
        float2 starP = input.tc * 4.0;
        float2 starCell = floor(starP * 60.0); // ID komórki
        float2 starUv = frac(starP * 60.0);   // UV wewnątrz komórki

        // Losowanie pozycji gwiazdy wewnątrz danej komórki
        float2 starPos = float2(hash21(starCell), hash21(starCell + 17.0));
        float dist = length(starUv - starPos); // Odległość piksela od losowej gwiazdy
        
        float starBright = hash21(starCell + 31.0); // Losowa jasność danej gwiazdy
        starBright = starBright * 0.5 + 0.5;

        // Migotanie (twinkle) gwiazd oparte na sinusie i haszu komórki (gwiazdy migotają asynchronicznie)
        float twinkle = 0.6 + 0.4 * sin(time * 2.5 + hash21(starCell + 7.0) * 200.0);
        starBright *= twinkle;

        // Narysowanie małego punktu (gwiazdy)
        float star = (1.0 - smoothstep(0.0, 0.018, dist)) * starBright;

        // Wygaszenie gwiazd blisko horyzontu (perspektywa grubej atmosfery)
        float horizonFade = smoothstep(0.0, 0.25, input.heightFrac);
        star *= horizonFade;

        // Gwiazdy są zasłaniane przez chmury (1.0 - cloudFactor)
        star *= starVis * (1.0 - cloudFactor);

        // Nadanie losowego odcienia gwiazdy (ciepła pomarańczowa / zimna niebieska)
        float3 starColor = lerp(float3(0.7, 0.8, 1.0), float3(1.0, 0.85, 0.6), starBright * 0.3);
        result += starColor * star;
    }

    if (fogEnabled != 0u)
    {
        // Horizon fog blending for skydome
        float horizonThickness = 0.22; // Height thickness of the horizon fog layer in sky space
        float skyFogFactor = saturate(1.0 - max(viewDir.y, 0.0) / horizonThickness);
        skyFogFactor = pow(skyFogFactor, 2.5); // Sharp transition to blend perfectly at the horizon
        result = lerp(result, fogColor.rgb, skyFogFactor);
    }

    return float4(result, 1.0);
}
