#include <SDL3/SDL.h>

#include "core/engine.h"
#include "core/entity.h"
#include "render/renderer.h"
#include "core/mesh_component.h"
#include "camera/camera_component.h"

using namespace engine3d;

int main() {
    // Tworzymy silnik gry.
    Engine engine;

    // Inicjalizujemy silnik w wyższej rozdzielczości (1024x768).
    if (!engine.initialize("engine3d: rotating cube", 1024, 768)) {
        return 1;
    }

    // Pobieramy aktywną scenę.
    auto* scene = engine.getSceneManager()->activeScene();
    if (!scene) {
        return 1;
    }

    // 1. Definiujemy wierzchołki trójwymiarowego sześcianu (Cube).
    // Sześcian ma 6 ścian. Każda ściana ma 4 wierzchołki. Razem definiujemy 24 wierzchołki.
    // Definiujemy pozycje wierzchołków, wektory normalne (zawsze prostopadłe do powierzchni),
    // kolory wierzchołków (białe) oraz współrzędne UV (dla teksturowania).
    MeshData cubeMesh;
    cubeMesh.vertices = {
        // Ściana przednia (Z = +0.75)
        {{{-0.75f, -0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
        {{{0.75f, -0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
        {{{0.75f, 0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
        {{{-0.75f, 0.75f, 0.75f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
        // Ściana tylna (Z = -0.75)
        {{{0.75f, -0.75f, -0.75f}}, {{0.0f, 0.0f, -1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
        {{{-0.75f, -0.75f, -0.75f}}, {{0.0f, 0.0f, -1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
        {{{-0.75f, 0.75f, -0.75f}}, {{0.0f, 0.0f, -1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
        {{{0.75f, 0.75f, -0.75f}}, {{0.0f, 0.0f, -1.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
        // Ściana lewa (X = -0.75)
        {{{-0.75f, -0.75f, -0.75f}}, {{-1.0f, 0.0f, 0.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
        {{{-0.75f, -0.75f, 0.75f}}, {{-1.0f, 0.0f, 0.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
        {{{-0.75f, 0.75f, 0.75f}}, {{-1.0f, 0.0f, 0.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
        {{{-0.75f, 0.75f, -0.75f}}, {{-1.0f, 0.0f, 0.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
        // Ściana prawa (X = +0.75)
        {{{0.75f, -0.75f, 0.75f}}, {{1.0f, 0.0f, 0.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
        {{{0.75f, -0.75f, -0.75f}}, {{1.0f, 0.0f, 0.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
        {{{0.75f, 0.75f, -0.75f}}, {{1.0f, 0.0f, 0.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
        {{{0.75f, 0.75f, 0.75f}}, {{1.0f, 0.0f, 0.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
        // Ściana górna (Y = +0.75)
        {{{-0.75f, 0.75f, 0.75f}}, {{0.0f, 1.0f, 0.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
        {{{0.75f, 0.75f, 0.75f}}, {{0.0f, 1.0f, 0.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
        {{{0.75f, 0.75f, -0.75f}}, {{0.0f, 1.0f, 0.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
        {{{-0.75f, 0.75f, -0.75f}}, {{0.0f, 1.0f, 0.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
        // Ściana dolna (Y = -0.75)
        {{{-0.75f, -0.75f, -0.75f}}, {{0.0f, -1.0f, 0.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 0.0f}}},
        {{{0.75f, -0.75f, -0.75f}}, {{0.0f, -1.0f, 0.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 0.0f}}},
        {{{0.75f, -0.75f, 0.75f}}, {{0.0f, -1.0f, 0.0f}}, {{1.0f, 1.0f, 1.0f}}, {{1.0f, 1.0f}}},
        {{{-0.75f, -0.75f, 0.75f}}, {{0.0f, -1.0f, 0.0f}}, {{1.0f, 1.0f, 1.0f}}, {{0.0f, 1.0f}}},
    };
    
    // Indeksy definiują, jak z 24 wierzchołków utworzyć 12 trójkątów (2 trójkąty na każdą z 6 ścian).
    cubeMesh.indices = {
        0, 1, 2, 2, 3, 0,       // przód
        4, 5, 6, 6, 7, 4,       // tył
        8, 9, 10, 10, 11, 8,    // lewo
        12, 13, 14, 14, 15, 12, // prawo
        16, 17, 18, 18, 19, 16, // góra
        20, 21, 22, 22, 23, 20  // dół
    };
    
    // Ładujemy siatkę do MeshManagera jako pojedynczy zasób o nazwie "cube".
    engine.getMeshManager()->loadMesh("cube", cubeMesh);

    // Funkcja pomocnicza lambda do ładowania tekstury i rejestrowania materiału.
    auto createMaterial = [&](const std::string& name, const std::string& texName, const std::string& texPath) -> MaterialHandle {
        // Ładowanie pliku tekstury z dysku i przesyłanie do pamięci GPU.
        TextureHandle texHandle = engine.getRenderer()->loadTexture(texName, texPath);
        
        // Tworzenie materiału powiązanego z załadowaną teksturą.
        Material mat;
        mat.name = name;
        mat.textureHandle = texHandle;
        return engine.getMaterialManager()->add(mat);
    };

    // Ładujemy 3 różne tekstury i tworzymy z nich 3 materiały.
    // Silnik obsługuje formaty BMP, TGA oraz zaawansowany DirectDraw Surface (DDS).
    auto mat1 = createMaterial("metal", "metal_tex", "assets/textures/metal.bmp");
    auto mat2 = createMaterial("cactus", "cactus_tex", "assets/sprite/cactus.tga");
    auto mat3 = createMaterial("wood", "wood_tex", "assets/textures/drewno.dds");

    // Funkcja pomocnicza lambda do tworzenia encji sześcianu o zadanych parametrach transformacji.
    auto makeCube = [&](const std::string& name, const Vec3& pos,
                        const Vec3& scale, MaterialHandle materialHandle) {
        auto* e = scene->createEntity(name);
        
        // Każda kostka używa tej samej siatki "cube" z MeshManagera.
        auto* mc = e->addComponent<MeshComponent>("cube");
        
        // Każda kostka ma jednak inny materiał (wygląd) i transformację (pozycja/skala).
        mc->materialHandle = materialHandle;
        e->transform().position = pos;
        e->transform().scale = scale;
    };

    // Wstawiamy 3 kostki obok siebie na scenie.
    makeCube("cube1", {-2.2f, 0.0f, 0.0f}, {0.8f, 0.8f, 0.8f}, mat1); // Metalowa po lewej
    makeCube("cube2", {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, mat2);  // Kaktusowa na środku (nieco większa)
    makeCube("cube3", {2.2f, 0.0f, 0.0f}, {0.8f, 0.8f, 0.8f}, mat3);  // Drewniana po prawej

    // Tworzenie encji kamery i odsunięcie jej na Z = 5.5, aby objąć wzrokiem wszystkie kostki.
    auto* camEntity = scene->createEntity("camera");
    auto* camera = camEntity->addComponent<CameraComponent>();
    camEntity->transform().position = {0.0f, 0.0f, 5.5f};
    camera->fieldOfViewDegrees = 45.0f;
    // Kamera patrzy na środek układu współrzędnych, oś Y w górę.
    camera->target = {0.0f, 0.0f, 0.0f};
    camera->up = {0.0f, 1.0f, 0.0f};
    scene->setActiveCamera(camera);

    // Definiujemy logikę klatki gry.
    engine.onUpdate = [](float dt, Engine& e) {
        // Escape zamyka aplikację (pętla silnika kończy się po tej klatce).
        if (e.keyPressed(SDL_SCANCODE_ESCAPE)) e.requestQuit();

        auto* activeScene = e.getSceneManager()->activeScene();
        
        // Funkcja pomocnicza do rotacji zadanej encji wokół jej lokalnych osi X, Y, Z.
        auto rotate = [&](const std::string& name, float rx, float ry, float rz) {
            auto* entity = activeScene ? activeScene->getEntity(name) : nullptr;
            if (entity) {
                entity->transform().rotation.x += dt * rx;
                entity->transform().rotation.y += dt * ry;
                entity->transform().rotation.z += dt * rz;
            }
        };
        
        // Każdej z kostek nadajemy inne prędkości obrotu w różnych osiach.
        rotate("cube1", 0.65f, 1.0f, 0.0f);
        rotate("cube2", 0.0f, -1.2f, 0.4f);
        rotate("cube3", 0.3f, 0.9f, -0.5f);
    };

    // Uruchamiamy silnik.
    engine.run();
    return 0;
}
