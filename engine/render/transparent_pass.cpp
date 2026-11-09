#include <algorithm>
#include "render/packet_draw.h"
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

        drawRenderPacket(commandBuffer, packet, packet.material->getPipeline(), globalDescriptorSet, boundLayout);
    }
}

TransparentDepthPrepass::TransparentDepthPrepass() : name_("TransparentDepthPrepass") {}

// Wstępny zapis głębokości dla przezroczystych obiektów z Material::depthPrepass (np. podgląd budowy, hologram).
// Wykonywany w części 1 klatki, gdy bufor głębokości jest jeszcze zapisywalny. Potok "tylko głębokość"
// (bez zapisu koloru) zostawia w buforze najbliższą powierzchnię obiektu; w TransparentPass test
// LESS_OR_EQUAL przepuszcza wtedy tylko tę powierzchnię - tylne i wewnętrzne ściany modelu nie
// nakładają się już warstwami (bez prepassu złożony model z alfą wygląda "brudno").
void TransparentDepthPrepass::execute(VkCommandBuffer commandBuffer, uint32_t currentFrame, const RenderScene& scene,
                                      VkDescriptorSet globalDescriptorSet) {
    (void)currentFrame;
    VkPipelineLayout boundLayout = VK_NULL_HANDLE;
    for (const auto& packet : scene.getTransparentPackets()) {
        if (!packet.material || packet.vertexBuffer == VK_NULL_HANDLE) continue;
        const VulkanPipeline* depthOnly = packet.material->getDepthPrepassPipeline();
        if (!depthOnly || depthOnly->pipeline == VK_NULL_HANDLE) continue;
        drawRenderPacket(commandBuffer, packet, *depthOnly, globalDescriptorSet, boundLayout);
    }
}

} // namespace engine3d
