# Architektura silnika engine3d

## Filozofia projektowa

Silnik `engine3d` to nowoczesny, modularny silnik 3D napisany w C++20. Jego architektura ewoluowała od prostego modelu obiektowego do systemu opartego o separację danych od logiki (ECS-lite) oraz nowoczesny backend graficzny Vulkan 1.4.

Kluczowe założenia:
- **Separacja odpowiedzialności**: Wyraźny podział na świat (scena), renderowanie i platformę.
- **Data-Driven**: Logika świata i definicje obiektów są opisane w zewnętrznych plikach zasobów.
- **Wydajność**: Minimalizacja narzutu CPU (ekstrakcja danych renderujących, systemy przetwarzające masowo).

---

## Struktura Modułowa

Silnik podzielony jest na logiczne moduły (katalogi w `engine/`):

| Moduł | Odpowiedzialność | Kluczowe Klasy |
|-------|-----------------|----------------|
| **Core** | Fundament silnika, pętla główna, ECS, logger, matematyka. | `Engine`, `Logger`, `Entity`, `Component` |
| **Render** | Backend Vulkan, zarządzanie pamięcią (VMA), shadery, materiały. | `Renderer`, `MaterialSystem`, `VulkanContext` |
| **Scene** | Zarządzanie hierarchią obiektów, serializacja, systemy ECS. | `Scene`, `SceneManager`, `TransformSystem` |
| **Object** | Wysokopoziomowe obiekty geometryczne i loadery. | `Terrain`, `Model3ds`, `GltfModel`, `Md2Model` |
| **Feature** | Specjalistyczne systemy renderujące i UI. | `ParticleSystem`, `SpriteSystem`, `GuiManager` |
| **Platform** | Abstrakcja systemu operacyjnego, okno, wejście. | `Platform`, `InputActions` |
| **Camera** | System kamer i kontrolery ruchu. | `CameraComponent`, `CameraController` |

---

## Przepływ danych (Frame Lifecycle)

W każdej klatce dane przechodzą przez uporządkowany proces:

1. **Input Phase**: `Platform` pobiera zdarzenia, `InputActions` aktualizuje stany akcji.
2. **Logic Phase**:
   - Wywołanie `Engine::onUpdate`.
   - Aktualizacja systemów sceny (np. animacje, cząsteczki).
   - `TransformSystem`: Przeliczanie macierzy światowych dla brudnych (`dirty`) transformacji.
3. **Extraction Phase**:
   - `CameraExtractionSystem`: Przygotowanie danych kamery (`RenderView`).
   - `RenderExtractionSystem`: Tłumaczenie komponentów sceny na paczki renderujące (`RenderPacket`).
   - `LightSystem`: Zbiór aktywnych świateł dla renderera.
4. **Render Phase**:
   - `RenderOrchestrator` wykonuje plan renderowania (Cienie -> G-Buffer -> Lighting -> Transparent -> UI).
   - Wysyłanie komend do GPU przez `Renderer`.

---

## Kluczowe Mechanizmy

### System ECS-lite
Obiekty w świecie to `Entity` (identyfikowane przez `EntityId`). Każda encja posiada listę komponentów (`Component`). Logika przetwarzająca wiele encji naraz jest zawarta w systemach (np. `LightSystem`), co zapewnia lepszą lokalność danych i łatwiejszą optymalizację.

### Asset Registry & Handles
Zasoby (tekstury, materiały, meshe) nie są referencjonowane przez stringi w runtime. Zamiast tego używa się typowanych uchwytów (`TextureHandle`, `MaterialHandle`). `AssetRegistry` zarządza mapowaniem nazw na uchwyty i dba o unikalność zasobów.

### Material & Shader System
Silnik wykorzystuje shadery HLSL kompilowane do SPIR-V. `MaterialSystem` i `MaterialCompiler` zarządzają stanem potoku (`VkPipeline`). Pipeline'y są tworzone na żądanie i cache'owane na podstawie parametrów materiału.

### Deferred Shading
Renderer używa potoku odroczonego. W pierwszej fazie (G-Buffer) zapisywane są parametry geometrii (albedo, normalne, materiał), a w fazie oświetlenia wyliczany jest finalny kolor na podstawie wielu źródeł światła i mapy cieni.

---

## System Logowania (Logger)
Nowy moduł diagnostyczny umożliwia precyzyjne śledzenie stanu silnika. Wspiera różne poziomy logowania (`INFO`, `WARN`, `ERROR`) i pozwala na przekierowanie wyjścia do konsoli lub pliku. Jest bezpieczny wątkowo i zintegrowany z wszystkimi kluczowymi modułami.
