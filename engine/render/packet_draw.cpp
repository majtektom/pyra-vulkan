#include "render/packet_draw.h"
#include "render/push_constants.h"

#include <array>

namespace engine3d {

void DrawBindCache::bindPipeline(VkCommandBuffer cmd, VkPipeline p) {
    if (p == pipeline) { ++skipped; return; }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
    pipeline = p;
}

void DrawBindCache::bindGlobalSet(VkCommandBuffer cmd, VkPipelineLayout l, VkDescriptorSet set) {
    if (l == layout && set == set0) { ++skipped; return; }
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, l, 0, 1, &set, 0, nullptr);
    if (l != layout) set1 = VK_NULL_HANDLE;   // inny layout - nie zakładamy, że set 1 przetrwał
    layout = l;
    set0 = set;
}

void DrawBindCache::bindSet1(VkCommandBuffer cmd, VkPipelineLayout l, VkDescriptorSet set) {
    if (l == layout && set == set1) { ++skipped; return; }
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, l, 1, 1, &set, 0, nullptr);
    set1 = set;
}

void DrawBindCache::bindVertexBuffer(VkCommandBuffer cmd, uint32_t slot, VkBuffer buffer) {
    if (vertex[slot] == buffer) { ++skipped; return; }
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, slot, 1, &buffer, &offset);
    vertex[slot] = buffer;
}

void DrawBindCache::bindIndexBuffer(VkCommandBuffer cmd, VkBuffer buffer) {
    if (index == buffer) { ++skipped; return; }
    vkCmdBindIndexBuffer(cmd, buffer, 0, VK_INDEX_TYPE_UINT32);
    index = buffer;
}

void drawRenderPacket(VkCommandBuffer commandBuffer, const RenderPacket& packet, const VulkanPipeline& pipeline,
                      VkDescriptorSet globalDescriptorSet, DrawBindCache& bound) {
    VkPipelineLayout matLayout = pipeline.layout;

    // Set 0 (UBO + bindless) musi być zbindowany layoutem zgodnym z potokiem materiału - DrawBindCache
    // binduje go ponownie, gdy zmienia się layout (np. po przebiegu deferred), a pomija, gdy jest ten sam.
    bound.bindGlobalSet(commandBuffer, matLayout, globalDescriptorSet);

    // Krok 1: Potok graficzny (Pipeline) materiału - ustawienia shaderów, blendingu, depth testu.
    bound.bindPipeline(commandBuffer, pipeline.pipeline);

    // Krok 2: Descriptor set kości (obiekt animowany szkieletowo, set 1).
    if (packet.boneDescriptorSet != VK_NULL_HANDLE)
        bound.bindSet1(commandBuffer, matLayout, packet.boneDescriptorSet);

    // Krok 3: Bufor wierzchołków (slot 0) - kolejne paczki tej samej siatki używają go bez ponownego bindowania.
    bound.bindVertexBuffer(commandBuffer, 0, packet.vertexBuffer);

    // Krok 4: Opcjonalny slot 1: instancje albo wierzchołki animacji szkieletowej.
    if (packet.instanceBuffer != VK_NULL_HANDLE)
        bound.bindVertexBuffer(commandBuffer, 1, packet.instanceBuffer);
    else if (packet.skinVertexBuffer != VK_NULL_HANDLE)
        bound.bindVertexBuffer(commandBuffer, 1, packet.skinVertexBuffer);

    // Krok 5: Bufor indeksów.
    if (packet.indexBuffer != VK_NULL_HANDLE) bound.bindIndexBuffer(commandBuffer, packet.indexBuffer);

    // Krok 6: Push Constants - parametry obiektu przesyłane razem z poleceniem rysowania (układ: push_constants.h).
    const MaterialPushConstants push = makeMaterialPush(packet);
    vkCmdPushConstants(commandBuffer, matLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                       VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
                       0, sizeof(push), &push);

    // Krok 7: Wywołanie rysowania (Draw Call).
    if (packet.indexBuffer != VK_NULL_HANDLE) {
        uint32_t instCount = packet.instanceCount > 0 ? packet.instanceCount : 1;
        vkCmdDrawIndexed(commandBuffer, packet.indexCount, instCount, 0, 0, 0);
    }
}

} // namespace engine3d
