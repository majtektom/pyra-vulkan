#include "feature/gui/gui_renderer.h"
#include "render/pipeline_builder.h"
#include "render/vulkan_util.h"
#include <cstring>
#include <stdexcept>

namespace engine3d {

VkVertexInputBindingDescription GuiVertex::bindingDescription() {
    return {0, sizeof(GuiVertex), VK_VERTEX_INPUT_RATE_VERTEX};
}

std::array<VkVertexInputAttributeDescription, 3> GuiVertex::attributeDescriptions() {
    return {{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GuiVertex, pos)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GuiVertex, texCoord)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(GuiVertex, color)},
    }};
}

void GuiRenderer::init(VkDevice device, VkPhysicalDevice physicalDevice, VkFormat colorFormat, VkFormat depthFormat) {
    device_ = device;
    physicalDevice_ = physicalDevice;

    // GUI używa shaderów nakładki tekstu (ten sam format wierzchołków).
    const std::string dir = "bin/assets/shaders/text_overlay/";
    const vk::ShaderModule vert(device, dir + "text.vert.spv");
    const vk::ShaderModule frag(device, dir + "text.frag.spv");

    // Set 0: jedna tekstura, podawana przy rysowaniu (push descriptors - bez puli i setów).
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &textureSetLayout_);

    // Okna leżą na różnych głębokościach (z) - test LEQUAL układa je jedno nad drugim.
    pipelineLayout_ = vk::createPipelineLayout(device, {textureSetLayout_}, 0, 0, "gui");
    pipeline_ = vk::GraphicsPipelineBuilder()
        .shaders(vert, frag)
        .vertexInput(GuiVertex::bindingDescription(), GuiVertex::attributeDescriptions())
        .cull(VK_CULL_MODE_NONE)
        .depth(true, true, VK_COMPARE_OP_LESS_OR_EQUAL)
        .colorFormat(colorFormat, vk::BlendMode::Alpha)
        .depthFormat(depthFormat)
        .build(device, pipelineLayout_, "gui");

    createWhiteTexture();
    ready_ = true;
}

// Biała tekstura 1x1 (prostokąty i tekst używają tego samego potoku co obrazki) - piksel kopiowany z CPU.
void GuiRenderer::createWhiteTexture() {
    const uint32_t white = 0xFFFFFFFF;
    vk::createImage(device_, physicalDevice_, 1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_HOST_TRANSFER_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, whiteImage_, whiteMemory_);
    vk::hostTransitionToCopyLayout(device_, whiteImage_);
    vk::hostCopyToImage(device_, whiteImage_, &white, 1, 1);
    whiteView_ = vk::createImageView(device_, whiteImage_, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    sampler_ = vk::createClampSampler(device_);
}

void GuiRenderer::addRect(float x, float y, float w, float h, std::array<float,4> color, float z) {
    const float depth = (z == kUseCurrentDepth) ? currentDepth_ : z;
    GuiVertex v;
    v.texCoord[0] = 0.5f;   // środek białej tekstury 1x1
    v.texCoord[1] = 0.5f;
    for (int i = 0; i < 4; ++i) v.color[i] = color[i];
    auto corner = [&](float px, float py) {
        v.pos[0] = px;
        v.pos[1] = py;
        v.pos[2] = depth;
        vertices_.push_back(v);
    };
    // Dwa trójkąty: (lewy-górny, prawy-górny, prawy-dolny) i (lewy-górny, prawy-dolny, lewy-dolny).
    corner(x, y);     corner(x + w, y);     corner(x + w, y + h);
    corner(x, y);     corner(x + w, y + h); corner(x, y + h);
}

void GuiRenderer::addText(const std::vector<GuiVertex>& verts) {
    vertices_.insert(vertices_.end(), verts.begin(), verts.end());
}

int GuiRenderer::createImage(uint32_t width, uint32_t height) {
    if (images_.size() >= kMaxGuiImages || width == 0 || height == 0) return -1;
    GuiImage img;
    img.width = width;
    img.height = height;
    img.data.assign(static_cast<size_t>(width) * height * 4, 0);
    for (uint32_t f = 0; f < kGuiMaxFrames; ++f) {
        // Obraz dla każdej klatki w locie; treść kopiowana z CPU prosto do obrazu (hostImageCopy), bez bufora pośredniego.
        vk::createImage(device_, physicalDevice_, width, height, 1, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_USAGE_HOST_TRANSFER_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        img.image[f], img.memory[f]);
        img.view[f] = vk::createImageView(device_, img.image[f], VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);
        vk::hostTransitionToCopyLayout(device_, img.image[f]);
        vk::hostCopyToImage(device_, img.image[f], img.data.data(), width, height);   // start: czarny obraz
        img.uploaded[f] = img.version;
    }
    images_.push_back(std::move(img));
    return static_cast<int>(images_.size()) - 1;
}

void GuiRenderer::updateImage(int id, const std::vector<uint8_t>& rgba) {
    if (id < 0 || id >= static_cast<int>(images_.size())) return;
    GuiImage& img = images_[static_cast<size_t>(id)];
    if (rgba.size() != img.data.size()) return;
    img.data = rgba;
    ++img.version;
}

void GuiRenderer::addImage(int id, float x, float y, float w, float h, float z) {
    if (id < 0 || id >= static_cast<int>(images_.size())) return;
    auto& out = images_[static_cast<size_t>(id)].vertices;
    GuiVertex v;
    v.color[0] = v.color[1] = v.color[2] = 1.0f;
    v.color[3] = 2.0f;   // znacznik dla shadera (text.hlsl): obrazek - kolor RGBA z tekstury, nie maska glifu
    auto put = [&](float px, float py, float u, float t) {
        v.pos[0] = px; v.pos[1] = py; v.pos[2] = z;
        v.texCoord[0] = u; v.texCoord[1] = t;
        out.push_back(v);
    };
    put(x, y, 0, 0); put(x + w, y, 1, 0); put(x + w, y + h, 1, 1);
    put(x, y, 0, 0); put(x + w, y + h, 1, 1); put(x, y + h, 0, 1);
}

// Przed rysowaniem klatki (po czekaniu na jej fence - GPU nie używa jej obrazów): zmienione obrazki kopiujemy z CPU.
void GuiRenderer::uploadImages(uint32_t frameIndex) {
    for (auto& img : images_) {
        if (img.uploaded[frameIndex] == img.version) continue;
        vk::hostCopyToImage(device_, img.image[frameIndex], img.data.data(), img.width, img.height);
        img.uploaded[frameIndex] = img.version;
    }
}

void GuiRenderer::build(uint32_t frameIndex) {
    // Obrazki dopisujemy za prostokątami w tym samym buforze wierzchołków; rysowane są z własną teksturą
    // (recordDrawCommands), więc zapamiętujemy, gdzie leży każdy z nich.
    rectVertexCount_ = static_cast<uint32_t>(vertices_.size());
    for (auto& img : images_) {
        img.first[frameIndex] = static_cast<uint32_t>(vertices_.size());
        img.count[frameIndex] = static_cast<uint32_t>(img.vertices.size());
        vertices_.insert(vertices_.end(), img.vertices.begin(), img.vertices.end());
        img.vertices.clear();
    }
    vertexCount_ = static_cast<uint32_t>(vertices_.size());
    if (vertexCount_ == 0) return;

    const VkDeviceSize size = sizeof(GuiVertex) * vertices_.size();
    ensureVertexBuffer(size, frameIndex);
    void* mapped = nullptr;
    vkMapMemory(device_, vertexMemory_[frameIndex], 0, size, 0, &mapped);
    std::memcpy(mapped, vertices_.data(), static_cast<size_t>(size));
    vkUnmapMemory(device_, vertexMemory_[frameIndex]);
    vertices_.clear();
}

// Bufor rośnie tylko, gdy wierzchołków jest więcej niż kiedykolwiek wcześniej.
void GuiRenderer::ensureVertexBuffer(VkDeviceSize size, uint32_t frameIndex) {
    if (vertexBuffer_[frameIndex] && size <= vertexCapacity_[frameIndex]) return;
    if (vertexBuffer_[frameIndex]) {
        vkDestroyBuffer(device_, vertexBuffer_[frameIndex], nullptr);
        vkFreeMemory(device_, vertexMemory_[frameIndex], nullptr);
    }
    vertexCapacity_[frameIndex] = size;
    vk::createBuffer(device_, physicalDevice_, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     vertexBuffer_[frameIndex], vertexMemory_[frameIndex]);
}

void GuiRenderer::recordDrawCommands(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (vertexCount_ == 0) return;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer_[frameIndex], &offset);

    // Prostokąty i tekst: biała tekstura.
    const VkDescriptorImageInfo white{sampler_, whiteView_, vk::hostCopyLayout()};
    vk::pushImageSamplers(cmd, pipelineLayout_, 0, &white, 1);
    if (rectVertexCount_ > 0) vkCmdDraw(cmd, rectVertexCount_, 1, 0, 0);

    // Obrazki: każdy ze swoją teksturą.
    for (auto& img : images_) {
        if (img.count[frameIndex] == 0) continue;
        const VkDescriptorImageInfo info{sampler_, img.view[frameIndex], vk::hostCopyLayout()};
        vk::pushImageSamplers(cmd, pipelineLayout_, 0, &info, 1);
        vkCmdDraw(cmd, img.count[frameIndex], 1, img.first[frameIndex], 0);
    }
}

void GuiRenderer::cleanup() {
    for (auto& img : images_) {
        for (uint32_t f = 0; f < kGuiMaxFrames; ++f) {
            if (img.view[f]) vkDestroyImageView(device_, img.view[f], nullptr);
            if (img.image[f]) vkDestroyImage(device_, img.image[f], nullptr);
            if (img.memory[f]) vkFreeMemory(device_, img.memory[f], nullptr);
        }
    }
    images_.clear();
    for (uint32_t f = 0; f < kGuiMaxFrames; f++) {
        if (!vertexBuffer_[f]) continue;
        vkDestroyBuffer(device_, vertexBuffer_[f], nullptr);
        vkFreeMemory(device_, vertexMemory_[f], nullptr);
        vertexBuffer_[f] = VK_NULL_HANDLE;
    }
    if (sampler_) { vkDestroySampler(device_, sampler_, nullptr); sampler_ = VK_NULL_HANDLE; }
    if (whiteView_) { vkDestroyImageView(device_, whiteView_, nullptr); whiteView_ = VK_NULL_HANDLE; }
    if (whiteImage_) { vkDestroyImage(device_, whiteImage_, nullptr); vkFreeMemory(device_, whiteMemory_, nullptr); whiteImage_ = VK_NULL_HANDLE; }
    if (pipeline_) { vkDestroyPipeline(device_, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (pipelineLayout_) { vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr); pipelineLayout_ = VK_NULL_HANDLE; }
    if (textureSetLayout_) { vkDestroyDescriptorSetLayout(device_, textureSetLayout_, nullptr); textureSetLayout_ = VK_NULL_HANDLE; }
    ready_ = false;
}

} // namespace engine3d
