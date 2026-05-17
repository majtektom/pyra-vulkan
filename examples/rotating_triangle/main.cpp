#include "engine/Engine.hpp"

#include <string>
#include <utility>

int main() {
    // Tworzymy konfiguracje minimalnej sceny edukacyjnej: jeden shader, jedna siatka i jeden obiekt.
    engine3d::AppConfig config;
    config.windowTitle = "engine3d: rotating triangle";

    // Manifest shaderow mowi silnikowi, z jakich plikow SPIR-V ma korzystac podczas tworzenia pipeline'u.
    config.shaderManifest.entries = {
        {
            "default",
            {
                std::string(EXAMPLE_SHADER_DIR) + "/triangle.vert.spv",
                std::string(EXAMPLE_SHADER_DIR) + "/triangle.frag.spv",
                "vertexMain",
                "fragmentMain",
            },
        },
    };

    // Kamera jest ustawiona blisko sceny, bo trojkat lezy w plaszczyznie z=0.
    config.camera.position = {0.0f, 0.0f, 2.4f};

    // Definiujemy jedna siatke trojkata wraz z pozycjami, kolorami i wspolrzednymi UV.
    config.meshes = {
        {
            "triangle",
            {
                {
                    {{{0.0f, -0.65f, 0.0f}}, {{1.0f, 0.25f, 0.25f}}, {{0.5f, 0.0f}}},
                    {{{0.65f, 0.45f, 0.0f}}, {{0.2f, 1.0f, 0.35f}}, {{1.0f, 1.0f}}},
                    {{{-0.65f, 0.45f, 0.0f}}, {{0.2f, 0.45f, 1.0f}}, {{0.0f, 1.0f}}},
                },
                {0, 1, 2},
            },
        },
    };

    // W scenie umieszczamy jeden obiekt korzystajacy z tej siatki i obracajacy sie wokol osi Z.
    config.sceneObjects = {
        {
            "triangle",
            {},
            {0.0f, 0.0f, 1.1f},
        },
    };

    // Uruchamiamy silnik z przygotowana konfiguracja.
    return engine3d::runEngine(std::move(config));
}
