#include "vulkan_resources.h"
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

    if (vkCreateBuffer(context_->device(), &bufferInfo, nullptr, &buffer_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(context_->device(), buffer_, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    
    // Find memory type
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(context_->physicalDevice(), &memProperties);
    
    uint32_t typeIndex = 0;
    bool found = false;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((memRequirements.memoryTypeBits & (1 << i)) && 
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            typeIndex = i;
            found = true;
            break;
        }
    }
    
    if (!found) throw std::runtime_error("failed to find suitable memory type");
    
    allocInfo.memoryTypeIndex = typeIndex;

    if (vkAllocateMemory(context_->device(), &allocInfo, nullptr, &memory_) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate buffer memory");
    }

    vkBindBufferMemory(context_->device(), buffer_, memory_, 0);
}

VulkanBuffer::~VulkanBuffer() {
    vkDestroyBuffer(context_->device(), buffer_, nullptr);
    vkFreeMemory(context_->device(), memory_, nullptr);
}

void* VulkanBuffer::map() {
    void* data;
    vkMapMemory(context_->device(), memory_, 0, size_, 0, &data);
    return data;
}

void VulkanBuffer::unmap() {
    vkUnmapMemory(context_->device(), memory_);
}

void VulkanBuffer::upload(const void* data, VkDeviceSize size) {
    void* mapped = map();
    std::memcpy(mapped, data, static_cast<size_t>(size));
    unmap();
}

// VulkanImage implementation simplified for brevity
VulkanImage::VulkanImage(std::shared_ptr<VulkanContext> context,
            uint32_t width, uint32_t height,
            VkFormat format, VkImageUsageFlags usage) : context_(context) {}
VulkanImage::~VulkanImage() {}

} // namespace engine3d
