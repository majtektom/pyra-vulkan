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
- Vulkan (nagłówki + loader)
- SDL3 (okno, wejście, wyjście dźwięku), SDL3_ttf (czcionki), SDL3_image (obrazy: mapy wysokości, tekstury glTF)
- `glslangValidator` lub `glslc` (kompilacja shaderów HLSL do SPIR-V)
- nagłówek `nlohmann/json.hpp`
- w repozytorium (bez instalacji): `stb_vorbis` (dekodowanie OGG), `tiny_gltf`, własne wczytywanie DDS/BMP

Fedora: `sudo dnf install gcc-c++ cmake vulkan-loader-devel glslang SDL3-devel SDL3_ttf-devel SDL3_image-devel json-devel`
Ubuntu 25.04+: `sudo apt install g++ cmake pkg-config libvulkan-dev glslang-tools libsdl3-dev libsdl3-ttf-dev libsdl3-image-dev nlohmann-json3-dev`

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

### Paczka do przekazania innym (Linux)

```bash
tools/build_release.sh        # wynik: dist/osadnicy-linux-x86_64.tar.gz
```

Skrypt buduje Osadników w kontenerze Ubuntu 25.04 (podman) z `-DENGINE3D_PORTABLE=ON`:
- libstdc++ i libgcc są wkompilowane statycznie (u odbiorcy nie trzeba tej samej wersji kompilatora),
- `libengine3d.so` leży obok binarki i jest szukany w jej katalogu (RUNPATH `$ORIGIN`),
- biblioteka C (glibc) jest dynamiczna - binarka działa na systemie, na którym ją zbudowano, i nowszych
  (dlatego budujemy w starszym Ubuntu, a nie na najnowszej Fedorze); wymagane glibc 2.38+,
- w paczce są tylko assety używane przez grę (~40 MB) i skrypt `uruchom.sh`.

U odbiorcy: sterownik Vulkan 1.3 oraz SDL3, SDL3_ttf i SDL3_image
(Ubuntu 25.04+: `sudo apt install libsdl3-0 libsdl3-ttf0 libsdl3-image0 libvulkan1 mesa-vulkan-drivers`).

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
| `gui_demo` | Widgety GUI (button, checkbox, combobox, memo, scrollbar), pasek narzędzi z ikonami, elementy HUD |
| `terrain` | Teren z generatorów (Plasma/Fault/Deposition) + multi-texturing z maskami |
| `10_settlers` | Gra strategiczna w stylu "Osadników": budowa osady, ekonomia, wojsko i walka z AI |

### 10_settlers — Osadnicy

```bash
./bin/10_settlers_example
```

Osadnicy sami budują i pracują, a gospodarka opiera się na **łańcuchach produkcji** (12 towarów):
drwal → tartak (kłody → deski, materiał budowlany), kamieniarz i kopalnia kamienia, farma → młyn → piekarnia
(zboże → mąka → chleb), rybak i myśliwy (ryby, mięso), kopalnie węgla i rudy (w górach, zjadają żywność) →
huta (żelazo) → kuźnia (broń potrzebna do szkolenia żołnierzy); leśniczy sadzi las. Domy podnoszą limit ludności,
koszary szkolą mieczników i łuczników (ręcznie albo **Auto-rekrutacją** z ustawioną proporcją), wieże poszerzają terytorium i strzelają do wrogów; wieża przy granicy odbiera wrogowi pas ziemi
(granica wypada w połowie nakładania się zasięgów), a budynki na przejętym terenie płoną.
Wieża strzela tylko wtedy, gdy ma **załogę** (do 3 żołnierzy; wolni żołnierze obsadzają puste wieże sami, PPM na
własnej wieży wysyła zaznaczonych). Wrogiej wieży się nie burzy — **szturmuje się ją** (PPM): obrońcy wychodzą do
walki, a żołnierz, który wejdzie do pustej wieży, przejmuje ją razem z otaczającym terenem.

Gospodarka opiera się na **drogach i tragarzach**: warsztat odkłada towary na fladze przed wejściem (najwyżej 8 —
gdy nikt ich nie odbiera, produkcja staje). Drogi łączą budynki z magazynami i przyspieszają marsz. Wolni tragarze
(wspólna pula — liczbę ustawia się w oknie **Gospodarka** (I) przyciskami −/+ albo trybem Auto) niosą towar całą trasą
z warsztatu do najbliższego magazynu z miejscem, a w drugą stronę — materiały z magazynu na place budowy i koszt
szkolenia do koszar (budowa i szkolenie postępują w miarę dostaw). Dostawa może też wziąć towar prosto z flagi
warsztatu (np. deski z tartaku na budowę) - działa to nawet przy pełnych magazynach. W oknie Gospodarka każdy towar
ma **limit zapasu** (−/+, domyślnie limitowane są surowce i półprodukty: drewno 40, kamień 60, zboże/mąka/węgiel/
ruda/żelazo po 20): powyżej limitu tragarze nie wnoszą towaru do magazynu, więc magazyny nie zapychają się jednym
towarem, a warsztat z pełną flagą czeka, aż ktoś zużyje jego wyroby. Surowce można wydać dopiero wtedy, gdy są
w magazynie; zamek i Magazyn mieszczą po 200 towarów, więc Magazyny warto stawiać blisko produkcji. Okno Gospodarka
pokazuje towary w magazynach, na flagach i w drodze, zapełnienie magazynów, tragarzy i wstrzymane warsztaty.
Komputerowy przeciwnik rozbudowuje swoją osadę i wysyła fale ataku. Wygrywa ten, kto zniszczy wrogi zamek.
**Mgła wojny**: teren poza zasięgiem wzroku twoich ludzi jest czarny, dopóki go nie odkryjesz; odkryty, ale bez
zwiadu - przyciemniony (widać teren i zapamiętane budynki wroga, nie widać jego wojska). Najdalej widzą łucznicy,
wieże i zamek. Teren, roślinność, budynki, morze i minimapa są przyciemniane tą samą teksturą widoczności.
Komputer też gra w mgle: zna tylko teren odkryty przez swoich ludzi - wysyła zwiadowcę (łucznika), rozbudowuje się
w stronę znanych lasów i skał, a falę ataku kieruje dopiero na budynek, który już zobaczył.
Po zwycięstwie przycisk **Graj dalej** przełącza w wolną grę - osadę można rozbudowywać bez końca (zapis też działa).
AI nie ma listy budowy: co kilka sekund czyta te same statystyki co gracz (braki towarów, przestoje, zapasy,
ludność) i stawia to, czego brakuje, pilnując proporcji warsztatów (1 tartak obsługuje 4 drwali, 1 leśniczy
wystarcza na 3 drwali, 1 młyn na 3 farmy); wyczerpanych kamieniarzy burzy, a gdy brakuje miejsca lub surowców,
stawia wieżę w ich stronę. Gra na tych samych zasadach co gracz - bez darmowych surowców.

Sterowanie: WSAD/krawędź ekranu — kamera, Q/E/środkowy przycisk — obrót, kółko — zoom,
Budowanie: dolny pasek z ikonami budynków pogrupowanymi tematycznie (karta z opisem i kosztem po najechaniu).
LPM — zaznaczanie (ramka), PPM — ruch/atak, Ctrl+PPM — marsz bojowy, 1–9 — budowa, T — droga,
R — obrót budynku, Del — zburzenie / usunięcie flagi, Tab — całe wojsko, I — gospodarka, M — muzyka, F1 — pomoc, ESC — pauza.

Menu (główne i pauza) ma okna **Ustawienia** (muzyka/efekty wł./wył. i głośność, głośność ogólna, rozdzielczość okna,
pełny ekran, synchronizacja pionowa, synchronizacja GPU-CPU, limit FPS, cienie — zapis w `save/settlers_settings.json`)
oraz **Sterowanie** z pełną listą klawiszy.

Kod przykładu jest podzielony na symulację (`World`, pliki `world_*.cpp`) i aplikację (`SettlersApp`, pliki `app_*.cpp`).
Mapa plików i kolejność czytania: komentarz na początku `examples/10_settlers/main.cpp`; opisy klas
w `settlers_world.h` i `settlers_app.h`.

**Zapis gry**: F5 albo przycisk w menu pauzy, **wczytanie**: F8 albo przycisk w menu głównym / pauzy
(plik `save/settlers_save.json`). Zapis to ziarno mapy (z niego odtwarza się teren, lasy i skały) oraz listy budynków,
jednostek, flag, dróg, zleceń transportu, stan obu stron i zmiany w surowcach mapy - po wczytaniu gra toczy się
dokładnie tak, jak by toczyła się bez zapisu (zapisany jest też stan generatora losowego).

Prędkość czasu: klawisze **[** i **]** (x1, x2, x4, x8, x16, x24) albo przycisk w menu pauzy. W menu głównym
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
- GUI (okna, buttony, checkboxy, combobox, memo, scrollbar, trackbar, pasek narzędzi `GuiToolbar` z ikonami pixel-art i kartami informacji, elementy HUD: paski, linie, napisy)
- Ustawienia gracza `GameSettings` (JSON, okno, VSync, FPS, dźwięk), szum `ValueNoise` do terenu proceduralnego
- Tekstowy overlay 2D z atlasu glifów (UTF-8)
- Font manager (TTF przez SDL3_ttf)
- Kamera RTS (`RtsCameraController`), picking myszą (`screenPointToRay`, `worldToScreen`, `Terrain::raycast`)
- Nawigacja: siatka `NavGrid` z wyszukiwaniem ścieżek A* i wygładzaniem
- `MeshBuilder` — proceduralne modele low-poly z prymitywów (box, walec, stożek, sfera, dach)
- `AudioSystem` (`Engine::getAudio()`) — efekty WAV/OGG z wariantami i limitami, dźwięk przestrzenny, strumieniowana muzyka OGG z przenikaniem (SDL3 + stb_vorbis)
- HUD w trybie immediate (`GuiMgr::addOverlayRect/addOverlayFrame`)
- Obsługa tekstur: DDS (DXT1/3/5), BMP, TGA

## Gdy gra się wywali

Opis awarii trafia do katalogu `crash/` (plik `<program>_<data>.log`): przyczyna, miejsce w kodzie (plik:linia),
stos wywołań i ostatnie wpisy logu. Pliki i linie pojawiają się, gdy w systemie jest `addr2line` (pakiet binutils).
Taki plik wystarczy dołączyć do zgłoszenia błędu.
