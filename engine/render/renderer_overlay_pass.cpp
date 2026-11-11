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
        VkImageMemoryBarrier overlayDepthBarrier{};
        overlayDepthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        overlayDepthBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        overlayDepthBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        overlayDepthBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        overlayDepthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        overlayDepthBarrier.image = swapchain_->depthImage(imageIndex);
        overlayDepthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        overlayDepthBarrier.subresourceRange.levelCount = 1;
        overlayDepthBarrier.subresourceRange.layerCount = 1;
        // Kolor: zapisy części 2 muszą być widoczne dla loadOp = LOAD części 3.
        VkImageMemoryBarrier overlayColorBarrier{};
        overlayColorBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        overlayColorBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        overlayColorBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        overlayColorBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        overlayColorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        overlayColorBarrier.image = swapchain_->images()[imageIndex];
        overlayColorBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        overlayColorBarrier.subresourceRange.levelCount = 1;
        overlayColorBarrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, nullptr, 0, nullptr, 1, &overlayColorBarrier);

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &overlayDepthBarrier);

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
