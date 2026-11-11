#include "render/renderer_impl.h"

namespace engine3d {

void Renderer::RendererImpl::createFogOfWarResources() {
    VkDevice device = vulkanContext_->device();
    VkPhysicalDevice phys = vulkanContext_->physicalDevice();
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(kFowSize) * kFowSize;
    fowSampler_ = vk::createSampler(device, phys, 1, TextureFilter::Linear, AddressMode::ClampToEdge);
    for (int f = 0; f < kMaxFrames; ++f) {
        vk::createImage(device, phys, kFowSize, kFowSize, 1, VK_FORMAT_R8_UNORM,
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        fowImage_[f], fowImageMem_[f]);
        fowView_[f] = vk::createImageView(device, fowImage_[f], VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);
        vk::createBuffer(device, phys, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, fowStaging_[f], fowStagingMem_[f]);
        vkMapMemory(device, fowStagingMem_[f], 0, bytes, 0, &fowStagingPtr_[f]);   // mapowanie na stałe
        // Start: wszystko widoczne (gra bez mgły wojny wygląda jak dotąd).
        vk::transitionImageLayout(device, vulkanContext_->commandPool(), vulkanContext_->graphicsQueue(), fowImage_[f],
                                  VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
        std::memcpy(fowStagingPtr_[f], fowData_.data(), bytes);
        VkCommandBuffer cmd = beginSingleTimeCommands();
        recordFogOfWarCopy(cmd, f, false);
        endSingleTimeCommands(cmd);
        fowUploaded_[f] = fowVersion_;
        const std::string name = "fog_of_war_" + std::to_string(f);
        registerTextureInBindless(name, fowView_[f], fowSampler_);
        fowBindless_[f] = getTextureIndex(name);
    }
}

// Kopia bufora przejściowego klatki f do jej obrazu (z barierami: odczyt w shaderze <-> zapis transferu).
void Renderer::RendererImpl::recordFogOfWarCopy(VkCommandBuffer cmd, int f, bool fromShaderRead) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = fowImage_[f];
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (fromShaderRead) {
        b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &b);
    }
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {kFowSize, kFowSize, 1};
    vkCmdCopyBufferToImage(cmd, fowStaging_[f], fowImage_[f], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &b);
}

// Na początku klatki: gdy dane widoczności się zmieniły, przesyłamy je do obrazu tej klatki.
void Renderer::RendererImpl::uploadFogOfWarIfChanged(VkCommandBuffer cmd) {
    const int f = currentFrame_;
    if (fowUploaded_[f] == fowVersion_) return;
    std::memcpy(fowStagingPtr_[f], fowData_.data(), fowData_.size());
    recordFogOfWarCopy(cmd, f, true);
    fowUploaded_[f] = fowVersion_;
}

void Renderer::RendererImpl::setFogOfWar(bool enabled, float originX, float originZ, float worldSize, float exploredBrightness) {
    fowEnabled_ = enabled;
    fowOriginX_ = originX;
    fowOriginZ_ = originZ;
    fowSize_ = std::max(1.0f, worldSize);
    fowExplored_ = exploredBrightness;
}

void Renderer::RendererImpl::destroyFogOfWarResources() {
    VkDevice device = vulkanContext_->device();
    for (int f = 0; f < kMaxFrames; ++f) {
        if (fowView_[f]) vkDestroyImageView(device, fowView_[f], nullptr);
        if (fowImage_[f]) vkDestroyImage(device, fowImage_[f], nullptr);
        if (fowImageMem_[f]) vkFreeMemory(device, fowImageMem_[f], nullptr);
        if (fowStaging_[f]) vkDestroyBuffer(device, fowStaging_[f], nullptr);
        if (fowStagingMem_[f]) vkFreeMemory(device, fowStagingMem_[f], nullptr);   // zwalnia też mapowanie
        fowView_[f] = VK_NULL_HANDLE;
        fowImage_[f] = VK_NULL_HANDLE;
        fowImageMem_[f] = VK_NULL_HANDLE;
        fowStaging_[f] = VK_NULL_HANDLE;
        fowStagingMem_[f] = VK_NULL_HANDLE;
    }
    if (fowSampler_) vkDestroySampler(device, fowSampler_, nullptr);
    fowSampler_ = VK_NULL_HANDLE;
}

} // namespace engine3d
