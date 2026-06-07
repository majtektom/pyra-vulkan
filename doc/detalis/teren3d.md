# System Terenu 3D (Terrain) - Szczegółowa Dokumentacja

> **Pliki źródłowe:** `engine/object/terrain/`

---

## 1. Architektura

System terenu składa się z następujących komponentów:

```
object/terrain/
├── terrain.h / terrain.cpp               # Główna klasa Terrain
├── terrain_index_generator.h / .cpp       # Generator indeksów LOD + stitching
├── terrain_heightmap_generator.h / .cpp   # Algorytmy proceduralne
├── terrain_component.h / .cpp             # Komponent ECS
├── terrain_system.h / .cpp                # System ECS (aktualizacja LOD)
├── terrain_placement_system.h / .cpp      # System ECS (przyklejanie do terenu)
└── terrain_placement_component.h          # Komponent placementu
```

### Zasada Pojedynczej Odpowiedzialności (SRP)

Po refaktoryzacji każda klasa ma jeden powód do zmiany:

| Klasa | Odpowiedzialność |
|-------|-----------------|
| `Terrain` | Przechowywanie danych terenu, koordynacja budowy drzewa, dostarczanie wysokości |
| `TerrainIndexGenerator` | Generowanie buforów indeksowych LOD z algorytmem zszywania krawędzi |
| `TerrainHeightmapGenerator` | Implementacja algorytmów proceduralnego generowania terenu |
| `TerrainComponent` | Przechowywanie danych materiałowych i delegowanie renderowania |
| `TerrainSystem` | Aktualizacja LOD co klatkę na podstawie pozycji kamery |
| `TerrainPlacementSystem` | Automatyczne umieszczanie obiektów na powierzchni terenu |

---

## 2. Struktura Danych: Drzewo Czwórkowe (Quadtree)

### Węzeł (Node)

```cpp
struct Node {
    float cx, cz;          // Środek węzła w przestrzeni świata
    float sizeXZ;          // Długość boku kwadratu węzła
    float minY, maxY;      // Min/max wysokość w węźle
    int lodLevel;          // Aktualny poziom LOD (0-4)
    Node* children[4];     // Dzieci: [0]=lewo-góra, [1]=prawo-góra, [2]=lewo-dół, [3]=prawo-dół
    Node* parent;          // Rodzic
    Node* neighbor[4];     // Sąsiedzi: [0]=lewo, [1]=góra, [2]=prawo, [3]=dół
    bool visible;          // Czy widoczny w frustum kamery
    int meshId;            // Unikalny ID siatki GPU
};
```

### Proces budowy drzewa

1. **Korzeń** jest tworzony z `sizeXZ = halfWorld` (połowa całego terenu).
2. `buildTree()` rekurencyjnie dzieli każdy węzeł na 4 dzieci, dopóki:
   - Rozmiar węzła w pikselach heightmapy jest mniejszy niż `gridRes_` (16), lub
   - Osiągnięto maksymalną liczbę poziomów (`maxLodLevels_`, domyślnie 5).
3. Dla każdego dziecka obliczane jest `minY` i `maxY` przez iterację po heightmapie.
4. `findNeighbors()` znajduje sąsiadów każdego węzła - węzły tego samego rozmiaru na czterech krawędziach.

### Węzły liściowe

Tylko węzły liściowe (bez dzieci) zawierają siatkę geometryczną:
- Rozdzielczość: 17×17 wierzchołków (gridRes_ = 16 komórek)
- Każdy liść ma własny bufor wierzchołków w GPU
- Bufory indeksowe są **współdzielone** między wszystkimi liśćmi

### Statyczna struktura

Drzewo czwórkowe jest **statyczne** - nie zmienia się podczas działania programu. Zmieniają się tylko wartości `lodLevel` i `visible` w węzłach. Dzięki temu nie ma potrzeby przebudowywania drzewa co klatkę.

---

## 3. System LOD (Level of Detail)

### Poziomy szczegółowości

Każdy węzeł może być renderowany na 5 poziomach LOD:

| LOD | skok (próbkowanie) | Trójkątów (bez stitchingu) | Opis |
|-----|-------------------|---------------------------|------|
| 0 | 1 (każdy wierzchołek) | 512 | Pełna rozdzielczość |
| 1 | 2 (co drugi) | 128 | Połowa rozdzielczości |
| 2 | 4 (co czwarty) | 32 | Ćwierć rozdzielczości |
| 3 | 8 (co ósmy) | 8 | 1/8 rozdzielczości |
| 4 | 16 (co szesnasty) | 2 | Minimalna rozdzielczość |

### Algorytm wyboru LOD

Dla każdego liścia, co klatkę:

```
1. Oblicz odległość od kamery:
   dist = |centerNode - cameraPos|

2. Oblicz nachylenie terenu w węźle:
   slope = (maxY - minY) / (sizeXZ + 0.1)

3. Wyznacz mnożnik (uwzględnia stromiznę):
   multiplier = min(1.0 + slope * 2.0, 4.0)

4. Wybierz LOD (iterując od 0 do maxLodLevels-1):
   Dla każdego poziomu pp:
     threshold = (sizeXZ * 2 * (pp+1) + sizeXZ * 2) * multiplier
     Jeśli dist < threshold: zatrzymaj na LOD = pp
     W przeciwnym razie: LOD = pp + 1 (jeśli nie przekroczono limitu)
```

**Efekt mnożnika stromizny:**
- Płaski teren (slope ≈ 0): multiplier ≈ 1.0 → szybciej przechodzi na niższy LOD
- Stromy/górzysty teren (slope > 1): multiplier do 4.0 → dłużej zachowuje wysoki LOD
- Maksymalny mnożnik to 4.0, więc nawet najstromszy teren nie zachowa LOD 0 na nieskończoną odległość

### Ograniczenie sąsiedztwa

Po wyznaczeniu LOD dla każdego węzła, system wyrównuje LOD między sąsiadami:

```
Różnica LOD między sąsiadami ≤ 1

Jeśli node.lodLevel > neighbor.lodLevel + 1:
    node.lodLevel = neighbor.lodLevel + 1
```

Algorytm iteruje aż do osiągnięcia stanu stabilnego (brak zmian).

**Dlaczego to ważne:** Zapobiega sytuacji, gdzie węzeł z LOD 4 sąsiaduje z węzłem LOD 0, co wymagałoby ekstremalnie skomplikowanego zszywania.

---

## 4. Zszywanie Krawędzi (Stitching)

### Problem T-Junction

Gdy dwa sąsiednie węzły mają różne LOD, na ich granicy pojawia się problem:
- Węzeł o wyższym LOD (mniej trójkątów) ma mniej wierzchołków na krawędzi
- Węzeł o niższym LOD (więcej trójkątów) ma więcej wierzchołków na krawędzi
- Bez zszywania, dodatkowe wierzchołki szczegółowego węzła nie są połączone z krawędzią sąsiada → **szczeliny**

### Rozwiązanie

Dla węzła o **niższym** LOD (wyższa rozdzielczość), który sąsiaduje z węzłem o wyższym LOD:
- Na krawędzi styku, zamiast renderować wszystkie trójkąty, renderowane są specjalne trójkąty zszywające
- Trójkąty te pomijają co drugi wierzchołek na krawędzi, dopasowując się do rzadszej siatki sąsiada
- Wzór trójkątów przeplata się (toggle): raz pełny trójkąt, raz trójkąt zszywający

### 16 konfiguracji zszywania

Konfiguracja (0-15) określa, które krawędzie wymagają zszycia:

| Konfiguracja | Krawędzie |
|-------------|-----------|
| 0 | Brak |
| 1 | Lewa |
| 2 | Prawa |
| 3 | Dolna |
| 4 | Górna |
| 5 | Lewa + Prawa |
| 6 | Dolna + Górna |
| 7 | Lewa + Dolna |
| 8 | Dolna + Prawa |
| 9 | Prawa + Górna |
| 10 | Górna + Lewa |
| 11 | Lewa + Dolna + Prawa |
| 12 | Dolna + Prawa + Górna |
| 13 | Prawa + Górna + Lewa |
| 14 | Górna + Lewa + Dolna |
| 15 | Wszystkie cztery |

### Implementacja

Generowanie indeksów odbywa się w `TerrainIndexGenerator`. Dla każdej konfiguracji, algorytm:

1. Iteruje po komórkach siatki z krokiem `skok = 2^lodLevel`
2. Dla każdej komórki na krawędzi wymagającej zszycia:
   - Generuje 9 indeksów (3 trójkąty zszywające) zamiast 6 (2 standardowe trójkąty)
   - Przeplata wzór (toggle), aby uniknąć nakładania się trójkątów
3. Dla komórek wewnętrznych generuje standardowe 2 trójkąty

Bufory indeksowe są pre-generowane podczas `uploadTo()` - 80 buforów (5 LOD × 16 konfiguracji), współdzielonych między wszystkimi węzłami.

---

## 5. Widoczność i Frustum Culling

### Algorytm

Dla każdego węzła (rekurencyjnie od korzenia):

```
1. Sprawdź odległość:
   Jeśli dist > farPlane + radius → niewidoczny

2. Sprawdź tył kamery (back-face culling):
   Jeśli dot(toNode, camForward) < -radius → niewidoczny

3. Jeśli widoczny i ma dzieci → sprawdź dzieci rekurencyjnie
4. Jeśli widoczny i jest liściem → dodaj do visibleNodes_
```

Gdzie `radius = sizeXZ * 1.5` (sfera otaczająca węzeł).

---

## 6. Potok Renderowania

### Inicjalizacja (jednorazowo)

```
loadFromImage() / loadFromJson()
  → wczytaj heightmapę
  → buildTree() - zbuduj drzewo czwórkowe
  → findNeighbors() - znajdź sąsiadów

uploadTo(meshManager)
  → assignNodeIds() - przydziel ID węzłom
  → uploadNodeMeshes() - dla każdego liścia:
      generuj 17×17 wierzchołków
      oblicz normalne
      wyślij bufor wierzchołków do GPU
  → Dla 5 LOD × 16 konfiguracji:
      TerrainIndexGenerator::generate()
      wyślij bufor indeksowy do GPU
```

### Aktualizacja co klatkę

```
TerrainSystem::update()
  → Terrain::update(camera)
    → updateLeafLods() - oblicz LOD dla każdego liścia
    → wyrównaj LOD sąsiadów (relaksacja)
    → updateNodes() - sprawdź widoczność
```

### Renderowanie

```
TerrainComponent::collectRenderPackets()
  → Dla każdego visibleNodes_:
      pobierz bufor wierzchołków węzła z GPU
      TerrainIndexGenerator::computeStitchConfig() - określ konfigurację zszywania
      pobierz bufor indeksowy (LOD + stitchConfig) z GPU
      utwórz RenderPacket
```

---

## 7. Generowanie Proceduralne

Wszystkie algorytmy są zaimplementowane w `TerrainHeightmapGenerator` jako metody statyczne.

### Fault Formation (Uskoki tektoniczne)

**Zasada:** Wielokrotne przesuwanie wysokości wzdłuż losowych linii podziału.

**Parametry:**
- `size`: rozmiar mapy (np. 256)
- `iterations`: liczba iteracji (np. 200)
- `maxDelta`: maks. zmiana wysokości na początku (np. 50)
- `minDelta`: min. zmiana wysokości na końcu (np. 2)
- `itersPerFilter`: co ile iteracji filtrować (np. 50)
- `filter`: współczynnik filtru (np. 0.5)

**Proces:**
1. Start: płaska powierzchnia (wszystkie piksele = 10)
2. Dla każdej iteracji:
   - Losuj linię podziału (2 punkty)
   - Podnieś teren po jednej stronie linii
   - Opcjonalnie wygładź krawędzie mapy
3. Normalizuj wysokości do zakresu 0-300

**Wynik:** Kanciasty teren z wyraźnymi grzbietami i uskokami.

### Plasma / Midpoint Displacement (Diamond-Square)

**Zasada:** Rekurencyjne dzielenie kwadratów z losowymi przemieszczeniami punktów środkowych.

**Parametry:**
- `size`: rozmiar mapy (np. 256, musi być potęgą 2)
- `roughness`: chropowatość (0-1)

**Proces:**
1. Diamond step: Oblicz środek kwadratu = średnia narożników + losowe przemieszczenie
2. Square step: Oblicz punkty na krawędziach = średnia sąsiadów + losowe przemieszczenie
3. Zmniejsz rozmiar kwadratu o połowę
4. Zmniejsz amplitudę przemieszczeń: `dh *= 2^(-roughness)`
5. Powtarzaj aż rozmiar kwadratu = 0

**Wynik:** Fraktalny, naturalnie wyglądający teren. Wyższa chropowatość = więcej szczegółów.

### Particle Deposition (Osadzanie cząstek)

**Zasada:** Symulacja osadzania się cząstek (np. lawy wulkanicznej).

**Parametry:**
- `size`: rozmiar mapy (np. 256)
- `jumps`: liczba serii cząstek
- `peakWalk`: kroki wędrówki na szczyt
- `minPart`/`maxPart`: zakres liczby cząstek na serię
- `caldera`: współczynnik kaldery (0-1)

**Proces:**
1. Start: płaska powierzchnia
2. Dla każdej serii:
   - Losuj punkt startowy
   - Wygeneruj N cząstek
   - Każda cząstka wykonuje losowy spacer, zwiększając wysokość
   - Po spacerze cząstka "wędruje na szczyt" (peakWalk)
3. Opcjonalnie utwórz kalderę (zapadnięty środek wulkanu)

**Wynik:** Teren przypominający wulkany tarczowe z centralnymi zagłębieniami.

---

## 8. Zapytania o Wysokość (getHeight)

```cpp
float Terrain::getHeight(float worldX, float worldZ) const
```

**Algorytm (interpolacja dwuliniowa):**

1. Przelicz współrzędne świata na indeksy heightmapy:
   ```
   gridX = worldX / scaleXZ + hmSize / 2
   gridZ = worldZ / scaleXZ + hmSize / 2
   ```

2. Wyznacz 4 najbliższe piksele: `(ix,iz)`, `(ix+1,iz)`, `(ix,iz+1)`, `(ix+1,iz+1)`

3. Interpoluj dwuliniowo:
   ```
   h_top = h00 * (1-fx) + h10 * fx
   h_bottom = h01 * (1-fx) + h11 * fx
   height = (h_top * (1-fz) + h_bottom * fz) * scaleY
   ```

**Zastosowanie:**
- Fizyka i kolizje
- Umieszczanie obiektów na terenie (TerrainPlacementSystem)
- AI i pathfinding

---

## 9. System ECS

### TerrainSystem

```cpp
void TerrainSystem::update(Scene& scene, float deltaTime)
```

Co klatkę:
1. Pobiera aktywną kamerę ze sceny
2. Dla każdej encji z `TerrainComponent`:
   - Wywołuje `terrain->update(camera)` - aktualizuje LOD i widoczność

### TerrainPlacementSystem

```cpp
void TerrainPlacementSystem::update(Scene& scene, float deltaTime)
```

Co klatkę:
1. Znajduje pierwszy `TerrainComponent` na scenie
2. Dla każdej encji z `TerrainPlacementComponent`:
   - Ustawia `transform.position.y = terrain->getHeight(x, z) + tpc->yOffset`

Dzięki temu obiekty (np. drzewa, kamienie) automatycznie "przyklejają się" do powierzchni terenu.

---

## 10. Integracja - Przykład Użycia

```cpp
// 1. Tworzenie terenu z pliku JSON
auto terrain = std::make_shared<Terrain>();
terrain->loadFromJson("assets/teren/przyklad/terrain.json");

// 2. Wysłanie siatki do GPU
terrain->uploadTo(*meshManager);

// 3. Dodanie do sceny jako komponent
auto entity = scene.createEntity("Terrain");
auto* tc = entity->addComponent<TerrainComponent>(terrain);
tc->castShadows = true;

// 4. Dodanie systemów do sceny
scene.addSystem(std::make_unique<TerrainSystem>(*meshManager, *matMgr, *matSys));
scene.addSystem(std::make_unique<TerrainPlacementSystem>());

// 5. Renderowanie (automatyczne przez TerrainComponent::collectRenderPackets)

// 6. Pobranie wysokości
float h = terrain->getHeight(playerX, playerZ);
player.position.y = h;
```

---

## 11. Optymalizacje

### Zastosowane techniki

| Technika | Opis |
|----------|------|
| **Quadtree LOD** | Redukcja trójkątów dalekich węzłów (do 256× mniej trójkątów) |
| **Frustum culling** | Nie renderuje węzłów poza ekranem |
| **Back-face culling** | Nie renderuje węzłów za kamerą |
| **Współdzielone bufory indeksowe** | 80 buforów dla wszystkich węzłów, nie per-węzeł |
| **Statyczne drzewo** | Brak alokacji/dealokacji co klatkę |
| **Pre-generowane indeksy** | Indeksy LOD liczone raz podczas uploadTo() |

### Potencjalne dalsze optymalizacje

1. **Horizon culling** - nie renderuj węzłów zasłoniętych przez horyzont
2. **Geometry clipmapping** - alternatywny algorytm LOD lepszy dla bardzo dużych terenów
3. **GPU-driven LOD selection** - przenieś wybór LOD na GPU (compute shader)
4. **Morphing LOD** - płynne przejścia między poziomami LOD zamiast skokowych

---

## 12. Znane Problemy i Ograniczenia

1. **Parser JSON** jest uproszczony i nie obsługuje zagnieżdżonych struktur ani escape'owanych znaków.
2. **Frustum culling** używa testu sfery, nie właściwego testu AABB - może powodować false positives (renderowanie niewidocznych węzłów), ale nigdy false negatives.
3. **Brak smooth LOD transitions** - przejścia między LOD są skokowe (pop), brak geomorphingu.
4. **Wysokość poza granicami** mapy zwraca 0 - może powodować "przyklejanie" obiektów do zera przy krawędziach.
