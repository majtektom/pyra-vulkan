#include "standard_passes.h"
#include <algorithm>
#include <array>

namespace engine3d {

OpaquePass::OpaquePass() : name_("OpaquePass") {}

void OpaquePass::execute(VkCommandBuffer commandBuffer, uint32_t currentFrame, const RenderScene& scene,
                         VkPipelineLayout pipelineLayout, const TextureDescriptorLookup& getTextureDescSet) {
    auto packets = scene.getOpaquePackets();
    
    // Sort front-to-back (nearest first) for early-Z optimization
    std::sort(packets.begin(), packets.end(), [](const RenderPacket& a, const RenderPacket& b) {
        return a.distanceToCamera < b.distanceToCamera;
    });
    
    for (const auto& packet : packets) {
        if (!packet.material || packet.vertexBuffer == VK_NULL_HANDLE) continue;
        
        VkPipelineLayout matLayout = packet.material->getPipeline().layout;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, packet.material->getPipeline().pipeline);

        VkDescriptorSet descSet = getTextureDescSet(packet.textureName, currentFrame);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                matLayout, 0, 1, &descSet, 0, nullptr);

        VkBuffer vertexBuffers[] = {packet.vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        
        if (packet.indexBuffer != VK_NULL_HANDLE) {
            vkCmdBindIndexBuffer(commandBuffer, packet.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        }

        struct PushConstants {
            Matrix4 model;
            std::array<float, 4> color;
        } push;
        push.model = packet.transform;
        push.color = {1.0f, 1.0f, 1.0f, 1.0f};

        vkCmdPushConstants(commandBuffer, matLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(PushConstants), &push);

        if (packet.indexBuffer != VK_NULL_HANDLE) {
            vkCmdDrawIndexed(commandBuffer, packet.indexCount, 1, 0, 0, 0);
        }
    }
}

} // namespace engine3d
