# engine3d

Minimalna namiastka silnika 3D w C++ i Vulkanie. Projekt buduje bibliotekę `engine3d` oraz dwie osobne aplikacje przykładowe:

- `rotating_triangle_example`
- `rotating_cube_example`

## Struktura

- `CMakeLists.txt`: tylko wejście projektu i `add_subdirectory(...)`
- `cmake/ProjectSettings.cmake`: wspólne ustawienia i zależności całego projektu
- `cmake/ShaderBundles.cmake`: wspólna logika kompilacji shaderów GLSL/HLSL/Slang
- `engine/include` i `engine/src`: kod biblioteki silnika
- `engine/CMakeLists.txt`: build biblioteki `engine3d`
- `examples/CMakeLists.txt`: agregacja przykładów i wspólnego targetu shaderów
- `examples/rotating_triangle`: przykład obracającego się trójkąta
- `examples/rotating_cube`: przykład obracającego się sześcianu
- `assets/shaders`: źródła shaderów GLSL, HLSL albo Slang pogrupowane per aplikacja lub pipeline
- `assets/textures` i `assets/sprite`: assety tekstur używane przez przykłady i silnik

## Wymagania

- kompilator C++20
- CMake 3.20+
- biblioteki deweloperskie Vulkan
- biblioteki deweloperskie X11
- `freetype-devel` jeżeli chcesz ładować fonty TTF i renderować tekst 2D na ekranie
- `glslc` albo `glslangValidator` dla GLSL
- `dxc` albo `glslangValidator` dla HLSL
- `slangc` dla Slang

## Budowanie

```bash
cmake -S . -B build
cmake --build build
```

## Uruchamianie

```bash
./build/examples/rotating_triangle/rotating_triangle_example
./build/examples/rotating_cube/rotating_cube_example
```

## Co jest w silniku

- inicjalizacja okna X11 i kontekstu Vulkan
- swapchain, render pass, pipeline i depth buffer
- staging buffer do uploadu meshy i docelowe vertex/index buffery w pamięci GPU
- globalny uniform buffer dla kamery oraz push constants dla model matrix per obiekt
- prosta scena z wieloma obiektami współdzielącymi te same meshe
- kamera oparta o `position`, `target`, `up`
- manifest shaderów rozwiązywany po nazwie programu
- texture manager rozwiązujący asset tekstury po nazwie oraz domyślna biała tekstura fallback
- font manager rozwiązujący assety TTF po nazwie oraz overlay tekstowy 2D budowany z atlasu glifów
- atlas fontu obsługuje UTF-8, więc polskie litery z TTF działają poprawnie; dodatkowo `FontAsset.glyphsUtf8` pozwala jawnie określić, które znaki mają trafić do atlasu
- `AppConfig.updateTextLabels` pozwala zmieniać etykiety w runtime; gdy callback wprowadzi nowy znak spoza aktualnego atlasu, silnik przebuduje atlas i bufory tekstu automatycznie
- upload tekstur BMP, TGA i DDS do `VK_IMAGE_USAGE_SAMPLED_BIT` przez staging buffer i sampler/image view, niezależnie od tego czy asset leży w `assets/textures/...` czy `assets/sprite/...`
- TGA 32-bit i DDS DXT3/DXT5 zachowują kanał alpha; DDS DXT1/DXT3/DXT5 są ładowane jako skompresowane formaty BC1/BC2/BC3
- opcjonalne validation layers w debug, jeśli są dostępne

## Zarządzanie shaderami

Profesjonalnie shadery traktuje się jak osobne assety, a nie jak stringi zaszyte w kodzie. Typowy workflow wygląda tak:

- źródła shaderów są trzymane w repo jako pliki `.vert`, `.frag`, `.comp` albo ich warianty `.vert.hlsl`, `.frag.hlsl`, `.vert.slang`, `.frag.slang`,
- build albo osobny asset pipeline kompiluje je do SPIR-V,
- runtime ładuje gotowe artefakty SPIR-V,
- warianty shaderów kontroluje się przez makra, specialization constants albo manifest pipeline, zamiast kopiować całe pliki.

W tym projekcie CMake kompiluje shadery z `assets/shaders/...` do `bin/assets/shaders/...`, a aplikacja składa `ShaderManifest`, z którego silnik rozwiązuje aktywny program po nazwie. Runtime nadal widzi wyłącznie gotowe SPIR-V, więc język źródłowy shaderów nie zmienia kodu renderera.

Obsługiwane konwencje nazw wejściowych:

- GLSL: `triangle.vert`, `triangle.frag`
- HLSL: `triangle.vert.hlsl`, `triangle.frag.hlsl`
- Slang: `triangle.vert.slang`, `triangle.frag.slang`

Stage jest wyciągany z części `.vert`, `.frag` albo `.comp`, więc wynikowy plik SPIR-V ma zawsze stabilną nazwę typu `triangle.vert.spv` niezależnie od języka źródłowego.

Kolejne sensowne kroki dla większego projektu:

- dodać hot reload shaderów w trybie developerskim,
- wydzielić wspólne include shaderowe,
- rozszerzyć manifest o wiele pipeline'ów i wariantów materiałów,
- użyć refleksji SPIR-V do automatycznego budowania layoutów descriptorów.