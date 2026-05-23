# Scena, encje i komponenty

## Filozofia: ECS light

Silnik używa uproszczonego wzorca **Entity-Component** (bez Systemów):

```
Entity = kontener na komponenty
Component = dane + zachowanie
```

Każda encja ma unikalną nazwę i może zawierać dowolne komponenty.
Komponenty same się aktualizują — nie ma centralnego "systemu".

---

## Encja (Entity)

```cpp
class Entity {
    std::string name;
    bool active = true;

    // dodaj komponent (tworzony na miejscu)
    template<typename T, typename... Args>
    T* addComponent(Args&&... args);

    // pobierz komponent po typie
    template<typename T>
    T* getComponent();

    // aktualizuj wszystkie komponenty
    void update(TimeDelta deltaTime);
};
```

Encje są przechowywane w `Engine::entities` (vector unique_ptr) i
indeksowane w `Engine::entityMap` (unordered_map nazwa → wskaźnik).

### Tworzenie encji

```cpp
auto* entity = engine.createEntity("kostka");
auto* mc = entity->addComponent<MeshComponent>("siatka_kostki");
auto* tc = entity->addComponent<TransformComponent>();
tc->translation = {0.0f, 0.0f, 0.0f};
tc->scale = {2.0f, 2.0f, 2.0f};
```

---

## Komponenty

### MeshComponent
Definiuje renderowalną siatkę:

```cpp
class MeshComponent : public Component {
    std::string meshName;                          // nazwa siatki w MeshManager
    std::string materialName;                      // nazwa materiału w MaterialManager
    std::shared_ptr<MaterialInstance> materialInstance; // instancja materiału (leniwa inicjalizacja)

    void collectRenderPackets(vector<RenderPacket>& packets, MeshManager& mm);
    // zbiera dane do renderowania: bufor wierzchołków, indeksów, macierz modelu, materiał
};
```

`collectRenderPackets()` pobiera `GpuMesh` z `MeshManager` (bufory Vulkan), macierz modelu
z `TransformComponent` (rodzica), i tworzy `RenderPacket`.

### TransformComponent
Pozycja, rotacja, skala w przestrzeni świata:

```cpp
class TransformComponent : public Component {
    Vec3 translation{0,0,0};
    Vec3 rotation{0,0,0};    // kąty Eulera w radianach
    Vec3 scale{1,1,1};

    Matrix4 getModelMatrix(); // macierz TRS
};
```

Macierz modelu: `Translation * RotationZ * RotationY * RotationX * Scale`.

### CameraComponent
Parametry kamery:

```cpp
class CameraComponent : public Component {
    Vec3 position{0,0,4};
    Vec3 target{0,0,0};
    Vec3 up{0,1,0};
    float fieldOfViewDegrees = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 10.0f;

    Matrix4 getViewMatrix(); // lookAt(position, target, up)
};
```

---

## RenderPacket — jednostka rysowania

```cpp
struct RenderPacket {
    VkBuffer vertexBuffer;        // bufor wierzchołków GPU
    VkBuffer indexBuffer;         // bufor indeksów GPU
    uint32_t indexCount;          // liczba indeksów do narysowania
    Matrix4 transform;            // macierz modelu
    std::shared_ptr<MaterialInstance> material; // materiał
    std::string textureName;      // nazwa tekstury (do deskryptora)
    float distanceToCamera;       // do sortowania przezroczystości
};
```

---

## Przepływ renderowania sceny

```
Engine::renderFrame()
│
├─ renderScene.clear()
├─ Dla każdej aktywnej encji:
│   ├─ Pobierz MeshComponent
│   ├─ Jeśli materialInstance == null → rozwiąż z MaterialManager
│   └─ collectRenderPackets() → dodaj do listy
│
├─ Dla każdego RenderPacket:
│   ├─ Oblicz distanceToCamera (do sortowania przezroczystości)
│   └─ renderScene.addPacket() → opaque / transparent
│
└─ renderer->render(renderScene, activeCamera)
    ├─ OpaquePass (dowolna kolejność, depth test sortuje)
    └─ TransparentPass (sortowane back-to-front)
```

---

## MeshManager — bufory GPU

```cpp
struct GpuMesh {
    std::unique_ptr<VulkanBuffer> vertexBuffer;  // VkBuffer z wierzchołkami
    std::unique_ptr<VulkanBuffer> indexBuffer;   // VkBuffer z indeksami
    uint32_t indexCount;
};

class MeshManager {
    void loadMesh(name, meshData);   // tworzy bufory GPU
    void updateMesh(name, meshData); // aktualizuje bufory (dla dynamicznych meshów)
    shared_ptr<GpuMesh> getMesh(name);
};
```

Bufory są tworzone z `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT` — CPU może je
aktualizować przez `memcpy`. Dla statycznych meshów używa się `loadMesh()`,
dla dynamicznych (cząstki) — `updateMesh()` co klatkę.

---

## MaterialManager — rejestr materiałów

```cpp
class MaterialManager {
    void add(material);            // dodaj materiał
    const Material* get(name);     // znajdź po nazwie
};
```

To prosty słownik `nazwa → Material`. Materiały są rejestrowane przy starcie
i wyszukiwane przy renderowaniu.
