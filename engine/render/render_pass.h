#pragma once

#include "render_types.h"
#include <functional>
#include <vector>

namespace engine3d {

class RenderScene {
public:
    void clear() {
        opaquePackets.clear();
        transparentPackets.clear();
    }

    void addPacket(const RenderPacket& packet) {
        if (packet.material->isTransparent()) {
            transparentPackets.push_back(packet);
        } else {
            opaquePackets.push_back(packet);
        }
    }

    const std::vector<RenderPacket>& getOpaquePackets() const { return opaquePackets; }
    const std::vector<RenderPacket>& getTransparentPackets() const { return transparentPackets; }

private:
    std::vector<RenderPacket> opaquePackets;
    std::vector<RenderPacket> transparentPackets;
};

class IRenderPass {
public:
    virtual ~IRenderPass() = default;
    using TextureDescriptorLookup = std::function<VkDescriptorSet(const std::string& textureName, uint32_t frameIndex)>;
    virtual void execute(VkCommandBuffer commandBuffer, uint32_t currentFrame, const RenderScene& scene,
                         VkPipelineLayout pipelineLayout, const TextureDescriptorLookup& getTextureDescSet) = 0;
    virtual const std::string& getName() const = 0;
};

} // namespace engine3d
