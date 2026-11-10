#ifndef SHARED_GLOBALS_HLSL
#define SHARED_GLOBALS_HLSL

// =====================================================================================
// Wspólne dane wszystkich shaderów (set 0): bufor jednolity klatki, tablica tekstur bindless i jej sampler.
//
// JEDNO miejsce deklaracji - każdy shader dołącza ten plik zamiast kopiować cbuffer. Nowe pole UBO:
// dopisać na końcu tutaj ORAZ w struct GlobalUBO w engine/render/renderer_core.cpp (ta sama kolejność).
// Pola skalarne (float, uint) układają się w HLSL i C++ tak samo; float4/float4x4 wyrównane do 16 bajtów.
// =====================================================================================

#include "shared/light_types.hlsl"

cbuffer GlobalUniformBufferObject : register(b0, space0)
{
    column_major float4x4 view;          // świat -> kamera
    column_major float4x4 proj;          // kamera -> ekran (rzutowanie)
    column_major float4x4 invViewProj;   // ekran -> świat (odtwarzanie pozycji z głębokości)
    float4 camRight;                     // wektory kamery w przestrzeni świata (cząsteczki, sprite'y zwrócone do kamery)
    float4 camUp;
    float4 cameraPos;
    float4 ambientColor;
    float ambientStrength;
    uint lightingEnabled;
    uint lightCount;
    uint shadowsEnabled;
    LightData lights[16];
    float4 directionalShadowSplits;      // granice kaskad cieni słońca
    uint directionalShadowCascadeCount;
    float3 shadowPadding;
    float4 fogColor;                     // mgła odległości
    float fogStart;
    float fogEnd;
    float fogDensity;
    uint fogEnabled;
    float windTime;                      // wiatr (roślinność)
    float windStrength;
    float fowOriginX;                    // mgła wojny (shared/fog_of_war.hlsl)
    float fowOriginZ;
    float fowSize;
    float fowExplored;
    uint fowEnabled;
    uint fowTextureIndex;                // tekstura widoczności bieżącej klatki w tablicy bindless
};

// Tablica wszystkich tekstur (bindless) - indeksy przychodzą w push constants / UBO.
SamplerState globalSampler : register(s1, space0);
Texture2D textures[] : register(t1, space0);

// Tablica map cieni (kaskady słońca + atlas świateł lokalnych) - dla shaderów forward (przezroczyste, woda).
// Przebieg deferred ma tę samą mapę we własnym zestawie (set 1) i przekazuje ją do shadeSurface().
[[vk::binding(2, 0)]] Texture2DArray<float> globalShadowMap;
[[vk::binding(2, 0)]] SamplerComparisonState globalShadowSampler;

#endif
