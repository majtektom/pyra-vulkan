#include "standard_passes.h"
#include <algorithm>

namespace engine3d {

TransparentPass::TransparentPass() : name_("TransparentPass") {}

void TransparentPass::execute(VkCommandBuffer commandBuffer, uint32_t currentFrame, const RenderScene& scene,
                              VkPipelineLayout pipelineLayout) {
    auto packets = scene.getTransparentPackets();
    
    // Sort: lower priority first, then back-to-front within same priority
    std::stable_sort(packets.begin(), packets.end(), [](const RenderPacket& a, const RenderPacket& b) {
        if (a.renderPriority != b.renderPriority) return a.renderPriority < b.renderPriority;
        return a.distanceToCamera > b.distanceToCamera;
    });

    for (const auto& packet : packets) {
        if (!packet.material || packet.vertexBuffer == VK_NULL_HANDLE) continue;

        VkPipelineLayout matLayout = packet.material->getPipeline().layout;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, packet.material->getPipeline().pipeline);

        // Global descriptor set (set 0) is already bound in Renderer::render
        // We only need to bind bone descriptor set (set 1) if present
        if (packet.boneDescriptorSet != VK_NULL_HANDLE)
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    matLayout, 1, 1, &packet.boneDescriptorSet, 0, nullptr);

        VkBuffer vertexBuffers[] = {packet.vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

        if (packet.instanceBuffer != VK_NULL_HANDLE) {
            VkBuffer instVBs[] = {packet.instanceBuffer};
            VkDeviceSize instOffs[] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 1, 1, instVBs, instOffs);
        } else if (packet.skinVertexBuffer != VK_NULL_HANDLE) {
            VkBuffer skinVBs[] = {packet.skinVertexBuffer};
            VkDeviceSize skinOffs[] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 1, 1, skinVBs, skinOffs);
        }
        
        if (packet.indexBuffer != VK_NULL_HANDLE) {
            vkCmdBindIndexBuffer(commandBuffer, packet.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        }

        struct PushConstants {
            Matrix4 model;
            std::array<float, 4> color;
            std::array<float, 4> lightDir;
            float bumpIntensity;
            uint32_t diffuseIndex;
            uint32_t normalIndex;
            float padding;
        } push;
        push.model = packet.transform;
        push.color = packet.materialColor;
        push.lightDir = {0.5f, 1.0f, 0.3f, 0.25f};
        push.bumpIntensity = packet.bumpIntensity;
        push.diffuseIndex = packet.diffuseTextureIndex;
        push.normalIndex = packet.normalTextureIndex;
        push.padding = 0.0f;

        vkCmdPushConstants(commandBuffer, matLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(PushConstants), &push);

        if (packet.indexBuffer != VK_NULL_HANDLE) {
            uint32_t instCount = packet.instanceCount > 0 ? packet.instanceCount : 1;
            vkCmdDrawIndexed(commandBuffer, packet.indexCount, instCount, 0, 0, 0);
        }
    }
}

} // namespace engine3d
