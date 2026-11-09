#include <algorithm>
#include <array>
#include "render/packet_draw.h"
#include "render/standard_passes.h"

namespace engine3d {

OpaquePass::OpaquePass() : name_("OpaquePass") {}

void OpaquePass::execute(VkCommandBuffer commandBuffer, uint32_t currentFrame, const RenderScene& scene,
                         VkDescriptorSet globalDescriptorSet) {
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
    VkPipelineLayout boundLayout = VK_NULL_HANDLE;
    for (const auto* packetPtr : sortedPackets) {
        const auto& packet = *packetPtr;
        if (!packet.forwardOpaquePass) continue;
        if (!packet.material || packet.vertexBuffer == VK_NULL_HANDLE) continue;
        
        drawRenderPacket(commandBuffer, packet, packet.material->getPipeline(), globalDescriptorSet, boundLayout);
    }
}

} // namespace engine3d
