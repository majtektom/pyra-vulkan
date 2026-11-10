#ifndef SHARED_GBUFFER_OUTPUT_HLSL
#define SHARED_GBUFFER_OUTPUT_HLSL

#include "shared/surface.hlsl"

// =====================================================================================
// Wyjście shaderów zapisujących G-Buffer (rendering odroczony). Każdy taki shader (zwykły gbuffer, teren)
// zwraca packGBuffer(powierzchnia) - jedno kodowanie danych, które czyta przebieg deferred.
// =====================================================================================
struct GBufferOutput
{
    float4 albedoMetallic : SV_Target0;     // RGB = albedo, A = metaliczność
    float4 normalRoughness : SV_Target1;    // RGB = normalna świata z [-1, 1] na [0, 1], A = chropowatość
    float4 occlusionEmissive : SV_Target2;  // R = AO, GBA = samozjarzenie
    float4 worldPosition : SV_Target3;      // XYZ = pozycja świata, W = 1 (maska: w pikselu jest geometria)
};

GBufferOutput packGBuffer(SurfaceData s)
{
    GBufferOutput o;
    o.albedoMetallic = float4(s.albedo, s.metallic);
    o.normalRoughness = float4(s.normal * 0.5 + 0.5, s.roughness);
    o.occlusionEmissive = float4(s.ao, s.emissive);
    o.worldPosition = float4(s.worldPos, 1.0);
    return o;
}

#endif
