#include <algorithm>
#include "render/standard_passes.h"

namespace engine3d {

TransparentPass::TransparentPass() : name_("TransparentPass") {}

void TransparentPass::execute(VkCommandBuffer commandBuffer, uint32_t currentFrame, const RenderScene& scene,
                              VkDescriptorSet globalDescriptorSet) {
    const auto& packets = scene.getTransparentPackets();
    std::vector<const RenderPacket*> sortedPackets;
    sortedPackets.reserve(packets.size());
    for (const auto& packet : packets) {
        sortedPackets.push_back(&packet);
    }
    
    // Sortowanie obiektów przezroczystych:
    // 1. Najpierw według priorytetu renderowania (renderPriority).
    // 2. Następnie od tyłu do przodu (back-to-front / obiekty najdalsze rysowane jako pierwsze).
    // Sortujemy wskaźniki zamiast całych struktur RenderPacket, co zapobiega zbędnym kopiom i narzutom na ref-count shared_ptr.
     // Dlaczego sortujemy od tyłu do przodu?
    // Prawidłowe renderowanie półprzezroczystych powierzchni (Alpha Blending) wymaga algorytmu malarza (Painters Algorithm).
    // Aby poprawnie wymieszać kolor nowego piksela z tłem, obiekty leżące z tyłu muszą już być narysowane.
    // Gdybyśmy narysowali najpierw obiekt bliższy z włączonym testem głębokości, zablokowałby on (zasłonił) obiekty za nim, 
    // a wyłączenie testu głębokości spowodowałoby błędne nakładanie warstw.
    std::stable_sort(sortedPackets.begin(), sortedPackets.end(), [](const RenderPacket* a, const RenderPacket* b) {
        if (a->renderPriority != b->renderPriority) return a->renderPriority < b->renderPriority;
        return a->distanceToCamera > b->distanceToCamera;
    });

    // Rysowanie każdej półprzezroczystej paczki geometrii:
    VkPipelineLayout boundLayout = VK_NULL_HANDLE;
    for (const auto* packetPtr : sortedPackets) {
        const auto& packet = *packetPtr;
        if (!packet.material || packet.vertexBuffer == VK_NULL_HANDLE) continue;

        VkPipelineLayout matLayout = packet.material->getPipeline().layout;

        // Set 0 (UBO + bindless) musi być zbindowany layoutem zgodnym z potokiem materiału.
        // Layouty są zgodne tylko przy identycznych zakresach push constants, więc set
        // zbindowany wcześniej innym layoutem (np. deferred) nie jest tu ważny.
        if (matLayout != boundLayout) {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    matLayout, 0, 1, &globalDescriptorSet, 0, nullptr);
            boundLayout = matLayout;
        }
        
        // Krok 1: Bindowanie potoku graficznego (Pipeline) z włączonym blendingiem (Alpha Blend).
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, packet.material->getPipeline().pipeline);

        // Krok 2: Bindowanie descriptor setów kości dla animacji szkieletowej (set 1).
        if (packet.boneDescriptorSet != VK_NULL_HANDLE)
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    matLayout, 1, 1, &packet.boneDescriptorSet, 0, nullptr);

        // Krok 3: Bindowanie bufora wierzchołków (slot 0).
        VkBuffer vertexBuffers[] = {packet.vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

        // Krok 4: Bindowanie dodatkowych buforów (instancje lub animacja szkieletowa, slot 1).
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

        // Krok 6: Konfiguracja Push Constants dla shadera.
        struct PushConstants {
            Matrix4 model;
            std::array<float, 4> baseColorFactor;
            float metallicFactor;
            float roughnessFactor;
            float bumpIntensity;
            uint32_t baseColorTextureSet;
            uint32_t physicalDescriptorTextureSet;
            uint32_t normalTextureSet;
            uint32_t occlusionTextureSet;
            uint32_t emissiveTextureSet;
            float alphaMaskCutoff;
            uint32_t layerBlendEnabled;
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

        // Przesyłamy Push Constants do rejestrów GPU.
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
