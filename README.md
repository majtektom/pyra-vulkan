# engine3d

Silnik 3D w C++20 i Vulkanie z dynamiczną biblioteką `libengine3d.so`.

## Struktura

- `engine/` — biblioteka silnika (`libengine3d.so`)
- `examples/` — aplikacje przykładowe (każda ładuje `libengine3d.so`)
- `assets/` — shadery, tekstury, modele, fonty, teren
- `cmake/` — wspólne ustawienia CMake i kompilacja shaderów

## Wymagania

- C++20 (gcc 13+)
- CMake 3.20+
- Vulkan SDK
- SDL3 + SDL3_ttf
- `dxc` (do kompilacji shaderów HLSL)
- `stb_image.h`

## Budowanie

### Release (optymalizacja -O2)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j11
```

### Debug (symbole -g, bez optymalizacji)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j11
```

Binaria lądują w `bin/`:
- `bin/libengine3d.so` — biblioteka silnika
- `bin/terrain_example`, `bin/flyaround_example`, itd. — przykłady

## Przykłady

| Przykład | Opis |
|----------|------|
| `rotating_triangle` | Obracający się trójkąt |
| `rotating_cube` | Obracający się sześcian z teksturą |
| `flyaround` | Lot nad sceną z modelami 3DS, MD2, GLTF |
| `particles` | System cząsteczek |
| `gui_demo` | Widgety GUI (button, checkbox, combobox, memo, scrollbar) |
| `terrain` | Teren z generatorów (Plasma/Fault/Deposition) + multi-texturing z maskami |

## Uruchamianie

```bash
cd bin
./terrain_example
./flyaround_example
```

## Funkcje silnika

- Vulkan 1.3, swapchain, depth buffer
- VMA (Vulkan Memory Allocator) do zarządzania pamięcią GPU
- System materiałów z cache pipeline'ów
- Normal mapping (binding 2 w descriptor layout)
- Skeletal animation (skinning w vertex shaderze, max 64 kości)
- GLTF/GLB loader (tinygltf, osadzone tekstury, animacje szkieletowe)
- MD2 loader z animacją klatkową
- 3DS loader
- System terenu (QuadTree LOD, generatory, multi-texturing z maskami, wygładzanie krawędzi)
- GUI (okna, buttony, checkboxy, combobox, memo, scrollbar, trackbar)
- Tekstowy overlay 2D z atlasu glifów (UTF-8)
- Font manager (TTF przez SDL3_ttf)
- Obsługa tekstur: DDS (DXT1/3/5), BMP, TGA
