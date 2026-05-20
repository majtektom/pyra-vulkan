#include "engine/Engine.hpp"

#include <cmath>
#include <string>
#include <utility>

int main() {
    // Przyklad tworzy kilka kostek, aby pokazac jednoczesna obsluge wielu tekstur i obiektow sceny.
    engine3d::AppConfig config;
    config.windowTitle = "engine3d: rotating cube";

    // Shader programu definiuje sposob transformacji wierzcholkow i probkowania tekstur na kostce.
    config.shaderManifest.entries = {
        {
            "default",
            {
                std::string(EXAMPLE_SHADER_DIR) + "/cube.vert.spv",
                std::string(EXAMPLE_SHADER_DIR) + "/cube.frag.spv",
                "vertexMain",
                "fragmentMain",
            },
        },
    };

    // Kazda nazwa logiczna wskazuje inny plik tekstury, dzieki czemu obiekty moga wybierac asset po nazwie.
    config.textures = {
        {"bmp_metal", "assets/textures/metal.bmp"},
        {"tga_cactus", "assets/sprite/cactus.tga"},
        {"dds_metal", "assets/textures/drewno.dds"},
    };

#if ENGINE3D_HAS_FREETYPE
    // Font TTF i kilka etykiet 2D pokazuja, jak nakladka tekstowa moze opisywac scene ponad renderem 3D.
    config.fonts = {
        {"ui", "assets/fonts/times.ttf", 40},
    };

    config.textLabels = {
        {"ui", "engine3d: rotating cube", 24.0f, 18.0f, 1.0f, {1.0f, 0.95f, 0.8f, 1.0f}},
        {"ui", "BMP | TGA | DDS", 24.0f, 62.0f, 0.75f, {0.7f, 0.9f, 1.0f, 0.92f}},
        {"ui", "FPS: --", static_cast<float>(config.width) - 24.0f, 18.0f, 0.75f, {0.95f, 1.0f, 0.85f, 0.95f}, engine3d::TextHorizontalAlign::Right},
    };

    // Callback moze zmieniac tresc etykiet w runtime; gdy pojawia sie nowe znaki, atlas fontu przebuduje sie automatycznie.
    config.updateTextLabels = [](float elapsedSeconds, uint32_t viewportWidth, uint32_t, std::vector<engine3d::TextLabel>& labels) {
        if (labels.size() < 3) {
            return;
        }

        const bool showPolish = (static_cast<int>(elapsedSeconds) / 3) % 2 == 1;
        labels[1].text = showPolish ? "Zażółć gęślą jaźń" : "BMP | TGA | DDS";

        static float previousElapsedSeconds = elapsedSeconds;
        const float deltaSeconds = elapsedSeconds - previousElapsedSeconds;
        previousElapsedSeconds = elapsedSeconds;

        if (deltaSeconds > 0.0f) {
            const int fps = static_cast<int>(std::lround(1.0f / deltaSeconds));
            labels[2].text = "FPS: " + std::to_string(fps);
        }

        labels[2].x = static_cast<float>(viewportWidth) - 24.0f;
    };
#endif

    // Kamera jest bardziej oddalona niz w trojkacie, bo scena zawiera kilka kostek obok siebie.
    config.camera.position = {0.0f, 0.0f, 5.5f};

    // Jedna siatka kostki jest wspoldzielona przez wszystkie obiekty sceny.
    config.meshes = {
        {
            "cube",
            {
                {
                    {{{-0.75f, -0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
                    {{{0.75f, -0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
                    {{{0.75f, 0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
                    {{{-0.75f, 0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},

                    {{{0.75f, -0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
                    {{{-0.75f, -0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
                    {{{-0.75f, 0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
                    {{{0.75f, 0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},

                    {{{-0.75f, -0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
                    {{{-0.75f, -0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
                    {{{-0.75f, 0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
                    {{{-0.75f, 0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},

                    {{{0.75f, -0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
                    {{{0.75f, -0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
                    {{{0.75f, 0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
                    {{{0.75f, 0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},

                    {{{-0.75f, 0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
                    {{{0.75f, 0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
                    {{{0.75f, 0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
                    {{{-0.75f, 0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},

                    {{{-0.75f, -0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
                    {{{0.75f, -0.75f, -0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
                    {{{0.75f, -0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
                    {{{-0.75f, -0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
                },
                {
                    0, 1, 2, 2, 3, 0,
                    4, 5, 6, 6, 7, 4,
                    8, 9, 10, 10, 11, 8,
                    12, 13, 14, 14, 15, 12,
                    16, 17, 18, 18, 19, 16,
                    20, 21, 22, 22, 23, 20,
                },
            },
        },
    };

    // Scene objects korzystaja z tej samej geometrii, ale kazdy wybiera inna teksture i inny ruch.
    config.sceneObjects = {
        engine3d::SceneObject{
            "cube",
            "bmp_metal",
            "",
            engine3d::Transform{{-2.2f, 0.0f, 0.0f}, {}, {0.8f, 0.8f, 0.8f}},
            {0.65f, 1.0f, 0.0f},
        },
        engine3d::SceneObject{
            "cube",
            "tga_cactus",
            "",
            engine3d::Transform{{0.0f, 0.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}},
            {0.0f, -1.2f, 0.4f},
        },
        engine3d::SceneObject{
            "cube",
            "dds_metal",
            "",
            engine3d::Transform{{2.2f, 0.0f, 0.0f}, {}, {0.8f, 0.8f, 0.8f}},
            {0.3f, 0.9f, -0.5f},
        },
    };

    // Uruchamiamy silnik z gotowa scena demonstracyjna.
    return engine3d::runEngine(std::move(config));
}
