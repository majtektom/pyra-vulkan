#include "render/renderer_impl.h"

namespace engine3d {

// Część 2: ocean i obiekty przezroczyste (głębokość tylko do odczytu).
void Renderer::RendererImpl::recordTransparentPass(VkCommandBuffer cmd, const RenderScene& scene, const RenderView& view, uint32_t imageIndex) {
    const VkViewport viewport = swapchainViewport();
    const VkRect2D scissor = swapchainScissor();
    // PART 2: Continue rendering on swapchain — ocean + transparent + overlay
    // =====================================================================
    VkRenderingAttachmentInfo swColorAttachment2{};
    swColorAttachment2.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    swColorAttachment2.imageView = swapchain_->imageView(imageIndex);
    swColorAttachment2.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swColorAttachment2.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    swColorAttachment2.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    // Depth in READ_ONLY layout — can be sampled by shaders while still used for depth test
    VkRenderingAttachmentInfo swDepthAttachment2{};
    swDepthAttachment2.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    swDepthAttachment2.imageView = swapchain_->depthImageView(imageIndex);
    swDepthAttachment2.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    swDepthAttachment2.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    // Depth jest tu tylko do odczytu — STORE_OP_NONE (Vulkan 1.3) nie generuje zapisu na końcu przebiegu.
    swDepthAttachment2.storeOp = VK_ATTACHMENT_STORE_OP_NONE;

    VkRenderingInfo renderingInfo2{};
    renderingInfo2.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo2.renderArea = {{0, 0}, swapchain_->extent()};
    renderingInfo2.layerCount = 1;
    renderingInfo2.colorAttachmentCount = 1;
    renderingInfo2.pColorAttachments = &swColorAttachment2;
    renderingInfo2.pDepthAttachment = &swDepthAttachment2;

    vkCmdBeginRendering(cmd, &renderingInfo2);

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    executeFeatures(cmd, view, IRenderFeature::Stage::AfterTransparent);

    for (auto& pass : passes_) {
        if (pass->getName() == "TransparentPass") {
            pass->execute(cmd, currentFrame_, scene, descriptorSets_[currentFrame_]);
        }
    }

    vkCmdEndRendering(cmd);
}

// Część 3: nakładka (GUI, tekst) z własnym, wyczyszczonym buforem głębokości.
void Renderer::RendererImpl::recordOverlayPass(VkCommandBuffer cmd, const RenderView& view, uint32_t imageIndex) {
    const VkViewport viewport = swapchainViewport();
    const VkRect2D scissor = swapchainScissor();
    // PART 3: Overlay (GUI, tekst) — własny przebieg z wyczyszczonym, zapisywalnym depth.
    // Część 2 ma depth w layoucie READ_ONLY (próbkowany przez ocean), więc nie wolno w niej
    // ani czyścić depth, ani do niego pisać.
    if (hasActiveFeature(IRenderFeature::Stage::Overlay)) {
        // Kolor: zapisy części 2 widoczne dla loadOp = LOAD części 3. Głębia: z "tylko do odczytu" (próbkował ją ocean)
        // znowu zapisywalna - nakładka czyści ją i rysuje GUI z testem głębokości.
        const vk::ImageBarrier toOverlay[2] = {
            {.image = swapchain_->images()[imageIndex],
             .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
             .srcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, .srcAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
             .dstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
             .dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT},
            {.image = swapchain_->depthImage(imageIndex),
             .oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
             .srcStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                         VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
             .srcAccess = VK_ACCESS_2_NONE,
             .dstStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
             .dstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
             .aspect = VK_IMAGE_ASPECT_DEPTH_BIT},
        };
        vk::imageBarriers(cmd, toOverlay, 2);

        VkRenderingAttachmentInfo overlayDepthAttachment{};
        overlayDepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        overlayDepthAttachment.imageView = swapchain_->depthImageView(imageIndex);
        overlayDepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        overlayDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        overlayDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        overlayDepthAttachment.clearValue.depthStencil = {1.0f, 0};

        VkRenderingAttachmentInfo overlayColorAttachment{};
        overlayColorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        overlayColorAttachment.imageView = swapchain_->imageView(imageIndex);
        overlayColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        overlayColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        overlayColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo overlayInfo{};
        overlayInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        overlayInfo.renderArea = {{0, 0}, swapchain_->extent()};
        overlayInfo.layerCount = 1;
        overlayInfo.colorAttachmentCount = 1;
        overlayInfo.pColorAttachments = &overlayColorAttachment;
        overlayInfo.pDepthAttachment = &overlayDepthAttachment;
        vkCmdBeginRendering(cmd, &overlayInfo);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        executeFeatures(cmd, view, IRenderFeature::Stage::Overlay);
        vkCmdEndRendering(cmd);
    }
}

bool Renderer::RendererImpl::hasActiveFeature(IRenderFeature::Stage stage) const {
    for (const auto& feature : features_) {
        if (feature->isActive() && feature->getStage() == stage) return true;
    }
    return false;
}

void Renderer::RendererImpl::executeFeatures(VkCommandBuffer cmd, const RenderView& view, IRenderFeature::Stage stage) {
    if (stage == IRenderFeature::Stage::Overlay) {
        auto rit = features_.rbegin();
        auto rend = features_.rend();
        for (; rit != rend; ++rit) {
            if ((*rit)->isActive() && (*rit)->getStage() == stage) {
                (*rit)->render(cmd, currentFrame_, view);
            }
        }
    } else {
        for (auto& feature : features_) {
            if (feature->isActive() && feature->getStage() == stage) {
                feature->render(cmd, currentFrame_, view);
            }
        }
    }
}

} // namespace engine3d
