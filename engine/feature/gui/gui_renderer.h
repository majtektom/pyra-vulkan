#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <functional>
#include <string>
#include <vector>

namespace engine3d {

struct GuiVertex {
    float pos[3], texCoord[2], color[4];
    static VkVertexInputBindingDescription bindingDescription();
    static std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions();
};

static constexpr uint32_t kGuiMaxFrames = 2;

// Prosty renderer 2D dla GUI — rysuje kolorowe prostokąty i tekst w NDC.
class GuiRenderer {
public:
    void init(VkDevice device, VkPhysicalDevice pd, VkFormat colorFormat, VkFormat depthFormat, VkCommandPool cp, VkQueue q, VkExtent2D ext);
    void cleanup();

    void addRect(float x, float y, float w, float h, std::array<float,4> color, float z = -999.0f);
    void addRectPx(int x, int y, int w, int h, std::array<float,4> color, float sw, float sh, float z = -999.0f) {
        float nx0 = (float(x) / sw) * 2.0f - 1.0f;
        float ny0 = (float(y) / sh) * 2.0f - 1.0f;
        float nx1 = (float(x + w) / sw) * 2.0f - 1.0f;
        float ny1 = (float(y + h) / sh) * 2.0f - 1.0f;
        addRect(nx0, ny0, nx1 - nx0, ny1 - ny0, color, z);
    }
    void addText(const std::vector<GuiVertex>& verts);

    void build(uint32_t frameIndex);
    void recordDrawCommands(VkCommandBuffer cmd, uint32_t frameIndex);
    [[nodiscard]] bool isReady() const { return ready_; }

    [[nodiscard]] VkPipelineLayout pipelineLayout() const { return pipelineLayout_; }

    void setDepth(float z) { currentDepth_ = z; }
    [[nodiscard]] float getDepth() const { return currentDepth_; }

private:
    float currentDepth_ = 0.0f;
    bool ready_ = false;
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice pd_ = VK_NULL_HANDLE;
    VkCommandPool cp_ = VK_NULL_HANDLE;
    VkQueue q_ = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl_ = VK_NULL_HANDLE;
    VkDescriptorPool dp_ = VK_NULL_HANDLE;
    VkDescriptorSet ds_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkImage whiteImage_ = VK_NULL_HANDLE; VkDeviceMemory whiteMem_ = VK_NULL_HANDLE;
    VkImageView whiteView_ = VK_NULL_HANDLE;

    std::array<VkBuffer, kGuiMaxFrames> vb_{};
    std::array<VkDeviceMemory, kGuiMaxFrames> vbm_{};
    std::array<VkDeviceSize, kGuiMaxFrames> cap_{};
    uint32_t vc_ = 0;
    std::vector<GuiVertex> vertices_;

    void createWhiteTexture();
    void createDescriptorSet();
    void ensureBuffer(VkDeviceSize size, uint32_t frameIndex);
};

} // namespace engine3d
