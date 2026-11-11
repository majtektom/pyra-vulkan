#pragma once

/**
 * push_constants.h - jedno miejsce na układ push constants (małe dane "na obiekt" przesyłane do shadera
 * razem z poleceniem rysowania, bez bufora). Układ bajtów MUSI się zgadzać z blokiem [[vk::push_constant]]
 * w shaderze - stąd static_assert na rozmiar i komentarz, który shader odpowiada której strukturze.
 * Nowe pole: dopisać tu i w odpowiednich shaderach (ta sama kolejność, wyrównanie do 16 B dla wektorów).
 */
#include <array>
#include <cstdint>

#include "core/math.h"
#include "render/render_types.h"

namespace engine3d {

/**
 * Materiał obiektu - w shaderach blok assets/shaders/shared/material_push.hlsl (include w default, G-Buffer,
 * terrain, vegetation, particle, sprite). Nowe pole: tu i w tym jednym pliku.
 * Zakres push constants w layoutach potoków: 256 B.
 */
struct MaterialPushConstants {
    Matrix4 model;                              // macierz świata obiektu
    std::array<float, 4> baseColorFactor;       // kolor bazowy materiału
    float metallicFactor;
    float roughnessFactor;
    float bumpIntensity;                        // siła mapy normalnych
    uint32_t baseColorTextureSet;               // indeksy tekstur w tablicy bindless
    uint32_t physicalDescriptorTextureSet;      // metallic-roughness
    uint32_t normalTextureSet;
    uint32_t occlusionTextureSet;
    uint32_t emissiveTextureSet;
    float alphaMaskCutoff;                      // próg odrzucania przezroczystości (alpha clipping)
    uint32_t layerBlendEnabled;                 // teren: mieszanie warstw tekstur
    uint32_t layerCount;
    float layerWorldSize;
    uint32_t layerBaseTextureSets[3][4];        // uint4[3] w HLSL - 12 warstw
    uint32_t layerMaskTextureSets[3][4];
    uint32_t heightmapTextureSet;               // teren teselowany (terrain.hlsl)
    float scaleY;
    float tessellationFactor;
    uint32_t heightmapEnabled;
};
static_assert(sizeof(MaterialPushConstants) == 240, "układ musi odpowiadać blokowi push constants w shaderach");
static_assert(sizeof(MaterialPushConstants) <= 256, "zakres push constants w layoutach potoków to 256 B");

/** Mapa cieni zwykłych i animowanych siatek - w shaderach shared/shadow_push.hlsl. */
struct ShadowPushConstants {
    Matrix4 model;
    Matrix4 lightViewProj;                      // kaskada słońca albo światło lokalne
    float alphaCutoff;                          // > 0: odrzucanie przezroczystych pikseli (liście)
    uint32_t baseColorTextureSet;
    float pad[2];
};
static_assert(sizeof(ShadowPushConstants) == 144, "układ jak w shared/shadow_push.hlsl");

/** Cień terenu teselowanego (terrain_shadow.hlsl) - ta sama teselacja i wysokość co na ekranie. */
struct TerrainShadowPushConstants {
    Matrix4 model;
    Matrix4 lightViewProj;
    uint32_t heightmapTextureSet;
    float scaleY;
    float tessellationFactor;
    float layerWorldSize;
};
static_assert(sizeof(TerrainShadowPushConstants) == 144, "układ jak w terrain_shadow.hlsl");

// Wypełnia push constants materiału z paczki (indeksy tekstur są już ustalone przez renderer).
inline MaterialPushConstants makeMaterialPush(const RenderPacket& packet) {
    MaterialPushConstants push{};
    push.model = packet.transform;
    push.baseColorFactor = packet.materialColor;
    push.metallicFactor = packet.metallicFactor;
    push.roughnessFactor = packet.roughnessFactor;
    push.bumpIntensity = packet.bumpIntensity;
    push.baseColorTextureSet = packet.diffuseTextureIndex;
    push.physicalDescriptorTextureSet = packet.metallicRoughnessTextureIndex;
    push.normalTextureSet = packet.normalTextureIndex;
    push.occlusionTextureSet = packet.occlusionTextureIndex;
    push.emissiveTextureSet = packet.emissiveTextureIndex;
    push.alphaMaskCutoff = 0.5f;
    push.tessellationFactor = packet.tessellationFactor;
    if (const TerrainSurface* t = packet.terrain.get()) {
        push.layerBlendEnabled = t->layerBlendEnabled ? 1u : 0u;
        push.layerCount = t->layerCount;
        push.layerWorldSize = t->layerWorldSize;
        for (uint32_t i = 0; i < kMaxTerrainLayers; ++i) {
            push.layerBaseTextureSets[i / 4][i % 4] = t->layerTextureIndices[i];
            push.layerMaskTextureSets[i / 4][i % 4] = t->layerMaskIndices[i];
        }
        push.heightmapTextureSet = t->heightmapTextureIndex;
        push.scaleY = t->scaleY;
        push.heightmapEnabled = t->heightmapEnabled ? 1u : 0u;
    } else {
        push.layerWorldSize = 1.0f;
        push.scaleY = 1.0f;
    }
    return push;
}

} // namespace engine3d
