static const float PI = 3.14159265359;

/**
 * @brief Funkcja rozkładu mikrofasetek Trowbridge-Reitz GGX (Normal Distribution Function - NDF).
 * 
 * Określa stopień wyrównania mikropłaszczyzn (mikrofasetek) powierzchni względem
 * wektora połówkowego (Halfway vector H). Wpływa bezpośrednio na kształt i rozmycie
 * odblasku światła (specular highlight): im gładsza powierzchnia (mniejsza chropowatość),
 * tym bardziej skoncentrowany i jaśniejszy odblask.
 */
float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a; // Kwadrat chropowatości (zapewnia bardziej naturalne przejścia dla materiałów półmatowych)
    float NdotH2 = NdotH * NdotH;
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return nom / denom;
}

/**
 * @brief Geometria Smitha (Geometry Shadowing/Masking Function).
 * 
 * Określa stopień samomaskowania (self-shadowing) mikrofasetek.
 * Wylicza prawdopodobieństwo, że światło odbite od mikrofasetki zostanie zablokowane
 * przez inną sąsiadującą mikrofasetkę na drodze do oka obserwatora (V) lub od światła (L).
 */
float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0; // Współczynnik k dopasowany do światła kierunkowego bezpośredniego (Direct Lighting)
    float ggx1 = NdotV / (NdotV * (1.0 - k) + k);
    float ggx2 = NdotL / (NdotL * (1.0 - k) + k);
    return ggx1 * ggx2;
}

/**
 * @brief Przybliżenie Fresnela-Schlicka (Fresnel Reflexion Approximation).
 * 
 * Określa stosunek światła odbitego (Specular) do załamanego/wchłoniętego (Diffuse)
 * w zależności od kąta patrzenia. Im bardziej płaski kąt patrzania (kąt rozwarcia bliski 90 stopni),
 * tym silniejsze odbicie lustrzane na krawędziach obiektu (efekt Fresnela).
 * 
 * @param cosTheta Kosinus kąta pomiędzy wektorem patrzenia a wektorem połówkowym.
 * @param F0 Współczynnik odbicia pod kątem prostopadłym (specular reflectance at normal incidence).
 */
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
}

/**
 * @brief Wylicza oświetlenie PBR (Cook-Torrance BRDF) dla bezpośredniego światła punktowego/kierunkowego.
 * 
 * Łączy składową rozproszoną (Lambertian Diffuse) oraz zwierciadlaną (Cook-Torrance Specular BRDF):
 * f = kD * f_diffuse + kS * f_specular
 */
float3 pbrDirectLight(float3 N, float3 V, float3 baseColor, float metallic, float roughness,
                      float NdotV, float3 F0, float3 L, float3 radiance)
{
    float3 H = normalize(V + L); // Wektor połówkowy (Halfway vector)
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    // Wyliczenie trzech czynników fizycznych PBR
    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySmith(NdotV, NdotL, roughness);
    float3 F = FresnelSchlick(HdotV, F0);

    // Specular Cook-Torrance BRDF: D * G * F / (4 * (N.V) * (N.L))
    // Dodajemy małe 0.0001 w mianowniku w celu uniknięcia dzielenia przez zero przy kątach prostopadłych
    float3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);

    // kS to współczynnik odbicia zwierciadlanego (równy Fresnelowi)
    float3 kS = F;
    
    // kD to współczynnik rozproszenia dyfuzyjnego (energia nieskopiowana na odbicia).
    // Metale (metallic = 1.0) nie posiadają składowej dyfuzyjnej (wchłaniają światło załamane).
    float3 kD = (float3(1.0, 1.0, 1.0) - kS) * (1.0 - metallic);

    // Suma energii: dyfuzyjna (Lambertian: Color/PI) + zwierciadlana (specular) modulowana przez jasność źródła i kąt padania
    return (kD * baseColor / PI + specular) * radiance * NdotL;
}
