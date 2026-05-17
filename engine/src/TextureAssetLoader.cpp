#include "TextureAssetLoader.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace engine3d {
namespace {

// Odczytuje 32-bitowa wartosc little-endian z surowych bajtow naglowka pliku.
[[nodiscard]] std::uint32_t readLittleEndianUInt32(const std::array<char, 4>& bytes) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[0])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[1])) << 8u) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[2])) << 16u) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[3])) << 24u);
}

// Odczytuje 16-bitowa wartosc little-endian z surowych bajtow.
[[nodiscard]] std::uint16_t readLittleEndianUInt16(const std::array<char, 2>& bytes) {
    return static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[0])) |
           (static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[1])) << 8u);
}

// Dekoduje prosty plik BMP 24-bit do wspolnego formatu RGBA.
[[nodiscard]] TextureUploadData loadBmpTexture(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open BMP texture: " + path);
    }

    // BMP ma osobny naglowek pliku oraz naglowek informacji o obrazie.
    std::array<char, 14> fileHeader{};
    std::array<char, 40> infoHeader{};
    file.read(fileHeader.data(), static_cast<std::streamsize>(fileHeader.size()));
    file.read(infoHeader.data(), static_cast<std::streamsize>(infoHeader.size()));

    if (!file || fileHeader[0] != 'B' || fileHeader[1] != 'M') {
        throw std::runtime_error("unsupported BMP signature: " + path);
    }

    const std::uint32_t pixelOffset = readLittleEndianUInt32({fileHeader[10], fileHeader[11], fileHeader[12], fileHeader[13]});
    const std::uint32_t dibHeaderSize = readLittleEndianUInt32({infoHeader[0], infoHeader[1], infoHeader[2], infoHeader[3]});
    const std::uint32_t width = readLittleEndianUInt32({infoHeader[4], infoHeader[5], infoHeader[6], infoHeader[7]});
    const std::uint32_t height = readLittleEndianUInt32({infoHeader[8], infoHeader[9], infoHeader[10], infoHeader[11]});
    const std::uint16_t planes = readLittleEndianUInt16({infoHeader[12], infoHeader[13]});
    const std::uint16_t bitsPerPixel = readLittleEndianUInt16({infoHeader[14], infoHeader[15]});
    const std::uint32_t compression = readLittleEndianUInt32({infoHeader[16], infoHeader[17], infoHeader[18], infoHeader[19]});

    if (dibHeaderSize < 40 || planes != 1 || bitsPerPixel != 24 || compression != 0) {
        throw std::runtime_error("only uncompressed 24-bit BMP textures are supported: " + path);
    }

    // Kazdy wiersz BMP jest wyrownany do 4 bajtow, wiec stride moze byc wiekszy niz sama szerokosc * 3.
    const std::size_t rowStride = (static_cast<std::size_t>(width) * 3u + 3u) & ~std::size_t{3u};
    std::vector<std::uint8_t> rowData(rowStride * height);
    file.seekg(static_cast<std::streamoff>(pixelOffset), std::ios::beg);
    file.read(reinterpret_cast<char*>(rowData.data()), static_cast<std::streamsize>(rowData.size()));
    if (!file) {
        throw std::runtime_error("failed to read BMP texture pixels: " + path);
    }

    std::vector<std::uint8_t> rgbaPixels(static_cast<std::size_t>(width) * height * 4u);

    // BMP zapisuje piksele jako BGR i zwykle od dolu, dlatego podczas kopiowania odwracamy kolejnosc wierszy.
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::size_t sourceRow = static_cast<std::size_t>(height - 1 - y) * rowStride;
        const std::size_t destinationRow = static_cast<std::size_t>(y) * width * 4u;
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t sourceOffset = sourceRow + static_cast<std::size_t>(x) * 3u;
            const std::size_t destinationOffset = destinationRow + static_cast<std::size_t>(x) * 4u;
            rgbaPixels[destinationOffset + 0] = rowData[sourceOffset + 2];
            rgbaPixels[destinationOffset + 1] = rowData[sourceOffset + 1];
            rgbaPixels[destinationOffset + 2] = rowData[sourceOffset + 0];
            rgbaPixels[destinationOffset + 3] = 255;
        }
    }

    return TextureAssetLoader::fromRgbaPixels(width, height, std::move(rgbaPixels));
}

// Dekoduje tekstury TGA w wariancie true-color bez mapy kolorow.
[[nodiscard]] TextureUploadData loadTgaTexture(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open TGA texture: " + path);
    }

    std::array<char, 18> header{};
    file.read(header.data(), static_cast<std::streamsize>(header.size()));
    if (!file) {
        throw std::runtime_error("failed to read TGA header: " + path);
    }

    const std::uint8_t idLength = static_cast<std::uint8_t>(header[0]);
    const std::uint8_t colorMapType = static_cast<std::uint8_t>(header[1]);
    const std::uint8_t imageType = static_cast<std::uint8_t>(header[2]);
    const std::uint16_t width = readLittleEndianUInt16({header[12], header[13]});
    const std::uint16_t height = readLittleEndianUInt16({header[14], header[15]});
    const std::uint8_t bitsPerPixel = static_cast<std::uint8_t>(header[16]);
    const std::uint8_t imageDescriptor = static_cast<std::uint8_t>(header[17]);

    if (colorMapType != 0 || (imageType != 2 && imageType != 10) || (bitsPerPixel != 24 && bitsPerPixel != 32)) {
        throw std::runtime_error("only true-color 24/32-bit TGA textures are supported: " + path);
    }

    file.seekg(idLength, std::ios::cur);

    // TGA wystepuje tu jako 24-bit lub 32-bit, wiec liczbe bajtow na piksel liczymy dynamicznie.
    const std::size_t bytesPerPixel = bitsPerPixel / 8u;
    const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
    std::vector<std::uint8_t> sourcePixels(pixelCount * bytesPerPixel);

    // Typ 2 to dane surowe, a typ 10 oznacza RLE, czyli prosta kompresje przebiegow.
    if (imageType == 2) {
        file.read(reinterpret_cast<char*>(sourcePixels.data()), static_cast<std::streamsize>(sourcePixels.size()));
    } else {
        std::size_t offset = 0;
        while (offset < sourcePixels.size()) {
            unsigned char packetHeader = 0;
            file.read(reinterpret_cast<char*>(&packetHeader), 1);
            if (!file) {
                throw std::runtime_error("failed while decoding TGA RLE stream: " + path);
            }

            const std::size_t packetPixelCount = static_cast<std::size_t>((packetHeader & 0x7Fu) + 1u);
            if ((packetHeader & 0x80u) != 0) {
                std::vector<std::uint8_t> pixel(bytesPerPixel);
                file.read(reinterpret_cast<char*>(pixel.data()), static_cast<std::streamsize>(bytesPerPixel));
                for (std::size_t index = 0; index < packetPixelCount; ++index) {
                    std::memcpy(sourcePixels.data() + offset, pixel.data(), bytesPerPixel);
                    offset += bytesPerPixel;
                }
            } else {
                const std::size_t packetBytes = packetPixelCount * bytesPerPixel;
                file.read(reinterpret_cast<char*>(sourcePixels.data() + offset), static_cast<std::streamsize>(packetBytes));
                offset += packetBytes;
            }
        }
    }

    if (!file) {
        throw std::runtime_error("failed to read TGA texture pixels: " + path);
    }

    // Flaga origin mowi, czy pierwszy wiersz w pliku odpowiada gorze czy dolowi obrazu.
    const bool topOrigin = (imageDescriptor & 0x20u) != 0;
    std::vector<std::uint8_t> rgbaPixels(pixelCount * 4u);

    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint32_t sourceY = topOrigin ? y : (height - 1u - y);
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t sourceOffset = (static_cast<std::size_t>(sourceY) * width + x) * bytesPerPixel;
            const std::size_t destinationOffset = (static_cast<std::size_t>(y) * width + x) * 4u;
            rgbaPixels[destinationOffset + 0] = sourcePixels[sourceOffset + 2];
            rgbaPixels[destinationOffset + 1] = sourcePixels[sourceOffset + 1];
            rgbaPixels[destinationOffset + 2] = sourcePixels[sourceOffset + 0];
            rgbaPixels[destinationOffset + 3] = bytesPerPixel == 4u ? sourcePixels[sourceOffset + 3] : 255;
        }
    }

    return TextureAssetLoader::fromRgbaPixels(width, height, std::move(rgbaPixels));
}

// Buduje znacznik FourCC dla formatow DDS typu DXT1/DXT3/DXT5.
[[nodiscard]] std::uint32_t makeFourCC(char a, char b, char c, char d) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8u) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16u) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24u);
}

// Dekoduje naglowek DDS i przygotowuje opis skompresowanych mipmap bez rozpakowywania ich na CPU.
[[nodiscard]] TextureUploadData loadDdsTexture(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open DDS texture: " + path);
    }

    std::array<char, 4> magic{};
    std::array<char, 124> header{};
    file.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    file.read(header.data(), static_cast<std::streamsize>(header.size()));
    if (!file || std::string(magic.data(), magic.size()) != "DDS ") {
        throw std::runtime_error("unsupported DDS signature: " + path);
    }

    const std::uint32_t headerSize = readLittleEndianUInt32({header[0], header[1], header[2], header[3]});
    const std::uint32_t height = readLittleEndianUInt32({header[8], header[9], header[10], header[11]});
    const std::uint32_t width = readLittleEndianUInt32({header[12], header[13], header[14], header[15]});
    const std::uint32_t mipMapCount = readLittleEndianUInt32({header[24], header[25], header[26], header[27]});
    const std::uint32_t pixelFormatSize = readLittleEndianUInt32({header[72], header[73], header[74], header[75]});
    const std::uint32_t fourCC = readLittleEndianUInt32({header[80], header[81], header[82], header[83]});

    if (headerSize != 124 || pixelFormatSize != 32) {
        throw std::runtime_error("unsupported DDS header layout: " + path);
    }

    // W DDS zostawiamy dane w skompresowanej postaci blokowej, bo Vulkan potrafi probkowac je bezposrednio.
    TextureUploadData texture;
    texture.width = width;
    texture.height = height;
    texture.mipLevels = std::max(1u, mipMapCount);

    std::size_t blockSize = 0;
    if (fourCC == makeFourCC('D', 'X', 'T', '1')) {
        texture.format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        blockSize = 8;
    } else if (fourCC == makeFourCC('D', 'X', 'T', '3')) {
        texture.format = VK_FORMAT_BC2_UNORM_BLOCK;
        blockSize = 16;
    } else if (fourCC == makeFourCC('D', 'X', 'T', '5')) {
        texture.format = VK_FORMAT_BC3_UNORM_BLOCK;
        blockSize = 16;
    } else {
        throw std::runtime_error("unsupported DDS compression format: " + path);
    }

    file.seekg(0, std::ios::end);
    const std::streamoff endPosition = file.tellg();
    file.seekg(128, std::ios::beg);
    const std::size_t dataSize = static_cast<std::size_t>(endPosition - static_cast<std::streamoff>(128));
    texture.pixelBytes.resize(dataSize);
    file.read(reinterpret_cast<char*>(texture.pixelBytes.data()), static_cast<std::streamsize>(texture.pixelBytes.size()));
    if (!file) {
        throw std::runtime_error("failed to read DDS pixel data: " + path);
    }

    std::size_t offset = 0;
    uint32_t mipWidth = width;
    uint32_t mipHeight = height;
    // Dla kazdego poziomu mipmapy wyliczamy osobny region kopiowania do obrazu Vulkan.
    for (uint32_t mipLevel = 0; mipLevel < texture.mipLevels; ++mipLevel) {
        const uint32_t blockWidth = std::max(1u, (mipWidth + 3u) / 4u);
        const uint32_t blockHeight = std::max(1u, (mipHeight + 3u) / 4u);
        const std::size_t mipSize = static_cast<std::size_t>(blockWidth) * blockHeight * blockSize;
        if (offset + mipSize > texture.pixelBytes.size()) {
            throw std::runtime_error("DDS mip data is truncated: " + path);
        }

        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = mipLevel;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {mipWidth, mipHeight, 1};
        texture.copyRegions.push_back(region);

        offset += mipSize;
        mipWidth = std::max(1u, mipWidth / 2u);
        mipHeight = std::max(1u, mipHeight / 2u);
    }

    return texture;
}

// Normalizuje rozszerzenie pliku, aby parser nie zalezyl od wielkosci liter w nazwie assetu.
[[nodiscard]] std::string lowercaseExtension(const std::string& path) {
    const std::size_t lastDot = path.find_last_of('.');
    if (lastDot == std::string::npos) {
        return {};
    }

    std::string extension = path.substr(lastDot);
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

}  // namespace

// Publiczny punkt wejscia loadera: wybiera odpowiedni parser po rozszerzeniu.
TextureUploadData TextureAssetLoader::load(const std::string& path) {
    const std::string extension = lowercaseExtension(path);
    if (extension == ".bmp") {
        return loadBmpTexture(path);
    }
    if (extension == ".tga") {
        return loadTgaTexture(path);
    }
    if (extension == ".dds") {
        return loadDdsTexture(path);
    }
    throw std::runtime_error("unsupported texture extension: " + path);
}

// Zamienia zwykla tablice RGBA na strukture zgodna z oczekiwaniami etapu uploadu do obrazu Vulkan.
TextureUploadData TextureAssetLoader::fromRgbaPixels(
    uint32_t width,
    uint32_t height,
    std::vector<std::uint8_t> rgbaPixels
) {
    TextureUploadData texture;
    texture.width = width;
    texture.height = height;
    texture.format = VK_FORMAT_R8G8B8A8_UNORM;
    texture.pixelBytes = std::move(rgbaPixels);

    // Dla nieskompresowanej tekstury 2D wystarczy jeden region kopiowania obejmujacy caly obraz.
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {width, height, 1};
    texture.copyRegions.push_back(region);
    return texture;
}

}  // namespace engine3d