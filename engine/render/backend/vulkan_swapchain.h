#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include "vulkan_context.h"

namespace engine3d {

class VulkanSwapchain {
public:
    VulkanSwapchain(std::shared_ptr<VulkanContext> context, uint32_t width, uint32_t height);
    ~VulkanSwapchain();

    VkSwapchainKHR handle() const { return swapchain_; }
    VkRenderPass renderPass() const { return renderPass_; }
    VkExtent2D extent() const { return extent_; }
    VkFramebuffer framebuffer(uint32_t index) const { return framebuffers_[index]; }
    
    uint32_t acquireNextImage(VkSemaphore signalSemaphore);
    bool present(uint32_t imageIndex, VkSemaphore waitSemaphore);

    void recreate(uint32_t width, uint32_t height);

private:
    void createSwapchain(uint32_t width, uint32_t height);
    void createImageViews();
    void createDepthResources();
    void createRenderPass();
    void createFramebuffers();
    void cleanup();

    std::shared_ptr<VulkanContext> context_;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat imageFormat_;
    VkExtent2D extent_;
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;

    std::vector<VkImage> depthImages_;
    std::vector<VkDeviceMemory> depthMemories_;
    std::vector<VkImageView> depthImageViews_;
};

} // namespace engine3d
