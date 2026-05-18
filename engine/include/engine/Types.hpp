#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "engine/Math.hpp"

namespace engine3d {

// Pojedynczy wierzcholek przekazywany do GPU.
struct Vertex {
    std::array<float, 3> position;
    std::array<float, 3> color;
    std::array<float, 2> texCoord;

    // Opisuje, jak duzy jest pojedynczy rekord w buforze wierzcholkow.
    [[nodiscard]] static VkVertexInputBindingDescription bindingDescription() {
        VkVertexInputBindingDescription description{};
        description.binding = 0;
        description.stride = sizeof(Vertex);
        description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return description;
    }

    // Opisuje pola struktury Vertex jako atrybuty wejscia shaderow.
    [[nodiscard]] static std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 3> descriptions{};

        // location 0 w shaderze odpowiada pozycji 3D.
        descriptions[0].binding = 0;
        descriptions[0].location = 0;
        descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        descriptions[0].offset = offsetof(Vertex, position);

        // location 1 niesie kolor wierzcholka.
        descriptions[1].binding = 0;
        descriptions[1].location = 1;
        descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        descriptions[1].offset = offsetof(Vertex, color);

        // location 2 niesie wspolrzedne UV dla probkowania tekstury.
        descriptions[2].binding = 0;
        descriptions[2].location = 2;
        descriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        descriptions[2].offset = offsetof(Vertex, texCoord);

        return descriptions;
    }
};

// Surowe dane siatki po stronie CPU.
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
};

// Zestaw plikow shaderow tworzacy jeden program renderujacy.
struct ShaderProgram {
    std::string vertexPath;
    std::string fragmentPath;
    std::string vertexEntryPoint = "main";
    std::string fragmentEntryPoint = "main";
};

// Jeden wpis manifestu shaderow mapujacy nazwe logiczna na konkretne pliki.
struct ShaderManifestEntry {
    std::string name;
    ShaderProgram program;
};

// Kolekcja wszystkich programow shaderowych dostepnych dla sceny.
struct ShaderManifest {
    std::vector<ShaderManifestEntry> entries;
};

// Predkosc obrotu obiektu wokol kazdej osi.
struct RotationSpeed {
    float x = 0.0f;
    float y = 1.0f;
    float z = 0.0f;
};

// Transformacja obiektu w scenie: polozenie, obrot i skala.
struct Transform {
    Vec3 translation{};
    Vec3 rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

// Nazwany asset siatki, aby obiekty sceny mogly odwolac sie do geometrii przez identyfikator.
struct MeshAsset {
    std::string name;
    MeshData mesh;
};

// Nazwany asset tekstury wskazujacy plik na dysku.
struct TextureAsset {
    std::string name;
    std::string path;
    bool mirroredRepeat = false;
};

// Nazwany asset fontu TrueType wskazujacy plik TTF oraz rozmiar rasteryzacji glifow.
struct FontAsset {
    std::string name;
    std::string path;
    uint32_t pixelHeight = 48;
    std::string glyphsUtf8;
};

// Poziome wyrownanie etykiety tekstowej wzgledem punktu zakotwiczenia X.
enum class TextHorizontalAlign {
    Left,
    Center,
    Right,
};

// Etykieta 2D rysowana na ekranie jako nakladka ponad scena 3D.
struct TextLabel {
    std::string fontName;
    std::string text;
    float x = 16.0f;
    float y = 32.0f;
    float scale = 1.0f;
    std::array<float, 4> color = {1.0f, 1.0f, 1.0f, 1.0f};
    TextHorizontalAlign horizontalAlign = TextHorizontalAlign::Left;
};

// Jeden obiekt sceny laczacy siatke, teksture i animacje transformacji.
struct SceneObject {
    std::string meshName;
    std::string textureName;
    Transform transform{};
    RotationSpeed rotation{};
};

// Podstawowe parametry kamery perspektywicznej.
struct CameraConfig {
    Vec3 position{0.0f, 0.0f, 4.0f};
    Vec3 target{0.0f, 0.0f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    float fieldOfViewDegrees = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 10.0f;
};

// Dane globalne wspoldzielone przez wszystkie obiekty w danej klatce.
struct GlobalUniformBufferObject {
    Matrix4 view;
    Matrix4 proj;
};

struct InputState {
    bool keys[256] = {};
    int mouseDeltaX = 0;
    int mouseDeltaY = 0;
    bool mouseButtons[5] = {};
    int mouseX = 0;
    int mouseY = 0;
};

struct GridConfig {
    bool enabled = false;
    float size = 200.0f;
    float spacing = 10.0f;
    std::array<float, 3> color = {0.5f, 0.5f, 0.5f};
};

struct SkyboxConfig {
    bool enabled = false;
    std::string directory;
    float size = 3500.0f;
    float verticalOffset = 100.0f;
};

// Glowna konfiguracja silnika przekazywana do uruchomienia sceny.
struct AppConfig {
    // Parametry okna i koloru tla.
    std::string windowTitle = "engine3d app";
    uint32_t width = 1024;
    uint32_t height = 768;
    std::array<float, 4> clearColor = {0.06f, 0.08f, 0.12f, 1.0f};
    // Definicje shaderow i wybor aktywnego programu.
    ShaderManifest shaderManifest;
    std::string activeShaderProgram = "default";
    // Zasoby sceny: geometria, tekstury i obiekty wykorzystujace te zasoby.
    std::vector<MeshAsset> meshes;
    std::vector<TextureAsset> textures;
    std::vector<FontAsset> fonts;
    std::string activeTexture;
    std::vector<SceneObject> sceneObjects;
    std::vector<TextLabel> textLabels;
    std::function<void(float, uint32_t, uint32_t, std::vector<TextLabel>&)> updateTextLabels;
    // Kamera globalna sterujaca punktem widzenia calej sceny.
    CameraConfig camera;
    InputState input;
    GridConfig grid;
    SkyboxConfig skybox;
    std::function<void(float, AppConfig&)> onUpdate;
};

}  // namespace engine3d
