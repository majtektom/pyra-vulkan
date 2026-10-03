#ifndef SHARED_LIGHT_TYPES_HLSL
#define SHARED_LIGHT_TYPES_HLSL

/**
 * @brief Struktura opisująca dane pojedynczego źródła światła (Light Data).
 * 
 * Zdefiniowana w sposób zgodny z wyrównaniem std140/std430, aby zapobiec 
 * problemom z rozbieżnością ułożenia danych w pamięci na CPU (C++) i GPU (HLSL/SPIR-V).
 * Każda zmienna typu float4 lub float4x4 jest wyrównana do 16 bajtów.
 */
struct LightData {
    column_major float4x4 lightViewProj[4]; // Dla directional light: 4 kaskady, dla pozostałych używany jest element 0
    float4 position;    // xyz = pozycja świata, w = indeks warstwy tekstury mapy cieni
    float4 direction;   // xyz = wektor kierunkowy, w = typ światła (1.0 = directional, 2.0 = point, 3.0 = spot)
    float4 color;       // rgb = kolor światła, w = intensywność (siła świecenia)
    float4 params;      // x = cosinus kąta wewnętrznego, y = cosinus kąta zewnętrznego, z = zasięg, w = liczba kaskad
};

#endif
