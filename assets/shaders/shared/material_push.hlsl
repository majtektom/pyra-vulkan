#ifndef SHARED_MATERIAL_PUSH_HLSL
#define SHARED_MATERIAL_PUSH_HLSL

// Push constants materiału - małe dane "na obiekt" wysyłane razem z poleceniem rysowania.
// Układ bajtów = struktura MaterialPushConstants w engine/render/push_constants.h (240 B) - nowe pole
// dopisujemy w OBU miejscach, w tej samej kolejności. Jeden blok dla wszystkich shaderów materiałów
// (default, G-Buffer, teren, roślinność, cząsteczki, sprite'y); pola terenu inne shadery po prostu pomijają.
[[vk::push_constant]]
cbuffer PushConstants
{
    column_major float4x4 model;            // macierz świata obiektu
    float4 baseColorFactor;                 // kolor bazowy materiału
    float metallicFactor;
    float roughnessFactor;
    float bumpIntensity;                    // siła mapy normalnych
    uint baseColorTextureSet;               // indeksy tekstur w tablicy bindless
    uint physicalDescriptorTextureSet;      // metallic-roughness
    uint normalTextureSet;
    uint occlusionTextureSet;
    uint emissiveTextureSet;
    float alphaMaskCutoff;                  // próg odrzucania przezroczystości (alpha clipping)
    // --- teren ---
    uint layerBlendEnabled;                 // mieszanie warstw tekstur
    uint layerCount;                        // liczba warstw (maksymalnie 12)
    float layerWorldSize;                   // szerokość świata terenu (skala UV masek)
    uint4 layerBaseTextureSets[3];          // spakowane indeksy tekstur warstw (3 * 4 = 12)
    uint4 layerMaskTextureSets[3];          // spakowane indeksy masek przejść (12)
    uint heightmapTextureSet;               // teren teselowany: mapa wysokości
    float scaleY;
    float tessellationFactor;
    uint heightmapEnabled;
};

#endif
