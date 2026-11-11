#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <functional>
#include <string>
#include <vector>

#include "render/renderer.h"

namespace engine3d {

// Wierzchołek GUI: pozycja w NDC (x, y z zakresu -1..1, z = głębokość warstwy okna), UV i kolor.
struct GuiVertex {
    float pos[3], texCoord[2], color[4];
    static VkVertexInputBindingDescription bindingDescription();
    static std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions();
};

static constexpr uint32_t kGuiMaxFrames = Renderer::kMaxFrames;

// Prosty renderer 2D dla GUI — rysuje kolorowe prostokąty, tekst i obrazki we współrzędnych NDC.
// Wszystko idzie jednym potokiem: prostokąty i tekst z białą teksturą 1x1, obrazki z własną.
class GuiRenderer {
public:
    void init(VkDevice device, VkPhysicalDevice physicalDevice, VkFormat colorFormat, VkFormat depthFormat);
    void cleanup();

    // Głębokość "bieżąca" (setDepth) - domyślna wartość z w addRect / addRectPx.
    static constexpr float kUseCurrentDepth = -999.0f;

    // Prostokąt w NDC (x, y - lewy górny róg, w, h - rozmiar).
    void addRect(float x, float y, float w, float h, std::array<float,4> color, float z = kUseCurrentDepth);
    // Prostokąt w pikselach ekranu o rozmiarze screenW x screenH.
    void addRectPx(int x, int y, int w, int h, std::array<float,4> color, float screenW, float screenH,
                   float z = kUseCurrentDepth) {
        const float x0 = (float(x) / screenW) * 2.0f - 1.0f;
        const float y0 = (float(y) / screenH) * 2.0f - 1.0f;
        const float x1 = (float(x + w) / screenW) * 2.0f - 1.0f;
        const float y1 = (float(y + h) / screenH) * 2.0f - 1.0f;
        addRect(x0, y0, x1 - x0, y1 - y0, color, z);
    }
    void addText(const std::vector<GuiVertex>& verts);

    // Obrazki RGBA (np. minimapa): jedna tekstura zamiast tysięcy prostokątów. Osobna kopia na każdą klatkę
    // w locie - updateImage zapisuje dane, uploadImages (przed rysowaniem klatki) kopiuje je z CPU prosto do obrazu
    // tej klatki (Vulkan 1.4 hostImageCopy) tylko po zmianie.
    int createImage(uint32_t width, uint32_t height);
    void updateImage(int id, const std::vector<uint8_t>& rgba);
    void addImage(int id, float x, float y, float w, float h, float z);   // NDC jak addRect
    void uploadImages(uint32_t frameIndex);

    void build(uint32_t frameIndex);
    void recordDrawCommands(VkCommandBuffer cmd, uint32_t frameIndex);
    [[nodiscard]] bool isReady() const { return ready_; }

    void setDepth(float z) { currentDepth_ = z; }
    [[nodiscard]] float getDepth() const { return currentDepth_; }

private:
    float currentDepth_ = 0.0f;
    bool ready_ = false;
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout textureSetLayout_ = VK_NULL_HANDLE;   // set 0: jedna tekstura (push descriptor)
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkImage whiteImage_ = VK_NULL_HANDLE;
    VkDeviceMemory whiteMemory_ = VK_NULL_HANDLE;
    VkImageView whiteView_ = VK_NULL_HANDLE;

    // Bufor wierzchołków osobno dla każdej klatki w locie (pamięć hosta, przepisywany co klatkę).
    std::array<VkBuffer, kGuiMaxFrames> vertexBuffer_{};
    std::array<VkDeviceMemory, kGuiMaxFrames> vertexMemory_{};
    std::array<VkDeviceSize, kGuiMaxFrames> vertexCapacity_{};   // rozmiar bufora w bajtach
    uint32_t vertexCount_ = 0;     // wszystkie wierzchołki klatki
    uint32_t rectVertexCount_ = 0; // z tego prostokąty i tekst (leżą przed obrazkami)
    std::vector<GuiVertex> vertices_;   // zbierane w trakcie klatki, wysyłane w build()

    struct GuiImage {
        uint32_t width = 0, height = 0;
        std::array<VkImage, kGuiMaxFrames> image{};
        std::array<VkDeviceMemory, kGuiMaxFrames> memory{};
        std::array<VkImageView, kGuiMaxFrames> view{};
        std::vector<uint8_t> data;                     // piksele RGBA po stronie CPU
        uint64_t version = 1;                          // rośnie przy każdym updateImage
        std::array<uint64_t, kGuiMaxFrames> uploaded{};   // wersja skopiowana do obrazu danej klatki
        std::vector<GuiVertex> vertices;               // quady tej klatki
        std::array<uint32_t, kGuiMaxFrames> first{}, count{};   // zakres w buforze wierzchołków klatki
    };
    std::vector<GuiImage> images_;
    static constexpr uint32_t kMaxGuiImages = 16;

    void createWhiteTexture();
    void ensureVertexBuffer(VkDeviceSize size, uint32_t frameIndex);
};

} // namespace engine3d
