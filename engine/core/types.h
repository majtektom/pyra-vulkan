#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "core/math.h"
#include "core/asset_registry.h"

namespace engine3d {

class SceneManager;

// Stabilny identyfikator encji wykorzystywany przez Scene i systemy ECS.
using EntityId = uint64_t;

/**
 * @brief Struktura reprezentująca pojedynczy wierzchołek w przestrzeni 3D (Vertex).
 *
 * Każdy punkt siatki 3D zawiera:
 * - position: współrzędne 3D (X, Y, Z) określające położenie w przestrzeni.
 * - normal: wektor normalny (kierunek prostopadły do powierzchni), używany do obliczeń oświetlenia dynamicznego (np. PBR).
 * - color: kolor przypisany bezpośrednio do wierzchołka.
 * - texCoord: współrzędne mapowania tekstury (U, V) z zakresu [0.0, 1.0].
 *
 * Zawiera również statyczne funkcje zwracające opisy formatu danych dla interfejsu Vulkan:
 * - bindingDescription: definiuje krok (stride) i szybkość pobierania danych (na wierzchołek).
 * - attributeDescriptions: definiuje układy i przesunięcia w pamięci (offset) dla poszczególnych zmiennych.
 */
struct Vertex {
    std::array<float, 3> position;
    std::array<float, 3> normal;
    std::array<float, 3> color;
    std::array<float, 2> texCoord;

    // Definiuje połączenie bufora wierzchołków z potokiem graficznym (Pipeline).
    [[nodiscard]] static VkVertexInputBindingDescription bindingDescription() {
        VkVertexInputBindingDescription description{};
        description.binding = 0; // Identyfikator bindingu bufora.
        description.stride = sizeof(Vertex); // Rozmiar pojedynczego elementu w bajtach.
        description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX; // Dane są czytane dla każdego wierzchołka po kolei.
        return description;
    }

    // Informuje Vulkana o formacie i przesunięciu poszczególnych atrybutów w pamięci struktury Vertex.
    [[nodiscard]] static std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 4> descriptions{};
        
        // Atrybut 0: Pozycja wierzchołka (location = 0 w shaderze vertex)
        descriptions[0].binding = 0;
        descriptions[0].location = 0;
        descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT; // Typ: vec3 (3 floaty)
        descriptions[0].offset = offsetof(Vertex, position);

        // Atrybut 1: Wektor normalny (location = 1)
        descriptions[1].binding = 0;
        descriptions[1].location = 1;
        descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT; // Typ: vec3 (3 floaty)
        descriptions[1].offset = offsetof(Vertex, normal);

        // Atrybut 2: Kolor wierzchołka (location = 2)
        descriptions[2].binding = 0;
        descriptions[2].location = 2;
        descriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT; // Typ: vec3 (3 floaty)
        descriptions[2].offset = offsetof(Vertex, color);

        // Atrybut 3: Współrzędne tekstury UV (location = 3)
        descriptions[3].binding = 0;
        descriptions[3].location = 3;
        descriptions[3].format = VK_FORMAT_R32G32_SFLOAT; // Typ: vec2 (2 floaty)
        descriptions[3].offset = offsetof(Vertex, texCoord);

        return descriptions;
    }
};

/**
 * @brief Kompletne dane siatki geometrycznej (Mesh) przechowywane w pamięci CPU przed wysłaniem na GPU.
 */
struct MeshData {
    std::vector<Vertex> vertices;  // Lista wierzchołków.
    std::vector<uint32_t> indices; // Lista indeksów łączących wierzchołki w trójkąty.
};

/**
 * @brief Dane instancji pojedynczej cząsteczki dla systemu cząsteczkowego (GPU Instancing).
 */
struct ParticleInstanceData {
    float posX, posY, posZ, size; // Pozycja 3D i rozmiar cząsteczki na ekranie.
    float r, g, b, a;             // Kolor (RGBA).
};

/**
 * @brief Dane instancji sprajtu dla szybkiego renderowania 2D.
 */
struct SpriteInstanceData {
    float posX, posY, posZ, size;
    float r, g, b, a;
    float uvOffsetU, uvOffsetV, uvScaleU, uvScaleV; // Parametry wycinania klatki z atlasu tekstur.
};

// Definicja programu shaderowego (ścieżki do plików SPIR-V lub kodu źródłowego).
struct ShaderProgram {
    std::string vertexPath;
    std::string fragmentPath;
    std::string vertexEntryPoint = "main";
    std::string fragmentEntryPoint = "main";
};

// Wpis w manifeście shaderów.
struct ShaderManifestEntry {
    std::string name;
    ShaderProgram program;
};

// Rejestr wszystkich shaderów wykorzystywanych przez silnik.
struct ShaderManifest {
    std::vector<ShaderManifestEntry> entries;
};

// Parametry pomocnicze prędkości rotacji.
struct RotationSpeed {
    float x = 0.0f;
    float y = 1.0f;
    float z = 0.0f;
};

// Lokalna transformacja obiektu 3D w przestrzeni (TRS: Translation, Rotation, Scale).
struct Transform {
    Vec3 translation{};
    Vec3 rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

// Struktury reprezentujące zasoby ładowane przez menedżery zasobów (Asset Registry):
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

// Wyrównanie tekstu na ekranie.
enum class TextHorizontalAlign {
    Left,
    Center,
    Right,
};

// Etykieta tekstowa do renderowania w nakładce 2D (GUI / HUD).
struct TextLabel {
    std::string fontName;
    std::string text;
    float x = 16.0f;
    float y = 32.0f;
    float scale = 1.0f;
    std::array<float, 4> color = {1.0f, 1.0f, 1.0f, 1.0f};
    TextHorizontalAlign horizontalAlign = TextHorizontalAlign::Left;
};

// Filtrowanie tekstury na GPU (Nearest - pikselowy styl retro, Linear - wygładzanie, Anisotropic - najlepsza jakość).
enum class TextureFilter {
    Nearest,
    Linear,
    Trilinear,
    Anisotropic2x,
    Anisotropic4x,
    Anisotropic8x,
    Anisotropic16x,
};

// Filtrowanie mipmap (redukcja migotania odległych tekstur).
enum class MipmapMode {
    Nearest,
    Linear,
};

// Sposób zawijania współrzędnych tekstury UV poza zakresem [0, 1].
enum class AddressMode {
    Repeat,         // Powtarzanie (np. kafelkowanie trawy).
    MirroredRepeat, // Powtarzanie z lustrzanym odbiciem.
    ClampToEdge,    // Rozciągnięcie krawędzi (np. dla interfejsu użytkownika).
};

// Tryb synchronizacji GPU-CPU.
enum class SynchronizationMode {
    Standard,
    Timeline, // Wykorzystuje Timeline Semaphores z nowszego standardu Vulkan.
    Off,
};

// Tryb prezentacji klatek (Swapchain Present Mode).
enum class PresentMode {
    Fifo,      // V-Sync włączony (blokada klatek do odświeżania monitora, brak screen tearing).
    Immediate, // V-Sync wyłączony (natychmiastowa prezentacja, możliwy screen tearing).
    Mailbox,   // Potrójne buforowanie (brak screen tearing, najniższe opóźnienia, brak blokady klatek).
};

// Tryby mieszania kolorów (Blending) przydatne dla obiektów przezroczystych.
enum class BlendMode {
    Disabled, // Nadpisywanie koloru (obiekty nieprzezroczyste).
    Alpha,    // Przezroczystość standardowa (mieszanie na podstawie kanału Alpha tekstury).
    Additive, // Mieszanie dodające (np. dla efektów ognia, cząsteczek, laserów).
};

// Cull Mode: Odrzucanie niewidocznych ścian trójkątów (Back-face Culling) dla oszczędności GPU.
enum class CullMode {
    Disabled,
    Front,
    Back, // Standardowo odrzucamy tylne ściany (te, które są skierowane tyłem do kamery).
    FrontAndBack,
};

/**
 * @brief Struktura opisująca parametry wizualne powierzchni (Materiał).
 *
 * Materiał określa jak obiekt reaguje na oświetlenie (PBR - Physically Based Rendering).
 * Silnik obsługuje zarówno klasyczne kolory cieniowania (ambient, diffuse, specular),
 * jak i nowoczesny przepływ pracy Metallic-Roughness.
 */
struct Material {
    std::string name;
    TextureHandle textureHandle{};
    TextureHandle normalMapHandle{};
    std::string shaderProgram;
    TextureFilter minFilter = TextureFilter::Linear;
    TextureFilter magFilter = TextureFilter::Linear;
    MipmapMode mipmapMode = MipmapMode::Linear;
    AddressMode addressModeU = AddressMode::Repeat;
    AddressMode addressModeV = AddressMode::Repeat;
    BlendMode blendMode = BlendMode::Disabled;
    bool depthTest = true;     // Czy sprawdzać głębokość piksela (czy obiekt jest zasłonięty).
    bool depthWrite = true;    // Czy zapisywać głębokość piksela do Depth Buffer.
    bool colorWrite = true;    // Czy zapisywać kolor (false = potok "tylko głębokość").
    // Tylko dla przezroczystych (blendMode != Disabled): najpierw zapis samej głębokości obiektu, potem kolor.
    // Widać wtedy tylko najbliższą powierzchnię - złożony model (podgląd budowy, hologram) nie pokazuje
    // swoich tylnych i wewnętrznych ścian nałożonych warstwami. Koszt: drugi draw obiektu.
    bool depthPrepass = false;
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
    int renderPriority = 0; // Wyższy priorytet oznacza rysowanie obiektu później (np. obiekty przezroczyste).

    // Parametry standardu PBR (Physically Based Rendering):
    float metallicFactor = 0.0f;              // Stopień metaliczności powierzchni (0.0 - dielektryk, 1.0 - czysty metal).
    float roughnessFactor = 1.0f;             // Stopień szorstkości (0.0 - idealne lustro, 1.0 - mat).
    TextureHandle metallicRoughnessTextureHandle{}; // Mapa metaliczności i szorstkości.
    TextureHandle occlusionTextureHandle{};         // Mapa okluzji otoczenia (cienie w zakamarkach).
    TextureHandle emissiveTextureHandle{};          // Mapa emisji światła.
};

// Konfiguracja kamery (perspektywy patrzenia na świat).
struct CameraConfig {
    Vec3 position{0.0f, 0.0f, 4.0f};
    Vec3 target{0.0f, 0.0f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    float fieldOfViewDegrees = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 10.0f;
};

// Stan urządzeń wejściowych (Klawiatura i Mysz).
struct InputState {
    bool keys[256] = {};
    int mouseDeltaX = 0;
    int mouseDeltaY = 0;
    bool mouseButtons[5] = {};
    int mouseX = 0;
    int mouseY = 0;
};

// Globalne dane przesyłane do shaderów (Uniform Buffer Object).
struct GlobalUniformBufferObject {
    Matrix4 view; // Macierz widoku kamery.
    Matrix4 proj; // Macierz rzutowania perspektywicznego.
};

// Konfiguracja siatki pomocniczej (Grid Renderer) w edytorze/scenie.
struct GridConfig {
    bool enabled = false;
    float size = 200.0f;
    float spacing = 10.0f;
    std::array<float, 3> color = {0.5f, 0.5f, 0.5f};
};

// Konfiguracja Skyboxa (tekstura otoczenia / sześcienna mapa nieba).
struct SkyboxConfig {
    bool enabled = false;
    std::string directory;
    float size = 3500.0f;
    float verticalOffset = 100.0f;
};

// Konfiguracja Skydome (dynamiczna kopuła nieba z chmurami).
struct SkydomeConfig {
    bool enabled = false;
    float domeScale = 1.0f;
    float timeOfDay = 12.0f;
    float cloudCoverage = 0.3f;
    float sunAzimuth = 0.0f;
    float sunElevation = 45.0f;
    float cloudSpeed0 = 1.0f;
    float cloudSpeed1 = 0.5f;
    float cloudScale0 = 800.0f;
    float cloudScale1 = 400.0f;
    bool enableSunLight = false;
};

// Konfiguracja renderingu powierzchni wody (Ocean).
struct OceanConfig {
    bool enabled = false;
    float waterLevel = -8.0f;
    float waveAmplitude = 1.0f;
    float waveSpeed = 1.0f;
    float waveFrequency = 1.0f;
    float foamThreshold = 0.5f;
};

enum class SceneLightType {
    Directional,
    Point,
    Spot,
};

/**
 * @brief Uproszczona struktura opisująca światło, przekazywana bezpośrednio do shaderów GPU.
 */
struct SceneLight {
    SceneLightType type = SceneLightType::Point;
    Vec3 position{};
    Vec3 direction{0.0f, -1.0f, 0.0f};
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 30.0f;
    float innerAngleDeg = 20.0f;
    float outerAngleDeg = 30.0f;
};

}  // namespace engine3d
