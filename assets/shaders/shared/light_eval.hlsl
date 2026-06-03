#ifndef SHARED_LIGHT_EVAL_HLSL
#define SHARED_LIGHT_EVAL_HLSL

#include "shared/pbr.hlsl"
#include "shared/light_types.hlsl"

/**
 * @brief Dekoduje dane światła i oblicza wektor kierunku padania (L) oraz natężenie (radiance) w danym punkcie sceny.
 * 
 * Funkcja wspiera trzy typy źródeł światła na podstawie pola `direction.w`:
 * 1. Światło kierunkowe (Directional, 1u): Promienie równoległe (brak tłumienia odległością).
 * 2. Światło punktowe (Point, 2u): Promienie rozchodzące się sferycznie z punktu, podlegające tłumieniu kwadratowemu.
 * 3. Światło reflektora (Spot, 3u): Podobne do punktowego, ale ograniczone stożkiem (funkcja smoothstep dla wygładzenia krawędzi stożka).
 * 
 * @param light Struktura wejściowa światła.
 * @param worldPos Pozycja świata aktualnie cieniowanego piksela.
 * @param L (wyjście) Wektor jednostkowy skierowany do źródła światła.
 * @param radiance (wyjście) Kolor i natężenie światła docierające do piksela.
 * @param valid (wyjście) Czy światło jest prawidłowego typu (1 = tak, 0 = nie).
 */
void unpackDirectLight(LightData light, float3 worldPos, out float3 L, out float3 radiance, out uint valid)
{
    uint ltype = uint(light.direction.w); // Pobranie typu światła
    float3 pos = light.position.xyz;
    float3 dir = light.direction.xyz;
    float3 col = light.color.rgb;
    float intensity = light.color.w;
    float4 par = light.params;

    valid = 1u;

    // --- 1. Światło kierunkowe ---
    if (ltype == 1u)
    {
        L = normalize(dir);
        radiance = col * intensity;
        return;
    }

    // --- 2. Światło punktowe lub reflektor ---
    if (ltype == 2u || ltype == 3u)
    {
        float3 Lvec = pos - worldPos; // Wektor od piksela do źródła światła
        float dist = length(Lvec);    // Odległość piksela od światła
        float range = max(par.z, 0.001); // Zasięg maksymalny światła (zapobiegamy dzieleniu przez zero)
        
        // Tłumienie odległości (Attenuation) - formuła dopasowana do gier (odwrotny kwadrat odległości)
        float attenuation = 1.0 / (1.0 + (dist * dist) / (range * range));
        
        L = Lvec / dist; // Normalizacja wektora światła
        radiance = col * intensity * attenuation;

        // --- 3. Dodatkowe cieniowanie stożkowe dla reflektora (Spot) ---
        if (ltype == 3u)
        {
            // Iloczyn skalarny (-L i dir) wyznacza kąt między kierunkiem reflektora a wektorem do piksela
            float theta = dot(-L, normalize(dir));
            
            // smoothstep interpoluje gładko jasność między kątem zewnętrznym (par.y) a kątem wewnętrznym (par.x)
            radiance *= smoothstep(par.y, par.x, theta);
        }

        return;
    }

    valid = 0u;
    L = float3(0.0, 0.0, 0.0);
    radiance = float3(0.0, 0.0, 0.0);
}

#endif
