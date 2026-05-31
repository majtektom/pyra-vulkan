#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include "render/backend/vulkan_context.h"

namespace engine3d {

class VulkanSwapchain {
public:
    VulkanSwapchain(std::shared_ptr<VulkanContext> context, uint32_t width, uint32_t height,
                    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR);
    ~VulkanSwapchain();

    VkSwapchainKHR handle() const { return swapchain_; }
    VkExtent2D extent() const { return extent_; }
    
    VkImageView imageView(uint32_t index) const { return imageViews_[index]; }
    VkImageView depthImageView(uint32_t index) const { return depthImageViews_[index]; }
    VkFormat imageFormat() const { return imageFormat_; }
    VkFormat depthFormat() const { return depthFormat_; }
    const std::vector<VkImage>& images() const { return images_; }
    
    uint32_t acquireNextImage(VkSemaphore signalSemaphore);
    bool present(uint32_t imageIndex, VkSemaphore waitSemaphore);
 
    void recreate(uint32_t width, uint32_t height);
    void setPresentMode(VkPresentModeKHR mode) { presentMode_ = mode; }
    VkPresentModeKHR presentMode() const { return presentMode_; }
 
private:
    void createSwapchain(uint32_t width, uint32_t height);
    void createImageViews();
    void createDepthResources();
    void cleanup();
 
    std::shared_ptr<VulkanContext> context_;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat imageFormat_;
    VkFormat depthFormat_;
    VkExtent2D extent_;
    VkPresentModeKHR presentMode_ = VK_PRESENT_MODE_FIFO_KHR;
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
 
    std::vector<VkImage> depthImages_;
    std::vector<VkDeviceMemory> depthMemories_;
    std::vector<VkImageView> depthImageViews_;
};

} // namespace engine3d
