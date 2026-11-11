#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include "feature/font/text_overlay_renderer.h"
#include "render/renderer.h"
#include "render/pipeline_builder.h"
#include "render/vulkan_util.h"
#include "feature/font/utf8.h"

namespace engine3d {

TextOverlayRenderer::TextOverlayRenderer() = default;
TextOverlayRenderer::~TextOverlayRenderer() { cleanup(); }

void TextOverlayRenderer::init(Renderer* renderer) {
    renderer_ = renderer;
    VkDevice device = renderer_->device();
    VkPhysicalDevice physicalDevice = renderer_->physicalDevice();
    VkFormat colorFormat = renderer_->colorFormat();
    VkFormat depthFormat = renderer_->depthFormat();

    device_ = device;
    physicalDevice_ = physicalDevice;

    std::string shaderDir = "bin/assets/shaders/text_overlay/";
    const vk::ShaderModule vert(device, shaderDir + "text.vert.spv");
    const vk::ShaderModule frag(device, shaderDir + "text.frag.spv");

    // Set 0: atlas znaków (jeden sampler obrazu).
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo setLayoutInfo{};
    setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setLayoutInfo.bindingCount = 1;
    setLayoutInfo.pBindings = &samplerBinding;
    vkCreateDescriptorSetLayout(device, &setLayoutInfo, nullptr, &descriptorSetLayout_);

    // Napisy: przezroczystość alfa, bez cullingu, głębokość LEQUAL (etykiety w świecie chowają się za obiektami).
    pipelineLayout_ = vk::createPipelineLayout(device, {descriptorSetLayout_}, 0, 0, "text_overlay");
    pipeline_ = vk::GraphicsPipelineBuilder()
        .shaders(vert, frag)
        .vertexInput(TextVertex::bindingDescription(), TextVertex::attributeDescriptions())
        .cull(VK_CULL_MODE_NONE)
        .depth(true, true, VK_COMPARE_OP_LESS_OR_EQUAL)
        .colorFormat(colorFormat, vk::BlendMode::Alpha)
        .depthFormat(depthFormat)
        .build(device, pipelineLayout_, "text_overlay");
    ready_ = true;
}

void TextOverlayRenderer::render(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView&) {
    if (!ready_ || !fontLoaded_) return;
    buildTexts(frameIndex);
    recordDrawCommands(cmd, frameIndex);
    clearTexts();
}

void TextOverlayRenderer::loadFont(const FontAsset& fontAsset) {
    fontAsset_ = fontAsset;
    fontAtlas_ = FontAtlasLoader::load(fontAsset, {});
    uploadFontAtlas();
    fontLoaded_ = true;
}

// Atlas znaków (fontAtlas_) -> obraz GPU + widok + sampler + descriptor set. Wspólne dla pierwszego wczytania
// i przebudowy po pojawieniu się nowych znaków. Piksele idą z CPU prosto do obrazu (hostImageCopy, Vulkan 1.4).
void TextOverlayRenderer::uploadFontAtlas() {
    vk::createImage(device_, physicalDevice_, fontAtlas_.atlasWidth, fontAtlas_.atlasHeight, 1,
                    fontAtlas_.format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_HOST_TRANSFER_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, fontImage_, fontImageMemory_);
    vk::hostTransitionToCopyLayout(device_, fontImage_);
    vk::hostCopyToImage(device_, fontImage_, fontAtlas_.pixelBytes.data(), fontAtlas_.atlasWidth, fontAtlas_.atlasHeight);

    fontImageView_ = vk::createImageView(device_, fontImage_, fontAtlas_.format, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    fontSampler_ = vk::createClampSampler(device_);

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_);

    VkDescriptorSetAllocateInfo descAlloc{};
    descAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descAlloc.descriptorPool = descriptorPool_;
    descAlloc.descriptorSetCount = 1;
    descAlloc.pSetLayouts = &descriptorSetLayout_;
    vkAllocateDescriptorSets(device_, &descAlloc, &descriptorSet_);

    VkDescriptorImageInfo imageInfo{fontSampler_, fontImageView_, vk::hostCopyLayout()};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

void TextOverlayRenderer::deleteVertexBuffer(uint32_t fi) {
    if (vertexBuffer_[fi] != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, vertexBuffer_[fi], nullptr);
        vertexBuffer_[fi] = VK_NULL_HANDLE;
    }
    if (vertexBufferMemory_[fi] != VK_NULL_HANDLE) {
        vkFreeMemory(device_, vertexBufferMemory_[fi], nullptr);
        vertexBufferMemory_[fi] = VK_NULL_HANDLE;
    }
    vertexBufferCapacity_[fi] = 0;
}

void TextOverlayRenderer::addText(const std::string& text, float x, float y, float scale,
                                   const std::array<float, 4>& color,
                                   float screenWidth, float screenHeight, float depth) {
    if (!fontLoaded_ || fontAtlas_.glyphs.empty()) return;
    labels_.push_back({text, x, y, scale, color, screenWidth, screenHeight, depth});
}

float TextOverlayRenderer::measureText(const std::string& text, float scale) const {
    if (!fontLoaded_) return 0.0f;
    float width = 0.0f;
    float lineWidth = 0.0f;
    const float fallbackAdvance = static_cast<float>(fontAtlas_.lineHeight) * 0.5f;
    for (uint32_t cp : utf8::decode(text)) {
        if (cp == '\n') {
            width = std::max(width, lineWidth);
            lineWidth = 0.0f;
            continue;
        }
        auto it = fontAtlas_.glyphs.find(cp);
        lineWidth += (it != fontAtlas_.glyphs.end() ? static_cast<float>(it->second.advanceX) : fallbackAdvance) * scale;
    }
    return std::max(width, lineWidth);
}

void TextOverlayRenderer::clearTexts() {
    labels_.clear();
}

void TextOverlayRenderer::buildTexts(uint32_t fi) {
    if (!fontLoaded_ || labels_.empty()) { vertexCount_ = 0; return; }

    wantedCodepoints_.clear();
    for (const auto& label : labels_) {
        for (auto cp : utf8::decode(label.text)) wantedCodepoints_.insert(cp);
    }
    ensureGlyphs();
    if (!fontLoaded_ || fontAtlas_.glyphs.empty()) { vertexCount_ = 0; return; }

    std::vector<TextVertex> vertices;

    for (const auto& label : labels_) {
        float cursorX = label.x;
        float baselineY = label.y;

        auto codepoints = utf8::decode(label.text);
        for (uint32_t cp : codepoints) {
            if (cp == '\n') {
                cursorX = label.x;
                baselineY += fontAtlas_.lineHeight * label.scale;
                continue;
            }
            auto it = fontAtlas_.glyphs.find(cp);
            if (it == fontAtlas_.glyphs.end()) {
                it = fontAtlas_.glyphs.find(static_cast<uint32_t>('?'));
                if (it == fontAtlas_.glyphs.end()) continue;
            }

            const auto& g = it->second;
            float gx = cursorX + g.bearingX * label.scale;
            float gy = baselineY - g.bearingY * label.scale;
            float gw = g.width * label.scale;
            float gh = g.height * label.scale;

            float ndcX0 = (gx      / label.screenWidth)  * 2.0f - 1.0f;
            float ndcX1 = ((gx+gw) / label.screenWidth)  * 2.0f - 1.0f;
            float ndcY0 = (gy      / label.screenHeight) * 2.0f - 1.0f;
            float ndcY1 = ((gy+gh) / label.screenHeight) * 2.0f - 1.0f;

            TextVertex v0, v1, v2, v3;
            v0.pos[0] = ndcX0; v0.pos[1] = ndcY0; v0.pos[2] = label.depth; v0.texCoord[0] = g.u0; v0.texCoord[1] = g.v0;
            v1.pos[0] = ndcX1; v1.pos[1] = ndcY0; v1.pos[2] = label.depth; v1.texCoord[0] = g.u1; v1.texCoord[1] = g.v0;
            v2.pos[0] = ndcX1; v2.pos[1] = ndcY1; v2.pos[2] = label.depth; v2.texCoord[0] = g.u1; v2.texCoord[1] = g.v1;
            v3.pos[0] = ndcX0; v3.pos[1] = ndcY1; v3.pos[2] = label.depth; v3.texCoord[0] = g.u0; v3.texCoord[1] = g.v1;

            for (int i = 0; i < 4; ++i) {
                v0.color[i] = v1.color[i] = v2.color[i] = v3.color[i] = label.color[i];
            }

            vertices.push_back(v0); vertices.push_back(v1); vertices.push_back(v2);
            vertices.push_back(v2); vertices.push_back(v3); vertices.push_back(v0);

            cursorX += g.advanceX * label.scale;
        }
    }

    if (vertices.empty()) { vertexCount_ = 0; return; }

    vertexCount_ = static_cast<uint32_t>(vertices.size());
    VkDeviceSize requiredSize = sizeof(TextVertex) * vertices.size();

    if (vertexBuffer_[fi] == VK_NULL_HANDLE || requiredSize > vertexBufferCapacity_[fi]) {
        deleteVertexBuffer(fi);
        vertexBufferCapacity_[fi] = requiredSize;
        vk::createBuffer(device_, physicalDevice_, requiredSize,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         vertexBuffer_[fi], vertexBufferMemory_[fi]);
    }

    void* mapped;
    vkMapMemory(device_, vertexBufferMemory_[fi], 0, requiredSize, 0, &mapped);
    std::memcpy(mapped, vertices.data(), static_cast<size_t>(requiredSize));
    vkUnmapMemory(device_, vertexBufferMemory_[fi]);
}

void TextOverlayRenderer::ensureGlyphs() {
    if (!fontLoaded_ || fontAsset_.path.empty()) return;

    bool needRebuild = false;
    for (auto cp : wantedCodepoints_) {
        if (!fontAtlas_.glyphs.count(cp)) { needRebuild = true; break; }
    }
    if (!needRebuild) return;

    // Stary atlas i jego descriptor set mogą być używane przez klatki w locie.
    // Przebudowa jest rzadka (tylko przy nowych znakach), więc proste czekanie na GPU wystarcza.
    vkDeviceWaitIdle(device_);
    releaseFontResources();

    std::vector<uint32_t> allGlyphs(wantedCodepoints_.begin(), wantedCodepoints_.end());
    fontAtlas_ = FontAtlasLoader::load(fontAsset_, allGlyphs);

    uploadFontAtlas();
}

void TextOverlayRenderer::releaseFontResources() {
    // Zniszczenie puli zwalnia jej sety (pula nie ma FREE_DESCRIPTOR_SET_BIT).
    descriptorSet_ = VK_NULL_HANDLE;
    if (descriptorPool_ != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device_, descriptorPool_, nullptr); descriptorPool_ = VK_NULL_HANDLE; }
    if (fontSampler_ != VK_NULL_HANDLE) { vkDestroySampler(device_, fontSampler_, nullptr); fontSampler_ = VK_NULL_HANDLE; }
    if (fontImageView_ != VK_NULL_HANDLE) { vkDestroyImageView(device_, fontImageView_, nullptr); fontImageView_ = VK_NULL_HANDLE; }
    if (fontImage_ != VK_NULL_HANDLE) { vkDestroyImage(device_, fontImage_, nullptr); vkFreeMemory(device_, fontImageMemory_, nullptr); fontImage_ = VK_NULL_HANDLE; }
}

void TextOverlayRenderer::recordDrawCommands(VkCommandBuffer commandBuffer, uint32_t fi) const {
    if (!ready_ || !fontLoaded_ || vertexCount_ == 0) return;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);

    VkBuffer vb[] = {vertexBuffer_[fi]};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vb, offsets);
    vkCmdDraw(commandBuffer, vertexCount_, 1, 0, 0);
}

void TextOverlayRenderer::cleanup() {
    for (uint32_t i = 0; i < kTxtMaxFrames; i++) {
        deleteVertexBuffer(i);
    }
    releaseFontResources();
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        descriptorSetLayout_ = VK_NULL_HANDLE;
    }
    ready_ = false;
    fontLoaded_ = false;
}

} // namespace engine3d
