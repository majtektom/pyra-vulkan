# engine3d (pyra-vulkan)

Edukacyjny silnik 3D w **C++20** na **Vulkan 1.4** i **SDL3**.

Silnik pokazuje od środka, jak działa nowoczesny renderer (deferred PBR, cienie,
bindless, teren LOD, animacje szkieletowe) i jak złożyć z niego grę: scenę ECS,
kamerę, GUI, dźwięk, nawigację i zapis stanu.

## Szybki start

Wymagania: GCC 13+, CMake 3.20+, Vulkan 1.4, SDL3 + SDL3_ttf + SDL3_image,
`glslangValidator`, `nlohmann/json.hpp`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j11
./bin/10_settlers_example
```

Aplikacje uruchamiaj z katalogu głównego repo — ścieżki `assets/` są względne.
Lista pakietów dla Fedory i Ubuntu znajduje się w dokumentacji.

## Przykłady

| Przykład | Co pokazuje |
|---|---|
| `01_rotating_triangle` | minimalna aplikacja 3D |
| `03_flyaround` | modele 3DS/MD2/glTF, skybox, kamera FPS |
| `05_gui_demo` | okna, widgety, HUD |
| `06_terrain` | teren LOD, splatting, ocean, mgła |
| `07_shadows` | światła i cienie |
| `09_vegetation` | roślinność i wiatr |
| `10_settlers` | pełna gra RTS z AI, ekonomią i mgłą wojny |

## Dokumentacja

**[doc/README.md](doc/README.md)** — dokumentacja modułów, opis API,
lekcje mechanizmów i algorytmów oraz diagramy PlantUML.

Najważniejsze punkty wejścia:

* [architektura silnika](doc/01_architektura.md)
* [Vulkan od podstaw: swapchain, bariery, bindless](doc/27_vulkan_od_podstaw.md)
* [algorytmy silnika i ich uzasadnienie](doc/29_algorytmy_silnika.md)
* [pierwsza gra / przewodnik po przykładach](doc/23_przyklady.md)
* [gra Osadnicy](doc/25_settlers.md)
* [algorytmy Osadników, w tym pamięć wyników Dijkstry](doc/28_settlers_algorytmy.md)
* [audyt dokumentacji i kolejność nauki](doc/30_audyt_dokumentacji.md)
* [znane ograniczenia i plan](doc/26_ograniczenia_i_plan.md)
* [rozwiązywanie problemów](doc/24_rozwiazywanie_problemow.md)

## Licencja zasobów

Muzyka i efekty w `10_settlers` są na licencji CC0 — szczegóły:
`assets/sound/settlers/CREDITS.md`.
