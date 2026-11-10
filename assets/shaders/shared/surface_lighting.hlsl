#ifndef SHARED_SURFACE_LIGHTING_HLSL
#define SHARED_SURFACE_LIGHTING_HLSL

// =====================================================================================
// JEDNO miejsce oświetlenia sceny. Używają go:
//   - przebieg deferred - cała scena nieprzezroczysta (budynki, modele, teren),
//   - shadery forward - obiekty przezroczyste (materiał domyślny z alfą, roślinność) i woda.
// Nowy efekt (rodzaj światła, cienie, mgła, pogoda) dopisany tutaj działa od razu w całej scenie.
// =====================================================================================

#include "shared/globals.hlsl"
#include "shared/surface.hlsl"
#include "shared/light_eval.hlsl"
#include "shared/shadow_pcf.hlsl"
#include "shared/fog_of_war.hlsl"

// Światło padające na powierzchnię: wszystkie źródła (PBR Cook-Torrance) z cieniami + światło otoczenia + emisja.
// shadowMap / shadowCmp: tablica map cieni (deferred ma ją w set 1, forward - globalShadowMap z set 0).
float3 shadeSurface(SurfaceData s, Texture2DArray<float> shadowMap, SamplerComparisonState shadowCmp)
{
    float3 N = s.normal;
    if (lightingEnabled == 0u)
    {
        // Bez oświetlenia: sama tekstura; cień tylko przyciemnia, z siłą zależną od siły słońca (noc - bez cieni).
        float3 color = s.albedo;
        if (shadowsEnabled != 0u && lightCount > 0u)
        {
            float shadow = sampleShadowPcf(shadowMap, shadowCmp, lights[0], s.worldPos, N);
            float strength = saturate(lights[0].color.w * 0.5);
            color *= lerp(1.0, lerp(0.35, 1.0, shadow), strength);
        }
        return color;
    }

    float3 V = normalize(cameraPos.xyz - s.worldPos);
    float NdotV = max(dot(N, V), 0.0);
    float roughness = max(s.roughness, 0.04);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), s.albedo, s.metallic);

    float3 Lo = float3(0.0, 0.0, 0.0);
    uint count = min(lightCount, 16u);
    for (uint i = 0; i < count; i++)
    {
        float3 L;
        float3 radiance;
        uint valid;
        unpackDirectLight(lights[i], s.worldPos, L, radiance, valid);
        if (valid == 0u) continue;
        float shadow = (shadowsEnabled != 0u) ? sampleShadowPcf(shadowMap, shadowCmp, lights[i], s.worldPos, N) : 1.0;
        Lo += pbrDirectLight(N, V, s.albedo, s.metallic, roughness, NdotV, F0, L, radiance) * shadow;
    }
    float3 ambient = s.albedo * ambientStrength * ambientColor.rgb * s.ao;
    return saturate(ambient + Lo + s.emissive);
}

// To, co leży między kamerą a powierzchnią: mgła odległości i mgła wojny (nieodkryty teren).
float3 applyAtmosphere(float3 color, float3 worldPos)
{
    if (fogEnabled != 0u)
    {
        float dist = distance(cameraPos.xyz, worldPos);
        float fogFactor = saturate((fogEnd - dist) / (fogEnd - fogStart));
        color = lerp(fogColor.rgb, color, fogFactor);
    }
    if (fowEnabled != 0u)
    {
        float vis = fogOfWarVisibility(textures[fowTextureIndex], globalSampler,
                                       fogOfWarUv(worldPos, fowOriginX, fowOriginZ, fowSize));
        color = applyFogOfWar(color, vis, fowExplored);
    }
    return color;
}

// Shadery forward: oświetlenie + atmosfera w jednym wywołaniu.
float3 shadeSurfaceForward(SurfaceData s)
{
    return applyAtmosphere(shadeSurface(s, globalShadowMap, globalShadowSampler), s.worldPos);
}

#endif
