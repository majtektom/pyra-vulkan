#include "render/backend/vulkan_swapchain.h"
#include <stdexcept>
#include <algorithm>
#include <array>

namespace engine3d {

static VkFormat findSupportedFormat(VkPhysicalDevice d, const std::vector<VkFormat>& fmt, VkImageTiling t, VkFormatFeatureFlags f) {
    for (auto& format : fmt) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(d, format, &props);
        if (t == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & f) == f) return format;
        if (t == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & f) == f) return format;
    }
    throw std::runtime_error("no supported depth format");
}

static VkFormat findDepthFormat(VkPhysicalDevice d) {
    return findSupportedFormat(d, {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
                               VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

VulkanSwapchain::VulkanSwapchain(std::shared_ptr<VulkanContext> ctx, uint32_t w, uint32_t h, VkPresentModeKHR mode)
    : context_(std::move(ctx)), presentMode_(mode) { recreate(w, h); }

VulkanSwapchain::~VulkanSwapchain() { cleanup(); }

void VulkanSwapchain::recreate(uint32_t w, uint32_t h) {
    vkDeviceWaitIdle(context_->device());
    cleanup();
    createSwapchain(w, h);
    createImageViews();
    createDepthResources();
}

void VulkanSwapchain::cleanup() {
    VkDevice d = context_->device();
    for (auto v : depthImageViews_) vkDestroyImageView(d, v, nullptr);
    for (size_t i = 0; i < depthImages_.size(); ++i) {
        if (depthImages_[i]) vkDestroyImage(d, depthImages_[i], nullptr);
        if (depthMemories_[i]) vkFreeMemory(d, depthMemories_[i], nullptr);
    }
    for (auto v : imageViews_) vkDestroyImageView(d, v, nullptr);
    if (swapchain_) vkDestroySwapchainKHR(d, swapchain_, nullptr);
    imageViews_.clear();
    depthImages_.clear();
    depthMemories_.clear();
    depthImageViews_.clear();
}

void VulkanSwapchain::createSwapchain(uint32_t w, uint32_t h) {
    VkPhysicalDevice pd = context_->physicalDevice();
    VkSurfaceKHR surf = context_->surface();
    VkDevice dev = context_->device();

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, surf, &caps);
    extent_ = (caps.currentExtent.width != UINT32_MAX) ? caps.currentExtent : VkExtent2D{std::clamp(w, caps.minImageExtent.width, caps.maxImageExtent.width), std::clamp(h, caps.minImageExtent.height, caps.maxImageExtent.height)};

    uint32_t fmtCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surf, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surf, &fmtCount, fmts.data());
    imageFormat_ = VK_FORMAT_B8G8R8A8_SRGB;
    for (auto& f : fmts) { if (f.format == VK_FORMAT_B8G8R8A8_SRGB) { imageFormat_ = f.format; break; } }

    uint32_t imgCount = std::max(2u, caps.minImageCount);

    VkSwapchainCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = surf;
    ci.minImageCount = imgCount;
    ci.imageFormat = imageFormat_;
    ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    ci.imageExtent = extent_;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = presentMode_;
    ci.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(dev, &ci, nullptr, &swapchain_) != VK_SUCCESS)
        throw std::runtime_error("swapchain create failed");

    vkGetSwapchainImagesKHR(dev, swapchain_, &imgCount, nullptr);
    images_.resize(imgCount);
    vkGetSwapchainImagesKHR(dev, swapchain_, &imgCount, images_.data());
}

void VulkanSwapchain::createImageViews() {
    VkDevice dev = context_->device();
    imageViews_.resize(images_.size());
    for (size_t i = 0; i < images_.size(); ++i) {
        VkImageViewCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image = images_[i];
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = imageFormat_;
        ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ci.subresourceRange.levelCount = 1;
        ci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(dev, &ci, nullptr, &imageViews_[i]) != VK_SUCCESS)
            throw std::runtime_error("image view create failed");
    }
}

void VulkanSwapchain::createDepthResources() {
    VkDevice dev = context_->device();
    VkPhysicalDevice pd = context_->physicalDevice();
    depthFormat_ = findDepthFormat(pd);
    size_t n = images_.size();
    depthImages_.resize(n);
    depthMemories_.resize(n);
    depthImageViews_.resize(n);
    for (size_t i = 0; i < n; ++i) {
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.extent = {extent_.width, extent_.height, 1};
        ci.mipLevels = 1; ci.arrayLayers = 1;
        ci.format = depthFormat_;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        if (vkCreateImage(dev, &ci, nullptr, &depthImages_[i]) != VK_SUCCESS)
            throw std::runtime_error("depth image create failed");

        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(dev, depthImages_[i], &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        uint32_t tBits = mr.memoryTypeBits;
        for (uint32_t j = 0; j < 32; ++j) {
            if (tBits & (1u << j)) {
                VkPhysicalDeviceMemoryProperties mp;
                vkGetPhysicalDeviceMemoryProperties(pd, &mp);
                if (mp.memoryTypes[j].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
                    { ai.memoryTypeIndex = j; break; }
            }
        }
        if (vkAllocateMemory(dev, &ai, nullptr, &depthMemories_[i]) != VK_SUCCESS)
            throw std::runtime_error("depth memory alloc failed");
        vkBindImageMemory(dev, depthImages_[i], depthMemories_[i], 0);

        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = depthImages_[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = depthFormat_;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(dev, &vi, nullptr, &depthImageViews_[i]) != VK_SUCCESS)
            throw std::runtime_error("depth view create failed");
    }
}


uint32_t VulkanSwapchain::acquireNextImage(VkSemaphore s) {
    uint32_t idx = 0;
    VkResult result = vkAcquireNextImageKHR(context_->device(), swapchain_, UINT64_MAX, s, VK_NULL_HANDLE, &idx);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("swapchain out of date");
    }
    if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to acquire swapchain image");
    }
    return idx;
}

bool VulkanSwapchain::present(uint32_t idx, VkSemaphore s) {
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &s;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &idx;
    VkResult result = vkQueuePresentKHR(context_->presentQueue(), &pi);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        return false;
    }
    return true;
}

} // namespace engine3d
