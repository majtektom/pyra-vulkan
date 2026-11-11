#include <SDL3/SDL.h>

#include "core/engine.h"
#include "core/entity.h"
#include "render/renderer.h"
#include "core/mesh_component.h"
#include "camera/camera_component.h"

using namespace engine3d;

int main() {
    // 1. Tworzymy główny obiekt silnika. Odpowiada on za zarządzanie cyklem życia aplikacji.
    Engine engine;

    // Inicjalizujemy silnik z tytułem okna oraz rozdzielczością 800x600.
    if (!engine.initialize("engine3d: rotating triangle", 800, 600)) {
        return 1;
    }

    // 2. Pobieramy aktywną scenę z menedżera scen.
    // Scena działa jako kontener (World) dla obiektów (Entity) i systemów (System).
    auto* scene = engine.getSceneManager()->activeScene();
    if (!scene) {
        return 1;
    }

    // 3. Ręczne definiowanie wierzchołków trójkąta (MeshData).
    // Każdy wierzchołek (Vertex) w naszym silniku posiada:
    // - pozycję (position - x, y, z)
    // - normalną (normal - używaną do obliczeń oświetlenia)
    // - kolor (color - podstawowy kolor wierzchołka)
    // - współrzędne tekstury UV (texCoords)
    MeshData triangleMesh;
    triangleMesh.vertices = {
        // Pozycja               Normalna            Kolor (RGB)         UV
        {{{0.0f, -0.5f, 0.0f}}, {{0.0f, 0.0f, 1.0f}}, {{1.0f, 0.0f, 0.0f}}, {{0.5f, 0.0f}}}, // Czerwony wierzchołek na górze
        {{{0.5f, 0.5f, 0.0f}}, {{0.0f, 0.0f, 1.0f}}, {{0.0f, 1.0f, 0.0f}}, {{1.0f, 1.0f}}},  // Zielony wierzchołek na dole po prawej
        {{{-0.5f, 0.5f, 0.0f}}, {{0.0f, 0.0f, 1.0f}}, {{0.0f, 0.0f, 1.0f}}, {{0.0f, 1.0f}}}, // Niebieski wierzchołek na dole po lewej
    };
    // Indeksy definiują kolejność łączenia wierzchołków w trójkąty.
    // W tym przypadku łączymy wierzchołki 0, 1 i 2 w jeden trójkąt.
    triangleMesh.indices = {0, 1, 2};
    
    // Rejestrujemy siatkę w MeshManagerze pod unikalną nazwą "triangle".
    engine.getMeshManager()->loadMesh("triangle", triangleMesh);

    // 4. Tworzenie materiału dla trójkąta.
    // Materiał określa, jak światło reaguje z powierzchnią obiektu.
    Material mat;
    mat.name = "default";
    const auto matHandle = engine.getMaterialManager()->add(mat);

    // 5. Tworzenie encji trójkąta w ECS.
    // Encja to pusty pojemnik na komponenty.
    auto* entity = scene->createEntity("triangle");
    
    // Dodajemy MeshComponent, który informuje silnik, że ta encja ma być renderowana
    // jako model o nazwie "triangle" przy użyciu wskazanego materiału.
    auto* mc = entity->addComponent<MeshComponent>("triangle");
    mc->materialHandle = matHandle;

    // 6. Tworzenie encji kamery.
    // Bez kamery renderer nie wiedziałby, z jakiej perspektywy rysować świat 3D.
    auto* camEntity = scene->createEntity("camera");
    
    // Dodajemy komponent kamery (CameraComponent).
    auto* camera = camEntity->addComponent<CameraComponent>();
    
    // Przesuwamy kamerę wzdłuż osi Z o 2 jednostki w tył, aby widzieć środek sceny.
    camEntity->transform().position = {0.0f, 0.0f, 2.0f};
    
    // Kąt widzenia kamery (Field of View) w stopniach.
    camera->fieldOfViewDegrees = 45.0f;
    
    // Ustawiamy tę kamerę jako aktywną kamerę sceny.
    // Kamera patrzy na środek układu współrzędnych, oś Y w górę.
    camera->target = {0.0f, 0.0f, 0.0f};
    camera->up = {0.0f, 1.0f, 0.0f};
    scene->setActiveCamera(camera);

    // 7. Definiowanie zachowania w każdej klatce (Update Loop).
    // Przekazujemy lambdę, która wywoła się w każdym cyklu głównej pętli.
    engine.onUpdate = [](float dt, Engine& e) {
        // Escape zamyka aplikację (pętla silnika kończy się po tej klatce).
        if (e.keyPressed(SDL_SCANCODE_ESCAPE)) e.requestQuit();

        auto* activeScene = e.getSceneManager()->activeScene();
        
        // Obracamy trójkąt wokół osi Z.
        // Wykorzystujemy Delta Time (dt), aby prędkość obrotu była stała (0.5 radiana na sekundę)
        // niezależnie od wydajności komputera (kolejnych klatek na sekundę).
        if (activeScene) {
            if (auto* tri = activeScene->getEntity("triangle")) {
                tri->transform().rotation.z += dt * 0.5f;
            }
        }
    };

    // 8. Uruchomienie głównej pętli gry.
    engine.run();
    return 0;
}
