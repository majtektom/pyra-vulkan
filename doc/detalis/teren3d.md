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
    int lodLevel;          // Aktualny poziom LOD (0-5)
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
   - Rozmiar węzła w pikselach heightmapy jest mniejszy niż 16, lub
   - Osiągnięto maksymalną liczbę poziomów (`maxLodLevels_`, domyślnie 6).
3. Dla każdego dziecka obliczane jest `minY` i `maxY` przez iterację po heightmapie.
4. `findNeighbors()` znajduje sąsiadów każdego węzła - węzły tego samego rozmiaru na czterech krawędziach.

### Węzły liściowe

Tylko węzły liściowe (bez dzieci) zawierają siatkę geometryczną:
- Rozdzielczość: 33×33 wierzchołków (gridRes_ = 32 komórki) z interpolacją biliniową heightmapy
- Każdy liść ma własny bufor wierzchołków w GPU
- Bufory indeksowe są **współdzielone** między wszystkimi liśćmi

### Statyczna struktura

Drzewo czwórkowe jest **statyczne** - nie zmienia się podczas działania programu. Zmieniają się tylko wartości `lodLevel` i `visible` w węzłach. Dzięki temu nie ma potrzeby przebudowywania drzewa co klatkę.

---

## 3. System LOD (Level of Detail)

### Poziomy szczegółowości

Każdy węzeł może być renderowany na 6 poziomach LOD:

| LOD | skok (próbkowanie) | Trójkątów (bez stitchingu) | Opis |
|-----|-------------------|---------------------------|------|
| 0 | 1 (każdy wierzchołek) | 2048 | Teselacja CPU (2x gęściej niż heightmapa) |
| 1 | 2 (co drugi) | 512 | Pełna rozdzielczość heightmapy |
| 2 | 4 (co czwarty) | 128 | Połowa rozdzielczości heightmapy |
| 3 | 8 (co ósmy) | 32 | Ćwierć rozdzielczości heightmapy |
| 4 | 16 (co szesnasty) | 8 | 1/8 rozdzielczości heightmapy |
| 5 | 32 (co trzydziesty drugi) | 2 | Minimalna rozdzielczość |

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

Bufory indeksowe są pre-generowane podczas `uploadTo()` - 96 buforów (6 LOD × 16 konfiguracji), współdzielonych między wszystkimi węzłami.

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
      generuj 33×33 wierzchołków
      oblicz normalne
      wyślij bufor wierzchołków do GPU
  → Dla 6 LOD × 16 konfiguracji:
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

### Renderowanie z obsługą teselacji GPU

```
TerrainComponent::collectRenderPackets()
  → materiał instancja rozwiązywany w RenderExtractionSystem::resolveMaterialIfNeeded()
  → Dla każdego visibleNodes_:
      pobierz bufor wierzchołków węzła z GPU
      TerrainIndexGenerator::computeStitchConfig() - określ konfigurację zszywania
      pobierz bufor indeksowy (LOD + stitchConfig) z GPU
      CPU LOD = (tessellationActive && lodLevel > 0) ? lodLevel - 1 : lodLevel
      utwórz RenderPacket z tessellationFactor
      → tessellationFactor = tessellationLevel / (1 << lodLevel), clamped ≥ 1.0
      → Gdy tessellation wyłączona: tessellationFactor = 1.0, CPU LOD = lodLevel
  → Wszystkie pakiety terenu mają forwardOpaquePass = true (renderowane w OpaquePass)
```

### Shader terrain (assets/shaders/terrain/terrain.hlsl)

Shader terenu występuje w dwóch wariantach wybieranych automatycznie przez `MaterialCompiler`:

| Wariant | Vertex Shader | Tessellation | Warunek |
|---------|--------------|--------------|---------|
| **Z teselacją** | `vertexMain` (zwykły pass-through do hull shadera) | Hull + Domain shader (`triangle_cw`) | `tessellationEnabled_ = true` i `isTessellationSupported()` |
| **Bez teselacji** | `vertexMainNoTess` (pełny vertex shader z projekcją) | Brak | gdytessellation nie jest wspierane lub wyłączone |

**Domain shader** (`domainMain`) wykonuje:
1. Interpolację barycentryczną pozycji, normalnej i UV z 3 punktów kontrolnych
2. Heightmap displacement: `SampleLevel(heightmap, uv, 0)` — stały mip level 0 (pełna rozdzielczość)
3. Obliczenie normalnej przez różnice centralne
4. Transformację world→view→proj

**Winding order:**
- Indeksy wierzchołków są generowane jako **CW** (clockwise)
- Projekcja macierzy `proj` ma Y-flip (`-inverseTanHalfFov` w `math.h`)
- Hull shader: `[outputtopology("triangle_cw")]` — teselator generuje CW
- Efekt: CW world → Y-flip → CCW NDC = front face przy `VK_FRONT_FACE_COUNTER_CLOCKWISE`

---

## 7. GPU Tessellation — szczegóły

### Hull Shader Constants

Stałe teselacji są obliczane w `ConstantsHS()` (funkcja stałych przedziału):

```
gl_TessLevelOuter[0..2] = tessellationFactor  (każda krawędź)
gl_TessLevelInner[0]    = tessellationFactor  (wnętrze trójkąta)
```

Gdzie `tessellationFactor` pochodzi z push constant ustawianego w `Terrain::collectRenderPackets`.

### Poziom teselacji

```
tessFactor = (lodLevel == 0) ? tessellationLevel : tessellationLevel * 0.5f
```
(wcześniejsza wersja)

```
tessFactor = tessellationLevel / (1 << lodLevel), clamped ≥ 1.0
```
(obecna wersja — dystansowa redukcja)

Przykład dla `tessellationLevel = 4.0`:

| LOD | tessFactor | Opis |
|-----|-----------|------|
| 0 | 4.0 | Wysoka gęstość blisko kamery |
| 1 | 2.0 | Średnia gęstość |
| 2 | 1.0 | Brak subpodziału (teselator przepuszcza bez zmian) |
| 3+ | 1.0 | Brak subpodziału daleko |

Gdy `tessellationActive` i LOD > 0, CPU LOD jest redukowane o 1, aby GPU mogło odtworzyć szczegóły przez teselację. Zmniejszenie tessFactor dla wyższych LOD zapobiega powstawaniu zbyt małych trójkątów w oddali (sub-pixel triangles), które powodują aliasing i migotanie tekstur.

### API Engine

```cpp
// engine/core/engine.h
void setTessellationEnabled(bool enabled);
bool isTessellationEnabled() const;
bool isTessellationSupported() const;
void setTessellationLevel(float level);    // domyślnie 4.0
float getTessellationLevel() const;
```

Przepływ wartości: `Engine → Renderer → RenderExtractionSystem → TerrainComponent::collectRenderPackets(…, float tessellationLevel) → Terrain::collectRenderPackets(…, float tessellationLevel)`

---

## 8. Teksturowanie i Filtrowanie

### Warstwy tekstur (Texture Splatting)

Terrain obsługuje do 12 warstw tekstur nakładanych przez splat mapy (maski wagowe). Każda warstwa to:
- Tekstura bazowa (diffuse/albedo)
- Opcjonalna maska wagowa (splat map)
- Wszystkie warstwy mają wspólne UV bazujące na pozycji świata: `texCoord = localPosXZ / worldSize`

### Mipmapy

- **Tekstury warstw** (ładowane przez `Renderer::loadTexture`): generowany pełny łańcuch mipmap przez `vkCmdBlitImage` (trilinear filtering)
- **Heightmapa** (ładowana przez `Renderer::loadTextureFromMemory`): tylko 1 mip level, sampling zawsze `SampleLevel(..., 0)` — celowo, displacement wymaga pełnej rozdzielczości

### TextureFilter — API zmiany filtrowania

```cpp
// engine/core/types.h
enum class TextureFilter {
    Nearest,        // VK_FILTER_NEAREST + VK_SAMPLER_MIPMAP_MODE_NEAREST
    Linear,         // VK_FILTER_LINEAR + VK_SAMPLER_MIPMAP_MODE_NEAREST
    Trilinear,      // VK_FILTER_LINEAR + VK_SAMPLER_MIPMAP_MODE_LINEAR
    Anisotropic2x,  // jw. + anisotropy 2x
    Anisotropic4x,  // jw. + anisotropy 4x (domyślnie)
    Anisotropic8x,  // jw. + anisotropy 8x
    Anisotropic16x, // jw. + anisotropy 16x
};
```

```cpp
// engine/core/engine.h
void setTextureFilter(TextureFilter filter);
TextureFilter getTextureFilter() const;
```

**Domyślnie:** `TextureFilter::Anisotropic4x`.

**Działanie:**
1. `setTextureFilter(filter)` → `Renderer::setTextureFilter(filter)`
2. Renderer czeka `waitIdle()` na zakończenie wszystkich operacji GPU
3. Iteruje wszystkie tekstury w bindless array
4. Dla każdej: niszczy stary VkSampler, tworzy nowy z wybranym filtrem + zachowanym `AddressMode` i liczbą mip leveli
5. Aktualizuje deskryptory bindless (dla wszystkich ramek w locie)

**Przykład:**
```cpp
engine.setTextureFilter(TextureFilter::Anisotropic16x);
```

### Sampler per-tekstura

Każda tekstura w bindless array (`VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`) ma swój własny sampler. Shaderowy `SamplerState globalSampler : register(s1, space0)` jest scalany z tablicą tekstur przez DXC — każdy wpis `textures[index]` używa własnego samplera.

---

## 9. Generowanie Proceduralne

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

## 10. Zapytania o Wysokość (getHeight)

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

## 11. System ECS

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

## 12. Integracja — Przykład Użycia

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

// 7. Konfiguracja teselacji i filtrowania
engine.setTessellationEnabled(true);
engine.setTessellationLevel(6.0f);  // wyższa gęstość
engine.setTextureFilter(TextureFilter::Anisotropic16x);
```

---

## 13. Optymalizacje

### Zastosowane techniki

| Technika | Opis |
|----------|------|
| **Quadtree LOD** | Redukcja trójkątów dalekich węzłów (do 256× mniej trójkątów) |
| **Frustum culling** | Nie renderuje węzłów poza ekranem |
| **Back-face culling** | Nie renderuje węzłów za kamerą |
| **Współdzielone bufory indeksowe** | 96 buforów dla wszystkich węzłów, nie per-węzeł |
| **Statyczne drzewo** | Brak alokacji/dealokacji co klatkę |
| **Pre-generowane indeksy** | Indeksy LOD liczone raz podczas uploadTo() |
| **GPU Tessellation** | Subpodział trójkątów na GPU zamiast CPU (redukcja CPU LOD o 1) |
| **Dystansowa redukcja lodFactor** | tessellationLevel / (1 << lodLevel) — mniej teselacji daleko |
| **Depth bias shadow** | `depthBiasConstantFactor=1.0f, slopeFactor=1.0f` — redukcja shadow acne przy CULL_MODE_NONE |
| **Anisotropic filtering 4x** | Domyślnie włączone dla wszystkich tekstur (zmienialne przez API) |

### Potencjalne dalsze optymalizacje

1. **Horizon culling** - nie renderuj węzłów zasłoniętych przez horyzont
2. **Geometry clipmapping** - alternatywny algorytm LOD lepszy dla bardzo dużych terenów
3. **GPU-driven LOD selection** - przenieś wybór LOD na GPU (compute shader)
4. **Morphing LOD** - płynne przejścia między poziomami LOD zamiast skokowych

---

## 14. Zarządzanie MaterialInstance (ważne!)

### Problem

Przełączanie wireframe lub teselacji (`Engine::setWireframeEnabled` / `Engine::setTessellationEnabled`) czyści cache materiałów (`materialCache_.clear()`) i niszczy wszystkie pipeline'y (`invalidateAllPipelines()`). Bez odpowiedniego odświeżenia:

1. `TerrainComponent::materialInstance` trzyma stary `shared_ptr` do zniszczonego pipeline'a
2. `RenderExtractionSystem::resolveMaterialIfNeeded()` pomija go (bo nie jest nullem)
3. GPU otrzymuje zniszczony VkPipeline → zachowanie nieokreślone

### Rozwiązanie

`Engine::reloadShader("")` iteruje także `TerrainComponent`:
1. Resetuje `tc->materialInstance.reset()`
2. Tworzy nową instancję przez `materialSystem->getOrCreateMaterialInstance(*mat)`
3. Nowa instancja zawiera pipeline skompilowany z aktualnymi flagami (wireframe, tessellation)

Metody wywołujące `reloadShader("")`:
- `Engine::setWireframeEnabled(bool)` — już wywoływała
- `Engine::setTessellationEnabled(bool)` — dodano

---

## 15. Cieniowanie (Shadows)

### Konfiguracja shadow pipeline

| Właściwość | Wartość | Uzasadnienie |
|------------|---------|-------------|
| `cullMode` | `VK_CULL_MODE_NONE` | Zapobiega dziurawym cieniom przy różnych orientacjach trójkątów względem światła |
| `frontFace` | `VK_FRONT_FACE_COUNTER_CLOCKWISE` | Zgodność z resztą potoku |
| `depthBiasEnable` | `VK_TRUE` | Redukcja shadow acne przy podwójnym renderowaniu ścian |
| `depthBiasConstantFactor` | `1.0f` | Stałe przesunięcie głębi |
| `depthBiasSlopeFactor` | `1.0f` | Przesunięcie proporcjonalne do nachylenia trójkąta |

Teren jest renderowany w forward opaque pass (nie w GBuffer), więc `VK_CULL_MODE_NONE` w GBuffer nie wpływa na teren. Shadow pipeline używa `VK_CULL_MODE_NONE`, aby uniknąć sytuacji, gdzie trójkąty CW są back-facing z perspektywy światła i nie trafiają do shadow mapy (torn shadows).

---

## 16. Znane Problemy i Ograniczenia

1. **Parser JSON** jest uproszczony i nie obsługuje zagnieżdżonych struktur ani escape'owanych znaków.
2. **Frustum culling** używa testu sfery, nie właściwego testu AABB - może powodować false positives (renderowanie niewidocznych węzłów), ale nigdy false negatives.
3. **Brak smooth LOD transitions** - przejścia między LOD są skokowe (pop), brak geomorphingu.
4. **Wysokość poza granicami** mapy zwraca 0 - może powodować "przyklejanie" obiektów do zera przy krawędziach.
5. **Heightmap SampleLevel(..., 0)** — zawsze próbkuje mip level 0, co przy bardzo bliskim przybliżeniu może powodować aliasing; przy oddalaniu brak automatycznego filtrowania wysokości, ponieważ displacement wymaga pełnej rozdzielczości.
6. **Aktualizacja pozycji encji po regeneracji** — w przykładzie 06, po naciśnięciu 1/2/3 (regeneracja terenu), należy jawnie ustawić `terrainEntity->transform().position = terrain->worldOffset()` i zaktualizować pozycję kamery, aby nie znalazła się wewnątrz terenu.

---

## 17. Changelog — ostatnie zmiany

| Data | Zmiana | Autor |
|------|--------|-------|
| 2025 | Dodanie teselacji GPU (hull + domain shader) | - |
| 2025 | Zmiana triangle winding na CW + `triangle_cw` w hull shaderze | - |
| 2025 | `VK_CULL_MODE_NONE` w shadow/GBuffer pipeline | - |
| 2025 | `Engine::setTessellationLevel(float)` — API do zmiany gęstości teselacji | - |
| 2025 | Obsługa `+`/`-` w przykładzie 06 do zmiany poziomu teselacji | - |
| 2025 | Dystansowa redukcja tessFactor: `tessellationLevel / (1 << lodLevel)` | - |
| 2025 | Depth bias w shadow pipeline | - |
| 2025 | Anisotropic filtering 4x domyślnie + `Engine::setTextureFilter()` | - |
| 2025 | MaterialInstance TerrainComponent odświeżany przy `reloadShader` | - |
| 2025 | `Engine::setTessellationEnabled` wywołuje `reloadShader("")` | - |
