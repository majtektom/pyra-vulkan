#include "engine/SkyboxRenderer.hpp"

#include "VulkanUtil.hpp"

#include <array>
#include <stdexcept>
#include <vector>

namespace engine3d {

SkyboxRenderer::SkyboxRenderer() = default;
SkyboxRenderer::~SkyboxRenderer() = default;

// Inicjalizacja skyboxa: tworzy pipeline (depth test LEQUAL, bez zapisu depth),
// bufory geometrii (20 wierzcholkow, 5 scian po 4 indeksy TRIANGLE_STRIP).
// Wzorowane na Plazer::CSkyBox::Init(), ktore generuje 20 wierzcholkow i 5 zestawow indeksow.
// Uwaga: deskryptory tekstur sa przekazywane w recordDrawCommands(), a nie tu,
// poniewaz kazda klatka moze miec inny UBO (nalezy uzywac deskryptora biezacej klatki).
void SkyboxRenderer::init(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VkRenderPass renderPass,
    VkDescriptorSetLayout descriptorSetLayout,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    VkExtent2D extent,
    const SkyboxConfig& config,
    const std::string& shaderDir
) {
    if (!config.enabled) {
        enabled_ = false;
        ready_ = false;
        return;
    }
    enabled_ = true;

    // ---------------------------------------------------------------
    // 1. LADOWANIE SHADEROW
    // ---------------------------------------------------------------
    const auto vertCode = vk::readFile(shaderDir + "/skybox.vert.spv");
    const auto fragCode = vk::readFile(shaderDir + "/skybox.frag.spv");

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
    // 9. TWORZENIE PIPELINE'U
    // ---------------------------------------------------------------
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
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

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
    // 10. TWORZENIE GEOMETRII SKYBOXA
    // ---------------------------------------------------------------
    const float odl = config.size;
    const float przes = config.verticalOffset;

    std::vector<Vertex> vertices(20);
    auto setV = [&](std::size_t i, float x, float y, float z, float u, float v) {
        vertices[i] = {{x, y, z}, {1.0f, 1.0f, 1.0f}, {u, v}};
    };

    setV(0,  -odl, 0 - przes, -odl, 0.0f, 0.5f);
    setV(1,   odl, 0 - przes, -odl, 1.0f, 0.5f);
    setV(2,   odl, odl - przes, -odl, 1.0f, 1.0f);
    setV(3,  -odl, odl - przes, -odl, 0.0f, 1.0f);

    setV(4,  -odl, 0 - przes, odl, 1.0f, 0.5f);
    setV(5,   odl, 0 - przes, odl, 0.0f, 0.5f);
    setV(6,   odl, odl - przes, odl, 0.0f, 1.0f);
    setV(7,  -odl, odl - przes, odl, 1.0f, 1.0f);

    setV(8,  -odl, 0 - przes, odl, 0.0f, 0.5f);
    setV(9,  -odl, 0 - przes, -odl, 1.0f, 0.5f);
    setV(10, -odl, odl - przes, -odl, 1.0f, 0.0f);
    setV(11, -odl, odl - przes, odl, 0.0f, 0.0f);

    setV(12, odl, 0 - przes, odl, 1.0f, 0.5f);
    setV(13, odl, 0 - przes, -odl, 0.0f, 0.5f);
    setV(14, odl, odl - przes, -odl, 0.0f, 0.0f);
    setV(15, odl, odl - przes, odl, 1.0f, 0.0f);

    setV(16, odl, odl - przes, -odl, 1.0f, 0.0f);
    setV(17, -odl, odl - przes, -odl, 1.0f, 1.0f);
    setV(18, -odl, odl - przes, odl, 0.0f, 1.0f);
    setV(19, odl, odl - przes, odl, 0.0f, 0.0f);

    std::vector<uint32_t> indices = {
        1, 2, 0, 3,
        5, 4, 6, 7,
        9, 10, 8, 11,
        13, 12, 14, 15,
        17, 16, 18, 19,
    };
    indexCount_ = static_cast<uint32_t>(indices.size());

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

// recordDrawCommands rysuje 5 scian skyboxa, kazda z odpowiednia tekstura.
// faceSets[0] = deskryptor dla front+back (box_12), [1] = left+right (box_34), [2] = top (box_5).
// Kazdy deskryptor musi zawierac UBO biezacej klatki (binding 0) i teksture (binding 1).
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

}  // namespace engine3d
