# Budowanie, testy i wdrażanie

Pliki: `CMakeLists.txt`, `cmake/*`, `engine/CMakeLists.txt`,
`examples/*/CMakeLists.txt`, `tests/*`, `tools/build_release.sh`.

---

## 1. Struktura projektu

```text
CMakeLists.txt          — projekt, tryb budowania, testy
engine/                 — biblioteka libengine3d.so
examples/NN_nazwa/      — aplikacje (każda linkuje engine3d)
cmake/                  — ProjectSettings.cmake, ShaderBundles.cmake
assets/                 — shadery, tekstury, modele, dźwięki, mapy
tests/                  — testy jednostkowe (scene_hierarchy_tests)
tools/build_release.sh  — paczka dla innych (kontener Ubuntu)
bin/                    — wynik: libengine3d.so, przykłady, assets/shaders
```

`examples/` są budowane jako osobne binarki. Każda ma własny
`CMakeLists.txt` z `target_link_libraries(... PRIVATE engine3d)`.

---

## 2. Budowanie

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j11
```

Tryby:

* `Release` — `-O2`, walidacja Vulkan wyłączona, `-g1` (tablice linii dla
  raportu awarii),
* `Debug` — `-g`, bez optymalizacji, walidacja domyślnie włączona.

Wynik:

```text
bin/libengine3d.so
bin/01_rotating_triangle_example
bin/... (pozostałe)
bin/assets/shaders/<bundle>/*.spv
```

### Shadery

`cmake/ShaderBundles.cmake` kompiluje HLSL do SPIR-V:

* `dxc` — jeśli jest w `PATH` (preferowany),
* `glslangValidator` — fallback,
* `slangc` — dla plików `.slang`.

`add_shader_bundle(nazwa plik=źródło@entry ...)` tworzy target
`<nazwa>_shaders` i katalog `bin/assets/shaders/<nazwa>/`.

Zmiana pliku z `assets/shaders/shared/*.hlsl` lub `*_common.hlsl` przebudowuje
wszystkie shadery (CMake śledzi include jawnie, bo nie analizuje `#include`).

### Nowy przykład

```cmake
# examples/CMakeLists.txt
add_subdirectory(11_moja_gra)
```

```cmake
# examples/11_moja_gra/CMakeLists.txt
add_executable(my_game main.cpp)
target_link_libraries(my_game PRIVATE engine3d)
```

Nowy plik `.cpp` dopisz do `add_executable`. Uruchamiaj z katalogu głównego repo
(ścieżki `assets/` są względne).

---

## 3. `ENGINE3D_PORTABLE` i paczka

```bash
tools/build_release.sh
# wynik: dist/osadnicy-linux-x86_64.tar.gz
```

Skrypt:

* buduje w kontenerze **Ubuntu 25.04** (starsza glibc = działa na nowszych),
* `-DENGINE3D_PORTABLE=ON` — statyczne `libstdc++`/`libgcc` (u odbiorcy nie
  trzeba tej samej wersji kompilatora),
* `-DENGINE3D_OUTPUT_DIR=...` — osobny katalog wyjścia, nie miesza się z `bin/`,
* kopiuje `libengine3d.so` obok binarki (RUNPATH `$ORIGIN`),
* pakuje tylko assety używane przez grę (~40 MB) i skrypt `uruchom.sh`.

Biblioteka C (glibc) zostaje dynamiczna — binarka działa na systemie, na którym
ją zbudowano, i nowszych. Dlatego budujemy w starszym Ubuntu, a nie na
najnowszej Fedorze. Wymagane **glibc 2.38+**.

Wymagania u odbiorcy: sterownik Vulkan 1.4 (NVIDIA 550+, Mesa 25+) oraz SDL3,
SDL3_ttf i SDL3_image:

```bash
# Ubuntu 25.04+
sudo apt install libsdl3-0 libsdl3-ttf0 libsdl3-image0 libvulkan1 mesa-vulkan-drivers

# Fedora
sudo dnf install SDL3 SDL3_ttf SDL3_image vulkan-loader mesa-vulkan-drivers
```

Zawartość paczki:

```text
osadnicy/
  uruchom.sh
  bin/10_settlers_example
  bin/libengine3d.so
  bin/assets/shaders/
  assets/...
```

Uruchomienie u odbiorcy: `./uruchom.sh` (skrypt przechodzi do katalogu paczki,
bo ścieżki `assets/` są względne).

---

## 4. Testy

```bash
ctest --test-dir build --output-on-failure
# albo:
./build/tests/scene_hierarchy_tests
```

`tests/scene_hierarchy_tests.cpp` sprawdza m.in.:

* hierarchię parent/child i ochronę przed cyklami,
* polityki usuwania,
* sanityzację lokalnych wolumenów mgły.

Testy gry (`10_settlers`) są wbudowane w aplikację:

```bash
SETTLERS_UITEST=1 ./bin/10_settlers_example
# kończy się linią: koniec, błędów: N
```

---

## 5. Uruchamianie i zmienne środowiskowe

```bash
./bin/10_settlers_example
```

Zawsze z katalogu głównego repo. Najważniejsze zmienne:

| Zmienna | Działanie |
|---|---|
| `ENGINE_VALIDATION=1` | walidacja Vulkan (wolno, do debugowania renderera) |
| `ENGINE_VALIDATION=0` | wyłącza walidację nawet w Debug |
| `ENGINE_PROFILE=1` | profil klatki co 3 s (`[profil] ...`) |
| `ENGINE_CRASH_TEST=segv\|abort\|throw` | test raportu awarii |
| `SETTLERS_UITEST=1` | test UI + symulacji |
| `SETTLERS_UITEST_KEEP=1` | nie zamyka okna po teście |
| `SETTLERS_AUTOPLAY=<s>` | AI gra za obie strony |
| `SETTLERS_SPEED=<x>` | mnożnik czasu (1..24) |
| `SETTLERS_AILOG=1` | decyzje AI w logu |
| `SETTLERS_FLAGDUMP=1` | wypis flag na końcu autoplay |
| `SETTLERS_DEBUG=1\|2` | tryb debug od startu (jak F3) |
| `SETTLERS_OPEN=settings\|controls\|help\|demo\|economy\|game` | otwiera okno / startuje grę |
| `SETTLERS_PLACE=<n>` | tryb stawiania budynku |
| `SETTLERS_HOVERBAR=<n>` | kursor nad przyciskiem paska |
| `SETTLERS_CAMERA=<odl>` | odległość kamery |
| `SETTLERS_SHOWCASE=1` | wszystkie budynki obok siebie |
| `SETTLERS_SAVEAT=<s>` | zapis w danej sekundzie autoplay |
| `SETTLERS_OPEN=load` (+ `SETTLERS_LOAD=<plik>`) | start z zapisu |
| `SETTLERS_SEED=<n>` | stała mapa (powtarzalne testy) |
| `SETTLERS_AIFOG=0` | AI bez mgły wojny |
| `SETTLERS_NOFOG=1` | bez mgły wojny (zrzuty) |
| `SETTLERS_STRESS=<n>` | dodatkowi osadnicy (wydajność) |
| `SETTLERS_SELECT=barracks` | gotowe koszary zaznaczone |
| `GAME08_MAP=demo.map` | przykład 08: mapa |
| `GAME08_HOUR=12` | przykład 08: godzina |
| `GAME08_CAMY=900` | przykład 08: wysokość kamery |
| `GAME08_NOLIGHT=1` / `GAME08_NOFOG=1` | przykład 08: bez światła/mgły |

W trybach testowych ustawienia gracza nie są wczytywane ani zapisywane.

---

## 6. Debugowanie

### Walidacja

```bash
ENGINE_VALIDATION=1 ./bin/06_terrain_example 2>&1 | grep -i validation
```

Po zmianach w rendererze **zawsze** uruchom z walidacją i sprawdź log.
W Release walidacja jest wyłączona, bo zjadała ~90% czasu klatki na CPU.

### Profiler

```bash
ENGINE_PROFILE=1 ./bin/10_settlers_example
```

Czasy sekcji (`update`, `render.cienie`, `render.gbuffer`, `render.deferred`,
`render.submit`, ...). Najpierw znajdź wąskie gardło, potem optymalizuj.

### Raport awarii

Po crashu zajrzyj do `crash/<program>_<data>.log`. Zawiera miejsce awarii,
stos i ostatnie wpisy logu. Test: `ENGINE_CRASH_TEST=segv`.

### Build z asercjami

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -D_GLIBCXX_ASSERTIONS"
```

ASan + UBSan pomagają złapać wyjścia poza tablicę i UB. ThreadSanitizer nie
współpracuje ze sterownikiem NVIDIA (wewnętrzny błąd przy starcie).

---

## 7. Zrzuty ekranu

```bash
./bin/10_settlers_example &
sleep 5
spectacle -a -b -n -o /tmp/zrzut.png
```

`spectacle -a` łapie **aktywne** okno — jeśli w tle działa inna gra, zrzut może
pokazać nie to. Sprawdź czas/stan.

---

## 8. Zasady pracy z repo

* Komentarze w kodzie i komunikaty gry po polsku.
* Nowa funkcja gry = test w `app_tests.cpp`, wpis w `README.md`, `changelog.md`
  i pomoc w grze (`kControls`, `drawHelp`).
* Nie commituj bez wyraźnej prośby.
* Zasoby przydatne innym grom (GUI, ustawienia, szum, matematyka) trafiają do
  `engine/`, nie do przykładu.

---

## 9. Checklist przed commitem

1. `cmake --build build -j11` — bez błędów i nowych ostrzeżeń.
2. `SETTLERS_UITEST=1 ./bin/10_settlers_example` — kilka razy (mapy losowe).
3. `SETTLERS_AUTOPLAY=900 SETTLERS_SPEED=24` — AI kończy partię.
4. `ENGINE_VALIDATION=1` na przykładzie, którego dotyczy zmiana.
5. Jeśli zmieniałeś renderer — sprawdź `[profil]`, czy nie ma regresji.
6. Zaktualizuj dokumentację modułu, którego dotyczy zmiana.
