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
| `10_settlers` | Gra strategiczna w stylu "Osadników": budowa osady, ekonomia, wojsko i walka z AI |

### 10_settlers — Osadnicy

```bash
./bin/10_settlers_example
```

Osadnicy sami budują i pracują (drwal, kamieniarz, farma), domy podnoszą limit ludności,
koszary szkolą mieczników i łuczników, wieże poszerzają terytorium i strzelają do wrogów.
Komputerowy przeciwnik rozbudowuje swoją osadę i wysyła fale ataku. Wygrywa ten, kto zniszczy wrogi zamek.

Sterowanie: WSAD/krawędź ekranu — kamera, Q/E/środkowy przycisk — obrót, kółko — zoom,
LPM — zaznaczanie (ramka), PPM — ruch/atak, Ctrl+PPM — marsz bojowy, 1–6 — budowa,
R — obrót budynku, Del — zburzenie, Tab — całe wojsko, M — muzyka, F1 — pomoc, ESC — pauza.

Menu (główne i pauza) ma okna **Ustawienia** (muzyka/efekty wł./wył. i głośność, głośność ogólna, rozdzielczość okna,
pełny ekran, synchronizacja pionowa, synchronizacja GPU-CPU, limit FPS, cienie — zapis w `save/settlers_settings.json`)
oraz **Sterowanie** z pełną listą klawiszy.

Kod przykładu jest podzielony na symulację (`World`, pliki `world_*.cpp`) i aplikację (`SettlersApp`, pliki `app_*.cpp`).
Mapa plików i kolejność czytania: komentarz na początku `examples/10_settlers/main.cpp`; opisy klas
w `settlers_world.h` i `settlers_app.h`.

Prędkość czasu: klawisze **[** i **]** (x1, x2, x4, x8, x16) albo przycisk w menu pauzy. W menu głównym
**Pokaz: AI kontra AI** — komputer gra za obie strony, a gracz ogląda (domyślnie x4).

Debugowanie: **F3** przełącza tryby (1: ścieżki, cele, stany jednostek, wejścia budynków, panel statystyk;
2: dodatkowo siatka kolizji wokół kamery), **F9** zrzuca do konsoli pełny stan zaznaczonych i zawieszonych jednostek.
`SETTLERS_DEBUG=1|2` włącza tryb debug od startu. Symulacja sama wykrywa zawieszone jednostki (log `[zawieszenie]`)
i próbuje je odblokować.

Muzyka (RandomMind) i efekty (Kenney) są na licencji CC0 — szczegóły w `assets/sound/settlers/CREDITS.md`.

Tryby testowe: `SETTLERS_AUTOPLAY=<sekundy> SETTLERS_SPEED=<mnożnik>` (AI gra za obie strony, stan w logu),
`SETTLERS_UITEST=1` (wstrzykuje zdarzenia klawiatury/myszy i sprawdza picking, budowanie, zaznaczanie i rozkazy).

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
- Kamera RTS (`RtsCameraController`), picking myszą (`screenPointToRay`, `worldToScreen`, `Terrain::raycast`)
- Nawigacja: siatka `NavGrid` z wyszukiwaniem ścieżek A* i wygładzaniem
- `MeshBuilder` — proceduralne modele low-poly z prymitywów (box, walec, stożek, sfera, dach)
- `AudioSystem` (`Engine::getAudio()`) — efekty WAV/OGG z wariantami i limitami, dźwięk przestrzenny, strumieniowana muzyka OGG z przenikaniem (SDL3 + stb_vorbis)
- HUD w trybie immediate (`GuiMgr::addOverlayRect/addOverlayFrame`)
- Obsługa tekstur: DDS (DXT1/3/5), BMP, TGA
