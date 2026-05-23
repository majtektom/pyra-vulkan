# Możliwości rozbudowy i poprawy silnika

## Renderowanie

### 1. Compute shadery dla cząstek
Przenieś fizykę cząstek na GPU. Zamiast aktualizować 75k cząstek na CPU,
użyj compute shadera z SSBO. Eliminuje to upload wierzchołków co klatkę.

### 2. Instanced rendering
Zamiast generować 4 wierzchołki na każdą cząstkę, użyj jednego quada +
per-instance data (pozycja, skala, kolor). `vkCmdDrawIndexed(4, particleCount, ...)`.
Redukuje zużycie pamięci i transfer CPU→GPU.

### 3. Deferred rendering / wiele render passów
Rozdziel geometrię od oświetlenia:
- Pass 1 (G-buffer): pozycje, normalne, albedo
- Pass 2 (lighting): oblicz oświetlenie per-piksel

### 4. Cienie (shadow mapping)
Renderuj scenę z perspektywy światła do tekstury głębi, potem porównuj
w głównym shaderze. Prosty shadow mapping to ~200 linii kodu.

### 5. Post-processing
- Bloom (poświata) — rozmycie jasnych obszarów
- SSAO (ambient occlusion) — cieniowanie w zagłębieniach
- Tone mapping — HDR → LDR

### 6. Mipmapy na GPU dla wszystkich formatów
Obecnie tylko BMP/TGA dostają mipmapy. DDS ich nie potrzebują (są w pliku).
Można ujednolicić: zawsze generuj mipmapy jeśli `mipLevels == 1`.

---

## System plików i zasobów

### 7. Wirtualny system plików (VPK)
Spakuj assety do jednego pliku (jak `.pak` w Quake'u). Łatwiejsza dystrybucja,
szybsze ładowanie (jeden odczyt zamiast wielu).

### 8. Hot-reloading shaderów
Wykrywaj zmiany w plikach `.hlsl` i rekompiluj w locie. Ogromnie przyspiesza
iterację podczas pisania shaderów.

### 9. Asynchroniczne ładowanie
Ładuj tekstury i modele w tle (osobny wątek). Pokazuj placeholder podczas ładowania.

---

## Scena i architektura

### 10. Pełny ECS (Entity-Component-System)
Obecnie komponenty same się aktualizują. Wprowadź "Systemy", które iterują
po encjach z określonymi komponentami:
```cpp
class PhysicsSystem { void update(vector<Entity*>& entities, float dt); };
class RenderSystem  { void render(vector<Entity*>& entities); };
```

### 11. Sceny i serializacja
Format `.scene` (JSON lub binary) do zapisu/odczytu całej sceny.
Edycja w zewnętrznym edytorze lub własnym.

### 12. Rodzicielstwo encji (transform hierarchy)
Encje powinny móc mieć rodzica. Transformacja dziecka = rodzic * dziecko.
Podstawa do szkieletów, broni przyczepionych do postaci itd.

---

## Fizyka

### 13. Prosty system kolizji
- AABB (bounding box) — szybkie testy kolizji
- Sphere collider dla pocisków
- Ray casting dla strzałów

### 14. Detekcja kolizji z terenem
Terrain heightmap + ray cast do chodzenia po terenie.

---

## Audio

### 15. OpenAL lub miniaudio
Dodaj dźwięk 3D — strzały, eksplozje, ambient. Biblioteka `miniaudio` jest
jednoplikowa i obsługuje wszystkie platformy.

---

## Animacja

### 16. Animacja szkieletowa (MD5)
Loader MD5 już istnieje w kodzie (`model_md5.cpp`) ale nie jest podłączony.
Szkielet + animacje kanałów to podstawa postaci.

### 17. Morph target animation
Prostsza alternatywa dla szkieletu — przechowuj kilka wersji mesh i interpoluj.

---

## Narzędzia

### 18. Własny edytor scen
Użyj ImGui do zbudowania edytora w silniku: dodawanie/usuwanie obiektów,
edycja materiałów, podgląd shaderów, profilowanie FPS.

### 19. Profiler GPU
Znaczniki czasowe (`vkCmdWriteTimestamp`) do mierzenia czasu GPU per-pass.
Wyświetlaj w UI.

---

## Stabilność i jakość kodu

### 20. Testy jednostkowe
- Testy loadera 3DS (znane pliki → oczekiwane wierzchołki)
- Testy systemu cząstek
- Testy matematyki (macierze, wektory)

### 21. CI/CD
GitHub Actions do automatycznego budowania i testowania na każdym pushu.

### 22. Alokator Vulkan (VMA)
Użyj `VulkanMemoryAllocator` zamiast ręcznego `vkAllocateMemory`.
Redukuje fragmentację i przyspiesza alokację.

### 23. Dynamiczna rekompilacja shaderów
Użyj `glslang` lub `shaderc` w runtime zamiast prekompilowanych SPIR-V.
Hot-reload bez restartu aplikacji.

---

## Wieloplatformowość

### 24. Abstrakcja okna (SDL2 / GLFW)
Zamiast bezpośredniego X11, użyj biblioteki okienkowej. Automatycznie działa
na Windows, Linux, macOS.

### 25. Metal i DirectX backend
Dodaj backendy dla innych API graficznych. Abstrakcyjny interfejs `IRenderer`
pozwala podmieniać implementację.

---

## Priorytety rozwoju

| Priorytet | Funkcja | Wpływ |
|-----------|---------|-------|
| 🔴 Wysoki | Cienie (shadow mapping) | Ogromna poprawa wizualna |
| 🔴 Wysoki | Instanced rendering dla cząstek | Duży wzrost FPS |
| 🟡 Średni | Animacja szkieletowa | Postacie, przeciwnicy |
| 🟡 Średni | Audio 3D | Immersja |
| 🟡 Średni | Wieloplatformowość (SDL2) | Więcej użytkowników |
| 🟢 Niski | Edytor ImGui | Szybszy development |
| 🟢 Niski | Hot-reload shaderów | Szybsze iterowanie |
