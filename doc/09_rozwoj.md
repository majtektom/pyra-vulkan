# Możliwości rozbudowy i poprawy silnika

## Renderowanie

### 1. Compute shadery dla cząstek
Przenieś fizykę cząstek na GPU. Zamiast aktualizować 75k cząstek na CPU,
użyj compute shadera z SSBO. Eliminuje to upload wierzchołków co klatkę.


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
