#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <string>

struct SDL_Window;

namespace engine3d {

class VulkanContext {
public:
    VulkanContext();
    ~VulkanContext();

    // Prevent copying
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    void initialize(const std::string& appName);
    void createSurface(SDL_Window* window);

    VkInstance instance() const { return instance_; }
    VkSurfaceKHR surface() const { return surface_; }
    VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    VkDevice device() const { return device_; }
    VkQueue graphicsQueue() const { return graphicsQueue_; }
    VkQueue presentQueue() const { return presentQueue_; }
    VkCommandPool commandPool() const { return commandPool_; }

private:
    void createInstance(const std::string& appName);
    void setupDebugMessenger();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createCommandPool();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
};

} // namespace engine3d
