# Dokumentacja silnika engine3d

## Pliki dokumentacji

| Plik | Temat |
|------|-------|
| [01_architektura.md](01_architektura.md) | Struktura katalogów, przepływ danych, warstwy abstrakcji |
| [02_pipeline_vulkan.md](02_pipeline_vulkan.md) | Inicjalizacja Vulkan, swapchain, renderowanie klatki |
| [03_system_materialow.md](03_system_materialow.md) | Material, pipeline cache, shadery HLSL |
| [04_modele_3ds.md](04_modele_3ds.md) | Format 3DS, ładowanie, konwersja współrzędnych |
| [05_scena_komponenty.md](05_scena_komponenty.md) | Encje, komponenty, MeshManager, MaterialManager |
| [06_kamera_sterowanie.md](06_kamera_sterowanie.md) | Kamera, kontroler FPS, macierze |
| [07_czastki.md](07_czastki.md) | System cząstek, RecyclingArray, optymalizacje |
| [08_tekstury_fonty.md](08_tekstury_fonty.md) | Tekstury DDS/BMP/TGA, font atlas, TextOverlay |
| [09_rozwoj.md](09_rozwoj.md) | Możliwości rozbudowy i poprawy |

## Szybki start

### Kompilacja
```bash
mkdir build && cd build
cmake .. -G Ninja
ninja
```

### Uruchamianie przykładów
```bash
./bin/flyaround_example        # latanie po scenie z modelami 3DS
./bin/rotating_cube_example    # obracające się kostki
./bin/rotating_triangle_example # obracający się trójkąt
./bin/particles_example        # system cząstek (4 emitery)
```

### Sterowanie (flyaround)
| Klawisz | Akcja |
|---------|-------|
| W/S | Przód/tył |
| A/D | Lewo/prawo |
| Spacja/LCtrl | Góra/dół |
| Mysz | Rozglądanie |
| Escape | Wyjście |

## Dla kogo jest ten silnik

Dla osób, które:
- Znają podstawy C++ i chcą zrozumieć jak działa silnik 3D od środka
- Chcą nauczyć się Vulkan API na praktycznym przykładzie
- Szukają bazy do własnego projektu 3D

Silnik **nie jest** gotowym produktem komercyjnym — to baza edukacyjna
z udokumentowanym kodem pod kątem nauki.
