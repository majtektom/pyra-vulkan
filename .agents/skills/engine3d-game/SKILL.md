---
name: engine3d-game
description: Jak napisać grę 3D na silniku engine3d (pyra-vulkan) - szkielet przykładu, scena i encje, proceduralne siatki MeshBuilder, materiały, kamera RTS i picking, wejście przez akcje, HUD i okna GUI, dźwięk 3D i muzyka, teren i nawigacja A*, cienie, tryby testowe i weryfikacja. Używaj przy tworzeniu nowego przykładu-gry w examples/ albo dodawaniu funkcji do istniejącej gry na tym silniku.
---

# Gra 3D na silniku engine3d

Wzorcowa, kompletna gra: `examples/10_settlers` (RTS). Prostsze wzory: `02_rotating_cube` (minimum),
`08_game` (ustawienia grafiki), `06_terrain`, `07_shadows`, `05_gui_demo`. Zanim napiszesz coś od zera,
sprawdź, jak zrobił to `10_settlers` - większość problemów jest tam już rozwiązana.

## 1. Szkielet nowego przykładu

```
examples/NN_nazwa/
  CMakeLists.txt     add_executable(NN_nazwa_example main.cpp ...) + target_link_libraries(... PRIVATE engine3d)
  main.cpp           tylko main() + komentarz z mapą plików
  game_defs.h/.cpp   stałe i tabele balansu (koszty, czasy, HP) - w jednym miejscu
  game_world.h + world_*.cpp   SYMULACJA (bez wejścia, bez GUI)
  game_app.h   + app_*.cpp     PREZENTACJA: silnik, kamera, wejście, HUD, dźwięk, debug, testy
```

Dopisz `add_subdirectory(NN_nazwa)` w `examples/CMakeLists.txt`. Uruchamiaj z katalogu głównego repo (`./bin/...`).

Podział symulacja/prezentacja jest kluczowy: świat da się wtedy testować bez okna (`world.update(0.1f)` w pętli),
a AI i gracz używają tych samych rozkazów (`placeBuilding`, `commandMove`...). Świat zgłasza dźwięki i komunikaty
do kolejek (`soundEvents`, `messages`), aplikacja je odtwarza/rysuje - świat nie zależy od audio ani GUI.

## 2. Silnik, scena, encje

```cpp
Engine engine;
engine.initialize("engine3d: Gra", 1440, 900);
engine.onUpdate = [&](float dt, Engine& e) { /* logika klatki */ };
engine.run();

Scene* scene = engine.getSceneManager()->activeScene();     // albo własna scena przez SceneManager
Entity* e = scene->createEntity("unikalna_nazwa");            // nazwy encji MUSZĄ być unikalne (licznik)
auto* mc = e->addComponent<MeshComponent>("nazwa_siatki");
mc->materialHandle = material;
mc->castShadows = false;                                      // dla płaskich rzeczy na ziemi (drogi, pierścienie)
e->transform().position = {x, y, z};  e->transform().rotation.y = yaw;  e->transform().scale = {1,1,1};
e->setActive(false);                                          // ukrycie bez usuwania
scene->removeEntity(e);
Entity* sun = scene->createDirectionalLight("sun", dir, color, intensity);
```

Obiekty gry trzymaj w `std::vector` z flagą `alive` - indeks = identyfikator, martwych nie usuwaj
(odwołania "cel = jednostka 17" zostają poprawne). Encje 3D aktualizuj raz na krok w `syncVisuals`.

## 3. Siatki proceduralne (low-poly, kolor w wierzchołkach)

```cpp
MeshBuilder b;
b.box(center, size, color);  b.cylinder(base, r, h, segments, color);  b.cone(...);  b.sphere(...);
b.gableRoof(...);  b.pyramid(...);  b.ring(...);  b.disk(...);
b.polygon({p0, p1, p2, p3}, insidePoint, color);   // normalna skierowana OD insidePoint (płaski quad: inside = środek - up)
engine.getMeshManager()->loadMesh("nazwa", b.build());
```

- Wszystkie siatki twórz raz przy starcie (`buildGameMeshes`); warianty per frakcja/ładunek przez nazwę (`"b_house_0"`).
- Siatki zmieniane w trakcie gry (drogi, granice) ładuj pod NOWĄ unikalną nazwą przy każdej zmianie.
- Jeden materiał "solid" wystarcza dla modeli z kolorem w wierzchołkach; półprzezroczyste podglądy:
  `Material m; m.blendMode = BlendMode::Alpha; m.depthWrite = false; m.color = {r,g,b,a};`
  Po zmianie `materialHandle` encji wyzeruj `mc->materialInstance.reset()`.
- Obiekt przykryty terenem/rośnięcie budowy: skaluj Y encji, fundament schodzący pod ziemię maskuje nierówności.

## 4. Kamera i wskazywanie myszą

- `CameraComponent` z `autoAspect = true`; `scene->setActiveCamera(cam)`.
- `RtsCameraController`: `pan`, `rotate`, `tilt`, `zoom(mouseWheelF)`, `moveTo`, `setFocus`, `setDistance`.
- Picking: `screenPointToRay(*camera, mx, my, screenW, screenH)` → `Terrain::raycast`, `raySphere`, `rayPlaneY`.
  Pozycja 3D → ekran: `worldToScreen(...)` (paski życia, etykiety, minimapa).
- Promień z ukośnej kamery łatwo "zahacza" o dach budynku przed wskazanym miejscem - przy wyborze celu
  najpierw sprawdzaj obiekty przy punkcie terenu, a budynek tylko gdy punkt terenu leży przy nim.

## 5. Wejście

- Akcje zamiast kodów klawiszy: `engine.inputActions().bindKey("build.road", SDL_SCANCODE_T)`,
  potem `e.actionPressed("build.road")` / `actionDown` / `actionReleased`. Mysz: `e.mouseDown(0..2)`,
  `mousePressed`, `mouseReleased`; stan: `e.input()` (`mouseX/Y`, `mouseDeltaX/Y`, `mouseWheelF`, `keys[]`).
- Przed pierwszym ruchem mysz ma pozycję (0,0) - przewijanie krawędzią włączaj dopiero po ruchu.
- Kliknięcie nad GUI nie może trafiać w świat: `gui->isPointerOverGui()` + własne prostokąty HUD.
- ESC w trybie stawiania/rysowania anuluje tryb zamiast pauzować (zapamiętaj stan z początku klatki).

## 6. HUD i okna GUI

- HUD w trybie immediate (co klatkę od nowa): `engine.setTextLabel(text, font, x, y, scale, color, depth)`,
  `engine.measureText(text, scale)`, `gui->addOverlayRect/addOverlayFrame(x, y, w, h, ..., depth)`.
  Głębokość: mniejsza = bliżej. Okna GUI leżą w 0.1..0.9, HUD ~0.93-0.97. Elementy na tle panelu
  muszą mieć głębokość MNIEJSZĄ niż tło panelu, inaczej znikną pod nim.
- Okna: `gui = std::make_shared<GuiMgr>(); gui->setTextDrawFn(...); renderer->addFeature(gui);`
  `GuiWindow* w = gui->addWindow("Tytuł", x, y, w, h);` + `GuiButton/GuiCheckBox/GuiComboBox/GuiTrackBar/GuiStaticText`
  (nazwy widgetów unikalne). Okna twórz raz, tryby gry tylko je pokazują (`setVisible`) i ustawiają (`setPosition`
  w `layoutGui` po zmianie rozdzielczości). Kolor przycisku ustawiaj co klatkę (hover go nadpisuje).
- Rozmiar ekranu bierz z renderera co klatkę (fullscreen/zmiana okna).

## 7. Dźwięk

```cpp
AudioSystem* audio = engine.getAudio();
audio->loadSound("chop", "assets/sound/.../chop.ogg");   audio->setSoundLimits("chop", 4, 0.08f);
audio->play("click", vol);   audio->play3D("chop", worldPos, vol);   audio->setListener(camPos, camRight);
audio->playMusic(path, loop, fadeSeconds);   setMasterVolume / setMusicVolume / setSfxVolume
```

Formaty: WAV i OGG (stb_vorbis w silniku). Darmowe zasoby: Kenney (CC0), OpenGameArt (sprawdź licencję),
dopisz autorów do `CREDITS.md` obok plików.

## 8. Teren, nawigacja, cienie

- Teren z heightmapy w pamięci: `terrain->setHeightmap(data, size); setScale(xz, y, ...); buildMesh(); uploadTo(meshManager)`.
  Wysokość/normalna: `getHeight(x,z)`, `getNormal(x,z)`.
- `NavGrid`: `init`, `setStaticBlocked` (woda, strome zbocza), `addObstacle/removeObstacle` (zliczane!),
  `findPath(from, to, out, allowPartial, maxNodes)`, `lineWalkable`, `computeRegions` + `sameRegion`
  (szybki test "czy w ogóle da się dojść" zanim wyślesz jednostkę).
- Cienie: `engine.configureDirectionalShadow(halfSize, start, lambda)` dopasuj do widocznego zakresu kamery;
  kwantyzuj kierunek słońca (np. co 0,5°), inaczej cienie migają przy ruchu słońca.

## 9. Testowalność i weryfikacja (obowiązkowo przy każdej zmianie)

Wbuduj w grę tryby sterowane zmiennymi środowiskowymi (wzór: `app_tests.cpp` w `10_settlers`):
- `GRA_UITEST=1` - scenariusz klatka po klatce: wstrzykuje `SDL_Event` (ruch/klik myszy, klawisze) i sprawdza efekt
  (`uiCheck("opis", warunek)`), na końcu `koniec, błędów: N` i wyjście. Testy symulacji wołają `world.update(0.1f)` w pętli.
- `GRA_AUTOPLAY=<s>` + `GRA_SPEED=<x>` - AI gra za obie strony, log stanu co 30 s gry.
- `GRA_OPEN=<okno>`, `GRA_CAMERA=<odl>` - do zrzutów ekranu.
- Tryb debug (F3): ścieżki, stany, siatka kolizji; zrzut stanu (F9) do konsoli.

Procedura: build bez ostrzeżeń → test UI kilka razy (mapy losowe; kroki klikające w menu bywają niestabilne,
gdy ktoś rusza prawdziwą myszą) → autoplay → zrzut ekranu (`spectacle -a -b -n -o plik.png` przy grze w tle)
i obejrzenie go. Wyniki raportuj uczciwie (także niestabilne/nieodtworzone błędy).

## 10. Styl kodu dla tego repo

- Komentarze i teksty w grze po polsku, pisane dla osoby uczącej się: nad każdą funkcją 1-3 zdania "co i po co".
- Pliki do ~500-700 linii, podział tematyczny; nagłówek klasy z mapą plików implementacji.
- Liczby balansu w tabelach defs, nie rozsiane po kodzie.
- Po nowej funkcji: test w trybie UITEST, `README.md`, `changelog.md`, pomoc w grze (lista sterowania).
