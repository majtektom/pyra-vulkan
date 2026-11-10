# pyra-vulkan (engine3d) — wskazówki dla agenta

Silnik 3D w C++20 na Vulkan 1.3 (dynamic rendering, deferred PBR) + SDL3, z przykładami-grami.
Komentarze w kodzie i komunikaty gry piszemy **po polsku**, z myślą o osobie, która się uczy.

## Budowanie i uruchamianie

```bash
cmake --build build -j11          # cały projekt (silnik, shadery, przykłady)
./bin/10_settlers_example         # uruchamiać z katalogu głównego repo (ścieżki assets/ są względne)
```

- Binarki trafiają do `bin/`, shadery HLSL kompiluje **glslangValidator** (dxc nie jest zainstalowany) do `bin/assets/shaders/`.
- `cmake/ShaderBundles.cmake` śledzi też `shared/*.hlsl` i `*_common.hlsl` — zmiana include przebudowuje shadery.
- Nowy przykład: katalog w `examples/NN_nazwa/` + `add_subdirectory` w `examples/CMakeLists.txt`,
  `target_link_libraries(... PRIVATE engine3d)`. Nowy plik .cpp przykładu trzeba dopisać do jego `CMakeLists.txt`.
- Paczka dla innych: `tools/build_release.sh` (kontener Ubuntu 25.04, `-DENGINE3D_PORTABLE=ON` = statyczne
  libstdc++/libgcc, `-DENGINE3D_OUTPUT_DIR` = osobny katalog wyjścia) → `dist/osadnicy-linux-x86_64.tar.gz`.
  Binarki mają RUNPATH `$ORIGIN` (libengine3d.so obok binarki). Zależności systemowe: Vulkan, SDL3, SDL3_ttf, SDL3_image
  (użytkownik woli biblioteki z rodziny SDL3 zamiast własnych/nagłówkowych zamienników - czcionki SDL3_ttf, obrazy
  SDL3_image; OGG zostaje na stb_vorbis, bo SDL3_sound/SDL3_mixer nie ma w pakietach Ubuntu). Nie budować paczki na
  hoście: glibc Fedory jest za nowa dla Ubuntu.
- Walidacja Vulkan jest WYŁĄCZONA w buildzie Release (zjadała ~90% czasu klatki na CPU) - po zmianach w renderze
  uruchomić z `ENGINE_VALIDATION=1` i sprawdzić log pod kątem `Validation`.
- Profiler klatki: `ENGINE_PROFILE=1` (co 3 s `[profil]` - czasy sekcji: update gry, sekcje świata, nagrywanie
  cieni/G-Buffer, czekanie na GPU, liczba paczek). W kodzie: `PROFILE_SCOPE("nazwa")` lub `Profiler::get().lap()`.
  Osadnicy: `SETTLERS_NOVSYNC=1`, `SETTLERS_SLOWAT=<s>` (autoplay zwalnia do x1), `SETTLERS_STRESS=<n>`.

## Układ silnika (`engine/`)

| Katalog | Zawartość |
|---|---|
| `core/` | `Engine` (pętla, `onUpdate`, wejście/akcje, tekst HUD, ustawienia), `Entity`/`Component`/`TransformComponent`, `MeshManager`, `MeshBuilder` (proceduralne low-poly z kolorem w wierzchołkach), `math.h` (Vec3: `add/subtract/multiply/normalize/dot/cross/lerp`, dla gier: `distanceXZ`, `smoothstep`, `rotateY`, `angleLerp`), `noise.h` (`ValueNoise`: value noise + fbm), `game_settings` (`GameSettings`: dźwięk/okno/VSync/FPS/cienie, JSON load/save, apply), logger (`LOG_INFO/WARN/ERROR`) |
| `camera/` | `CameraComponent` (`autoAspect`), `camera_picking` (`screenPointToRay`, `worldToScreen`, `rayPlaneY`, `raySphere`), `RtsCameraController` |
| `render/` | `renderer_core.cpp` (G-Buffer, cienie: 4 kaskady słońca + atlas 2x2 dla 4 świateł lokalnych), materiały (`MaterialManager::add(Material)`), shadery |
| `scene/` | `Scene` (`createEntity`, `createDirectionalLight`, `setActiveCamera`), `SceneManager` |
| `feature/` | `ai/nav_grid` (A*, przeszkody zliczane, regiony), `audio/audio_system` (SDL3 + stb_vorbis OGG, dźwięk 3D, muzyka), `gui/` (okna i widgety; overlay HUD: `addOverlayRect/Frame/Line/Bar/Text/TextCentered`; `GuiToolbar` - pasek z grupami ikon i kartą `GuiCard`; `PixelIcon` - ikony pixel-art z tekstu), `particle/`, `font/`, `fog/` |
| `object/` | teren (`Terrain::raycast/getNormal/setHeightmap`), ocean, skydome, roślinność, modele 3ds/md2/gltf |
| `platform/` | SDL3: okno, zdarzenia, rozmiar ekranu |

Dokumentacja architektury: `doc/01_architektura.md` … `doc/10_logger.md`. Historia zmian: `changelog.md`.

## Pułapki (już raz rozwiązane)

- Pozycja świata w G-Bufferze musi być `R32G32B32A32_SFLOAT` (RGBA16F przy współrzędnych ~300 dawało shadow acne).
- Bias/normal-offset cieni skalowany rozmiarem teksela kaskady (`assets/shaders/shared/shadow_pcf.hlsl`).
- Teren teselowany rzuca cień potokiem `terrain_shadow` (ta sama teselacja i `shared/terrain_height.hlsl` co ekran) -
  zmiana wysokości terenu w shaderze musi iść przez tę wspólną funkcję, inaczej wrócą "cienie znikąd".
- `depthClamp` tylko dla kaskad słońca (ortho); w mapach cieni świateł lokalnych (perspektywa) psuje obcinanie.
- Cienie słońca poruszają się płynnie (bez kwantyzacji kierunku - użytkownik woli płynne niż skokowe); zasięg ustawia gra `configureDirectionalShadow`
  (Osadnicy dopasowują go do kamery co klatkę). Gdy gra nie poda `startDistance`, renderer sam zaczyna kaskady
  przy najbliższej widocznej geometrii (granice siatek i węzłów terenu) i liczy zasięg od niej.
  `08_game`: `GAME08_MAP=demo.map GAME08_HOUR=12 GAME08_CAMY=900 GAME08_NOLIGHT=1 GAME08_NOFOG=1`
  (użytkownik ogląda cienie z wyłączonym światłem i mgłą - tryb `lightingEnabled == 0` w deferred.hlsl).
- Zmiana materiału encji: po podmianie `materialHandle` wyzerować `materialInstance`.
- Pozycja myszy SDL to (0,0) przed pierwszym ruchem — przewijanie krawędzią ekranu dopiero po ruchu myszy.
- Kółko myszy: używać `InputSnapshot::mouseWheelF` (float), nie wersji int.
- Obiekty półprzezroczyste nie zapisują głębokości (w TransparentPass depth jest tylko do odczytu), więc złożony model
  z alfą pokazuje swoje wnętrze warstwami. Dla podglądów/hologramów ustaw `Material::depthPrepass = true`
  (`TransparentDepthPrepass` zapisuje głębokość w części 1 klatki, potem widać tylko najbliższą powierzchnię).
- `MeshManager::loadMesh` z unikalną nazwą przy częstych zmianach siatki (np. drogi) — bezpieczniej niż nadpisywanie.

## Przykład 10_settlers (gra RTS w stylu Osadników)

Podział: `World` (`settlers_world.h`, pliki `world_*.cpp`) = symulacja, bez wejścia i GUI;
`SettlersApp` (`settlers_app.h`, pliki `app_*.cpp`) = prezentacja, wejście, HUD, dźwięk, debug, testy.
Panel budowy to `GuiToolbar` z silnika (`app_buildbar.cpp` dostarcza tylko ikony i treść kart).
Zasada: elementy przydatne innym grom (GUI, ustawienia, szum, matematyka) trafiają do `engine/`, a nie do przykładu.
Mapa plików i opis każdego modułu: komentarz na górze `main.cpp`, `settlers_world.h`, `settlers_app.h`.
Wszystkie liczby balansu: `settlers_defs.h/.cpp` (budynki, `productionDef` - co produkuje i zużywa warsztat,
jednostki, nazwy/kolory towarów). Nowy warsztat = wpis w obu tabelach + model (`settlers_assets.cpp`) + ikona
(`app_buildbar.cpp`) + pozycja w `kBuildMenu`; zachowanie wynika z `WorkKind`; dla AI: producent towaru w
`aiPlanFor`, ewentualnie proporcja w `kAiRatios` (`world_ai.cpp`) - czasy pracy dostroić pomiarem (log "wydajność").
AI to planista potrzeb (bez listy budowy) - decyzje w logu `[ai]` (`SETTLERS_AILOG=1` lub autoplay).

Tryby uruchomienia (zmienne środowiskowe):

| Zmienna | Działanie |
|---|---|
| `SETTLERS_UITEST=1` | test UI (wstrzykuje zdarzenia SDL) + testy symulacji; kończy się linią `koniec, błędów: N` |
| `SETTLERS_UITEST_KEEP=1` | nie zamyka okna po teście |
| `SETTLERS_AUTOPLAY=<s>` + `SETTLERS_SPEED=<x>` | AI gra za obie strony, log `[autoplay]` co 30 s gry (budynki, place, transport, wydajność) i decyzje `[ai]` |
| `SETTLERS_AILOG=1` | decyzje AI (`[ai] ... buduje X - powód`) także w zwykłej grze |
| `SETTLERS_FLAGDUMP=1` | przy końcu autoplay wypisuje flagi z towarami |
| `SETTLERS_DEBUG=1|2` | tryb debug od startu (jak F3) |
| `SETTLERS_OPEN=settings|controls|help|demo|economy|game` | otwiera okno / rozpoczyna partię od razu |
| `SETTLERS_PLACE=<n>` | z `OPEN=game`: od razu tryb stawiania n-tego budynku (podgląd na środku ekranu) |
| `SETTLERS_HOVERBAR=<n>` | kursor nad n-tym przyciskiem paska budowy (0-8, -1 droga) - do zrzutów karty informacji |
| `SETTLERS_CAMERA=<odl>` | odległość kamery (do zrzutów ekranu) |
| `SETTLERS_SHOWCASE=1` | wszystkie budynki obok siebie |
| `SETTLERS_STRESS=<n>` | każda strona dostaje n osadników więcej (wydajność; FPS w logu autoplay) |
| `SETTLERS_SELECT=barracks` | z `OPEN=game`: gotowe koszary obok zamku, zaznaczone (zrzut okna koszar) |

Weryfikacja zmian: build bez ostrzeżeń → `SETTLERS_UITEST=1` (kilka razy, mapy są losowe) →
`SETTLERS_AUTOPLAY=900 SETTLERS_SPEED=24` → zrzut ekranu (`spectacle -a -b -n -o plik.png` po uruchomieniu gry w tle).
Testy klikające w menu bywają niestabilne, gdy użytkownik rusza prawdziwą myszą — powtórzyć przebieg.
`spectacle -a` łapie AKTYWNE okno - gdy użytkownik gra równolegle, zrzut może pokazać jego grę (sprawdź czas/stan).

Ustawienia gracza: `save/settlers_settings.json` (w trybach testowych nie są wczytywane ani zapisywane).

## Zasady pracy

- Nie commitować bez wyraźnej prośby.
- Przy nowych funkcjach gry: dopisać test do `app_tests.cpp`, zaktualizować `README.md`, `changelog.md` i pomoc w grze (`kControls`, `drawHelp`).
- Skille projektu: `.agents/skills/` (dowiązane jako `.claude/skills/`).
