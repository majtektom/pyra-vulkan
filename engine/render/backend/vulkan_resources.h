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
    // Pamięć widoczna dla CPU (można map/upload). Bufor DEVICE_LOCAL wypełnia się kopią z bufora pośredniego.
    bool hostVisible() const { return hostVisible_; }
    void* map();
    void unmap();
    void upload(const void* data, VkDeviceSize size);

private:
    std::shared_ptr<VulkanContext> context_;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkDeviceSize size_;
    VkBufferUsageFlags usage_ = 0;
    bool hostVisible_ = true;
};

} // namespace engine3d
