#include "render/renderer_impl.h"

namespace engine3d {

// G-Buffer: obiekty nieprzezroczyste zapisują albedo, normalne, materiał i pozycję (oświetlenie - deferred).
void Renderer::RendererImpl::recordGBufferPass(VkCommandBuffer cmd, const RenderScene& scene, uint32_t imageIndex) {
    const VkViewport viewport = swapchainViewport();
    const VkRect2D scissor = swapchainScissor();
    // === GBUFFER PASS ===
    // Bariery przed G-Buffer: 4 tekstury G-Buffera (czytane w poprzedniej klatce przez oświetlenie) stają się celem
    // rysowania, a głębia swapchaina - z UNDEFINED (loadOp = CLEAR, stara zawartość niepotrzebna; obsługuje też
    // pierwsze użycie obrazu po odtworzeniu swapchaina).
    vk::ImageBarrier toDraw[kGBufferAttachments + 1];
    for (int a = 0; a < kGBufferAttachments; a++) {
        toDraw[a] = {.image = gbufferImages_[a][currentFrame_],
                     .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     .srcStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, .srcAccess = VK_ACCESS_2_NONE,
                     .dstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, .dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
    }
    toDraw[kGBufferAttachments] = {.image = swapchain_->depthImage(imageIndex),
                                   .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                   .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                   .srcStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                   .srcAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                   .dstStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                   .dstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                   .aspect = VK_IMAGE_ASPECT_DEPTH_BIT};
    vk::imageBarriers(cmd, toDraw, kGBufferAttachments + 1);

    VkRenderingAttachmentInfo gbColorAttachments[4] = {};
    for (int a = 0; a < kGBufferAttachments; a++) {
        gbColorAttachments[a].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        gbColorAttachments[a].imageView = gbufferViews_[a][currentFrame_];
        gbColorAttachments[a].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        gbColorAttachments[a].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        gbColorAttachments[a].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        float clearZero[4] = {0,0,0,0};
        memcpy(gbColorAttachments[a].clearValue.color.float32, clearZero, sizeof(clearZero));
    }

    VkRenderingAttachmentInfo gbDepthAttachment{};
    gbDepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    gbDepthAttachment.imageView = swapchain_->depthImageView(imageIndex);
    gbDepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    gbDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    gbDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    gbDepthAttachment.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo gbRenderingInfo{};
    gbRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    gbRenderingInfo.renderArea = {{0, 0}, swapchain_->extent()};
    gbRenderingInfo.layerCount = 1;
    gbRenderingInfo.colorAttachmentCount = kGBufferAttachments;
    gbRenderingInfo.pColorAttachments = gbColorAttachments;
    gbRenderingInfo.pDepthAttachment = &gbDepthAttachment;

    vkCmdBeginRendering(cmd, &gbRenderingInfo);

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // G-Buffer: obiekty nieprzezroczyste. Kolejność ma dwa cele:
    //  1. mało zmian stanu - grupa potoku (zwykłe, animowane, teren z własnym materiałem), a w niej
    //     siatka; DrawBindCache pomija wtedy powtórne vkCmdBindPipeline/DescriptorSets/buforów,
    //  2. front-to-back - bliższe najpierw, więc test głębokości (early-Z) odrzuca zasłonięte piksele
    //     dalszych obiektów, zanim policzy się ich shader. Teren rysujemy na końcu: domy i drzewa
    //     zasłaniają spory jego kawałek, a jego shader (teselacja, warstwy tekstur) jest najdroższy.
    // Kompromis 1 i 2: odległość dzielimy na pierścienie po kGBufferRing jednostek; w pierścieniu
    // paczki są pogrupowane po siatce (np. wszystkie drzewa jednego rodzaju), dalej - po odległości.
    const auto& opaquePackets = scene.getOpaquePackets();
    Profiler::get().count("paczki nieprzezroczyste", static_cast<double>(opaquePackets.size()));
    Profiler::get().count("paczki cieni", static_cast<double>(scene.shadowPacketCount()));

    constexpr float kGBufferRing = 40.0f;
    gbufferOrder_.clear();
    gbufferOrder_.reserve(opaquePackets.size());
    for (const auto& packet : opaquePackets) {
        if (packet.vertexBuffer == VK_NULL_HANDLE || packet.forwardOpaquePass) continue;
        if (packet.gbufferMaterialPass && !packet.material) continue;
        GBufferDraw d;
        d.packet = &packet;
        d.group = packet.gbufferMaterialPass ? 2 : (packet.boneDescriptorSet != VK_NULL_HANDLE ? 1 : 0);
        d.pipeline = packet.gbufferMaterialPass ? packet.material->getPipeline().pipeline : VK_NULL_HANDLE;
        d.ring = static_cast<int>(std::sqrt(std::max(packet.distanceToCamera, 0.0f)) / kGBufferRing);   // distanceToCamera = kwadrat odległości
        gbufferOrder_.push_back(d);
    }
    std::sort(gbufferOrder_.begin(), gbufferOrder_.end(), [](const GBufferDraw& a, const GBufferDraw& b) {
        if (a.group != b.group) return a.group < b.group;
        if (a.pipeline != b.pipeline) return a.pipeline < b.pipeline;
        if (a.ring != b.ring) return a.ring < b.ring;
        if (a.packet->vertexBuffer != b.packet->vertexBuffer) return a.packet->vertexBuffer < b.packet->vertexBuffer;
        return a.packet->distanceToCamera < b.packet->distanceToCamera;
    });

    DrawBindCache bound;
    for (const GBufferDraw& draw : gbufferOrder_) {
        const RenderPacket& packet = *draw.packet;
        if (packet.gbufferMaterialPass) {
            // Teren (własny shader z teselacją) zapisuje G-Buffer swoim potokiem materiału.
            drawRenderPacket(cmd, packet, packet.material->getPipeline(), descriptorSets_[currentFrame_], bound);
            continue;
        }

        bool isSkinned = (packet.boneDescriptorSet != VK_NULL_HANDLE);

        if (isSkinned) {
            bound.bindGlobalSet(cmd, gbufferSkinnedPipelineLayout_, descriptorSets_[currentFrame_]);
            bound.bindPipeline(cmd, gbufferSkinnedPipeline_);
            bound.bindSet1(cmd, gbufferSkinnedPipelineLayout_, packet.boneDescriptorSet);
        } else {
            bound.bindGlobalSet(cmd, gbufferPipelineLayout_, descriptorSets_[currentFrame_]);
            bound.bindPipeline(cmd, gbufferPipeline_);
        }

        bound.bindVertexBuffer(cmd, 0, packet.vertexBuffer);
        if (packet.skinVertexBuffer != VK_NULL_HANDLE) bound.bindVertexBuffer(cmd, 1, packet.skinVertexBuffer);
        if (packet.indexBuffer != VK_NULL_HANDLE) bound.bindIndexBuffer(cmd, packet.indexBuffer);

        const MaterialPushConstants push = makeMaterialPush(packet);   // układ: push_constants.h
        VkPipelineLayout layout = isSkinned ? gbufferSkinnedPipelineLayout_ : gbufferPipelineLayout_;
        vkCmdPushConstants(cmd, layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);

        if (packet.indexBuffer != VK_NULL_HANDLE)
            vkCmdDrawIndexed(cmd, packet.indexCount, 1, 0, 0, 0);
    }
    vkCmdEndRendering(cmd);
    Profiler::get().count("pominięte bindowania G-Buffer", static_cast<double>(bound.skipped));

    // G-Buffer gotowy: zapisy rysowania widoczne dla odczytu w shaderze oświetlenia (głębia zostaje celem rysowania).
    vk::ImageBarrier toRead[kGBufferAttachments];
    for (int a = 0; a < kGBufferAttachments; a++) {
        toRead[a] = {.image = gbufferImages_[a][currentFrame_],
                     .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     .srcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, .srcAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                     .dstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, .dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
    }
    vk::imageBarriers(cmd, toRead, kGBufferAttachments);

}

void Renderer::RendererImpl::cleanupGBuffer() {
    VkDevice device = vulkanContext_->device();
    for (int f = 0; f < kMaxFrames; f++) {
        for (int a = 0; a < kGBufferAttachments; a++) {
            if (gbufferViews_[a][f]) { vkDestroyImageView(device, gbufferViews_[a][f], nullptr); gbufferViews_[a][f] = VK_NULL_HANDLE; }
            if (gbufferImages_[a][f]) { vkDestroyImage(device, gbufferImages_[a][f], nullptr); gbufferImages_[a][f] = VK_NULL_HANDLE; }
            if (gbufferMemories_[a][f]) { vkFreeMemory(device, gbufferMemories_[a][f], nullptr); gbufferMemories_[a][f] = VK_NULL_HANDLE; }
        }
    }
    if (gbufferSampler_) { vkDestroySampler(device, gbufferSampler_, nullptr); gbufferSampler_ = VK_NULL_HANDLE; }
    if (gbufferPipeline_) { vkDestroyPipeline(device, gbufferPipeline_, nullptr); gbufferPipeline_ = VK_NULL_HANDLE; }
    if (gbufferPipelineLayout_) { vkDestroyPipelineLayout(device, gbufferPipelineLayout_, nullptr); gbufferPipelineLayout_ = VK_NULL_HANDLE; }
    if (gbufferSkinnedPipeline_) { vkDestroyPipeline(device, gbufferSkinnedPipeline_, nullptr); gbufferSkinnedPipeline_ = VK_NULL_HANDLE; }
    if (gbufferSkinnedPipelineLayout_) { vkDestroyPipelineLayout(device, gbufferSkinnedPipelineLayout_, nullptr); gbufferSkinnedPipelineLayout_ = VK_NULL_HANDLE; }
    if (deferredPipeline_) { vkDestroyPipeline(device, deferredPipeline_, nullptr); deferredPipeline_ = VK_NULL_HANDLE; }
    if (deferredPipelineLayout_) { vkDestroyPipelineLayout(device, deferredPipelineLayout_, nullptr); deferredPipelineLayout_ = VK_NULL_HANDLE; }
    if (deferredDescriptorSetLayout_) { vkDestroyDescriptorSetLayout(device, deferredDescriptorSetLayout_, nullptr); deferredDescriptorSetLayout_ = VK_NULL_HANDLE; }
}

void Renderer::RendererImpl::createGBufferResources() {
    VkDevice device = vulkanContext_->device();
    VkPhysicalDevice pd = vulkanContext_->physicalDevice();
    VkExtent2D ext = swapchain_->extent();

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 1.0f;
    vkCreateSampler(device, &samplerInfo, nullptr, &gbufferSampler_);

    for (int f = 0; f < kMaxFrames; f++) {
        for (int a = 0; a < kGBufferAttachments; a++) {
            VkImageCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ci.imageType = VK_IMAGE_TYPE_2D;
            ci.extent = {ext.width, ext.height, 1};
            ci.mipLevels = 1; ci.arrayLayers = 1;
            ci.format = gbufferAttachmentFormat(a);
            ci.tiling = VK_IMAGE_TILING_OPTIMAL;
            ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            ci.samples = VK_SAMPLE_COUNT_1_BIT;
            ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            vkCreateImage(device, &ci, nullptr, &gbufferImages_[a][f]);

            VkMemoryRequirements mr;
            vkGetImageMemoryRequirements(device, gbufferImages_[a][f], &mr);
            VkMemoryAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize = mr.size;
            ai.memoryTypeIndex = vk::findMemoryType(pd, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            vkAllocateMemory(device, &ai, nullptr, &gbufferMemories_[a][f]);
            vkBindImageMemory(device, gbufferImages_[a][f], gbufferMemories_[a][f], 0);

            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = gbufferImages_[a][f];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = gbufferAttachmentFormat(a);
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.levelCount = 1;
            vi.subresourceRange.layerCount = 1;
            vkCreateImageView(device, &vi, nullptr, &gbufferViews_[a][f]);

            vk::transitionImageLayout(device, vulkanContext_->commandPool(), vulkanContext_->graphicsQueue(),
                                      gbufferImages_[a][f], gbufferAttachmentFormat(a),
                                      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
        }
    }

    createGBufferPipeline();
    createGBufferSkinnedPipeline();
    createDeferredPipeline();
}

void Renderer::RendererImpl::createGBufferPipeline() {
    VkDevice device = vulkanContext_->device();

    std::string shaderDir = "bin/assets/shaders/gbuffer/";
    auto vertCode = vk::readFile(shaderDir + "gbuffer.vert.spv");
    auto fragCode = vk::readFile(shaderDir + "gbuffer.frag.spv");
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

    auto bindingDesc = Vertex::bindingDescription();
    // Tylko atrybuty czytane przez vertex shader (np. cienie: pozycja + UV).
    auto attrDescs = vk::filterVertexAttributes(vertCode, Vertex::attributeDescriptions());

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = wireframeEnabled_ ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAttachments[4] = {};
    for (int i = 0; i < 4; i++) {
        blendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 4;
    colorBlending.pAttachments = blendAttachments;

    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = 256;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkDescriptorSetLayout setLayouts[2] = {descriptorSetLayout_, boneDescriptorSetLayout_};
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(device, &layoutInfo, nullptr, &gbufferPipelineLayout_);

    VkFormat depthFmt = swapchain_->depthFormat();
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 4;
    VkFormat formats[4] = {gbufferAttachmentFormat(0), gbufferAttachmentFormat(1),
                           gbufferAttachmentFormat(2), gbufferAttachmentFormat(3)};
    renderingInfo.pColorAttachmentFormats = formats;
    renderingInfo.depthAttachmentFormat = depthFmt;

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
    pipelineInfo.layout = gbufferPipelineLayout_;
    pipelineInfo.pNext = &renderingInfo;

    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &gbufferPipeline_);
    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);
}

void Renderer::RendererImpl::createGBufferSkinnedPipeline() {
    VkDevice device = vulkanContext_->device();
    std::string shaderDir = "bin/assets/shaders/gbuffer_skinned/";
    auto vertCode = vk::readFile(shaderDir + "gbuffer_skinned.vert.spv");
    auto fragCode = vk::readFile(shaderDir + "gbuffer_skinned.frag.spv");
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

    auto bindingDesc = Vertex::bindingDescription();
    auto attrDescs = Vertex::attributeDescriptions();

    VkVertexInputBindingDescription binding2{};
    binding2.binding = 1;
    binding2.stride = sizeof(uint32_t) * 4 + sizeof(float) * 4;
    binding2.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription skinAttrs[2]{};
    skinAttrs[0].binding = 1;
    skinAttrs[0].location = 4;
    skinAttrs[0].format = VK_FORMAT_R32G32B32A32_UINT;
    skinAttrs[0].offset = 0;
    skinAttrs[1].binding = 1;
    skinAttrs[1].location = 5;
    skinAttrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    skinAttrs[1].offset = sizeof(uint32_t) * 4;

    std::vector<VkVertexInputBindingDescription> bindings = {bindingDesc, binding2};
    std::vector<VkVertexInputAttributeDescription> allAttrs(attrDescs.begin(), attrDescs.end());
    allAttrs.push_back(skinAttrs[0]);
    allAttrs.push_back(skinAttrs[1]);
    allAttrs = vk::filterVertexAttributes(vertCode, allAttrs);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(allAttrs.size());
    vertexInput.pVertexAttributeDescriptions = allAttrs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = wireframeEnabled_ ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.0f;
    rasterizer.depthBiasSlopeFactor = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAttachments[4] = {};
    for (int i = 0; i < 4; i++)
        blendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 4;
    colorBlending.pAttachments = blendAttachments;

    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = 256;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkDescriptorSetLayout setLayouts[2] = {descriptorSetLayout_, boneDescriptorSetLayout_};
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    vkCreatePipelineLayout(device, &layoutInfo, nullptr, &gbufferSkinnedPipelineLayout_);

    VkFormat depthFmt = swapchain_->depthFormat();
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 4;
    VkFormat formats[4] = {gbufferAttachmentFormat(0), gbufferAttachmentFormat(1),
                           gbufferAttachmentFormat(2), gbufferAttachmentFormat(3)};
    renderingInfo.pColorAttachmentFormats = formats;
    renderingInfo.depthAttachmentFormat = depthFmt;

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
    pipelineInfo.layout = gbufferSkinnedPipelineLayout_;
    pipelineInfo.pNext = &renderingInfo;

    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &gbufferSkinnedPipeline_);
    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);
}

} // namespace engine3d
