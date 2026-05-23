#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include "types.h"
#include "font_atlas_loader.h"

namespace engine3d {

struct TextVertex {
    float pos[2];
    float texCoord[2];
    float color[4];

    static VkVertexInputBindingDescription bindingDescription() {
        VkVertexInputBindingDescription desc{};
        desc.binding = 0;
        desc.stride = sizeof(TextVertex);
        desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return desc;
    }

    static std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 3> attrs{};
        attrs[0].binding = 0;
        attrs[0].location = 0;
        attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[0].offset = offsetof(TextVertex, pos);
        attrs[1].binding = 0;
        attrs[1].location = 1;
        attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[1].offset = offsetof(TextVertex, texCoord);
        attrs[2].binding = 0;
        attrs[2].location = 2;
        attrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[2].offset = offsetof(TextVertex, color);
        return attrs;
    }
};

class TextOverlayRenderer {
public:
    TextOverlayRenderer();
    ~TextOverlayRenderer();

    void init(
        VkDevice device,
        VkPhysicalDevice physicalDevice,
        VkRenderPass renderPass,
        VkCommandPool commandPool,
        VkQueue graphicsQueue,
        VkExtent2D extent
    );

    void cleanup();

    void loadFont(const FontAsset& fontAsset);

    void setText(const std::string& text, float x, float y, float scale,
                 const std::array<float, 4>& color, float screenWidth, float screenHeight);

    void recordDrawCommands(VkCommandBuffer commandBuffer) const;

    [[nodiscard]] VkPipelineLayout getPipelineLayout() const { return pipelineLayout_; }
    [[nodiscard]] bool isReady() const { return ready_ && fontLoaded_; }
    [[nodiscard]] bool isPipelineReady() const { return ready_; }

private:
    void deleteVertexBuffer();

    bool ready_ = false;
    bool fontLoaded_ = false;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;

    VkImage fontImage_ = VK_NULL_HANDLE;
    VkDeviceMemory fontImageMemory_ = VK_NULL_HANDLE;
    VkImageView fontImageView_ = VK_NULL_HANDLE;
    VkSampler fontSampler_ = VK_NULL_HANDLE;

    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory_ = VK_NULL_HANDLE;
    uint32_t vertexCount_ = 0;
    VkDeviceSize vertexBufferCapacity_ = 0;

    FontAtlasData fontAtlas_;
};

} // namespace engine3d
