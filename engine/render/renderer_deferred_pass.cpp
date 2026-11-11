#include "render/renderer_impl.h"

namespace engine3d {

// Część 1 na obrazie swapchaina: oświetlenie deferred (pełny ekran), obiekty forward, wstępna głębokość przezroczystych.
void Renderer::RendererImpl::recordLightingPass(VkCommandBuffer cmd, const RenderScene& scene, const RenderView& view, uint32_t imageIndex) {
    const VkViewport viewport = swapchainViewport();
    const VkRect2D scissor = swapchainScissor();
    // === SWAPCHAIN RENDERING ===
    // Obraz swapchaina staje się celem rysowania. srcStage musi być COLOR_ATTACHMENT_OUTPUT (etap, na którym czeka
    // semafor acquire), inaczej przejście układu może się wykonać, zanim silnik prezentacji zwolni obraz.
    vk::imageBarrier(cmd, {.image = swapchain_->images()[imageIndex],
                           .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                           .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                           .srcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, .srcAccess = VK_ACCESS_2_NONE,
                           .dstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, .dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT});

    // During GBuffer pass, depth was written (from clear=1.0, then opaque depths < 1.0).
    // The depth attachment stays in DEPTH_ATTACHMENT layout — no barrier needed.
    VkRenderingAttachmentInfo swColorAttachment{};
    swColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    swColorAttachment.imageView = swapchain_->imageView(imageIndex);
    swColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    swColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    swColorAttachment.clearValue.color = {{clearColor_[0], clearColor_[1], clearColor_[2], clearColor_[3]}};

    VkRenderingAttachmentInfo swDepthAttachment{};
    swDepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    swDepthAttachment.imageView = swapchain_->depthImageView(imageIndex);
    swDepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    swDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    swDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = {{0, 0}, swapchain_->extent()};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &swColorAttachment;
    renderingInfo.pDepthAttachment = &swDepthAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // PART 1: Render scene (without ocean) to swapchain
    // ===================================================
    executeFeatures(cmd, view, IRenderFeature::Stage::BeforeOpaque);

    // Deferred lighting pass (fullscreen, reads GBuffer)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, deferredPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, deferredPipelineLayout_, 0, 1, &descriptorSets_[currentFrame_], 0, nullptr);
    // Set 1 (push descriptors): G-Buffer tej klatki i mapa cieni - podane wprost, bez puli i setów na każdą klatkę.
    VkDescriptorImageInfo inputs[kGBufferAttachments + 1];
    for (int a = 0; a < kGBufferAttachments; a++) {
        inputs[a] = {gbufferSampler_, gbufferViews_[a][currentFrame_], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    }
    inputs[kGBufferAttachments] = {shadowSampler_, shadowMapView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    vk::pushImageSamplers(cmd, deferredPipelineLayout_, 1, inputs, kGBufferAttachments + 1);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    for (auto& pass : passes_) {
        if (pass->getName() == "OpaquePass") {
            pass->execute(cmd, currentFrame_, scene, descriptorSets_[currentFrame_]);
        }
    }

    executeFeatures(cmd, view, IRenderFeature::Stage::AfterOpaque);

    // Wstępny zapis głębokości przezroczystych obiektów z Material::depthPrepass - tutaj, bo w części 2
    // bufor głębokości jest tylko do odczytu (próbkuje go ocean).
    for (auto& pass : passes_) {
        if (pass->getName() == "TransparentDepthPrepass") {
            pass->execute(cmd, currentFrame_, scene, descriptorSets_[currentFrame_]);
        }
    }

    // End first pass — scene render to swapchain is done
    vkCmdEndRendering(cmd);
}

void Renderer::RendererImpl::createDeferredPipeline() {
    VkDevice device = vulkanContext_->device();

    std::string shaderDir = "bin/assets/shaders/deferred/";
    auto vertCode = vk::readFile(shaderDir + "deferred.vert.spv");
    auto fragCode = vk::readFile(shaderDir + "deferred.frag.spv");
    VkShaderModule vertModule = vk::createShaderModule(device, vertCode);
    VkShaderModule fragModule = vk::createShaderModule(device, fragCode);

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "vertexMain";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "fragmentMain";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 0;
    vertexInput.vertexAttributeDescriptionCount = 0;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttachment;

    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Set 1: GBuffer textures + Shadow Map
    VkDescriptorSetLayoutBinding gbufferBindings[5] = {};
    for (int i = 0; i < 4; i++) {
        gbufferBindings[i].binding = i;
        gbufferBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        gbufferBindings[i].descriptorCount = 1;
        gbufferBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    gbufferBindings[4].binding = 4;
    gbufferBindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    gbufferBindings[4].descriptorCount = 1;
    gbufferBindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dslInfo{};
    dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT;   // wypełniany przy rysowaniu (push descriptors)
    dslInfo.bindingCount = 5;
    dslInfo.pBindings = gbufferBindings;
    vkCreateDescriptorSetLayout(device, &dslInfo, nullptr, &deferredDescriptorSetLayout_);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkDescriptorSetLayout setLayouts[2] = {descriptorSetLayout_, deferredDescriptorSetLayout_};
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 0;
    vkCreatePipelineLayout(device, &layoutInfo, nullptr, &deferredPipelineLayout_);

    VkFormat colorFormat = swapchain_->imageFormat();
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;
    // Przebieg, w którym rysujemy oświetlenie, ma podpięty depth — format musi się zgadzać.
    renderingInfo.depthAttachmentFormat = swapchain_->depthFormat();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = deferredPipelineLayout_;
    pipelineInfo.pNext = &renderingInfo;

    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &deferredPipeline_);
    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);
}

} // namespace engine3d
