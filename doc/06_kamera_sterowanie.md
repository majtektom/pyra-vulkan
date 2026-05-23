# Kamera i sterowanie

## CameraComponent

Kamera to zwykły komponent encji. Przechowuje pozycję, punkt docelowy (na co patrzy)
i wektor "góra". Na tej podstawie tworzone są macierze widoku i projekcji.

```cpp
// Macierz widoku — transformuje świat do przestrzeni kamery
Matrix4 CameraComponent::getViewMatrix() {
    return lookAt(position, target, up);
    // lookAt:macierz 4x4 ustawiająca kamerę w position, patrzącą na target
}

// Macierz projekcji — perspektywa
Matrix4 perspective(fovYRad, aspect, near, far);
// tworzy macierz projekcji perspektywicznej:
// - fovYRad: kąt widzenia w pionie (radiany)
// - aspect:  szerokość/wysokość
// - near:    bliska płaszczyzna odcinania
// - far:     daleka płaszczyzna odcinania
```

---

## CameraController

`CameraController` to klasa odpowiedzialna za poruszanie kamerą w stylu FPS:

```cpp
class CameraController {
    void update(float dt, const InputKeys& keys, float mouseDX, float mouseDY,
                CameraConfig& config);
};
```

### Sterowanie

| Akcja | Klawisz |
|-------|---------|
| Ruch do przodu | W |
| Ruch do tyłu | S |
| Ruch w lewo | A |
| Ruch w prawo | D |
| Ruch w górę | Spacja |
| Ruch w dół | Lewy Ctrl |
| Obrót kamery | Mysz (gdy kursor złapany) |

### Jak działa obrót myszą

1. Przy starcie, kursor jest "chwytany" (`platform->grabPointer(true)`)
2. Każda klatka: `pollInput()` zwraca `mouseDeltaX`, `mouseDeltaY`
3. Kamera obraca się o kąt proporcjonalny do delty myszy
4. Ograniczenie pitcha: -89° do +89° (żeby kamera się nie przekręciła)

### Tryb latania

Przy wciśniętym klawiszu, kamera porusza się w kierunku, w którym patrzy:
```cpp
Vec3 forward = normalize(target - position);  // kierunek patrzenia
Vec3 right = normalize(cross(forward, up));    // kierunek w prawo
position += forward * speed * dt;              // ruch przód/tył
position += right * speed * dt;                // ruch lewo/prawo
```

---

## Konfiguracja kamery

```cpp
struct CameraConfig {
    Vec3 position{0,0,4};         // początkowa pozycja
    Vec3 target{0,0,0};           // początkowy punkt docelowy
    Vec3 up{0,1,0};               // wektor góry
    float fieldOfViewDegrees = 45;
    float nearPlane = 0.1f;
    float farPlane = 10.0f;
};
```

W `Engine::updateCameraControls()`, kamera jest aktualizowana na podstawie
wejścia. Jeśli `activeCamera == nullptr`, sterowanie jest pomijane.

---

## Macierze w shaderach

W shaderze HLSL, wierzchołki są transformowane przez 3 macierze:

```hlsl
output.position = mul(proj, mul(view, mul(model, float4(input.position, 1.0))));
```

Kolejność mnożenia (w HLSL `mul()`):
1. `model` — transformuje z przestrzeni lokalnej do świata
2. `view` — transformuje z przestrzeni świata do przestrzeni kamery
3. `proj` — transformuje z przestrzeni kamery do clip space (NDC)

Macierze są `column_major` — to standardowy format dla GPU.

---

## Przykład: statyczna kamera

W przykładach `rotating_cube` i `rotating_triangle`, kamera jest ustawiona na sztywno
przez nadpisywanie pozycji w `onUpdate`:

```cpp
engine.onUpdate = [](float dt, Engine& e) {
    auto* cam = const_cast<CameraComponent*>(e.getActiveCamera());
    if (cam) {
        cam->position = {0.0f, 0.0f, 5.5f};  // sztywna pozycja
        cam->target = {0.0f, 0.0f, 0.0f};    // patrzy na środek sceny
        cam->up = {0.0f, 1.0f, 0.0f};
    }
};
```

To nadpisuje zmiany wprowadzone przez `CameraController`, dając statyczny widok.
