#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include "render/backend/vulkan_context.h"

namespace engine3d {

/**
 * @brief Klasa zarządzająca łańcuchem wymiany (Swapchain) w interfejsie Vulkan.
 *
 * Łańcuch wymiany (Swapchain) to zbiór tekstur buforów ramki (Render Targets),
 * które są na zmianę zapisywane przez GPU i odczytywane przez monitor w celu
 * wyświetlenia płynnego obrazu bez efektu rwania (screen tearing).
 *
 * Klasa implementuje Double/Triple Buffering oraz zarządza powiązanymi buforami głębokości (Depth Buffers),
 * a także obsługuje zmianę rozdzielczości okna aplikacji (Swapchain Recreation).
 */
class VulkanSwapchain {
public:
    VulkanSwapchain(std::shared_ptr<VulkanContext> context, uint32_t width, uint32_t height,
                    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR);
    ~VulkanSwapchain();

    // Pobiera uchwyt do niskopoziomowego obiektu Swapchain.
    VkSwapchainKHR handle() const { return swapchain_; }
    
    // Zwraca aktualny rozmiar buforów ramki swapchaina.
    VkExtent2D extent() const { return extent_; }
    
    // Zwraca widok obrazu (ImageView) koloru i głębokości o danym indeksie w buforze.
    VkImageView imageView(uint32_t index) const { return imageViews_[index]; }
    VkImageView depthImageView(uint32_t index) const { return depthImageViews_[index]; }
    VkImage depthImage(uint32_t index) const { return depthImages_[index]; }
    
    // Pobiera formaty tekstur koloru i głębokości.
    VkFormat imageFormat() const { return imageFormat_; }
    VkFormat depthFormat() const { return depthFormat_; }
    
    // Zwraca obrazy koloru wchodzące w skład swapchaina.
    const std::vector<VkImage>& images() const { return images_; }
    
    /**
     * @brief Pobiera indeks następnego dostępnego obrazu, na którym możemy rysować w bieżącej klatce.
     * @param signalSemaphore Semafor, który zostanie zasygnalizowany przez GPU, gdy obraz będzie gotowy do zapisu.
     * @return Indeks wolnego bufora w swapchainie.
     */
    uint32_t acquireNextImage(VkSemaphore signalSemaphore);
    
    /**
     * @brief Zgłasza narysowaną klatkę do prezentacji na ekranie (wysyła do monitora).
     * @param imageIndex Indeks bufora swapchaina do wyświetlenia.
     * @param waitSemaphore Semafor, na który prezentacja musi poczekać (GPU musi skończyć rysować).
     * @return Zwraca true w przypadku sukcesu lub false jeśli swapchain wymaga przebudowania.
     */
    bool present(uint32_t imageIndex, VkSemaphore waitSemaphore);
 
    /**
     * @brief Przebudowuje swapchain (np. po zmianie rozmiaru okna lub trybu pełnoekranowego).
     */
    void recreate(uint32_t width, uint32_t height);
    
    // Konfiguracja trybu prezentacji (np. Mailbox dla braku limitu FPS, FIFO dla V-Sync).
    void setPresentMode(VkPresentModeKHR mode) { presentMode_ = mode; }
    VkPresentModeKHR presentMode() const { return presentMode_; }
 
private:
    void createSwapchain(uint32_t width, uint32_t height);
    void createImageViews();
    void createDepthResources();
    void cleanup();
 
    std::shared_ptr<VulkanContext> context_;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat imageFormat_;
    VkFormat depthFormat_;
    VkExtent2D extent_;
    VkPresentModeKHR presentMode_ = VK_PRESENT_MODE_FIFO_KHR;
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
 
    // Zasoby bufora głębokości (Depth Buffer) potrzebnego do prawidłowego sprawdzania przesłaniania obiektów 3D:
    std::vector<VkImage> depthImages_;
    std::vector<VkDeviceMemory> depthMemories_;
    std::vector<VkImageView> depthImageViews_;
};

} // namespace engine3d
