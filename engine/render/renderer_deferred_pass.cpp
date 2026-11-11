#include "render/renderer_impl.h"

namespace engine3d {

// Część 1 na obrazie swapchaina: oświetlenie deferred (pełny ekran), obiekty forward, wstępna głębokość przezroczystych.
void Renderer::RendererImpl::recordLightingPass(VkCommandBuffer cmd, const RenderScene& scene, const RenderView& view, uint32_t imageIndex) {
    const VkViewport viewport = swapchainViewport();
    const VkRect2D scissor = swapchainScissor();
    // === SWAPCHAIN RENDERING ===
    // Transition swapchain color image
    VkImageMemoryBarrier colorBarrier{};
    colorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    colorBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorBarrier.srcAccessMask = 0;
    colorBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    colorBarrier.image = swapchain_->images()[imageIndex];
    colorBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorBarrier.subresourceRange.levelCount = 1;
    colorBarrier.subresourceRange.layerCount = 1;
    // srcStage musi być COLOR_ATTACHMENT_OUTPUT (etap, na którym czeka semafor acquire),
    // inaczej przejście layoutu może się wykonać, zanim silnik prezentacji zwolni obraz.
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &colorBarrier);

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
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, deferredPipelineLayout_, 1, 1, &deferredDescriptorSets_[currentFrame_], 0, nullptr);
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
    dslInfo.bindingCount = 5;
    dslInfo.pBindings = gbufferBindings;
    vkCreateDescriptorSetLayout(device, &dslInfo, nullptr, &deferredDescriptorSetLayout_);

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5 * static_cast<uint32_t>(kMaxFrames)};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kMaxFrames;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    vkCreateDescriptorPool(device, &poolInfo, nullptr, &deferredDescriptorPool_);

    for (int f = 0; f < kMaxFrames; f++) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = deferredDescriptorPool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &deferredDescriptorSetLayout_;
        vkAllocateDescriptorSets(device, &allocInfo, &deferredDescriptorSets_[f]);

        VkWriteDescriptorSet writes[5] = {};
        VkDescriptorImageInfo imageInfos[5] = {};
        for (int a = 0; a < 4; a++) {
            imageInfos[a].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfos[a].imageView = gbufferViews_[a][f];
            imageInfos[a].sampler = gbufferSampler_;
            writes[a].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[a].dstSet = deferredDescriptorSets_[f];
            writes[a].dstBinding = a;
            writes[a].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[a].descriptorCount = 1;
            writes[a].pImageInfo = &imageInfos[a];
        }
        imageInfos[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[4].imageView = shadowMapView_;
        imageInfos[4].sampler = shadowSampler_;
        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = deferredDescriptorSets_[f];
        writes[4].dstBinding = 4;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].descriptorCount = 1;
        writes[4].pImageInfo = &imageInfos[4];
        vkUpdateDescriptorSets(device, 5, writes, 0, nullptr);
    }

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
