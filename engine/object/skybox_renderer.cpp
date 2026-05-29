#include "skybox_renderer.h"
#include "../render/renderer.h"
#include "vulkan_util.h"
#include <array>
#include <stdexcept>
#include <vector>

namespace engine3d {

SkyboxRenderer::SkyboxRenderer() = default;
SkyboxRenderer::~SkyboxRenderer() {
    if (renderer_) {
        cleanup(renderer_->device());
    }
}

void SkyboxRenderer::configure(const SkyboxConfig& config, const std::string& shaderDir) {
    config_ = config;
    config_.enabled = true;
    shaderDir_ = shaderDir;
}

void SkyboxRenderer::init(Renderer* renderer) {
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
    const auto vertCode = vk::readFile(shaderDir_ + "/skybox.vert.spv");
    const auto fragCode = vk::readFile(shaderDir_ + "/skybox.frag.spv");

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
    // 2. WEJSCIE WIERZCHOLKOW (wspolny Vertex dla wszystkich pipeline'ow)
    // ---------------------------------------------------------------
    const auto bindingDesc = Vertex::bindingDescription();
    const auto attribDescs = Vertex::attributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribDescs.size());
    vertexInput.pVertexAttributeDescriptions = attribDescs.data();

    // ---------------------------------------------------------------
    // 3. TOPOLOGIA: TRIANGLE_STRIP (4 wierzcholki = 2 trojkaty na sciane)
    // ---------------------------------------------------------------
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    // ---------------------------------------------------------------
    // 4. VIEWPORT
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
    // 5. RASTERYZACJA: bez cullingu (jestesmy wewnatrz kostki)
    // ---------------------------------------------------------------
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // ---------------------------------------------------------------
    // 6. TEST GLEBOKOSCI: LEQUAL, bez zapisu (skybox ma byc zawsze w tle)
    // ---------------------------------------------------------------
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    // ---------------------------------------------------------------
    // 7. MIESZANIE KOLOROW
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
    // 8. LAYOUT PIPELINE'U
    // ---------------------------------------------------------------
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        throw std::runtime_error("failed to create skybox pipeline layout");
    }

    // ---------------------------------------------------------------
    // 9. TWORZENIE PIPELINE'U Z DYNAMIC RENDERING
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
        throw std::runtime_error("failed to create skybox pipeline");
    }

    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);

    // ---------------------------------------------------------------
    // 10. TWORZENIE GEOMETRII SKYBOXA (Plazer-style)
    // ---------------------------------------------------------------
    const float odl = static_cast<float>(config_.size);
    const float przes = static_cast<float>(config_.verticalOffset);
    const float ep = 0.002f;

    std::vector<Vertex> vertices(20);
    auto setV = [&](std::size_t i, float x, float y, float z, float u, float v) {
        vertices[i] = {{{x, y, z}}, {{0.0f, 0.0f, 0.0f}}, {{1.0f, 1.0f, 1.0f}}, {{u, v}}};
    };

    setV(0,  -odl, 0 - przes, -odl, 0.0f + ep, 0.5f + ep);
    setV(1,   odl, 0 - przes, -odl, 1.0f - ep, 0.5f + ep);
    setV(2,   odl, odl - przes, -odl, 1.0f - ep, 1.0f - ep);
    setV(3,  -odl, odl - przes, -odl, 0.0f + ep, 1.0f - ep);

    setV(4,  -odl, 0 - przes, odl, 1.0f - ep, 0.5f + ep);
    setV(5,   odl, 0 - przes, odl, 0.0f + ep, 0.5f + ep);
    setV(6,   odl, odl - przes, odl, 0.0f + ep, 1.0f - ep);
    setV(7,  -odl, odl - przes, odl, 1.0f - ep, 1.0f - ep);

    setV(8,  -odl, 0 - przes, odl, 0.0f + ep, 0.5f - ep);
    setV(9,  -odl, 0 - przes, -odl, 1.0f - ep, 0.5f - ep);
    setV(10, -odl, odl - przes, -odl, 1.0f - ep, 0.0f + ep);
    setV(11, -odl, odl - przes, odl, 0.0f + ep, 0.0f + ep);

    setV(12, odl, 0 - przes, odl, 1.0f - ep, 0.5f - ep);
    setV(13, odl, 0 - przes, -odl, 0.0f + ep, 0.5f - ep);
    setV(14, odl, odl - przes, -odl, 0.0f + ep, 0.0f + ep);
    setV(15, odl, odl - przes, odl, 1.0f - ep, 0.0f + ep);

    setV(16, odl, odl - przes, -odl, 1.0f - ep, 0.0f + ep);
    setV(17, -odl, odl - przes, -odl, 1.0f - ep, 1.0f - ep);
    setV(18, -odl, odl - przes, odl, 0.0f + ep, 1.0f - ep);
    setV(19, odl, odl - przes, odl, 0.0f + ep, 0.0f + ep);

    std::vector<uint32_t> indices = {
        1, 2, 0, 3,
        5, 4, 6, 7,
        9, 10, 8, 11,
        13, 12, 14, 15,
        17, 16, 18, 19,
    };

    VkDeviceSize vbSize = sizeof(Vertex) * vertices.size();
    vk::createDeviceLocalBufferWithStaging(
        device, physicalDevice, commandPool, graphicsQueue,
        vertices.data(), vbSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        vertexBuffer_, vertexBufferMemory_
    );

    VkDeviceSize ibSize = sizeof(uint32_t) * indices.size();
    vk::createDeviceLocalBufferWithStaging(
        device, physicalDevice, commandPool, graphicsQueue,
        indices.data(), ibSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        indexBuffer_, indexBufferMemory_
    );

    ready_ = true;
}

void SkyboxRenderer::cleanup(VkDevice device) {
    if (vertexBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, vertexBuffer_, nullptr);
        vertexBuffer_ = VK_NULL_HANDLE;
    }
    if (vertexBufferMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, vertexBufferMemory_, nullptr);
        vertexBufferMemory_ = VK_NULL_HANDLE;
    }
    if (indexBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, indexBuffer_, nullptr);
        indexBuffer_ = VK_NULL_HANDLE;
    }
    if (indexBufferMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, indexBufferMemory_, nullptr);
        indexBufferMemory_ = VK_NULL_HANDLE;
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

void SkyboxRenderer::render(VkCommandBuffer cmd, uint32_t frameIndex, const RenderView& view) {
    if (!ready_ || !enabled_ || !renderer_) return;

    std::array<VkDescriptorSet, 3> faceSets = {};
    faceSets[0] = renderer_->getTextureDescriptorSet("skybox_fb", frameIndex);
    faceSets[1] = renderer_->getTextureDescriptorSet("skybox_lr", frameIndex);
    faceSets[2] = renderer_->getTextureDescriptorSet("skybox_top", frameIndex);

    recordDrawCommands(cmd, pipelineLayout_, view.cameraPosition, faceSets);
}

void SkyboxRenderer::recordDrawCommands(
    VkCommandBuffer commandBuffer,
    VkPipelineLayout skyboxPipelineLayout,
    const Vec3& cameraPosition,
    const std::array<VkDescriptorSet, 3>& faceSets
) const {
    if (!ready_ || !enabled_) return;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    PushConstants pushConstants{};
    pushConstants.cameraPosition[0] = cameraPosition.x;
    pushConstants.cameraPosition[1] = cameraPosition.y;
    pushConstants.cameraPosition[2] = cameraPosition.z;
    pushConstants.exposure = exposure_;
    vkCmdPushConstants(
        commandBuffer,
        skyboxPipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(PushConstants),
        &pushConstants
    );

    const VkBuffer vb[] = {vertexBuffer_};
    const VkDeviceSize offset[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vb, offset);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer_, 0, VK_INDEX_TYPE_UINT32);

    for (int face = 0; face < 5; ++face) {
        const int texIdx = kFaceMap[face];
        if (texIdx < 3 && faceSets[texIdx] != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(
                commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                skyboxPipelineLayout,
                0, 1, &faceSets[texIdx],
                0, nullptr
            );
        }
        vkCmdDrawIndexed(commandBuffer, 4, 1, face * 4, 0, 0);
    }
}

} // namespace engine3d
