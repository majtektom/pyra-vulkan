#define VMA_IMPLEMENTATION
#include "render/backend/vulkan_resources.h"
#include <cstring>
#include <stdexcept>

namespace engine3d {

VulkanBuffer::VulkanBuffer(std::shared_ptr<VulkanContext> context, 
                           VkDeviceSize size, 
                           VkBufferUsageFlags usage, 
                           VkMemoryPropertyFlags properties)
    : context_(context), size_(size) {
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    bool needsUpload = (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
    allocInfo.usage = needsUpload ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkResult res = vmaCreateBuffer(context_->allocator(), &bufferInfo, &allocInfo, &buffer_, &allocation_, nullptr);
    if (res != VK_SUCCESS) {
        allocInfo.flags = 0;
        res = vmaCreateBuffer(context_->allocator(), &bufferInfo, &allocInfo, &buffer_, &allocation_, nullptr);
    }
    if (res != VK_SUCCESS)
        throw std::runtime_error("failed to create VMA buffer");
}

VulkanBuffer::~VulkanBuffer() {
    vmaDestroyBuffer(context_->allocator(), buffer_, allocation_);
}

void* VulkanBuffer::map() {
    VmaAllocationInfo info;
    vmaGetAllocationInfo(context_->allocator(), allocation_, &info);
    if (info.pMappedData) return info.pMappedData;
    void* data;
    vmaMapMemory(context_->allocator(), allocation_, &data);
    return data;
}

void VulkanBuffer::unmap() {
    vmaUnmapMemory(context_->allocator(), allocation_);
}

void VulkanBuffer::upload(const void* data, VkDeviceSize size) {
    VmaAllocationInfo info;
    vmaGetAllocationInfo(context_->allocator(), allocation_, &info);
    if (info.pMappedData) {
        std::memcpy(info.pMappedData, data, static_cast<size_t>(size));
    } else {
        void* mapped = map();
        std::memcpy(mapped, data, static_cast<size_t>(size));
        unmap();
    }
}

VulkanImage::VulkanImage(std::shared_ptr<VulkanContext> context,
            uint32_t width, uint32_t height,
            VkFormat format, VkImageUsageFlags usage) : context_(context) {}
VulkanImage::~VulkanImage() {}

} // namespace engine3d
