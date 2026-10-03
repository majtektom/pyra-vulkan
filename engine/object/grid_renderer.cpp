#include <array>
#include <stdexcept>
#include <vector>
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
    VkExtent2D extent = renderer_->extent();
    VkDescriptorSetLayout descriptorSetLayout = renderer_->descriptorSetLayout();
    VkFormat colorFormat = renderer_->colorFormat();
    VkFormat depthFormat = renderer_->depthFormat();

    // ---------------------------------------------------------------
    // 1. LADOWANIE SHADEROW
    // ---------------------------------------------------------------
    const auto vertCode = vk::readFile(shaderDir_ + "/grid.vert.spv");
    const auto fragCode = vk::readFile(shaderDir_ + "/grid.frag.spv");

    const VkShaderModule vertModule = vk::createShaderModule(device, vertCode);
    const VkShaderModule fragModule = vk::createShaderModule(device, fragCode);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "vertexMain";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "fragmentMain";

    const std::array<VkPipelineShaderStageCreateInfo, 2> stages = {vertStage, fragStage};

    // ---------------------------------------------------------------
    // 2. USTAWIENIA WEJSCIA WIERZCHOLKOW (wspoldzielone z Vertex)
    // ---------------------------------------------------------------
    const auto bindingDesc = Vertex::bindingDescription();
    const auto attribDescs = vk::filterVertexAttributes(vertCode, Vertex::attributeDescriptions());

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribDescs.size());
    vertexInput.pVertexAttributeDescriptions = attribDescs.data();

    // ---------------------------------------------------------------
    // 3. TOPOLOGIA: LINIE (w przeciwienstwie do trojkatow w pipeline sceny)
    // ---------------------------------------------------------------
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    // ---------------------------------------------------------------
    // 4. VIEWPORT I SCISSOR
    // ---------------------------------------------------------------
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // ---------------------------------------------------------------
    // 5. USTAWIENIA RASTERYZACJI
    // ---------------------------------------------------------------
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;  // Szerokosc linii 1 piksel.
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // ---------------------------------------------------------------
    // 6. TEST GLEBOKOSCI (włączony, siatka ma byc zaslaniana przez obiekty)
    // ---------------------------------------------------------------
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    // ---------------------------------------------------------------
    // 7. MIESZANIE KOLOROW (proste, bez alphy)
    // ---------------------------------------------------------------
    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                 VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT |
                                 VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttach;

    // ---------------------------------------------------------------
    // 8. LAYOUT PIPELINE'U (tylko descriptor set, bez push constants)
    // ---------------------------------------------------------------
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 0;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        throw std::runtime_error("failed to create grid pipeline layout");
    }

    // ---------------------------------------------------------------
    // 9. TWORZENIE PIPELINE'U
    // ---------------------------------------------------------------
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    std::vector<VkDynamicState> dynStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dynamicState.pDynamicStates = dynStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;
    renderingInfo.depthAttachmentFormat = depthFormat;
    pipelineInfo.pNext = &renderingInfo;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_) != VK_SUCCESS) {
        vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        throw std::runtime_error("failed to create grid pipeline");
    }

    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);

    // ---------------------------------------------------------------
    // 10. TWORZENIE BUFORA WIERZCHOLKOW
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
