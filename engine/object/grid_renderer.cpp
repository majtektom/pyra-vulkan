#include <array>
#include <stdexcept>
#include <vector>
#include "render/pipeline_builder.h"
#include "render/vulkan_util.h"
#include "render/renderer.h"
#include "object/grid_renderer.h"

namespace engine3d {

GridRenderer::GridRenderer() : name_("GridRenderer") {}
GridRenderer::~GridRenderer() {
    if (renderer_) {
        cleanup(renderer_->device());
    }
}

void GridRenderer::configure(const GridConfig& config, const std::string& shaderDir) {
    config_ = config;
    config_.enabled = true;
    shaderDir_ = shaderDir;
}

void GridRenderer::init(Renderer* renderer) {
    renderer_ = renderer;
    if (!enabled_) {
        ready_ = false;
        return;
    }

    VkDevice device = renderer_->device();
    VkPhysicalDevice physicalDevice = renderer_->physicalDevice();
    VkCommandPool commandPool = renderer_->commandPool();
    VkQueue graphicsQueue = renderer_->graphicsQueue();
    VkDescriptorSetLayout descriptorSetLayout = renderer_->descriptorSetLayout();
    VkFormat colorFormat = renderer_->colorFormat();
    VkFormat depthFormat = renderer_->depthFormat();

    // ---------------------------------------------------------------
    // 1. POTOK: linie (LINE_LIST) zamiast trójkątów, bez cullingu, zwykły test i zapis głębokości.
    //    Layout: tylko descriptor set klatki, bez push constants.
    // ---------------------------------------------------------------
    const vk::ShaderModule vert(device, shaderDir_ + "/grid.vert.spv");
    const vk::ShaderModule frag(device, shaderDir_ + "/grid.frag.spv");

    pipelineLayout_ = vk::createPipelineLayout(device, {descriptorSetLayout}, 0, 0, "grid");
    pipeline_ = vk::GraphicsPipelineBuilder()
        .shaders(vert, frag)
        .vertexInput(Vertex::bindingDescription(), vk::filterVertexAttributes(vert.code(), Vertex::attributeDescriptions()))
        .topology(VK_PRIMITIVE_TOPOLOGY_LINE_LIST)
        .cull(VK_CULL_MODE_NONE)
        .colorFormat(colorFormat)
        .depthFormat(depthFormat)
        .build(device, pipelineLayout_, "grid");

    // ---------------------------------------------------------------
    // 2. TWORZENIE BUFORA WIERZCHOLKOW
    // ---------------------------------------------------------------
    const float halfSize = config_.size * 0.5f;
    const float spacing = config_.spacing;
    const auto& color = config_.color;

    const int linesPerAxis = static_cast<int>(config_.size / spacing);
    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<std::size_t>(linesPerAxis) * 4 + 6);

    for (int line = 0; line < linesPerAxis; ++line) {
        const float pos = -halfSize + static_cast<float>(line) * spacing;
        vertices.push_back({{{-halfSize, 0.0f, pos}}, {{0.0f, 1.0f, 0.0f}}, {{color[0], color[1], color[2]}}, {{0.0f, 0.0f}}});
        vertices.push_back({{{ halfSize, 0.0f, pos}}, {{0.0f, 1.0f, 0.0f}}, {{color[0], color[1], color[2]}}, {{0.0f, 0.0f}}});
        vertices.push_back({{{pos, 0.0f, -halfSize}}, {{0.0f, 1.0f, 0.0f}}, {{color[0], color[1], color[2]}}, {{0.0f, 0.0f}}});
        vertices.push_back({{{pos, 0.0f,  halfSize}}, {{0.0f, 1.0f, 0.0f}}, {{color[0], color[1], color[2]}}, {{0.0f, 0.0f}}});
    }

    vertices.push_back({{{0.0f, 0.0f, 0.0f}}, {{0.0f, 1.0f, 0.0f}}, {{1.0f, 0.0f, 0.0f}}, {{0.0f, 0.0f}}});
    vertices.push_back({{{0.0f, halfSize, 0.0f}}, {{0.0f, 1.0f, 0.0f}}, {{1.0f, 0.0f, 0.0f}}, {{0.0f, 0.0f}}});
    vertices.push_back({{{0.0f, 0.0f, 0.0f}}, {{0.0f, 1.0f, 0.0f}}, {{0.0f, 1.0f, 0.0f}}, {{0.0f, 0.0f}}});
    vertices.push_back({{{halfSize, 0.0f, 0.0f}}, {{0.0f, 1.0f, 0.0f}}, {{0.0f, 1.0f, 0.0f}}, {{0.0f, 0.0f}}});
    vertices.push_back({{{0.0f, 0.0f, 0.0f}}, {{0.0f, 1.0f, 0.0f}}, {{0.0f, 0.0f, 1.0f}}, {{0.0f, 0.0f}}});
    vertices.push_back({{{0.0f, 0.0f, halfSize}}, {{0.0f, 1.0f, 0.0f}}, {{0.0f, 0.0f, 1.0f}}, {{0.0f, 0.0f}}});

    vertexCount_ = static_cast<uint32_t>(vertices.size());

    // Przesylamy dane na GPU przez staging buffer.
    const VkDeviceSize bufferSize = sizeof(Vertex) * vertices.size();
    vk::createDeviceLocalBufferWithStaging(
        device, physicalDevice, commandPool, graphicsQueue,
        vertices.data(), bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        vertexBuffer_, vertexBufferMemory_
    );

    ready_ = true;
}

// cleanup niszczy pipeline, layout i bufor wierzcholkow.
void GridRenderer::cleanup(VkDevice device) {
    if (vertexBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, vertexBuffer_, nullptr);
        vertexBuffer_ = VK_NULL_HANDLE;
    }
    if (vertexBufferMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, vertexBufferMemory_, nullptr);
        vertexBufferMemory_ = VK_NULL_HANDLE;
    }
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    ready_ = false;
}

void GridRenderer::render(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView& view) {
    if (!ready_ || !enabled_ || !renderer_) return;
    VkDescriptorSet descriptorSet = renderer_->getTextureDescriptorSet("", frameIndex);
    recordDrawCommands(cmd, descriptorSet, frameIndex);
}

void GridRenderer::recordDrawCommands(
    VkCommandBuffer commandBuffer,
    VkDescriptorSet descriptorSet,
    uint32_t frameIndex
) const {
    if (!ready_ || !enabled_ || vertexCount_ == 0) return;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(
        commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout_, 0, 1, &descriptorSet, 0, nullptr
    );

    const VkBuffer vb[] = {vertexBuffer_};
    const VkDeviceSize offset[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vb, offset);
    vkCmdDraw(commandBuffer, vertexCount_, 1, 0, 0);
}

}  // namespace engine3d
