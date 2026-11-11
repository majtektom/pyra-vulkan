#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include "core/types.h"

namespace engine3d {

/**
 * @brief Moduł narzędziowy Vulkan (Vulkan Utilities).
 * 
 * Zbiór pomocniczych funkcji owijających powtarzalne, niskopoziomowe operacje API Vulkan.
 * Pomaga w zarządzaniu pamięcią GPU, ładowaniu shaderów, tworzeniu buforów oraz
 * manipulacji układem tekstur (transitions) i próbkowaniu (samplers).
 */
namespace vk {

/**
 * @brief Wczytuje cały plik binarny (np. skompilowany shader SPIR-V) z dysku.
 * @param path Ścieżka do pliku.
 * @return Wektor bajtów zawierający zawartość pliku.
 */
[[nodiscard]] std::vector<char> readFile(const std::string& path);

/**
 * @brief Konwertuje surowe dane SPIR-V na obiekt modułu shadera Vulkan (VkShaderModule).
 * @param device Logiczne urządzenie Vulkan.
 * @param code Dane binarne shadera SPIR-V.
 * @return Uchwyt VkShaderModule.
 */
[[nodiscard]] VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code);

/**
 * @brief Zostawia tylko te atrybuty wierzchołka, które vertex shader faktycznie czyta.
 *
 * Kompilator usuwa nieużywane wejścia z SPIR-V, a pipeline'y dostają pełny układ Vertex
 * (lokacje 0–3). Atrybuty bez odpowiadającego wejścia shadera są dozwolone, ale warstwa
 * walidacyjna zgłasza je jako ostrzeżenie wydajnościowe — GPU i tak pobiera te dane.
 * Lokacje wejść są odczytywane bezpośrednio z kodu SPIR-V (OpVariable Input + Location).
 * Gdy kod nie jest poprawnym SPIR-V, zwraca atrybuty bez zmian.
 */
[[nodiscard]] std::vector<VkVertexInputAttributeDescription> filterVertexAttributes(
    const std::vector<char>& vertexSpirv,
    const VkVertexInputAttributeDescription* attributes,
    size_t attributeCount);

template <typename Container>
[[nodiscard]] std::vector<VkVertexInputAttributeDescription> filterVertexAttributes(
    const std::vector<char>& vertexSpirv, const Container& attributes) {
    return filterVertexAttributes(vertexSpirv, std::data(attributes), std::size(attributes));
}

/**
 * @brief Wyszukuje indeks typu pamięci fizycznej karty graficznej, który spełnia wymagania sprzętowe i logiczne.
 * 
 * GPU posiada różne rodzaje pamięci (np. Device Local - szybka pamięć VRAM, Host Visible - pamięć dostępna dla CPU).
 * Funkcja ta dopasowuje typ pamięci zgłoszony przez bufor/obraz (typeFilter) z żądanymi flagami właściwości (properties).
 */
[[nodiscard]] uint32_t findMemoryType(
    VkPhysicalDevice physicalDevice,
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties
);

/**
 * @brief Tworzy zasób bufora Vulkan (VkBuffer) i alokuje dla niego pamięć GPU.
 * @param device Urządzenie logiczne.
 * @param physicalDevice Urządzenie fizyczne (karta graficzna).
 * @param size Rozmiar bufora w bajtach.
 * @param usage Przeznaczenie bufora (np. bufor wierzchołków, indeksów, uniform itp.).
 * @param properties Flagi właściwości pamięci (np. HOST_VISIBLE_BIT dla buforów modyfikowalnych przez CPU).
 * @param buffer Referencja na uchwyt tworzonego bufora.
 * @param bufferMemory Referencja na uchwyt zaalokowanej pamięci powiązanej z buforem.
 */
void createBuffer(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& buffer,
    VkDeviceMemory& bufferMemory
);

/**
 * @brief Tworzy optymalny bufor w pamięci lokalnej karty (Device Local) i przesyła do niego dane z CPU.
 * 
 * Najszybsza pamięć GPU (DEVICE_LOCAL_BIT) nie jest bezpośrednio mapowalna (widoczna) dla procesora CPU.
 * Aby wgrać do niej dane (np. geometrię modelu):
 * 1. Tworzy się tymczasowy bufor stagingowy (widoczny dla CPU i spójny).
 * 2. Kopiuje się do niego dane za pomocą memcpy.
 * 3. Tworzy się docelowy bufor oznaczony jako DEVICE_LOCAL_BIT.
 * 4. Zleca się karcie graficznej szybkie skopiowanie danych z bufora staging do bufora lokalnego (Transfer).
 */
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

/**
 * @brief Tworzy obiekt obrazu Vulkan (VkImage) i przydziela mu pamięć GPU.
 * 
 * Używane do tworzenia tekstur, map cieni, buforów głębokości czy buforów ramki G-Buffer.
 */
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
    VkDeviceMemory& imageMemory,
    uint32_t arrayLayers = 1,
    VkImageCreateFlags flags = 0
);

/**
 * @brief Tworzy widok obrazu (VkImageView).
 * 
 * W Vulkanie shader nie może czytać bezpośrednio z obiektu VkImage. Musi korzystać z 
 * widoku obrazu (VkImageView), który definiuje jak interpretować dany obraz (np. jako 
 * teksturę 2D, teksturę warstwową, czy kostkę 3D Skyboxa) oraz jakie kanały i poziomy mipmap udostępnić.
 */
[[nodiscard]] VkImageView createImageView(
    VkDevice device,
    VkImage image,
    VkFormat format,
    VkImageAspectFlags aspectFlags,
    uint32_t mipLevels = 1,
    uint32_t layerCount = 1,
    VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D
);

/**
 * @brief Bariera obrazu w stylu synchronizacji 2 (Vulkan 1.3): każdy obraz ma własne etapy i dostępy.
 *
 * Bariera mówi GPU: "zanim etapy dst zaczną dostęp dstAccess do obrazu, muszą skończyć się zapisy srcAccess
 * z etapów src - i przy okazji zmień układ obrazu z oldLayout na newLayout". Pola z nazwami (C++20) czyta się jak
 * zdanie, np. {.image = img, .oldLayout = ..., .newLayout = ..., .srcStage = ..., .srcAccess = ..., ...}.
 */
struct ImageBarrier {
    VkImage image = VK_NULL_HANDLE;
    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_2_NONE;   // etapy, które muszą skończyć pracę
    VkAccessFlags2 srcAccess = VK_ACCESS_2_NONE;                 // ich zapisy, które mają być widoczne
    VkPipelineStageFlags2 dstStage = VK_PIPELINE_STAGE_2_NONE;   // etapy, które czekają
    VkAccessFlags2 dstAccess = VK_ACCESS_2_NONE;                 // jak będą korzystać z obrazu
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    uint32_t baseMip = 0;
    uint32_t mipCount = VK_REMAINING_MIP_LEVELS;
    uint32_t baseLayer = 0;
    uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS;
};
// Jedno polecenie vkCmdPipelineBarrier2 dla wielu obrazów.
void imageBarriers(VkCommandBuffer cmd, const ImageBarrier* barriers, uint32_t count);
inline void imageBarrier(VkCommandBuffer cmd, const ImageBarrier& barrier) { imageBarriers(cmd, &barrier, 1); }
// Mipmapy: poziom 0 (już wgrany, w układzie TRANSFER_DST) jest kolejno zmniejszany o połowę (vkCmdBlitImage) do
// poziomów 1..mipLevels-1. Na końcu wszystkie poziomy są w układzie do odczytu w shaderze.
void generateMipmaps(VkCommandBuffer cmd, VkImage image, uint32_t width, uint32_t height, uint32_t mipLevels);
// Push descriptors (Vulkan 1.4): deskryptory podawane wprost w buforze poleceń, bez puli i setów.
// Set w layoucie potoku musi mieć flagę VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT.
void pushDescriptorSet(VkCommandBuffer cmd, VkPipelineBindPoint bindPoint, VkPipelineLayout layout, uint32_t set,
                       uint32_t count, const VkWriteDescriptorSet* writes);
// Tekstury (combined image sampler) do kolejnych bindingów 0..count-1 setu (potok graficzny).
void pushImageSamplers(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t set, const VkDescriptorImageInfo* images,
                       uint32_t count);
// Kopiowanie z CPU do obrazu (Vulkan 1.4, hostImageCopy): piksele trafiają do obrazu bez bufora pośredniego,
// bez poleceń GPU i bez czekania na kolejkę. Obraz musi mieć VK_IMAGE_USAGE_HOST_TRANSFER_BIT i nie może być
// w tej chwili używany przez GPU. hostCopyLayout() - układ, w którym takie obrazy żyją stale (i są próbkowane).
void setHostCopyLayout(VkImageLayout layout);   // VulkanContext przy wyborze karty
VkImageLayout hostCopyLayout();
// Pierwsze przejście nowego obrazu (UNDEFINED) do hostCopyLayout() - na CPU, bez bufora poleceń.
void hostTransitionToCopyLayout(VkDevice device, VkImage image, uint32_t mipLevels = 1);
// Kopia pikseli (ciasno upakowanych) do poziomu mip obrazu 2D w układzie hostCopyLayout().
void hostCopyToImage(VkDevice device, VkImage image, const void* pixels, uint32_t width, uint32_t height, uint32_t mip = 0);
// Bariera pamięci (bufory, wszystkie zasoby) - np. po kopiowaniu danych do bufora wierzchołków.
void memoryBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                   VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

/**
 * @brief Zmienia układ pamięci obrazu (Transition Image Layout) za pomocą barier potoku (Pipeline Barriers).
 * 
 * Obrazy w GPU mogą mieć różne układy zoptymalizowane pod konkretne operacje (np. 
 * TRANSFER_DST_OPTIMAL do szybkiego zapisywania danych, SHADER_READ_ONLY_OPTIMAL do 
 * wydajnego próbkowania w shaderach). Przejście (transition) informuje GPU o zmianie 
 * przeznaczenia zasobu, aby mogło ono przeorganizować dane w pamięci podręcznej (cache).
 */
/**
 * @brief Jednorazowy bufor poleceń (inicjalizacja zasobów): begin -> nagranie -> end (wysyła i CZEKA na GPU).
 * Tylko poza pętlą klatki - vkQueueWaitIdle zatrzymuje CPU do końca pracy kolejki.
 */
VkCommandBuffer beginOneTimeCommands(VkDevice device, VkCommandPool pool);
void endOneTimeCommands(VkDevice device, VkCommandPool pool, VkQueue queue, VkCommandBuffer cmd);

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

/**
 * @brief Tworzy sampler z konfigurowalnym filtrowaniem (TextureFilter).
 */
[[nodiscard]] VkSampler createSampler(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t mipLevels, TextureFilter filter, AddressMode addressMode = AddressMode::Repeat);

/**
 * @brief Tworzy sampler z ucinaniem krawędzi (CLAMP_TO_EDGE) - przydatny przy fontach i skyboxach.
 */
// filter: LINEAR (wygładzanie) albo NEAREST (odczyt dokładnie jednego teksela - np. G-Buffer).
[[nodiscard]] VkSampler createClampSampler(VkDevice device, VkFilter filter = VK_FILTER_LINEAR);

}  // namespace vk
}  // namespace engine3d
