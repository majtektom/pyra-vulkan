#pragma once

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include <vector>
#include <memory>
#include <string>

struct SDL_Window;

namespace engine3d {

class VulkanContext {
public:
    VulkanContext();
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    void initialize(const std::string& appName, bool enableValidationLayers = true);
    void createSurface(SDL_Window* window);

    VkInstance instance() const { return instance_; }
    VkSurfaceKHR surface() const { return surface_; }
    VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    VkDevice device() const { return device_; }
    VkQueue graphicsQueue() const { return graphicsQueue_; }
    VkQueue presentQueue() const { return presentQueue_; }
    VkQueue computeQueue() const { return computeQueue_; }
    VkCommandPool commandPool() const { return commandPool_; }
    VkCommandPool computeCommandPool() const { return computeCommandPool_; }
    VmaAllocator allocator() const { return allocator_; }
    uint32_t graphicsQueueFamily() const { return graphicsQueueFamily_; }
    uint32_t computeQueueFamily() const { return computeQueueFamily_; }

private:
    void createInstance(const std::string& appName);
    void setupDebugMessenger();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createCommandPool();
    void createComputeCommandPool();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VkQueue computeQueue_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandPool computeCommandPool_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    bool validationLayersEnabled_ = true;
    uint32_t graphicsQueueFamily_ = 0;
    uint32_t computeQueueFamily_ = 0;
};

} // namespace engine3d
