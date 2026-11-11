#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include "render/vulkan_util.h"

namespace engine3d {
namespace vk {

// ---------------------------------------------------------------
// ŁADOWANIE PLIKOW I SHADEROW
// ---------------------------------------------------------------

std::vector<char> readFile(const std::string& path) {
    // std::ios::ate ustawia kursor na końcu pliku (At The End), dzięki czemu tellg() od razu podaje rozmiar pliku.
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + path);
    }

    const auto fileSize = static_cast<std::size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    
    // Przesuwamy wskaźnik czytania z powrotem na początek pliku przed odczytem danych.
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
    return buffer;
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    
    // Kod shaderów SPIR-V w Vulkanie musi być przekazany jako wskaźnik na typ uint32_t (kod maszynowy SPIR-V składa się z 32-bitowych słów).
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module");
    }
    return shaderModule;
}

std::vector<VkVertexInputAttributeDescription> filterVertexAttributes(
    const std::vector<char>& vertexSpirv,
    const VkVertexInputAttributeDescription* attributes,
    size_t attributeCount) {
    std::vector<VkVertexInputAttributeDescription> all(attributes, attributes + attributeCount);

    constexpr uint32_t kSpirvMagic = 0x07230203;
    constexpr uint32_t kOpDecorate = 71;
    constexpr uint32_t kOpVariable = 59;
    constexpr uint32_t kDecorationLocation = 30;
    constexpr uint32_t kStorageClassInput = 1;

    const size_t wordCount = vertexSpirv.size() / sizeof(uint32_t);
    if (wordCount < 5) return all;
    std::vector<uint32_t> words(wordCount);
    std::memcpy(words.data(), vertexSpirv.data(), wordCount * sizeof(uint32_t));
    if (words[0] != kSpirvMagic) return all;

    // id -> lokacja oraz zbiór id zmiennych wejściowych. Wbudowane wejścia (np. SV_VertexID)
    // mają dekorację BuiltIn zamiast Location, więc naturalnie nie trafiają do wyniku.
    std::vector<std::pair<uint32_t, uint32_t>> locations;
    std::vector<uint32_t> inputIds;
    for (size_t i = 5; i < wordCount;) {
        const uint32_t opcode = words[i] & 0xFFFFu;
        const uint32_t length = words[i] >> 16;
        if (length == 0 || i + length > wordCount) return all; // uszkodzony SPIR-V
        if (opcode == kOpDecorate && length >= 4 && words[i + 2] == kDecorationLocation) {
            locations.emplace_back(words[i + 1], words[i + 3]);
        } else if (opcode == kOpVariable && length >= 4 && words[i + 3] == kStorageClassInput) {
            inputIds.push_back(words[i + 2]);
        }
        i += length;
    }

    std::vector<uint32_t> usedLocations;
    for (const auto& [id, location] : locations) {
        if (std::find(inputIds.begin(), inputIds.end(), id) != inputIds.end()) {
            usedLocations.push_back(location);
        }
    }

    std::vector<VkVertexInputAttributeDescription> used;
    for (const auto& attribute : all) {
        if (std::find(usedLocations.begin(), usedLocations.end(), attribute.location) != usedLocations.end()) {
            used.push_back(attribute);
        }
    }
    return used;
}

// ---------------------------------------------------------------
// ZARZĄDZANIE PAMIĘCIĄ GPU
// ---------------------------------------------------------------

uint32_t findMemoryType(
    VkPhysicalDevice physicalDevice,
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties
) {
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    // Pobranie informacji o pamięciach dostępnych w fizycznej karcie graficznej
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    // Iterujemy po wszystkich typach pamięci zgłoszonych przez kartę
    for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
        // Sprawdzamy czy dany typ jest zgodny z bitmaską wymagań zasobu (typeFilter)
        const bool typeMatches = (typeFilter & (1u << index)) != 0;
        // Sprawdzamy czy dany typ posiada wszystkie wymagane flagi (np. HOST_VISIBLE | HOST_COHERENT)
        const bool propertiesMatch =
            (memoryProperties.memoryTypes[index].propertyFlags & properties) == properties;
        
        if (typeMatches && propertiesMatch) {
            return index; // Zwracamy pasujący indeks pamięci
        }
    }

    throw std::runtime_error("failed to find suitable memory type");
}

void createBuffer(
    VkDevice device,
    VkPhysicalDevice physicalDevice,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& buffer,
    VkDeviceMemory& bufferMemory
) {
    // 1. Tworzenie logicznego obiektu bufora (VkBuffer) określającego rozmiar i sposób użycia
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // Dostępny tylko z jednej kolejki (np. graficznej)

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer");
    }

    // 2. Pobranie wymagań pamięciowych dla nowo utworzonego bufora (wielkość wyrównania, obsługiwane typy pamięci)
    VkMemoryRequirements memoryRequirements{};
    vkGetBufferMemoryRequirements(device, buffer, &memoryRequirements);

    // 3. Alokacja fizycznej pamięci na GPU dopasowanej do wymagań bufora
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memoryRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        physicalDevice, memoryRequirements.memoryTypeBits, properties
    );

    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device, buffer, nullptr);
        throw std::runtime_error("failed to allocate buffer memory");
    }

    // 4. Powiązanie przydzielonej pamięci z logicznym buforem (od offsetu 0)
    vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

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
) {
    if (bufferSize == 0) {
        buffer = VK_NULL_HANDLE;
        bufferMemory = VK_NULL_HANDLE;
        return;
    }

    // KROK 1: Tworzenie bufora stagingowego (pośredniczącego) widocznego dla CPU (HOST_VISIBLE).
    // HOST_COHERENT_BIT gwarantuje automatyczne synchronizowanie pamięci podręcznej CPU i GPU bez jawnego wywoływania flush.
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
    createBuffer(
        device, physicalDevice, bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, // Bufor będzie źródłem transferu kopiowania
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingBufferMemory
    );

    // KROK 2: Mapowanie pamięci staging bufora i zapisanie danych z procesora CPU.
    void* data = nullptr;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
    std::memcpy(data, sourceData, static_cast<std::size_t>(bufferSize));
    vkUnmapMemory(device, stagingBufferMemory);

    // KROK 3: Tworzenie docelowego bufora w szybkiej pamięci lokalnej karty (DEVICE_LOCAL_BIT).
    createBuffer(
        device, physicalDevice, bufferSize,
        usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, // Bufor będzie celem transferu kopiowania
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        buffer, bufferMemory
    );

    // KROK 4: Uruchomienie jednorazowego bufora komend (Command Buffer) do wykonania kopiowania buforów na GPU.
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; // Bufor będzie nagrany i wysłany tylko raz
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    // Dodanie komendy kopiowania pamięci na kolejce GPU
    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(commandBuffer, stagingBuffer, buffer, 1, &copyRegion);

    vkEndCommandBuffer(commandBuffer);

    // Złożenie zlecenia (Submit) do wykonania przez GPU
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue); // Czekamy na zakończenie transferu przez GPU (prosta synchronizacja CPU-GPU)

    // KROK 5: Posprzątanie bufora tymczasowego
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
}

// ---------------------------------------------------------------
// ZARZĄDZANIE OBRAZAMI (TEKSTURY, DEPTH)
// ---------------------------------------------------------------

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
    uint32_t arrayLayers,
    VkImageCreateFlags flags
) {
    // 1. Definiowanie struktury tworzenia obrazu 2D
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = flags; // Np. VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT dla Skyboxa (6 warstw)
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = arrayLayers;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL; // Układ pamięci optymalny dla GPU (tekstura jest przechowywana kafelkowo, a nie wierszami)
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Początkowy układ nieokreślony (dane mogą być zniszczone)
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image");
    }

    // 2. Pobranie wymagań alokacji pamięci dla obrazu
    VkMemoryRequirements memoryRequirements{};
    vkGetImageMemoryRequirements(device, image, &memoryRequirements);

    // 3. Alokacja pamięci fizycznej GPU
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memoryRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        physicalDevice, memoryRequirements.memoryTypeBits, properties
    );

    if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        vkDestroyImage(device, image, nullptr);
        throw std::runtime_error("failed to allocate image memory");
    }

    // 4. Powiązanie pamięci z obrazem
    vkBindImageMemory(device, image, imageMemory, 0);
}

VkImageView createImageView(
    VkDevice device,
    VkImage image,
    VkFormat format,
    VkImageAspectFlags aspectFlags,
    uint32_t mipLevels,
    uint32_t layerCount,
    VkImageViewType viewType
) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = viewType; // Np. VK_IMAGE_VIEW_TYPE_2D lub VK_IMAGE_VIEW_TYPE_CUBE
    viewInfo.format = format;
    
    // Zdefiniowanie podzasobów obrazu, do których widok ma dostęp
    viewInfo.subresourceRange.aspectMask = aspectFlags; // Zwykle VK_IMAGE_ASPECT_COLOR_BIT lub VK_IMAGE_ASPECT_DEPTH_BIT
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = layerCount;

    VkImageView imageView = VK_NULL_HANDLE;
    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image view");
    }
    return imageView;
}

void transitionImageLayout(
    VkDevice device,
    VkCommandPool commandPool,
    VkQueue graphicsQueue,
    VkImage image,
    VkFormat format,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    uint32_t mipLevels
) {
    // Alokacja i start jednorazowego bufora komend do przeprowadzenia tranzycji
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    // Bariera: zmiana układu obrazu. Etapy i dostępy zależą od rodzaju przejścia.
    ImageBarrier barrier{.image = image, .oldLayout = oldLayout, .newLayout = newLayout, .mipCount = mipLevels, .layerCount = 1};
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        // Przed wgraniem pikseli: poprzednia zawartość nieważna, czeka tylko zapis transferu.
        barrier.dstStage = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.dstAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        // Po wgraniu: zapis transferu musi być widoczny dla odczytu w shaderze pikseli.
        barrier.srcStage = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.srcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    } else {
        barrier.dstStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;   // inne przejścia (rzadkie): pełna bariera
        barrier.dstAccess = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    }
    imageBarrier(commandBuffer, barrier);

    vkEndCommandBuffer(commandBuffer);

    // Wysłanie komend do wykonania
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

// ---------------------------------------------------------------
// SAMPLERY
// ---------------------------------------------------------------

namespace {

float supportedAnisotropy(VkPhysicalDevice physicalDevice, float requestedLevel) {
    if (physicalDevice == VK_NULL_HANDLE || requestedLevel <= 1.0f) {
        return 1.0f;
    }

    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(physicalDevice, &features);
    if (features.samplerAnisotropy != VK_TRUE) {
        return 1.0f;
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    return std::max(1.0f, std::min(requestedLevel, properties.limits.maxSamplerAnisotropy));
}

float requestedAnisotropy(TextureFilter filter) {
    switch (filter) {
        case TextureFilter::Anisotropic2x:
            return 2.0f;
        case TextureFilter::Anisotropic4x:
            return 4.0f;
        case TextureFilter::Anisotropic8x:
            return 8.0f;
        case TextureFilter::Anisotropic16x:
            return 16.0f;
        default:
            return 1.0f;
    }
}

}

VkSampler createSampler(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t mipLevels, TextureFilter filter, AddressMode addressMode) {
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

    switch (filter) {
        case TextureFilter::Nearest:
            info.magFilter = VK_FILTER_NEAREST;
            info.minFilter = VK_FILTER_NEAREST;
            info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            info.anisotropyEnable = VK_FALSE;
            info.maxAnisotropy = 1.0f;
            break;
        case TextureFilter::Linear:
            info.magFilter = VK_FILTER_LINEAR;
            info.minFilter = VK_FILTER_LINEAR;
            info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            info.anisotropyEnable = VK_FALSE;
            info.maxAnisotropy = 1.0f;
            break;
        case TextureFilter::Trilinear:
            info.magFilter = VK_FILTER_LINEAR;
            info.minFilter = VK_FILTER_LINEAR;
            info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            info.anisotropyEnable = VK_FALSE;
            info.maxAnisotropy = 1.0f;
            break;
        case TextureFilter::Anisotropic2x:
            info.magFilter = VK_FILTER_LINEAR;
            info.minFilter = VK_FILTER_LINEAR;
            info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            info.maxAnisotropy = supportedAnisotropy(physicalDevice, requestedAnisotropy(filter));
            info.anisotropyEnable = info.maxAnisotropy > 1.0f ? VK_TRUE : VK_FALSE;
            break;
        case TextureFilter::Anisotropic4x:
            info.magFilter = VK_FILTER_LINEAR;
            info.minFilter = VK_FILTER_LINEAR;
            info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            info.maxAnisotropy = supportedAnisotropy(physicalDevice, requestedAnisotropy(filter));
            info.anisotropyEnable = info.maxAnisotropy > 1.0f ? VK_TRUE : VK_FALSE;
            break;
        case TextureFilter::Anisotropic8x:
            info.magFilter = VK_FILTER_LINEAR;
            info.minFilter = VK_FILTER_LINEAR;
            info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            info.maxAnisotropy = supportedAnisotropy(physicalDevice, requestedAnisotropy(filter));
            info.anisotropyEnable = info.maxAnisotropy > 1.0f ? VK_TRUE : VK_FALSE;
            break;
        case TextureFilter::Anisotropic16x:
            info.magFilter = VK_FILTER_LINEAR;
            info.minFilter = VK_FILTER_LINEAR;
            info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            info.maxAnisotropy = supportedAnisotropy(physicalDevice, requestedAnisotropy(filter));
            info.anisotropyEnable = info.maxAnisotropy > 1.0f ? VK_TRUE : VK_FALSE;
            break;
    }

    switch (addressMode) {
        case AddressMode::Repeat:
            info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            break;
        case AddressMode::MirroredRepeat:
            info.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            info.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            info.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            break;
        case AddressMode::ClampToEdge:
            info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            break;
    }

    info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    info.unnormalizedCoordinates = VK_FALSE;
    info.compareEnable = VK_FALSE;
    info.maxLod = static_cast<float>(mipLevels);

    VkSampler sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(device, &info, nullptr, &sampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create sampler");
    }
    return sampler;
}

VkSampler createClampSampler(VkDevice device) {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    
    // Tryb CLAMP_TO_EDGE: współrzędne UV poza [0, 1] są przycinane do krawędzi tekstury.
    // Zapobiega to prześwitom sąsiednich znaków w atlasach czcionek oraz czarnym liniom na łączeniach skyboxa.
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.maxLod = 1.0f;

    VkSampler sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create clamp sampler");
    }
    return sampler;
}

void imageBarriers(VkCommandBuffer cmd, const ImageBarrier* barriers, uint32_t count) {
    VkImageMemoryBarrier2 out[16];
    uint32_t n = 0;
    auto flush = [&]() {
        if (n == 0) return;
        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = n;
        dep.pImageMemoryBarriers = out;
        vkCmdPipelineBarrier2(cmd, &dep);
        n = 0;
    };
    for (uint32_t i = 0; i < count; ++i) {
        const ImageBarrier& b = barriers[i];
        VkImageMemoryBarrier2& o = out[n++];
        o = {};
        o.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        o.srcStageMask = b.srcStage;
        o.srcAccessMask = b.srcAccess;
        o.dstStageMask = b.dstStage;
        o.dstAccessMask = b.dstAccess;
        o.oldLayout = b.oldLayout;
        o.newLayout = b.newLayout;
        o.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        o.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        o.image = b.image;
        o.subresourceRange = {b.aspect, b.baseMip, b.mipCount, b.baseLayer, b.layerCount};
        if (n == 16) flush();
    }
    flush();
}

void generateMipmaps(VkCommandBuffer cmd, VkImage image, uint32_t width, uint32_t height, uint32_t mipLevels) {
    int32_t w = static_cast<int32_t>(width), h = static_cast<int32_t>(height);
    for (uint32_t i = 1; i < mipLevels; i++) {
        // Poziom i-1 gotowy (zapisany kopią albo poprzednim blitem) -> źródło blitu.
        imageBarrier(cmd, {.image = image, .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           .srcStage = VK_PIPELINE_STAGE_2_TRANSFER_BIT, .srcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           .dstStage = VK_PIPELINE_STAGE_2_BLIT_BIT, .dstAccess = VK_ACCESS_2_TRANSFER_READ_BIT,
                           .baseMip = i - 1, .mipCount = 1, .layerCount = 1});
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1};
        blit.srcOffsets[1] = {w, h, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
        blit.dstOffsets[1] = {w > 1 ? w / 2 : 1, h > 1 ? h / 2 : 1, 1};
        vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                       VK_FILTER_LINEAR);
        // Poziom i-1 już niepotrzebny do blitów -> odczyt w shaderze.
        imageBarrier(cmd, {.image = image, .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           .srcStage = VK_PIPELINE_STAGE_2_BLIT_BIT, .srcAccess = VK_ACCESS_2_NONE,
                           .dstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, .dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                           .baseMip = i - 1, .mipCount = 1, .layerCount = 1});
        if (w > 1) w /= 2;
        if (h > 1) h /= 2;
    }
    // Ostatni poziom (zapisany ostatnim blitem albo jedyny) -> odczyt w shaderze.
    imageBarrier(cmd, {.image = image, .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       .srcStage = VK_PIPELINE_STAGE_2_TRANSFER_BIT, .srcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                       .dstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, .dstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                       .baseMip = mipLevels - 1, .mipCount = 1, .layerCount = 1});
}

void pushDescriptorSet(VkCommandBuffer cmd, VkPipelineBindPoint bindPoint, VkPipelineLayout layout, uint32_t set,
                       uint32_t count, const VkWriteDescriptorSet* writes) {
    vkCmdPushDescriptorSet(cmd, bindPoint, layout, set, count, writes);
}

namespace {
VkImageLayout g_hostCopyLayout = VK_IMAGE_LAYOUT_GENERAL;
}

void setHostCopyLayout(VkImageLayout layout) { g_hostCopyLayout = layout; }
VkImageLayout hostCopyLayout() { return g_hostCopyLayout; }

void hostTransitionToCopyLayout(VkDevice device, VkImage image, uint32_t mipLevels) {
    VkHostImageLayoutTransitionInfo t{};
    t.sType = VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO;
    t.image = image;
    t.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    t.newLayout = g_hostCopyLayout;
    t.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};
    vkTransitionImageLayout(device, 1, &t);
}

void hostCopyToImage(VkDevice device, VkImage image, const void* pixels, uint32_t width, uint32_t height, uint32_t mip) {
    VkMemoryToImageCopy region{};
    region.sType = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY;
    region.pHostPointer = pixels;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
    region.imageExtent = {width, height, 1};
    VkCopyMemoryToImageInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO;
    info.dstImage = image;
    info.dstImageLayout = g_hostCopyLayout;
    info.regionCount = 1;
    info.pRegions = &region;
    vkCopyMemoryToImage(device, &info);
}

void pushImageSamplers(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t set, const VkDescriptorImageInfo* images,
                       uint32_t count) {
    VkWriteDescriptorSet writes[8];
    for (uint32_t i = 0; i < count && i < 8; ++i) {
        writes[i] = {};
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &images[i];
    }
    pushDescriptorSet(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, set, count < 8 ? count : 8, writes);
}

void memoryBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                   VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    VkMemoryBarrier2 b{};
    b.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    b.srcStageMask = srcStage;
    b.srcAccessMask = srcAccess;
    b.dstStageMask = dstStage;
    b.dstAccessMask = dstAccess;
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}

}  // namespace vk
}  // namespace engine3d
