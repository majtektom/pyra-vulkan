#include <algorithm>
#include <array>
#include "render/standard_passes.h"

namespace engine3d {

OpaquePass::OpaquePass() : name_("OpaquePass") {}

void OpaquePass::execute(VkCommandBuffer commandBuffer, uint32_t currentFrame, const RenderScene& scene,
                         VkPipelineLayout pipelineLayout) {
    const auto& packets = scene.getOpaquePackets();
    std::vector<const RenderPacket*> sortedPackets;
    sortedPackets.reserve(packets.size());
    for (const auto& packet : packets) {
        sortedPackets.push_back(&packet);
    }
    
    // Sortowanie od przodu do tyłu (front-to-back / najbliższe obiekty rysowane jako pierwsze).
    // Jest to kluczowa optymalizacja określana jako Early-Z (lub Depth Culling).
    // Rysowanie najbliższych obiektów najpierw sprawia, że piksele obiektów leżących za nimi 
    // zostaną automatycznie odrzucone przez test głębokości (Depth Test) na GPU,
    // co drastycznie zmniejsza kosztowny overdraw (nadpisywanie pikseli i zbędne uruchamianie Fragment Shaderów).
    // Sortujemy wskaźniki zamiast całych struktur RenderPacket, co zapobiega zbędnym kopiom i narzutom na ref-count shared_ptr.
    std::sort(sortedPackets.begin(), sortedPackets.end(), [](const RenderPacket* a, const RenderPacket* b) {
        return a->distanceToCamera < b->distanceToCamera;
    });
    
    // Rysowanie każdej nieprzezroczystej paczki geometrii:
    for (const auto* packetPtr : sortedPackets) {
        const auto& packet = *packetPtr;
        if (!packet.forwardOpaquePass) continue;
        if (!packet.material || packet.vertexBuffer == VK_NULL_HANDLE) continue;
        
        VkPipelineLayout matLayout = packet.material->getPipeline().layout;
        
        // Krok 1: Bindowanie potoku graficznego (Pipeline) dla materiału (ustawienia shaderów, blendingu, depth testu).
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, packet.material->getPipeline().pipeline);

        // Krok 2: Bindowanie descriptor setów kości (jeśli to obiekt animowany szkieletowo, set 1).
        // Globalny descriptor set (tekstury i bufory globalne, set 0) jest bindowany raz dla całej klatki w Rendererze.
        if (packet.boneDescriptorSet != VK_NULL_HANDLE)
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    matLayout, 1, 1, &packet.boneDescriptorSet, 0, nullptr);

        // Krok 3: Bindowanie bufora wierzchołków (slot 0).
        VkBuffer vertexBuffers[] = {packet.vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

        // Krok 4: Bindowanie opcjonalnych buforów (instancjonowanie lub wierzchołki animacji szkieletowej, slot 1).
        if (packet.instanceBuffer != VK_NULL_HANDLE) {
            VkBuffer instVBs[] = {packet.instanceBuffer};
            VkDeviceSize instOffs[] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 1, 1, instVBs, instOffs);
        } else if (packet.skinVertexBuffer != VK_NULL_HANDLE) {
            VkBuffer skinVBs[] = {packet.skinVertexBuffer};
            VkDeviceSize skinOffs[] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 1, 1, skinVBs, skinOffs);
        }
        
        // Krok 5: Bindowanie bufora indeksów.
        if (packet.indexBuffer != VK_NULL_HANDLE) {
            vkCmdBindIndexBuffer(commandBuffer, packet.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        }

        // Krok 6: Wypełnianie struktury Push Constants przesyłającej unikalne parametry obiektu bezpośrednio do rejestrów GPU.
        struct PushConstants {
            Matrix4 model;                          // Macierz świata (Model Matrix) obiektu.
            std::array<float, 4> baseColorFactor;   // Kolor bazowy materiału.
            float metallicFactor;                   // Metaliczność.
            float roughnessFactor;                  // Szorstkość.
            float bumpIntensity;                    // Siła mapowania normalnych.
            uint32_t baseColorTextureSet;           // Indeks tekstury albedo w tablicy bindless.
            uint32_t physicalDescriptorTextureSet;  // Indeks tekstury metallic-roughness.
            uint32_t normalTextureSet;              // Indeks tekstury normalnych.
            uint32_t occlusionTextureSet;           // Indeks tekstury okluzji.
            uint32_t emissiveTextureSet;            // Indeks tekstury emisji.
            float alphaMaskCutoff;                  // Próg odrzucania przezroczystości (alpha clipping).
            uint32_t layerBlendEnabled;             // Czy włączone mieszanie warstw (dla terenu).
            uint32_t layerCount;
            float layerWorldSize;
            uint32_t layerBaseTextureSets[3][4];
            uint32_t layerMaskTextureSets[3][4];
            uint32_t heightmapTextureSet;
            float scaleY;
            float tessellationFactor;
            uint32_t heightmapEnabled;
        } push;
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
        push.layerBlendEnabled = packet.layerBlendEnabled ? 1u : 0u;
        push.layerCount = packet.layerCount;
        push.layerWorldSize = packet.layerWorldSize;
        for (uint32_t group = 0; group < 3; ++group) {
            for (uint32_t lane = 0; lane < 4; ++lane) {
                const uint32_t index = group * 4 + lane;
                push.layerBaseTextureSets[group][lane] = packet.layerTextureIndices[index];
                push.layerMaskTextureSets[group][lane] = packet.layerMaskIndices[index];
            }
        }
        push.heightmapTextureSet = packet.heightmapTextureIndex;
        push.scaleY = packet.scaleY;
        push.tessellationFactor = packet.tessellationFactor;
        push.heightmapEnabled = packet.heightmapEnabled ? 1u : 0u;

        // Przesyłamy stałe (Push Constants) do shadera.
        vkCmdPushConstants(commandBuffer, matLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                           VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
                           0, sizeof(PushConstants), &push);

        // Krok 7: Wywołanie rysowania (Draw Call).
        if (packet.indexBuffer != VK_NULL_HANDLE) {
            uint32_t instCount = packet.instanceCount > 0 ? packet.instanceCount : 1;
            vkCmdDrawIndexed(commandBuffer, packet.indexCount, instCount, 0, 0, 0);
        }
    }
}

} // namespace engine3d
