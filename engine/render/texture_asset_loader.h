#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace engine3d {

/**
 * @brief Struktura przenosząca zdekodowane dane tekstury przygotowane do wysłania na GPU (Vulkan Upload).
 * 
 * Zawiera surowe bajty pikseli, format zgodny z Vulkanem (np. nieskompresowany RGBA8 lub
 * skompresowane bloki BC1/BC3) oraz zestaw regionów opisujących kopiowanie danych z bufora
 * tymczasowego (Staging Buffer) do ostatecznego obrazu na GPU (VkImage).
 */
struct TextureUploadData {
    uint32_t width = 0;                        // Szerokość tekstury w pikselach
    uint32_t height = 0;                       // Wysokość tekstury w pikselach
    uint32_t mipLevels = 1;                    // Liczba poziomów mipmap (szczególnie istotna przy DDS)
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;// Format koloru (np. VK_FORMAT_R8G8B8A8_UNORM dla BMP/TGA lub skompresowane formaty block dla DDS)
    std::vector<std::uint8_t> pixelBytes;      // Surowe bajty danych tekstury
    std::vector<VkBufferImageCopy> copyRegions;// Regiony kopiowania dla Vulkana określające układ danych w pamięci bufora staging
    bool mirroredRepeat = false;               // Czy tekstura ma być powtarzana lustrzanie
};

/**
 * @brief Dekoder formatów graficznych (BMP, TGA, DDS).
 * 
 * Klasa implementuje niezależny od API renderowania parser popularnych formatów grafik.
 * Została zrealizowana bez użycia zewnętrznych, ciężkich bibliotek (jak stb_image),
 * dzięki czemu w jasny sposób pokazuje strukturę nagłówków i dekodowanie plików:
 * - BMP: Proste czytanie nieskompresowanych pikseli 24-bit i odwracanie osi Y.
 * - TGA: Obsługa plików 24/32-bit (zarówno surowych, jak i z kompresją RLE - Run-Length Encoding).
 * - DDS: Wsparcie dla zaawansowanych tekstur skompresowanych (DXT1/BC1, DXT3/BC2, DXT5/BC3) 
 *        wraz z gotowymi poziomami mipmap, co pozwala na ich bezpośrednie przesłanie na GPU
 *        bez dekompresji na CPU, oszczędzając pamięć VRAM i czas procesora.
 */
class TextureAssetLoader {
public:
    /**
     * @brief Ładuje teksturę z pliku o podanej ścieżce.
     * @param path Ścieżka do pliku (.bmp, .tga, .dds).
     * @return TextureUploadData Struktura gotowych danych do wgrania na GPU.
     */
    [[nodiscard]] static TextureUploadData load(const std::string& path);

    /**
     * @brief Tworzy strukturę transferu tekstury bezpośrednio z tablicy pikseli RGBA znajdującej się w pamięci CPU.
     * @param width Szerokość obrazu.
     * @param height Wysokość obrazu.
     * @param rgbaPixels Surowy wektor pikseli RGBA.
     * @return TextureUploadData Skonfigurowana struktura z jednym regionem kopiowania.
     */
    [[nodiscard]] static TextureUploadData fromRgbaPixels(
        uint32_t width,
        uint32_t height,
        std::vector<std::uint8_t> rgbaPixels
    );
};

}  // namespace engine3d