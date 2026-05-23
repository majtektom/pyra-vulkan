#include "grid_renderer.h"

#include "vulkan_util.h"

#include <array>
#include <stdexcept>
#include <vector>

namespace engine3d {

GridRenderer::GridRenderer() = default;
GridRenderer::~GridRenderer() = default;

// Inicjalizacja tworzy pipeline Vulkan (VK_PRIMITIVE_TOPOLOGY_LINE_LIST) wraz z separatnym
// pipeline layoutem (tylko descriptor set, bez push constants) oraz bufor wierzcholkow.
// Wzorowane na Plazer::Grid3d::Init(), ktore tworzylo vertex buffer z liniami i osiami.
void GridRenderer::init(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VkRenderPass renderPass,
    VkDescriptorSetLayout descriptorSetLayout,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    VkExtent2D extent,
    const GridConfig& config,
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
    const auto vertCode = vk::readFile(shaderDir + "/grid.vert.spv");
    const auto fragCode = vk::readFile(shaderDir + "/grid.frag.spv");

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
    const auto attribDescs = Vertex::attributeDescriptions();

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
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

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
    // Wzorem starego Grid::Init(): generujemy linie w poziomie i pionie na XZ
    // oraz kolorowe osie X (zielona), Y (czerwona), Z (niebieska).
    const float halfSize = config.size * 0.5f;
    const float spacing = config.spacing;
    const auto& color = config.color;

    const int linesPerAxis = static_cast<int>(config.size / spacing);
    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<std::size_t>(linesPerAxis) * 4 + 6);

    // Poziome i pionowe linie siatki (kolor szary z configu).
    for (int line = 0; line < linesPerAxis; ++line) {
        const float pos = -halfSize + static_cast<float>(line) * spacing;
        vertices.push_back({{-halfSize, 0.0f, pos}, {color[0], color[1], color[2]}, {0.0f, 0.0f}});
        vertices.push_back({{ halfSize, 0.0f, pos}, {color[0], color[1], color[2]}, {0.0f, 0.0f}});
        vertices.push_back({{pos, 0.0f, -halfSize}, {color[0], color[1], color[2]}, {0.0f, 0.0f}});
        vertices.push_back({{pos, 0.0f,  halfSize}, {color[0], color[1], color[2]}, {0.0f, 0.0f}});
    }

    // Osie: Y-czerwona, X-zielona, Z-niebieska.
    vertices.push_back({{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}});
    vertices.push_back({{0.0f, halfSize, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}});
    vertices.push_back({{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}});
    vertices.push_back({{halfSize, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}});
    vertices.push_back({{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}});
    vertices.push_back({{0.0f, 0.0f, halfSize}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}});

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

// recordDrawCommands nagrywa polecenia rysowania linii siatki.
// Nalezy wywolac miedzy vkCmdBeginRenderPass a vkCmdEndRenderPass.
void GridRenderer::recordDrawCommands(
    VkCommandBuffer commandBuffer,
    VkPipelineLayout pipelineLayout,
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
