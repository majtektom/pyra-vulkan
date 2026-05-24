#pragma once

#include "render_types.h"
#include "../core/resource_manager.h"
#include "backend/vulkan_context.h"
#include <unordered_map>

namespace engine3d {

class MaterialSystem {
public:
    MaterialSystem(std::shared_ptr<VulkanContext> context, VkRenderPass renderPass);
    ~MaterialSystem();

    std::shared_ptr<MaterialInstance> getOrCreateMaterialInstance(const Material& materialData);

    void setRenderPass(VkRenderPass renderPass) { renderPass_ = renderPass; }

private:
    std::shared_ptr<VulkanContext> context_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    ResourceManager<MaterialInstance> materialCache_;
    
    // Cache for pipelines to reuse them across different material instances
    struct PipelineKey {
        std::string shaderName;
        BlendMode blendMode;
        bool depthTest;
        bool depthWrite;
        CullMode cullMode;
        bool operator==(const PipelineKey& other) const {
            return shaderName == other.shaderName && 
                   blendMode == other.blendMode && 
                   depthTest == other.depthTest &&
                   depthWrite == other.depthWrite &&
                   cullMode == other.cullMode;
        }
    };
    
    struct PipelineKeyHasher {
        std::size_t operator()(const PipelineKey& k) const {
            return std::hash<std::string>{}(k.shaderName) ^ 
                   (static_cast<std::size_t>(k.blendMode) << 16) ^ 
                   (k.depthTest ? 1u : 0u) ^
                   (k.depthWrite ? 2u : 0u) ^
                   (static_cast<std::size_t>(k.cullMode) << 8);
        }
    };

    std::unordered_map<PipelineKey, VulkanPipeline, PipelineKeyHasher> pipelineCache_;

    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout boneDescriptorSetLayout_ = VK_NULL_HANDLE;
    void createDescriptorSetLayout();
    VkDescriptorSetLayout createBoneDescriptorSetLayout();
    VulkanPipeline getOrCreatePipeline(const Material& materialData);
};

} // namespace engine3d
