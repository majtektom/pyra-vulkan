#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace engine3d {

// Struktura przenosi juz zdekodowane dane tekstury gotowe do wyslania na GPU.
struct TextureUploadData {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 1;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    std::vector<std::uint8_t> pixelBytes;
    std::vector<VkBufferImageCopy> copyRegions;
    bool mirroredRepeat = false;
};

// Dekoder assetow tekstur oddzielony od renderera, aby latwiej bylo go rozwijac i tlumaczyc.
class TextureAssetLoader {
public:
    // Wybiera odpowiedni parser na podstawie rozszerzenia pliku.
    [[nodiscard]] static TextureUploadData load(const std::string& path);

    // Pomocniczo pakuje zwykle piksele RGBA do formatu oczekiwanego przez upload do GPU.
    [[nodiscard]] static TextureUploadData fromRgbaPixels(
        uint32_t width,
        uint32_t height,
        std::vector<std::uint8_t> rgbaPixels
    );
};

}  // namespace engine3d