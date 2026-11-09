#pragma once

#include <array>
#include "render/render_types.h"
#include "render/material_compiler.h"

namespace engine3d {

class BaseMaterialInstance : public MaterialInstance {
public:
    BaseMaterialInstance(const Material& data, MaterialCompiler* compiler, VkDescriptorSet descriptorSet)
        : data_(data), compiler_(compiler), descriptorSet_(descriptorSet) {}

    void bind(VkCommandBuffer commandBuffer, uint32_t currentFrame) override {
        if (descriptorSet_ != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    getPipeline().layout, 0, 1, &descriptorSet_, 0, nullptr);
        }
    }

    const VulkanPipeline& getPipeline() const override {
        return compiler_->getOrCreatePipeline(data_);
    }
    
    bool isTransparent() const override { 
        return data_.blendMode != BlendMode::Disabled; 
    }

    // Wariant materiału: bez mieszania i bez zapisu koloru, z zapisem głębokości (ten sam shader wierzchołków,
    // więc głębokość jest identyczna jak w przebiegu przezroczystym).
    const VulkanPipeline* getDepthPrepassPipeline() const override {
        if (!data_.depthPrepass || data_.blendMode == BlendMode::Disabled) return nullptr;
        Material depthOnly = data_;
        depthOnly.blendMode = BlendMode::Disabled;
        depthOnly.depthWrite = true;
        depthOnly.colorWrite = false;
        depthOnly.depthPrepass = false;
        return &compiler_->getOrCreatePipeline(depthOnly);
    }

    TextureHandle getDiffuseHandle() const override {
        return data_.textureHandle;
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

    float getMetallicFactor() const override {
        return data_.metallicFactor;
    }

    float getRoughnessFactor() const override {
        return data_.roughnessFactor;
    }

    TextureHandle getMetallicRoughnessHandle() const override {
        return data_.metallicRoughnessTextureHandle;
    }

    TextureHandle getOcclusionHandle() const override {
        return data_.occlusionTextureHandle;
    }

    TextureHandle getEmissiveHandle() const override {
        return data_.emissiveTextureHandle;
    }

    TextureHandle getNormalHandle() const override {
        return data_.normalMapHandle;
    }

private:
    Material data_;
    MaterialCompiler* compiler_;
    VkDescriptorSet descriptorSet_;
};

} // namespace engine3d
