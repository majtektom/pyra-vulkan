#pragma once
#include <string>
#include "render/render_pass.h"

namespace engine3d {

class OpaquePass : public IRenderPass {
public:
    OpaquePass();
    void execute(VkCommandBuffer commandBuffer, uint32_t currentFrame, const RenderScene& scene,
                 VkDescriptorSet globalDescriptorSet) override;
    const std::string& getName() const override { return name_; }
private:
    std::string name_;
};

class TransparentPass : public IRenderPass {
public:
    TransparentPass();
    void execute(VkCommandBuffer commandBuffer, uint32_t currentFrame, const RenderScene& scene,
                 VkDescriptorSet globalDescriptorSet) override;
    const std::string& getName() const override { return name_; }
private:
    std::string name_;
};

} // namespace engine3d
