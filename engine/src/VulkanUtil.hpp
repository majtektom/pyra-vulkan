#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace engine3d {
namespace vk {

// Laduje caly plik binarny do pamieci, np. skompilowany shader SPIR-V.
// Zwraca wektor bajtow lub rzuca std::runtime_error jesli plik nie istnieje.
[[nodiscard]] std::vector<char> readFile(const std::string& path);

// Zamienia skompilowany SPIR-V na obiekt VkShaderModule rozumiany przez Vulkan.
[[nodiscard]] VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code);

// Wyszukuje indeks typu pamieci GPU/CPU zgodny z wymaganiami (typeFilter + properties).
[[nodiscard]] uint32_t findMemoryType(
    VkPhysicalDevice physicalDevice,
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties
);

// Tworzy bufor Vulkan oraz przydziela pamiec o odpowiednich wlasciwosciach.
void createBuffer(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& buffer,
    VkDeviceMemory& bufferMemory
);

// Tworzy szybki bufor device-local i wypelnia go danymi przez staging buffer widoczny dla CPU.
void createDeviceLocalBufferWithStaging(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    const void* sourceData,
    VkDeviceSize bufferSize,
    VkBufferUsageFlags usage,
    VkBuffer& buffer,
    VkDeviceMemory& bufferMemory
);

// Tworzy VkImage z pamiecia. Uzywane do tekstur, bufora glebokosci itp.
void createImage(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    uint32_t width,
    uint32_t height,
    uint32_t mipLevels,
    VkFormat format,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkImage& image,
    VkDeviceMemory& imageMemory
);

// Tworzy widok obrazu: laczy VkImage z VkFormat i VkImageAspectFlags w obiekt VkImageView.
[[nodiscard]] VkImageView createImageView(
    VkDevice device,
    VkImage image,
    VkFormat format,
    VkImageAspectFlags aspectFlags,
    uint32_t mipLevels = 1
);

// Zmienia uklad obrazu np. z UNDEFINED -> TRANSFER_DST -> SHADER_READ_ONLY.
void transitionImageLayout(
    VkDevice device,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    VkImage image,
    VkFormat format,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    uint32_t mipLevels
);

// Tworzy prosty sampler tekstury (liniowe filtrowanie, wrap mode = REPEAT).
[[nodiscard]] VkSampler createTextureSampler(VkDevice device, uint32_t mipLevels);

// Tworzy sampler z CLAMP_TO_EDGE (dla atlasow fontow i skyboxa).
[[nodiscard]] VkSampler createClampSampler(VkDevice device);

// Tworzy sampler z MIRRORED_REPEAT – kazde powielenie jest odbiciem poprzedniego,
// dzieki czemu krawedzie kafelkow lacza sie bez widocznych szwow.
[[nodiscard]] VkSampler createMirrorRepeatSampler(VkDevice device, uint32_t mipLevels);

}  // namespace vk
}  // namespace engine3d
