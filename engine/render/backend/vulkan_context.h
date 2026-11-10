#pragma once

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include <vector>
#include <memory>
#include <string>

struct SDL_Window;

namespace engine3d {

/**
 * @brief Klasa odpowiedzialna za inicjalizację i zarządzanie podstawowymi zasobami interfejsu Vulkan (Vulkan Context).
 *
 * VulkanContext tworzy i przechowuje główne obiekty potrzebne do komunikacji z GPU:
 * - VkInstance: punkt wejścia do API Vulkan, reprezentuje połączenie z biblioteką.
 * - VkPhysicalDevice: reprezentacja karty graficznej (sprzętu) zainstalowanej w komputerze.
 * - VkDevice: logiczny interfejs karty graficznej, z którego wysyła się komendy renderowania.
 * - VkQueue: kolejki komend (graficzna, prezentacyjna, obliczeniowa), do których przesyłane są zadania dla GPU.
 * - VkCommandPool: pule alokacji buforów komend GPU.
 * - VmaAllocator: alokator biblioteki Vulkan Memory Allocator (VMA), ułatwiający zarządzanie pamięcią GPU.
 */
class VulkanContext {
public:
    VulkanContext();
    ~VulkanContext();

    // Blokujemy kopiowanie kontekstu Vulkan, ponieważ zarządza on surowymi zasobami GPU.
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    /**
     * @brief Inicjalizuje instancję Vulkana, wybiera kartę graficzną i tworzy logiczne urządzenie.
     * @param appName Nazwa aplikacji.
     * @param enableValidationLayers Czy włączyć warstwy walidacji (Validation Layers) pomocne przy debugowaniu.
     */
    bool initialize(const std::string& appName, bool enableValidationLayers = true);
    
    /**
     * @brief Tworzy powierzchnię renderowania (VkSurfaceKHR) integrującą Vulkan z oknem systemu operacyjnego (poprzez bibliotekę SDL3).
     */
    void createSurface(SDL_Window* window);

    // Gettery do pobierania uchwytów zasobów Vulkan:
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
    bool isTessellationSupported() const { return tessellationSupported_; }
    // depthClamp: geometria przed bliską płaszczyzną jest "spłaszczana" zamiast obcinana (mapy cieni).
    bool isDepthClampSupported() const { return depthClampSupported_; }

private:
    // Wewnętrzne kroki inicjalizacji Vulkana:
    void createInstance(const std::string& appName);
    void setupDebugMessenger();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createCommandPool();
    void createComputeCommandPool();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE; // Narzędzie do przechwytywania błędów z warstw walidacyjnych.
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VkQueue computeQueue_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandPool computeCommandPool_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;                  // Zarządza pamięcią (buforami, teksturami).
    bool validationLayersEnabled_ = true;
    uint32_t graphicsQueueFamily_ = 0;                          // Indeks rodziny kolejek graficznych.
    uint32_t computeQueueFamily_ = 0;                           // Indeks rodziny kolejek obliczeniowych.
    bool tessellationSupported_ = false;
    bool depthClampSupported_ = false;
};

} // namespace engine3d
