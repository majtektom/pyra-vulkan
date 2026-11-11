# Dokumentacja silnika engine3d

Witamy w dokumentacji technicznej silnika `engine3d`. Silnik jest nowoczesną, edukacyjną bazą do tworzenia aplikacji 3D, opartą na API Vulkan 1.4 oraz architekturze zorientowanej na dane (ECS light).

## Struktura Dokumentacji

| Plik | Temat |
|------|-------|
| [01_architektura.md](01_architektura.md) | Podział modułowy, przepływ danych i warstwy silnika. |
| [02_pipeline_vulkan.md](02_pipeline_vulkan.md) | Detale backendu: Deferred Shading, Bindless Textures, Dynamic Rendering. |
| [03_system_materialow.md](03_system_materialow.md) | Materiały PBR, kompilacja shaderów HLSL i system uchwytów. |
| [04_modele_3ds.md](04_modele_3ds.md) | Obsługa formatu 3DS i specyfika importu geometrii. |
| [05_scena_komponenty.md](05_scena_komponenty.md) | Zarządzanie światem: Encje (ECS), hierarchia, komponenty. |
| [06_kamera_sterowanie.md](06_kamera_sterowanie.md) | System kamer, ekstrakcja widoku i mapowanie wejścia (Input Actions). |
| [07_czastki.md](07_czastki.md) | System cząsteczek, emitery i wielowątkowa aktualizacja. |
| [08_tekstury_fonty.md](08_tekstury_fonty.md) | Zarządzanie teksturami (DDS/BMP/TGA) oraz renderowanie tekstu (SDL3_ttf). |
| [09_rozwoj.md](09_rozwoj.md) | Historia zmian i kierunki dalszej rozbudowy. |
| [10_logger.md](10_logger.md) | **NOWOŚĆ:** System logowania diagnostycznego. |

## Główne Funkcje Silnika

- **Vulkan 1.4**: Wykorzystanie nowoczesnych funkcji jak Dynamic Rendering, Timeline Semaphores i Descriptor Indexing (Bindless).
- **Deferred PBR Renderer**: Potok renderujący dzielący geometrię (G-Buffer) od oświetlenia.
- **System ECS**: Encje i komponenty z jawnym podziałem na dane i systemy przetwarzające (`TransformSystem`, `LightSystem`, itp.).
- **Asset Registry**: Zarządzanie zasobami poprzez bezpieczne, typowane uchwyty (`Handle`).
- **Skeletal Animation**: Wsparcie dla animacji szkieletowych w formacie glTF.
- **Advanced Terrain**: System terenu z generacją proceduralną, wielowarstwowym teksturowaniem i LOD.

## Szybki Start

### wymagania

obsługa vulkana razem z paczką validacji

```bash
sudo dnf install vulkan-validation-layers
```

instalacja SDL3 do zarządzania oknem i inputem

### Kompilacja

Wymagany kompilator wspierający C++20 (np. GCC 13+) oraz zainstalowane Vulkan SDK i biblioteki SDL3.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j11
```

### Uruchamianie
Przykłady znajdują się w katalogu `bin/`:
```bash
cd bin
./08_game          # Pełne demo gry (wyspa, modele, oświetlenie)
./06_terrain_demo  # Demonstracja generatorów terenu
./04_particles     # System cząsteczek
```

---
*Silnik engine3d ma charakter edukacyjny i architektoniczny. Skupia się na pokazaniu nowoczesnych wzorców projektowych w C++ i grafice czasu rzeczywistego.*
