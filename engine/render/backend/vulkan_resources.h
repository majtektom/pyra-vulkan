#pragma once

#include <vulkan/vulkan.h>
#include "vulkan_context.h"

namespace engine3d {

class VulkanBuffer {
public:
    VulkanBuffer(std::shared_ptr<VulkanContext> context, 
                 VkDeviceSize size, 
                 VkBufferUsageFlags usage, 
                 VkMemoryPropertyFlags properties);
    ~VulkanBuffer();

    VkBuffer handle() const { return buffer_; }
    void* map();
    void unmap();
    void upload(const void* data, VkDeviceSize size);

private:
    std::shared_ptr<VulkanContext> context_;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkDeviceSize size_;
};

class VulkanImage {
public:
    VulkanImage(std::shared_ptr<VulkanContext> context,
                uint32_t width, uint32_t height,
                VkFormat format, VkImageUsageFlags usage);
    ~VulkanImage();

    VkImage handle() const { return image_; }
    VkImageView view() const { return view_; }
    VkSampler sampler() const { return sampler_; }

private:
    std::shared_ptr<VulkanContext> context_;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
};

} // namespace engine3d
