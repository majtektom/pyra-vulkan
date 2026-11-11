#include "core/logger.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "render/texture_asset_loader.h"

namespace engine3d {
namespace {

// Blok koloru BC1 (8 bajtów): dwa kolory bazowe 5:6:5 i 2-bitowy indeks dla każdego z 16 pikseli.
// Indeksy 2 i 3 to kolory pośrednie (1/3 i 2/3 drogi). Wyjątek: w DXT1, gdy color0 <= color1, indeks 2
// to połowa drogi, a 3 - przezroczysta czerń. W DXT3/DXT5 alfa jest osobno, więc zawsze 4 kolory.
void decodeBc1ColorBlock(const std::uint8_t* block, bool dxt1, std::uint8_t out[16][4]) {
    const std::uint16_t color0 = block[0] | (block[1] << 8);
    const std::uint16_t color1 = block[2] | (block[3] << 8);
    const std::uint32_t code = block[4] | (block[5] << 8) | (block[6] << 16) | (static_cast<std::uint32_t>(block[7]) << 24);

    auto expand = [](std::uint16_t c, int rgb[3]) {
        rgb[0] = ((c >> 11) & 31) * 255 / 31;
        rgb[1] = ((c >> 5) & 63) * 255 / 63;
        rgb[2] = (c & 31) * 255 / 31;
    };
    int c0[3], c1[3];
    expand(color0, c0);
    expand(color1, c1);

    std::uint8_t palette[4][4];
    const bool fourColors = !dxt1 || color0 > color1;
    for (int k = 0; k < 3; ++k) {
        palette[0][k] = static_cast<std::uint8_t>(c0[k]);
        palette[1][k] = static_cast<std::uint8_t>(c1[k]);
        palette[2][k] = static_cast<std::uint8_t>(fourColors ? (2 * c0[k] + c1[k]) / 3 : (c0[k] + c1[k]) / 2);
        palette[3][k] = static_cast<std::uint8_t>(fourColors ? (c0[k] + 2 * c1[k]) / 3 : 0);
    }
    palette[0][3] = palette[1][3] = palette[2][3] = 255;
    palette[3][3] = fourColors ? 255 : 0;

    for (int i = 0; i < 16; ++i) {
        std::memcpy(out[i], palette[(code >> (2 * i)) & 3], 4);
    }
}

// Blok alfy BC4/DXT5 (8 bajtów): dwie wartości bazowe i 3-bitowy indeks na piksel (6 lub 4 wartości pośrednie).
void decodeBc4AlphaBlock(const std::uint8_t* block, std::uint8_t out[16][4]) {
    const int a0 = block[0];
    const int a1 = block[1];
    std::uint64_t indices = 0;
    for (int k = 0; k < 6; ++k) indices |= static_cast<std::uint64_t>(block[2 + k]) << (8 * k);

    std::uint8_t alphas[8] = {static_cast<std::uint8_t>(a0), static_cast<std::uint8_t>(a1)};
    if (a0 > a1) {
        for (int k = 1; k <= 6; ++k) alphas[k + 1] = static_cast<std::uint8_t>(((7 - k) * a0 + k * a1) / 7);
    } else {
        for (int k = 1; k <= 4; ++k) alphas[k + 1] = static_cast<std::uint8_t>(((5 - k) * a0 + k * a1) / 5);
        alphas[6] = 0;
        alphas[7] = 255;
    }
    for (int i = 0; i < 16; ++i) out[i][3] = alphas[(indices >> (3 * i)) & 7];
}

/**
 * @brief Odczytuje 32-bitową wartość bez znaku w formacie little-endian z surowej tablicy 4 bajtów.
 * 
 * Little-endian to format zapisu danych, w którym najmniej znaczący bajt (LSB) jest zapisywany 
 * jako pierwszy. Stosowany powszechnie na architekturach x86/x64.
 */
[[nodiscard]] std::uint32_t readLittleEndianUInt32(const std::array<char, 4>& bytes) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[0])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[1])) << 8u) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[2])) << 16u) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[3])) << 24u);
}

/**
 * @brief Odczytuje 16-bitową wartość bez znaku w formacie little-endian z 2 bajtów.
 */
[[nodiscard]] std::uint16_t readLittleEndianUInt16(const std::array<char, 2>& bytes) {
    return static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[0])) |
           (static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[1])) << 8u);
}

/**
 * @brief Dekoduje nieskompresowany plik BMP 24-bit i konwertuje go na RGBA8.
 * 
 * BMP (Bitmap) posiada stały nagłówek pliku (14 bajtów) oraz nagłówek informacyjny (najczęściej BITMAPINFOHEADER - 40 bajtów).
 * Piksele w formacie 24-bit BMP są zapisane w kolejności BGR (Blue, Green, Red). 
 * Dodatkowo, wiersze pliku BMP są wyrównane do wielokrotności 4 bajtów (padding).
 * Standardowo pliki BMP przechowują piksele od dołu do góry (dolny wiersz obrazu jest pierwszy w pliku).
 */
[[nodiscard]] TextureUploadData loadBmpTexture(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("nie można otworzyć tekstury BMP: " + path);
    }

    std::array<char, 14> fileHeader{};
    std::array<char, 40> infoHeader{};
    file.read(fileHeader.data(), static_cast<std::streamsize>(fileHeader.size()));
    file.read(infoHeader.data(), static_cast<std::streamsize>(infoHeader.size()));

    // Sprawdzenie sygnatury pliku BMP (znaki 'B' i 'M' na początku)
    if (!file || fileHeader[0] != 'B' || fileHeader[1] != 'M') {
        throw std::runtime_error("to nie jest plik BMP (zła sygnatura): " + path);
    }

    // Odczytanie ważnych pól nagłówka
    const std::uint32_t pixelOffset = readLittleEndianUInt32({fileHeader[10], fileHeader[11], fileHeader[12], fileHeader[13]});
    const std::uint32_t dibHeaderSize = readLittleEndianUInt32({infoHeader[0], infoHeader[1], infoHeader[2], infoHeader[3]});
    const std::uint32_t width = readLittleEndianUInt32({infoHeader[4], infoHeader[5], infoHeader[6], infoHeader[7]});
    const std::uint32_t height = readLittleEndianUInt32({infoHeader[8], infoHeader[9], infoHeader[10], infoHeader[11]});
    const std::uint16_t planes = readLittleEndianUInt16({infoHeader[12], infoHeader[13]});
    const std::uint16_t bitsPerPixel = readLittleEndianUInt16({infoHeader[14], infoHeader[15]});
    const std::uint32_t compression = readLittleEndianUInt32({infoHeader[16], infoHeader[17], infoHeader[18], infoHeader[19]});

    // Wspieramy tylko prosty, nieskompresowany format 24-bit BGR
    if (dibHeaderSize < 40 || planes != 1 || bitsPerPixel != 24 || compression != 0) {
        throw std::runtime_error("obsługiwane są tylko nieskompresowane 24-bitowe BMP: " + path);
    }

    // Wyliczenie stride wiersza (każdy wiersz w pliku musi mieć rozmiar będący wielokrotnością 4 bajtów)
    const std::size_t rowStride = (static_cast<std::size_t>(width) * 3u + 3u) & ~std::size_t{3u};
    std::vector<std::uint8_t> rowData(rowStride * height);
    
    // Przesunięcie strumienia pliku do początku danych pikseli
    file.seekg(static_cast<std::streamoff>(pixelOffset), std::ios::beg);
    file.read(reinterpret_cast<char*>(rowData.data()), static_cast<std::streamsize>(rowData.size()));
    if (!file) {
        throw std::runtime_error("nie udało się odczytać pikseli BMP: " + path);
    }

    // Docelowy bufor w standardzie RGBA (4 bajty na piksel)
    std::vector<std::uint8_t> rgbaPixels(static_cast<std::size_t>(width) * height * 4u);

    // Kopiowanie danych pikseli z odwróceniem wierszy (z dołu-do-góry na z góry-do-dołu)
    // oraz konwersją formatu z BGR do RGBA (dodanie kanału alfa równego 255)
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::size_t sourceRow = static_cast<std::size_t>(height - 1 - y) * rowStride;
        const std::size_t destinationRow = static_cast<std::size_t>(y) * width * 4u;
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t sourceOffset = sourceRow + static_cast<std::size_t>(x) * 3u;
            const std::size_t destinationOffset = destinationRow + static_cast<std::size_t>(x) * 4u;
            
            rgbaPixels[destinationOffset + 0] = rowData[sourceOffset + 2]; // R
            rgbaPixels[destinationOffset + 1] = rowData[sourceOffset + 1]; // G
            rgbaPixels[destinationOffset + 2] = rowData[sourceOffset + 0]; // B
            rgbaPixels[destinationOffset + 3] = 255;                       // A (pełna nieprzezroczystość)
        }
    }

    return TextureAssetLoader::fromRgbaPixels(width, height, std::move(rgbaPixels));
}

/**
 * @brief Dekoduje pliki formatu TGA (Truevision Targa) 24-bit i 32-bit.
 * 
 * TGA wspiera zarówno surowe dane (Raw True-Color, imageType 2), jak i prostą 
 * kompresję bezstratną RLE (Run-Length Encoding, imageType 10).
 * Kanały kolorów zapisane są w kolejności BGR lub BGRA.
 * Nagłówek TGA ma rozmiar 18 bajtów.
 */
[[nodiscard]] TextureUploadData loadTgaTexture(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("nie można otworzyć tekstury TGA: " + path);
    }

    std::array<char, 18> header{};
    file.read(header.data(), static_cast<std::streamsize>(header.size()));
    if (!file) {
        throw std::runtime_error("nie udało się odczytać nagłówka TGA: " + path);
    }

    // Pola nagłówka TGA
    const std::uint8_t idLength = static_cast<std::uint8_t>(header[0]);
    const std::uint8_t colorMapType = static_cast<std::uint8_t>(header[1]);
    const std::uint8_t imageType = static_cast<std::uint8_t>(header[2]);
    const std::uint16_t width = readLittleEndianUInt16({header[12], header[13]});
    const std::uint16_t height = readLittleEndianUInt16({header[14], header[15]});
    const std::uint8_t bitsPerPixel = static_cast<std::uint8_t>(header[16]);
    const std::uint8_t imageDescriptor = static_cast<std::uint8_t>(header[17]);

    // Sprawdzamy czy to obsługiwany format: brak palety (colorMapType == 0),
    // typ obrazu 2 (raw) lub 10 (RLE), głębia 24 lub 32 bity.
    if (colorMapType != 0 || (imageType != 2 && imageType != 10) || (bitsPerPixel != 24 && bitsPerPixel != 32)) {
        throw std::runtime_error("obsługiwane są tylko 24/32-bitowe TGA (true-color): " + path);
    }

    // Pomiń opcjonalne pole ID na początku pliku
    file.seekg(idLength, std::ios::cur);

    const std::size_t bytesPerPixel = bitsPerPixel / 8u;
    const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
    std::vector<std::uint8_t> sourcePixels(pixelCount * bytesPerPixel);

    if (imageType == 2) {
        // Nieskompresowane dane: wczytujemy cały blok pikseli na raz
        file.read(reinterpret_cast<char*>(sourcePixels.data()), static_cast<std::streamsize>(sourcePixels.size()));
    } else {
        // Dane skompresowane metodą Run-Length Encoding (RLE):
        // Strumień składa się z pakietów. Każdy pakiet zaczyna się od 1-bajtowego nagłówka.
        // Jeśli bit 7 nagłówka jest ustawiony (1), to jest to pakiet RLE: kolejny piksel powtarzamy (nagłówek & 0x7F) + 1 razy.
        // Jeśli bit 7 jest czysty (0), to jest to pakiet surowy (raw): wczytujemy kolejnych (nagłówek & 0x7F) + 1 pikseli bezpośrednio z pliku.
        std::size_t offset = 0;
        while (offset < sourcePixels.size()) {
            unsigned char packetHeader = 0;
            file.read(reinterpret_cast<char*>(&packetHeader), 1);
            if (!file) {
                throw std::runtime_error("błąd dekodowania kompresji RLE w TGA: " + path);
            }

            const std::size_t packetPixelCount = static_cast<std::size_t>((packetHeader & 0x7Fu) + 1u);
            if ((packetHeader & 0x80u) != 0) {
                // Pakiet RLE (powtórzone piksele)
                std::vector<std::uint8_t> pixel(bytesPerPixel);
                file.read(reinterpret_cast<char*>(pixel.data()), static_cast<std::streamsize>(bytesPerPixel));
                for (std::size_t index = 0; index < packetPixelCount; ++index) {
                    std::memcpy(sourcePixels.data() + offset, pixel.data(), bytesPerPixel);
                    offset += bytesPerPixel;
                }
            } else {
                // Pakiet surowy (raw packet)
                const std::size_t packetBytes = packetPixelCount * bytesPerPixel;
                file.read(reinterpret_cast<char*>(sourcePixels.data() + offset), static_cast<std::streamsize>(packetBytes));
                offset += packetBytes;
            }
        }
    }

    if (!file) {
        throw std::runtime_error("nie udało się odczytać pikseli TGA: " + path);
    }

    // Bit 5 w bajcie imageDescriptor określa kierunek osi Y (tzw. screen origin).
    // Jeśli jest ustawiony, wiersze są ułożone od góry do dołu. W przeciwnym wypadku od dołu do góry.
    const bool topOrigin = (imageDescriptor & 0x20u) != 0;
    std::vector<std::uint8_t> rgbaPixels(pixelCount * 4u);

    // Kopiowanie danych do formatu RGBA8
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint32_t sourceY = topOrigin ? y : (height - 1u - y);
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t sourceOffset = (static_cast<std::size_t>(sourceY) * width + x) * bytesPerPixel;
            const std::size_t destinationOffset = (static_cast<std::size_t>(y) * width + x) * 4u;
            
            rgbaPixels[destinationOffset + 0] = sourcePixels[sourceOffset + 2]; // R (z BGR)
            rgbaPixels[destinationOffset + 1] = sourcePixels[sourceOffset + 1]; // G
            rgbaPixels[destinationOffset + 2] = sourcePixels[sourceOffset + 0]; // B (z BGR)
            rgbaPixels[destinationOffset + 3] = bytesPerPixel == 4u ? sourcePixels[sourceOffset + 3] : 255; // A (jeśli brak w pliku, to 255)
        }
    }

    return TextureAssetLoader::fromRgbaPixels(width, height, std::move(rgbaPixels));
}

/**
 * @brief Pomocnicza funkcja budująca wartość FourCC (kod czteroznakowy) z podanych znaków ASCII.
 * 
 * Używana do identyfikacji formatów kompresji blokowej w plikach DDS.
 */
[[nodiscard]] std::uint32_t makeFourCC(char a, char b, char c, char d) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8u) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16u) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24u);
}

/**
 * @brief Parser plików formatu DDS (DirectDraw Surface) - obsługuje skompresowane tekstury DXT1/3/5.
 * 
 * DDS to profesjonalny format gier przechowujący tekstury w postaci natywnie skompresowanej 
 * przez kartę graficzną (S3 Texture Compression - S3TC / DXT / BC).
 * Pozwala to na wysłanie spakowanych bloków (np. 8 lub 16 bajtów na siatkę pikseli 4x4)
 * bezpośrednio do VRAM. Karta graficzna dekompresuje te bloki w locie podczas próbkowania w shaderze,
 * co drastycznie oszczędza przepustowość pamięci GPU i redukuje zużycie VRAM (aż do 4-6 razy!).
 * Format DDS pozwala również na wbudowanie gotowych poziomów mipmap bezpośrednio w plik.
 */
[[nodiscard]] TextureUploadData loadDdsTexture(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("nie można otworzyć tekstury DDS: " + path);
    }

    std::array<char, 4> magic{};
    std::array<char, 124> header{};
    
    // Sygnatura pliku DDS to zawsze 4 znaki "DDS "
    file.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    file.read(header.data(), static_cast<std::streamsize>(header.size()));
    if (!file || std::string(magic.data(), magic.size()) != "DDS ") {
        throw std::runtime_error("to nie jest plik DDS (zła sygnatura): " + path);
    }

    const std::uint32_t headerSize = readLittleEndianUInt32({header[0], header[1], header[2], header[3]});
    const std::uint32_t height = readLittleEndianUInt32({header[8], header[9], header[10], header[11]});
    const std::uint32_t width = readLittleEndianUInt32({header[12], header[13], header[14], header[15]});
    const std::uint32_t mipMapCount = readLittleEndianUInt32({header[24], header[25], header[26], header[27]});
    const std::uint32_t pixelFormatSize = readLittleEndianUInt32({header[72], header[73], header[74], header[75]});
    const std::uint32_t fourCC = readLittleEndianUInt32({header[80], header[81], header[82], header[83]});

    if (headerSize != 124 || pixelFormatSize != 32) {
        throw std::runtime_error("nieobsługiwany nagłówek DDS: " + path);
    }

    TextureUploadData texture;
    texture.width = width;
    texture.height = height;
    texture.mipLevels = std::max(1u, mipMapCount);

    std::size_t blockSize = 0; // Rozmiar pojedynczego skompresowanego bloku pikseli 4x4 (w bajtach)
    char fourCCStr[5] = {};
    fourCCStr[0] = char(fourCC & 0xFF);
    fourCCStr[1] = char((fourCC >> 8) & 0xFF);
    fourCCStr[2] = char((fourCC >> 16) & 0xFF);
    fourCCStr[3] = char((fourCC >> 24) & 0xFF);
    const std::uint32_t ddpfFlags = readLittleEndianUInt32({header[76], header[77], header[78], header[79]});
    LOG_INFO("[DDS] {} fourCC={} flags=0x{:x} {}", path, fourCCStr, ddpfFlags, (ddpfFlags & 1 ? " ALPHA" : ""));

    // Mapowanie formatów FourCC na natywne formaty Vulkan Block Compression (BC)
    if (fourCC == makeFourCC('D', 'X', 'T', '1')) {
        // DXT1 / BC1: Wspiera kolory RGB i opcjonalnie 1-bitowy kanał alfa.
        // Blok 4x4 pikseli jest kodowany na 8 bajtach (kompresja 8:1).
        texture.format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        blockSize = 8;
    } else if (fourCC == makeFourCC('D', 'X', 'T', '3')) {
        // DXT3 / BC2: Dodaje jawny, nieskompresowany 4-bitowy kanał alfa na piksel.
        // Blok 4x4 zajmuje 16 bajtów (kompresja 4:1). Przydatny dla ostrych przejść alfy.
        texture.format = VK_FORMAT_BC2_UNORM_BLOCK;
        blockSize = 16;
    } else if (fourCC == makeFourCC('D', 'X', 'T', '5')) {
        // DXT5 / BC3: Koduje kanał alfa za pomocą interpolacji gradientu (podobnie jak kolor).
        // Blok 4x4 zajmuje 16 bajtów (kompresja 4:1). Zapewnia płynne przejścia alfy.
        texture.format = VK_FORMAT_BC3_UNORM_BLOCK;
        blockSize = 16;
    } else {
        throw std::runtime_error("nieobsługiwana kompresja DDS: " + path);
    }

    // Wczytanie całości skompresowanych danych pikseli (pomijając 128 bajtów sygnatury i nagłówka)
    file.seekg(0, std::ios::end);
    const std::streamoff endPosition = file.tellg();
    file.seekg(128, std::ios::beg);
    const std::size_t dataSize = static_cast<std::size_t>(endPosition - static_cast<std::streamoff>(128));
    texture.pixelBytes.resize(dataSize);
    file.read(reinterpret_cast<char*>(texture.pixelBytes.data()), static_cast<std::streamsize>(dataSize));
    if (!file) {
        throw std::runtime_error("nie udało się odczytać pikseli DDS: " + path);
    }

    if (texture.mipLevels <= 1) {
        // Plik bez mipmap: dekompresja do RGBA8 na CPU (mipmapy wygeneruje GPU)
        std::vector<std::uint8_t> decompressedPixels(width * height * 4);
        
        uint32_t numBlocksX = (width + 3u) / 4u;
        uint32_t numBlocksY = (height + 3u) / 4u;
        
        for (uint32_t by = 0; by < numBlocksY; ++by) {
            for (uint32_t bx = 0; bx < numBlocksX; ++bx) {
                std::size_t fileOffset = (by * numBlocksX + bx) * blockSize;
                if (fileOffset + blockSize > texture.pixelBytes.size()) {
                    throw std::runtime_error("plik DDS jest ucięty (dekompresja): " + path);
                }
                const std::uint8_t* blockData = &texture.pixelBytes[fileOffset];
                
                // Blok 4x4: najpierw kolory (wspólny format BC1 dla DXT1/3/5), potem alfa zależna od formatu.
                std::uint8_t pixels[16][4];
                if (fourCC == makeFourCC('D', 'X', 'T', '1')) {
                    decodeBc1ColorBlock(blockData, true, pixels);
                } else if (fourCC == makeFourCC('D', 'X', 'T', '3')) {
                    decodeBc1ColorBlock(blockData + 8, false, pixels);
                    // DXT3: jawna alfa 4-bitowa dla każdego piksela (8 bajtów przed kolorem).
                    for (int i = 0; i < 16; ++i) {
                        pixels[i][3] = static_cast<std::uint8_t>(((blockData[i / 2] >> (4 * (i % 2))) & 15) * 17);
                    }
                } else {
                    decodeBc1ColorBlock(blockData + 8, false, pixels);
                    decodeBc4AlphaBlock(blockData, pixels);   // DXT5: alfa interpolowana jak kolor
                }

                for (int yLocal = 0; yLocal < 4; ++yLocal) {
                    for (int xLocal = 0; xLocal < 4; ++xLocal) {
                        const std::uint32_t px = bx * 4 + xLocal;
                        const std::uint32_t py = by * 4 + yLocal;
                        if (px < width && py < height) {
                            std::memcpy(&decompressedPixels[(static_cast<std::size_t>(py) * width + px) * 4],
                                        pixels[yLocal * 4 + xLocal], 4);
                        }
                    }
                }
            }
        }
        
        texture.pixelBytes = std::move(decompressedPixels);
        texture.format = VK_FORMAT_R8G8B8A8_UNORM;
        texture.mipLevels = 1;
        
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {width, height, 1};
        texture.copyRegions.push_back(region);
        
        return texture;
    }

    std::size_t offset = 0;
    uint32_t mipWidth = width;
    uint32_t mipHeight = height;
    
    // Ponieważ DDS przechowuje hierarchię mipmap po kolei w pliku, musimy 
    // dla każdego poziomu mipmapy zdefiniować strukturę regionu kopiowania dla Vulkana.
    for (uint32_t mipLevel = 0; mipLevel < texture.mipLevels; ++mipLevel) {
        // Liczba bloków w poziomie (obraz o rozmiarze np. 1x1 i tak musi zająć min. 1 pełny blok 4x4)
        const uint32_t blockWidth = std::max(1u, (mipWidth + 3u) / 4u);
        const uint32_t blockHeight = std::max(1u, (mipHeight + 3u) / 4u);
        const std::size_t mipSize = static_cast<std::size_t>(blockWidth) * blockHeight * blockSize;
        
        if (offset + mipSize > texture.pixelBytes.size()) {
            throw std::runtime_error("plik DDS jest ucięty (brak danych mipmap): " + path);
        }

        // Definiowanie regionu transferu z bufora staging na obraz
        VkBufferImageCopy region{};
        region.bufferOffset = offset; // Przesunięcie w bajtach od początku danych dla tej konkretnej mipmapy
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = mipLevel;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {mipWidth, mipHeight, 1}; // Rozmiar tego poziomu mipmapy
        texture.copyRegions.push_back(region);

        offset += mipSize;
        
        // Zmniejszenie rozmiarów mipmapy o połowę dla kolejnego przebiegu pętli
        mipWidth = std::max(1u, mipWidth / 2u);
        mipHeight = std::max(1u, mipHeight / 2u);
    }

    return texture;
}

/**
 * @brief Pobiera rozszerzenie pliku i konwertuje je na małe litery, aby unikać problemów z wielkością liter.
 */
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
    throw std::runtime_error("nieobsługiwane rozszerzenie tekstury: " + path);
}

TextureUploadData TextureAssetLoader::fromRgbaPixels(
    uint32_t width,
    uint32_t height,
    std::vector<std::uint8_t> rgbaPixels
) {
    TextureUploadData texture;
    texture.width = width;
    texture.height = height;
    texture.format = VK_FORMAT_R8G8B8A8_UNORM; // Domyślny format nieskompresowany RGBA8
    texture.pixelBytes = std::move(rgbaPixels);

    // Cała tekstura kopiowana jest z offsetu 0 w jednym kroku
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