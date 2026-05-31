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

    void addPacket(const RenderPacket& packet) {
        if (packet.material && packet.material->isTransparent()) {
            transparentPackets.push_back(packet);
        } else {
            opaquePackets.push_back(packet);
        }
        if (packet.castShadows && packet.vertexBuffer != VK_NULL_HANDLE) {
            shadowPackets.push_back(packet);
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
                         VkPipelineLayout pipelineLayout) = 0;
    virtual const std::string& getName() const = 0;
};

} // namespace engine3d
