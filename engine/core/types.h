#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "math.h"

namespace engine3d {

class SceneManager;

struct Vertex {
    std::array<float, 3> position;
    std::array<float, 3> normal;
    std::array<float, 3> color;
    std::array<float, 2> texCoord;

    [[nodiscard]] static VkVertexInputBindingDescription bindingDescription() {
        VkVertexInputBindingDescription description{};
        description.binding = 0;
        description.stride = sizeof(Vertex);
        description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return description;
    }

    [[nodiscard]] static std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 4> descriptions{};
        descriptions[0].binding = 0;
        descriptions[0].location = 0;
        descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        descriptions[0].offset = offsetof(Vertex, position);

        descriptions[1].binding = 0;
        descriptions[1].location = 1;
        descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        descriptions[1].offset = offsetof(Vertex, normal);

        descriptions[2].binding = 0;
        descriptions[2].location = 2;
        descriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        descriptions[2].offset = offsetof(Vertex, color);

        descriptions[3].binding = 0;
        descriptions[3].location = 3;
        descriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
        descriptions[3].offset = offsetof(Vertex, texCoord);

        return descriptions;
    }
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

struct ParticleInstanceData {
    float posX, posY, posZ, size;
    float r, g, b, a;
};

struct SpriteInstanceData {
    float posX, posY, posZ, size;
    float r, g, b, a;
    float uvOffsetU, uvOffsetV, uvScaleU, uvScaleV;
};

struct ShaderProgram {
    std::string vertexPath;
    std::string fragmentPath;
    std::string vertexEntryPoint = "main";
    std::string fragmentEntryPoint = "main";
};

struct ShaderManifestEntry {
    std::string name;
    ShaderProgram program;
};

struct ShaderManifest {
    std::vector<ShaderManifestEntry> entries;
};

struct RotationSpeed {
    float x = 0.0f;
    float y = 1.0f;
    float z = 0.0f;
};

struct Transform {
    Vec3 translation{};
    Vec3 rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct MeshAsset {
    std::string name;
    MeshData mesh;
};

struct TextureAsset {
    std::string name;
    std::string path;
    bool mirroredRepeat = false;
};

struct FontAsset {
    std::string name;
    std::string path;
    uint32_t pixelHeight = 48;
    std::string glyphsUtf8;
};

enum class TextHorizontalAlign {
    Left,
    Center,
    Right,
};

struct TextLabel {
    std::string fontName;
    std::string text;
    float x = 16.0f;
    float y = 32.0f;
    float scale = 1.0f;
    std::array<float, 4> color = {1.0f, 1.0f, 1.0f, 1.0f};
    TextHorizontalAlign horizontalAlign = TextHorizontalAlign::Left;
};

enum class TextureFilter {
    Nearest,
    Linear,
};

enum class MipmapMode {
    Nearest,
    Linear,
};

enum class AddressMode {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
};

enum class BlendMode {
    Disabled,
    Alpha,
    Additive,
};

enum class CullMode {
    Disabled,
    Front,
    Back,
    FrontAndBack,
};

struct Material {
    std::string name;
    std::string textureName;
    std::string normalMapName;
    std::string shaderProgram;
    TextureFilter minFilter = TextureFilter::Linear;
    TextureFilter magFilter = TextureFilter::Linear;
    MipmapMode mipmapMode = MipmapMode::Linear;
    AddressMode addressModeU = AddressMode::Repeat;
    AddressMode addressModeV = AddressMode::Repeat;
    BlendMode blendMode = BlendMode::Disabled;
    bool depthTest = true;
    bool depthWrite = true;
    CullMode cullMode = CullMode::Back;
    std::string vertexEntryPoint = "vertexMain";
    std::string fragmentEntryPoint = "fragmentMain";
    std::array<float, 4> color = {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> ambientColor = {0.2f, 0.2f, 0.2f, 1.0f};
    std::array<float, 4> diffuseColor = {0.8f, 0.8f, 0.8f, 1.0f};
    std::array<float, 4> specularColor = {0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 4> emissionColor = {0.0f, 0.0f, 0.0f, 1.0f};
    float shininess = 32.0f;
    float bumpIntensity = 1.0f;
};

struct CameraConfig {
    Vec3 position{0.0f, 0.0f, 4.0f};
    Vec3 target{0.0f, 0.0f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    float fieldOfViewDegrees = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 10.0f;
};

struct InputState {
    bool keys[256] = {};
    int mouseDeltaX = 0;
    int mouseDeltaY = 0;
    bool mouseButtons[5] = {};
    int mouseX = 0;
    int mouseY = 0;
};

struct GlobalUniformBufferObject {
    Matrix4 view;
    Matrix4 proj;
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

}  // namespace engine3d
