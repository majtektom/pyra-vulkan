#include "render/renderer_impl.h"

namespace engine3d {

// G-Buffer: obiekty nieprzezroczyste zapisują albedo, normalne, materiał i pozycję (oświetlenie - deferred).
void Renderer::RendererImpl::recordGBufferPass(VkCommandBuffer cmd, const RenderScene& scene, uint32_t imageIndex) {
    Profiler::get().count("paczki nieprzezroczyste", static_cast<double>(scene.getOpaquePackets().size()));
    Profiler::get().count("paczki cieni", static_cast<double>(scene.shadowPacketCount()));

    beginGBufferRendering(cmd, imageIndex);
    sortGBufferDraws(scene);
    DrawBindCache bound;
    for (const GBufferDraw& draw : gbufferOrder_) drawGBufferPacket(cmd, bound, *draw.packet);
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

// Bariery i początek rysowania: 4 tekstury G-Buffera (czytane w poprzedniej klatce przez oświetlenie) stają się
// celem rysowania, a głębia swapchaina - z UNDEFINED (loadOp = CLEAR, stara zawartość niepotrzebna; obsługuje też
// pierwsze użycie obrazu po odtworzeniu swapchaina). Wszystko czyszczone: kolory do zera, głębia do 1.
void Renderer::RendererImpl::beginGBufferRendering(VkCommandBuffer cmd, uint32_t imageIndex) {
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

    VkRenderingAttachmentInfo colors[kGBufferAttachments] = {};
    for (int a = 0; a < kGBufferAttachments; a++) {
        colors[a].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colors[a].imageView = gbufferViews_[a][currentFrame_];
        colors[a].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colors[a].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colors[a].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colors[a].clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    }
    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView = swapchain_->depthImageView(imageIndex);
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = {{0, 0}, swapchain_->extent()};
    info.layerCount = 1;
    info.colorAttachmentCount = kGBufferAttachments;
    info.pColorAttachments = colors;
    info.pDepthAttachment = &depth;
    vkCmdBeginRendering(cmd, &info);

    const VkViewport viewport = swapchainViewport();
    const VkRect2D scissor = swapchainScissor();
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

// Kolejność rysowania G-Buffera ma dwa cele:
//  1. mało zmian stanu - grupa potoku (zwykłe, animowane, teren z własnym materiałem), a w niej siatka;
//     DrawBindCache pomija wtedy powtórne vkCmdBindPipeline/DescriptorSets/buforów,
//  2. od przodu - bliższe najpierw, więc test głębokości (early-Z) odrzuca zasłonięte piksele dalszych
//     obiektów, zanim policzy się ich shader. Teren rysujemy na końcu: domy i drzewa zasłaniają spory jego
//     kawałek, a jego shader (teselacja, warstwy tekstur) jest najdroższy.
// Kompromis 1 i 2: odległość dzielimy na pierścienie po kGBufferRing jednostek; w pierścieniu paczki są
// pogrupowane po siatce (np. wszystkie drzewa jednego rodzaju), dalej - po odległości.
void Renderer::RendererImpl::sortGBufferDraws(const RenderScene& scene) {
    constexpr float kGBufferRing = 40.0f;
    const auto& packets = scene.getOpaquePackets();
    gbufferOrder_.clear();
    gbufferOrder_.reserve(packets.size());
    for (const auto& packet : packets) {
        if (packet.vertexBuffer == VK_NULL_HANDLE || packet.forwardOpaquePass) continue;
        if (packet.gbufferMaterialPass && !packet.material) continue;
        GBufferDraw d;
        d.packet = &packet;
        d.group = packet.gbufferMaterialPass ? 2 : (packet.boneDescriptorSet != VK_NULL_HANDLE ? 1 : 0);
        d.pipeline = packet.gbufferMaterialPass ? packet.material->getPipeline().pipeline : VK_NULL_HANDLE;
        // distanceToCamera to kwadrat odległości.
        d.ring = static_cast<int>(std::sqrt(std::max(packet.distanceToCamera, 0.0f)) / kGBufferRing);
        gbufferOrder_.push_back(d);
    }
    std::sort(gbufferOrder_.begin(), gbufferOrder_.end(), [](const GBufferDraw& a, const GBufferDraw& b) {
        if (a.group != b.group) return a.group < b.group;
        if (a.pipeline != b.pipeline) return a.pipeline < b.pipeline;
        if (a.ring != b.ring) return a.ring < b.ring;
        if (a.packet->vertexBuffer != b.packet->vertexBuffer) return a.packet->vertexBuffer < b.packet->vertexBuffer;
        return a.packet->distanceToCamera < b.packet->distanceToCamera;
    });
}

void Renderer::RendererImpl::drawGBufferPacket(VkCommandBuffer cmd, DrawBindCache& bound, const RenderPacket& packet) {
    VkDescriptorSet globalSet = descriptorSets_[currentFrame_];
    if (packet.gbufferMaterialPass) {
        // Teren (własny shader z teselacją) zapisuje G-Buffer swoim potokiem materiału.
        drawRenderPacket(cmd, packet, packet.material->getPipeline(), globalSet, bound);
        return;
    }

    // Zwykłe i animowane siatki (animowane: set 1 z macierzami kości i drugi bufor wierzchołków).
    const bool skinned = packet.boneDescriptorSet != VK_NULL_HANDLE;
    const VkPipelineLayout layout = skinned ? gbufferSkinnedPipelineLayout_ : gbufferPipelineLayout_;
    bound.bindGlobalSet(cmd, layout, globalSet);
    bound.bindPipeline(cmd, skinned ? gbufferSkinnedPipeline_ : gbufferPipeline_);
    if (skinned) bound.bindSet1(cmd, layout, packet.boneDescriptorSet);

    bound.bindVertexBuffer(cmd, 0, packet.vertexBuffer);
    if (packet.skinVertexBuffer != VK_NULL_HANDLE) bound.bindVertexBuffer(cmd, 1, packet.skinVertexBuffer);
    if (packet.indexBuffer == VK_NULL_HANDLE) return;
    bound.bindIndexBuffer(cmd, packet.indexBuffer);

    const MaterialPushConstants push = makeMaterialPush(packet);   // układ: push_constants.h
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDrawIndexed(cmd, packet.indexCount, 1, 0, 0, 0);
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

    gbufferSampler_ = vk::createClampSampler(device, VK_FILTER_NEAREST);   // piksel G-Buffera 1:1, bez mieszania

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

// Wspólne ustawienia potoków G-Buffera: 4 cele koloru (formaty z gbufferAttachmentFormat) bez mieszania,
// zwykły test i zapis głębokości, set 0 = dane klatki, set 1 = macierze kości, push constants materiału.
vk::GraphicsPipelineBuilder Renderer::RendererImpl::gbufferPipelineBase() const {
    vk::GraphicsPipelineBuilder builder;
    builder.polygonMode(wireframeEnabled_ ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL)
        .depthFormat(swapchain_->depthFormat());
    for (int i = 0; i < kGBufferAttachments; i++) builder.colorFormat(gbufferAttachmentFormat(i));
    return builder;
}

VkPipelineLayout Renderer::RendererImpl::createGBufferPipelineLayout(const std::string& name) {
    return vk::createPipelineLayout(vulkanContext_->device(), {descriptorSetLayout_, boneDescriptorSetLayout_}, 256,
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, name);
}

void Renderer::RendererImpl::createGBufferPipeline() {
    VkDevice device = vulkanContext_->device();
    const std::string shaderDir = "bin/assets/shaders/gbuffer/";
    const vk::ShaderModule vert(device, shaderDir + "gbuffer.vert.spv");
    const vk::ShaderModule frag(device, shaderDir + "gbuffer.frag.spv");

    gbufferPipelineLayout_ = createGBufferPipelineLayout("gbuffer");
    gbufferPipeline_ = gbufferPipelineBase()
        .shaders(vert, frag)
        // Tylko atrybuty czytane przez vertex shader.
        .vertexInput(Vertex::bindingDescription(), vk::filterVertexAttributes(vert.code(), Vertex::attributeDescriptions()))
        .build(device, gbufferPipelineLayout_, "gbuffer");
}

void Renderer::RendererImpl::createGBufferSkinnedPipeline() {
    VkDevice device = vulkanContext_->device();
    const std::string shaderDir = "bin/assets/shaders/gbuffer_skinned/";
    const vk::ShaderModule vert(device, shaderDir + "gbuffer_skinned.vert.spv");
    const vk::ShaderModule frag(device, shaderDir + "gbuffer_skinned.frag.spv");

    gbufferSkinnedPipelineLayout_ = createGBufferPipelineLayout("gbuffer_skinned");
    gbufferSkinnedPipeline_ = gbufferPipelineBase()
        .shaders(vert, frag)
        .vertexInput(Vertex::bindingDescription(), vk::filterVertexAttributes(vert.code(), Vertex::attributeDescriptions()))
        .vertexInput(SkinVertex::bindingDescription(), vk::filterVertexAttributes(vert.code(), SkinVertex::attributeDescriptions()))
        .cull(VK_CULL_MODE_NONE)
        .depthBias(1.0f, 1.0f)
        .build(device, gbufferSkinnedPipelineLayout_, "gbuffer_skinned");
}

} // namespace engine3d
