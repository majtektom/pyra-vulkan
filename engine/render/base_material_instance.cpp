#include "render_types.h"
#include <array>

namespace engine3d {

class BaseMaterialInstance : public MaterialInstance {
public:
    BaseMaterialInstance(const Material& data, const VulkanPipeline& pipeline, VkDescriptorSet descriptorSet)
        : data_(data), pipeline_(pipeline), descriptorSet_(descriptorSet) {}

    void bind(VkCommandBuffer commandBuffer, uint32_t currentFrame) override {
        if (descriptorSet_ != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_.layout, 0, 1, &descriptorSet_, 0, nullptr);
        }
    }

    const VulkanPipeline& getPipeline() const override { return pipeline_; }
    
    bool isTransparent() const override { 
        return data_.blendMode != BlendMode::Disabled; 
    }

    const std::string& getTextureName() const override {
        return data_.textureName;
    }

    float getBumpIntensity() const override {
        return data_.bumpIntensity;
    }

    std::array<float,4> getColor() const override {
        return data_.color;
    }

    int getRenderPriority() const override {
        return data_.renderPriority;
    }

private:
    Material data_;
    VulkanPipeline pipeline_;
    VkDescriptorSet descriptorSet_;
};

} // namespace engine3d
