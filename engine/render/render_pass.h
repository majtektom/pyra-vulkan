#pragma once

#include <functional>
#include <vector>
#include "render/render_types.h"

namespace engine3d {

class RenderScene {
public:
    void clear() {
        opaquePackets.clear();
        transparentPackets.clear();
        shadowPackets.clear();
    }

    void addPacket(RenderPacket packet) {
        const bool isTransparent = packet.material && packet.material->isTransparent();
        const bool castShadows = packet.castShadows && packet.vertexBuffer != VK_NULL_HANDLE;

        if (castShadows) {
            if (isTransparent) {
                shadowPackets.push_back(packet);
                transparentPackets.push_back(std::move(packet));
            } else {
                shadowPackets.push_back(packet);
                opaquePackets.push_back(std::move(packet));
            }
        } else {
            if (isTransparent) {
                transparentPackets.push_back(std::move(packet));
            } else {
                opaquePackets.push_back(std::move(packet));
            }
        }
    }

    const std::vector<RenderPacket>& getOpaquePackets() const { return opaquePackets; }
    const std::vector<RenderPacket>& getTransparentPackets() const { return transparentPackets; }
    const std::vector<RenderPacket>& getShadowPackets() const { return shadowPackets; }

private:
    std::vector<RenderPacket> opaquePackets;
    std::vector<RenderPacket> transparentPackets;
    std::vector<RenderPacket> shadowPackets;
};

class IRenderPass {
public:
    virtual ~IRenderPass() = default;
    virtual void execute(VkCommandBuffer commandBuffer, uint32_t currentFrame, const RenderScene& scene,
                         VkDescriptorSet globalDescriptorSet) = 0;
    virtual const std::string& getName() const = 0;
};

} // namespace engine3d
