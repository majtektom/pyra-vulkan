#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <functional>
#include <string>
#include <vector>

#include "render/renderer.h"

namespace engine3d {

struct GuiVertex {
    float pos[3], texCoord[2], color[4];
    static VkVertexInputBindingDescription bindingDescription();
    static std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions();
};

static constexpr uint32_t kGuiMaxFrames = Renderer::kMaxFrames;

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

    // Obrazki RGBA (np. minimapa): jedna tekstura zamiast tysięcy prostokątów. Osobna kopia na każdą klatkę
    // w locie - updateImage zapisuje dane, uploadImages (przed rysowaniem, poza przebiegiem renderowania)
    // kopiuje je do obrazu bieżącej klatki tylko po zmianie.
    int createImage(uint32_t width, uint32_t height);
    void updateImage(int id, const std::vector<uint8_t>& rgba);
    void addImage(int id, float x, float y, float w, float h, float z);   // NDC jak addRect
    void uploadImages(VkCommandBuffer cmd, uint32_t frameIndex);

    void build(uint32_t frameIndex);
    void recordDrawCommands(VkCommandBuffer cmd, uint32_t frameIndex);
    [[nodiscard]] bool isReady() const { return ready_; }

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
    uint32_t rectCount_ = 0;   // wierzchołki prostokątów i tekstu (przed obrazkami)
    std::vector<GuiVertex> vertices_;

    struct GuiImage {
        uint32_t width = 0, height = 0;
        std::array<VkImage, kGuiMaxFrames> image{};
        std::array<VkDeviceMemory, kGuiMaxFrames> memory{};
        std::array<VkImageView, kGuiMaxFrames> view{};
        std::array<VkDescriptorSet, kGuiMaxFrames> set{};
        std::array<VkBuffer, kGuiMaxFrames> staging{};
        std::array<VkDeviceMemory, kGuiMaxFrames> stagingMemory{};
        std::array<void*, kGuiMaxFrames> stagingPtr{};
        std::vector<uint8_t> data;
        uint64_t version = 1;
        std::array<uint64_t, kGuiMaxFrames> uploaded{};
        std::vector<GuiVertex> vertices;               // quady tej klatki
        std::array<uint32_t, kGuiMaxFrames> first{}, count{};   // zakres w buforze wierzchołków klatki
    };
    std::vector<GuiImage> images_;
    VkDescriptorPool imagePool_ = VK_NULL_HANDLE;
    static constexpr uint32_t kMaxGuiImages = 16;
    void recordImageCopy(VkCommandBuffer cmd, GuiImage& img, uint32_t fi, bool fromShaderRead);

    void createWhiteTexture();
    void createDescriptorSet();
    void ensureBuffer(VkDeviceSize size, uint32_t frameIndex);
};

} // namespace engine3d
