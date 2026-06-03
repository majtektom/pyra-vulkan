#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include "core/types.h"
#include "feature/font/font_atlas_loader.h"
#include "render/render_feature.h"

namespace engine3d {

class Renderer;

struct TextVertex {
    float pos[3];
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
        attrs[0].binding = 0; attrs[0].location = 0;
        attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = offsetof(TextVertex, pos);
        attrs[1].binding = 0; attrs[1].location = 1;
        attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[1].offset = offsetof(TextVertex, texCoord);
        attrs[2].binding = 0; attrs[2].location = 2;
        attrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[2].offset = offsetof(TextVertex, color);
        return attrs;
    }
};

static constexpr uint32_t kTxtMaxFrames = 2;

class TextOverlayRenderer : public IRenderFeature {
public:
    struct Label {
        std::string text;
        float x, y, scale;
        std::array<float, 4> color;
        float screenWidth, screenHeight;
        float depth = 0.0f;
    };

    TextOverlayRenderer();
    ~TextOverlayRenderer() override;

    void init(Renderer* renderer) override;
    void render(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView& view) override;
    bool isActive() const override { return ready_ && fontLoaded_; }
    const std::string& getName() const override { static std::string name = "TextOverlayRenderer"; return name; }
    Stage getStage() const override { return Stage::Overlay; }

    void cleanup();
    void loadFont(const FontAsset& fontAsset);

    void addText(const std::string& text, float x, float y, float scale,
                 const std::array<float, 4>& color, float screenWidth, float screenHeight, float depth = 0.0f);
    void clearTexts();
    void buildTexts(uint32_t frameIndex);
    void recordDrawCommands(VkCommandBuffer commandBuffer, uint32_t frameIndex) const;

    [[nodiscard]] VkPipelineLayout getPipelineLayout() const { return pipelineLayout_; }
    [[nodiscard]] bool isReady() const { return ready_ && fontLoaded_; }
    [[nodiscard]] bool isPipelineReady() const { return ready_; }

private:
    void ensureGlyphs();
    void deleteVertexBuffer(uint32_t frameIndex);
    void releaseFontResources();

    bool ready_ = false;
    bool fontLoaded_ = false;
    std::vector<Label> labels_;
    Renderer* renderer_ = nullptr;

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

    std::array<VkBuffer, kTxtMaxFrames> vertexBuffer_{};
    std::array<VkDeviceMemory, kTxtMaxFrames> vertexBufferMemory_{};
    std::array<VkDeviceSize, kTxtMaxFrames> vertexBufferCapacity_{};
    uint32_t vertexCount_ = 0;

    FontAtlasData fontAtlas_;
    FontAsset fontAsset_;
    std::set<uint32_t> wantedCodepoints_;
};

} // namespace engine3d
