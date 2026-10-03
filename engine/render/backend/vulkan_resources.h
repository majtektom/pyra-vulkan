#pragma once

#include <vulkan/vulkan.h>
#include "render/backend/vulkan_context.h"

namespace engine3d {

class VulkanBuffer {
public:
    VulkanBuffer(std::shared_ptr<VulkanContext> context, 
                 VkDeviceSize size, 
                 VkBufferUsageFlags usage, 
                 VkMemoryPropertyFlags properties);
    ~VulkanBuffer();

    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;

    VkBuffer handle() const { return buffer_; }
    VkDeviceSize size() const { return size_; }
    VkBufferUsageFlags usage() const { return usage_; }
    void* map();
    void unmap();
    void upload(const void* data, VkDeviceSize size);

private:
    std::shared_ptr<VulkanContext> context_;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkDeviceSize size_;
    VkBufferUsageFlags usage_ = 0;
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
