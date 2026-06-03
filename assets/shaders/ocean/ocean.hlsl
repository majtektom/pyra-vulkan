cbuffer GlobalUniformBufferObject : register(b0)
{
    column_major float4x4 view;
    column_major float4x4 proj;
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
    float pad0;
    float4 lightDir;        // Kierunek słońca (do odblasków specularity)
};

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
    float alpha = lerp(0.75, 0.95, fresnel); // Woda przy horyzoncie staje się bardziej nieprzezroczysta

    // KROK 2: SUMOWANIE KOLORU GŁĘBI I PŁYCIZNY (Water body absorption)
    float3 deepColor   = float3(0.01, 0.05, 0.15); // Ciemny granat
    float3 shallowColor = float3(0.04, 0.25, 0.45); // Morski błękit
    float3 waterBody = lerp(deepColor, shallowColor, alpha);

    // KROK 3: ODBICIE Z NIEBA (Uproszczona odbicie koloru atmosfery)
    float3 skyReflect = lerp(float3(0.25, 0.45, 0.75), float3(0.55, 0.68, 0.88), fresnel);

    // KROK 4: BARDZO OSTRE REFLEKSY SŁONECZNE (Specular highlights)
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
    float3 result = lerp(waterBody, skyReflect, fresnel * 0.7);
    result += sunSpec * 0.8;
    result += subSurf;
    result = lerp(result, foamColor, foam * 0.4); // Nałożenie piany na grzbiety fal

    return float4(result, alpha);
}
