#ifndef SHARED_SHADOW_PUSH_HLSL
#define SHARED_SHADOW_PUSH_HLSL

// Push constants map cieni (zwykłe i animowane siatki).
// Układ = ShadowPushConstants w engine/render/push_constants.h (144 B).
[[vk::push_constant]]
cbuffer PushConstants
{
    column_major float4x4 model;         // macierz transformacji obiektu
    column_major float4x4 lightViewProj; // widok-projekcja światła (kaskada słońca albo światło lokalne)
    float alphaCutoff;                   // > 0: odrzucanie przezroczystych pikseli (liście)
    uint baseColorTextureSet;            // indeks tekstury albedo w tablicy bindless
    float2 _pad;
};

#endif
