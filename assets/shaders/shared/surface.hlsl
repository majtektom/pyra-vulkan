#ifndef SHARED_SURFACE_HLSL
#define SHARED_SURFACE_HLSL

// =====================================================================================
// Opis powierzchni w punkcie - to, co shader obiektu wie o materiale. Oświetlenie (shared/surface_lighting.hlsl)
// i zapis G-Buffera (shared/gbuffer_output.hlsl) dostają tę samą strukturę, więc shader obiektu wypełnia ją raz,
// a sposób rysowania (deferred / forward) jest osobną sprawą.
// =====================================================================================
struct SurfaceData
{
    float3 albedo;      // kolor bazowy
    float alpha;        // przezroczystość (forward; w G-Buffer obiekt jest nieprzezroczysty)
    float metallic;
    float roughness;
    float ao;           // ocienienie otoczenia
    float3 emissive;    // samozjarzenie
    float3 normal;      // normalna w przestrzeni świata (znormalizowana)
    float3 worldPos;
};

// Mapowanie normalnych bez tangentów w wierzchołkach: układ TBN liczony z pochodnych ekranowych (ddx/ddy)
// pozycji i UV - zmiana pozycji i UV między sąsiednimi pikselami wyznacza kierunki styczne.
// normalTex: odczyt z mapy normalnych w [0, 1]; bump: siła (0 = bez zmian).
float3 perturbNormal(float3 N, float3 worldPos, float2 uv, float3 normalTex, float bump)
{
    if (bump <= 0.001) return N;
    float3 n = normalTex * 2.0 - 1.0;
    n.xy *= bump;
    n = normalize(n);
    float3 dp1 = ddx(worldPos);
    float3 dp2 = ddy(worldPos);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);
    float r = 1.0 / (duv1.x * duv2.y - duv1.y * duv2.x + 0.0001);
    float3 tRaw = (dp1 * duv2.y - dp2 * duv1.y) * r;
    float3 tProj = tRaw - N * dot(N, tRaw);
    float lenSq = dot(tProj, tProj);
    if (lenSq <= 0.00001) return N;
    float3 T = tProj / sqrt(lenSq);
    float3 B = cross(N, T);
    return normalize(mul(n, float3x3(T, B, N)));
}

#endif
