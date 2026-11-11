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

    // Przebieg G-Buffer zapisał głębokość (czyszczenie do 1.0, potem nieprzezroczyste obiekty < 1.0).
    // Bufor głębokości zostaje w układzie DEPTH_ATTACHMENT - bariera niepotrzebna.
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

    // CZĘŚĆ 1: scena (bez oceanu) na obraz swapchaina
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

    // Koniec części 1 - scena narysowana na swapchainie
    vkCmdEndRendering(cmd);
}

void Renderer::RendererImpl::createDeferredPipeline() {
    VkDevice device = vulkanContext_->device();

    std::string shaderDir = "bin/assets/shaders/deferred/";
    const vk::ShaderModule vert(device, shaderDir + "deferred.vert.spv");
    const vk::ShaderModule frag(device, shaderDir + "deferred.frag.spv");

    // Set 1: 4 tekstury G-Buffera + mapa cieni, wypełniany przy rysowaniu (push descriptors).
    VkDescriptorSetLayoutBinding gbufferBindings[5] = {};
    for (uint32_t i = 0; i < 5; i++) {
        gbufferBindings[i].binding = i;
        gbufferBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        gbufferBindings[i].descriptorCount = 1;
        gbufferBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dslInfo{};
    dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT;
    dslInfo.bindingCount = 5;
    dslInfo.pBindings = gbufferBindings;
    vkCreateDescriptorSetLayout(device, &dslInfo, nullptr, &deferredDescriptorSetLayout_);

    deferredPipelineLayout_ = vk::createPipelineLayout(device, {descriptorSetLayout_, deferredDescriptorSetLayout_},
                                                       0, 0, "deferred");
    // Pełnoekranowy trójkąt bez wierzchołków, bez testu głębokości. Przebieg oświetlenia ma podpięty
    // bufor głębokości (rysują w nim później obiekty forward), więc format głębokości musi się zgadzać.
    deferredPipeline_ = vk::GraphicsPipelineBuilder()
        .shaders(vert, frag)
        .cull(VK_CULL_MODE_NONE)
        .depth(false, false)
        .colorFormat(swapchain_->imageFormat())
        .depthFormat(swapchain_->depthFormat())
        .build(device, deferredPipelineLayout_, "deferred");
}

} // namespace engine3d
