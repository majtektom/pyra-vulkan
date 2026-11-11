#define VMA_IMPLEMENTATION
#include "render/backend/vulkan_resources.h"
#include "core/logger.h"
#include <cstring>
#include <stdexcept>

namespace engine3d {

VulkanBuffer::VulkanBuffer(std::shared_ptr<VulkanContext> context, 
                           VkDeviceSize size, 
                           VkBufferUsageFlags usage, 
                           VkMemoryPropertyFlags properties)
    : context_(context), size_(size), usage_(usage),
      hostVisible_((properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) {
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    if (hostVisible_) {
        // Pamięć zapisywana przez CPU i czytana przez GPU (bufory dynamiczne, bufory pośrednie - staging).
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    } else {
        // Pamięć karty (VRAM) - najszybsza dla GPU, CPU nie ma do niej dostępu (dane trafiają kopią vkCmdCopyBuffer).
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        allocInfo.flags = 0;
    }

    VkResult res = vmaCreateBuffer(context_->allocator(), &bufferInfo, &allocInfo, &buffer_, &allocation_, nullptr);
    if (res != VK_SUCCESS) {
        LOG_WARN("[VulkanBuffer] First attempt to create buffer failed, retrying without MAPPED flag");
        allocInfo.flags = 0;
        res = vmaCreateBuffer(context_->allocator(), &bufferInfo, &allocInfo, &buffer_, &allocation_, nullptr);
    }
    if (res != VK_SUCCESS) {
        LOG_ERROR("[VulkanBuffer] Failed to create VMA buffer of size {}", size);
        throw std::runtime_error("failed to create VMA buffer");
    }
}

VulkanBuffer::~VulkanBuffer() {
    vmaDestroyBuffer(context_->allocator(), buffer_, allocation_);
}

void* VulkanBuffer::map() {
    VmaAllocationInfo info;
    vmaGetAllocationInfo(context_->allocator(), allocation_, &info);
    if (info.pMappedData) return info.pMappedData;
    void* data;
    VkResult res = vmaMapMemory(context_->allocator(), allocation_, &data);
    if (res != VK_SUCCESS) {
        LOG_ERROR("[VulkanBuffer] Failed to map memory");
        return nullptr;
    }
    return data;
}

void VulkanBuffer::unmap() {
    vmaUnmapMemory(context_->allocator(), allocation_);
}

void VulkanBuffer::upload(const void* data, VkDeviceSize size) {
    if (!hostVisible_) {
        LOG_ERROR("[VulkanBuffer] upload() do bufora DEVICE_LOCAL - użyj kopii z bufora pośredniego");
        return;
    }
    if (size > size_) {
        LOG_ERROR("[VulkanBuffer] Upload size ({}) exceeds buffer size ({})", size, size_);
        return;
    }
    VmaAllocationInfo info;
    vmaGetAllocationInfo(context_->allocator(), allocation_, &info);
    if (info.pMappedData) {
        std::memcpy(info.pMappedData, data, static_cast<size_t>(size));
    } else {
        void* mapped = map();
        if (mapped) {
            std::memcpy(mapped, data, static_cast<size_t>(size));
            unmap();
        }
    }
}

VulkanImage::VulkanImage(std::shared_ptr<VulkanContext> context,
            uint32_t width, uint32_t height,
            VkFormat format, VkImageUsageFlags usage) : context_(context) {
    LOG_WARN("[VulkanImage] Constructor not fully implemented yet");
}
VulkanImage::~VulkanImage() {}

} // namespace engine3d
