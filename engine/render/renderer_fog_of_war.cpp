#include "render/renderer_impl.h"

namespace engine3d {

// Obraz widoczności dla każdej klatki w locie. Aktualizacja: kopiowanie z CPU prosto do obrazu (Vulkan 1.4
// hostImageCopy) - bez bufora pośredniego, poleceń GPU i barier; obraz stale w układzie do odczytu (hostCopyLayout).
void Renderer::RendererImpl::createFogOfWarResources() {
    VkDevice device = vulkanContext_->device();
    VkPhysicalDevice phys = vulkanContext_->physicalDevice();
    fowSampler_ = vk::createSampler(device, phys, 1, TextureFilter::Linear, AddressMode::ClampToEdge);
    for (int f = 0; f < kMaxFrames; ++f) {
        vk::createImage(device, phys, kFowSize, kFowSize, 1, VK_FORMAT_R8_UNORM,
                        VK_IMAGE_USAGE_HOST_TRANSFER_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        fowImage_[f], fowImageMem_[f]);
        fowView_[f] = vk::createImageView(device, fowImage_[f], VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);
        vk::hostTransitionToCopyLayout(device, fowImage_[f]);
        vk::hostCopyToImage(device, fowImage_[f], fowData_.data(), kFowSize, kFowSize);   // start: wszystko widoczne
        fowUploaded_[f] = fowVersion_;
        const std::string name = "fog_of_war_" + std::to_string(f);
        registerTextureInBindless(name, fowView_[f], fowSampler_, vk::hostCopyLayout());
        fowBindless_[f] = getTextureIndex(name);
    }
}

// Na początku klatki (po czekaniu na jej fence - GPU nie używa już obrazu tej klatki): gdy dane widoczności się
// zmieniły, kopiujemy je z CPU do obrazu tej klatki.
void Renderer::RendererImpl::uploadFogOfWarIfChanged() {
    const int f = currentFrame_;
    if (fowUploaded_[f] == fowVersion_) return;
    vk::hostCopyToImage(vulkanContext_->device(), fowImage_[f], fowData_.data(), kFowSize, kFowSize);
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
        fowView_[f] = VK_NULL_HANDLE;
        fowImage_[f] = VK_NULL_HANDLE;
        fowImageMem_[f] = VK_NULL_HANDLE;
    }
    if (fowSampler_) vkDestroySampler(device, fowSampler_, nullptr);
    fowSampler_ = VK_NULL_HANDLE;
}

} // namespace engine3d
