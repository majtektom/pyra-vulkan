#pragma once

#include <string>
#include <vector>
#include <array>
#include <memory>
#include "core/types.h"
#include "core/math.h"
#include "render/render_types.h"

namespace engine3d {

class MeshManager;
class CameraComponent;
struct Ray;

/**
 * @brief Warstwa tekstury terenu (Terrain Layer).
 *
 * Przechowuje informacje o pojedynczej warstwie tekstury nakładanej na teren
 * w systemie texture splatting. Każda warstwa składa się z tekstury podłoża
 * (np. trawa, skała) oraz mapy wag (maski) określającej intensywność
 * tej warstwy w danym punkcie terenu.
 */
struct TerrainLayer {
    std::string maskPath;          // Ścieżka do pliku mapy wag (określa intensywność warstwy).
    std::string texturePath;       // Ścieżka do pliku tekstury podłoża.
    TextureHandle textureHandle{}; // Uchwyt do tekstury GPU.
    TextureHandle maskHandle{};    // Uchwyt do maski GPU.
};

/**
 * @brief Klasa reprezentująca trójwymiarowy teren z systemem LOD (Level of Detail).
 *
 * ## Architektura
 *
 * Teren jest podzielony przestrzennie za pomocą drzewa czwórkowego (Quadtree).
 * Każdy węzeł reprezentuje kwadratowy obszar terenu. Węzły liściowe (najmniejsze)
 * zawierają siatkę wierzchołków 17x17 (gridRes_ = 16 komórek), która jest
 * renderowana z różnym poziomem szczegółowości (LOD 0-4).
 *
 * ## System LOD
 *
 * Poziom szczegółowości (LOD) określa, co który wierzchołek jest używany podczas
 * renderowania węzła:
 *   - LOD 0: pełna rozdzielczość (wszystkie wierzchołki, ~289 trójkątów)
 *   - LOD 1: co drugi wierzchołek (~72 trójkąty)
 *   - LOD 2: co czwarty wierzchołek (~18 trójkątów)
 *   - LOD 3: co ósmy wierzchołek (~4 trójkąty)
 *   - LOD 4: co szesnasty wierzchołek (~2 trójkąty)
 *
 * Wybór LOD zależy od:
 *   1. Odległości węzła od kamery (im dalej, tym niższy LOD).
 *   2. Stromizny terenu w węźle (strome obszary zachowują wyższy LOD).
 *   3. Ograniczenia sąsiedztwa (różnica LOD między sąsiadami ≤ 1).
 *
 * ## Zszywanie krawędzi (Stitching)
 *
 * Gdy dwa sąsiednie węzły mają różne LOD, na ich granicy pojawiłyby się
 * szczeliny (T-junctions). System zapobiega temu przez wstawianie dodatkowych
 * trójkątów na krawędziach węzła o wyższej rozdzielczości, dopasowujących
 * siatkę do rzadszych wierzchołków sąsiada.
 *
 * ## Cykl życia terenu
 *
 * 1. Tworzenie: loadFromImage() lub loadFromJson() → ładuje heightmapę.
 * 2. Budowa drzewa: buildTree() → dzieli przestrzeń rekurencyjnie.
 * 3. Generowanie wierzchołków: uploadNodeMeshes() → tworzy bufory GPU.
 * 4. Generowanie indeksów LOD: uploadTo() → tworzy współdzielone bufory indeksowe.
 * 5. Aktualizacja co klatkę: update() → wybiera LOD i widoczność węzłów.
 * 6. Renderowanie: collectRenderPackets() → tworzy pakiety renderowania.
 */
class Terrain {
public:
    friend class TerrainComponent;

    /**
     * @brief Węzeł drzewa czwórkowego terenu (Quadtree Node).
     *
     * Reprezentuje kwadratowy obszar terenu o boku sizeXZ, wyśrodkowany w (cx, cz).
     * Węzeł może być podzielony na 4 dzieci (ćwiartki) lub być liściem.
     * Liście zawierają siatkę wierzchołków i są renderowane.
     */
    struct Node {
        float cx, cz;                    // Współrzędne środka węzła w przestrzeni świata.
        float sizeXZ;                    // Długość boku kwadratu węzła.
        float minY, maxY;                // Minimalna i maksymalna wysokość w węźle (dla frustum culling).
        int lodLevel = 0;                // Aktualny poziom szczegółowości (0 = najwyższy).
        Node* children[4]{};             // Dzieci: [0]=lewy-górny, [1]=prawy-górny, [2]=lewy-dolny, [3]=prawy-dolny.
        Node* parent = nullptr;          // Węzeł rodzicielski.
        Node* neighbor[4]{};             // Sąsiedzi: [0]=lewo, [1]=góra, [2]=prawo, [3]=dół.
        bool visible = false;            // Czy węzeł jest widoczny dla kamery.
        int meshId = -1;                 // Unikalny identyfikator siatki węzła.
        int neighborLodDiff[4]{};        // Różnica LOD z każdym sąsiadem.
        int vertexOffset = 0;            // Offset w globalnym buforze wierzchołków (nieużywane w obecnej wersji).
        int indexOffset = 0;             // Offset w globalnym buforze indeksów (nieużywane w obecnej wersji).
        int indexCount = 0;              // Liczba indeksów dla węzła (nieużywane w obecnej wersji).
    };

    Terrain() : name_("Terrain") {}
    ~Terrain();

    // =========================================================================
    // Ładowanie i konfiguracja
    // =========================================================================

    /**
     * @brief Ładuje mapę wysokości z pliku graficznego (PNG, JPG, BMP - skala szarości).
     *
     * Proces:
     *   1. Wczytuje obraz przez stb_image.
     *   2. Próbkuje co 4 piksel (downsample=4) dla zmniejszenia rozmiaru.
     *   3. Tworzy korzeń drzewa czwórkowego.
     *   4. Buduje całe drzewo czwórkowe (buildTree).
     *   5. Znajduje sąsiadów dla każdego węzła (findNeighbors).
     *
     * @param path Ścieżka do pliku graficznego (PNG/JPG/BMP).
     * @param scaleXZ Skala w płaszczyźnie XZ - ile jednostek świata na 1 piksel heightmapy (domyślnie 10).
     * @param scaleY Skala wysokości - mnożnik wartości pikseli (domyślnie 2, piksel=255 → wysokość=510).
     * @param texScale Skala teksturowania - ile razy tekstura jest kafelkowana na cały teren (domyślnie 20).
     * @param maxLodLevels Maksymalna liczba poziomów szczegółowości (domyślnie 5).
     * @return true jeśli załadowano pomyślnie.
     */
    bool loadFromImage(const std::string& path, float scaleXZ = 10.0f, float scaleY = 2.0f,
                       float texScale = 20.0f, int maxLodLevels = 6);

    /**
     * @brief Ładuje konfigurację terenu z pliku JSON.
     *
     * Plik JSON zawiera:
     *   - "heightMap": ścieżka do pliku obrazu mapy wysokości.
     *   - "scaleMap": skala XZ (opcjonalne).
     *   - "scaleHeight": skala Y (opcjonalne).
     *   - "scaleTexture": skala teksturowania (opcjonalne).
     *   - "layers": tablica obiektów {"mask": "ścieżka", "texture": "ścieżka"}.
     *
     * Uwaga: parser JSON jest uproszczony i nie obsługuje zagnieżdżonych struktur.
     *
     * @param path Ścieżka do pliku JSON.
     * @return true jeśli załadowano pomyślnie.
     */
    bool loadFromJson(const std::string& path);

    /**
     * @brief Ustawia parametry skalowania terenu.
     *
     * Pozwala zmienić skalę po załadowaniu, bez ponownego wczytywania heightmapy.
     * Nie przebudowuje automatycznie siatki - wymaga wywołania buildMesh().
     */
    void setScale(float scaleXZ = 10.0f, float scaleY = 2.0f,
                  float texScale = 20.0f, int maxLodLevels = 6);

    // =========================================================================
    // Generowanie proceduralne
    // =========================================================================

    /**
     * @brief Generuje teren algorytmem Fault Formation (uskoki tektoniczne).
     *
     * Działanie:
     *   1. Inicjalizuje płaską powierzchnię.
     *   2. W każdej iteracji losuje linię podziału i podnosi/obniża teren po jednej stronie.
     *   3. Co `itersPerFilter` iteracji wygładza krawędzie mapy filtrem dolnoprzepustowym.
     *   4. Na końcu normalizuje wysokości do zakresu 0-300.
     *
     * @param size Rozmiar mapy (size x size), np. 256.
     * @param iterations Liczba iteracji (np. 200).
     * @param maxDelta Maksymalna zmiana wysokości na początku (np. 50).
     * @param minDelta Minimalna zmiana wysokości na końcu (np. 2).
     * @param itersPerFilter Co ile iteracji filtrować krawędzie (0 = bez filtrowania).
     * @param filter Współczynnik filtru (0-1, np. 0.5).
     */
    void generateFault(int size, int iterations, int maxDelta, int minDelta,
                       int itersPerFilter, float filter);

    /**
     * @brief Generuje teren algorytmem Plasma / Midpoint Displacement.
     *
     * Działanie (algorytm Diamond-Square):
     *   1. Start z pustą mapą i wartościami w narożnikach = 0.
     *   2. Diamond step: oblicza środek każdego kwadratu jako średnią narożników + losowe przemieszczenie.
     *   3. Square step: oblicza punkty na krawędziach jako średnią sąsiadów + losowe przemieszczenie.
     *   4. Zmniejsza rozmiar kwadratu o połowę i powtarza, aż dojdziemy do pojedynczych pikseli.
     *   5. Amplituda przemieszczeń maleje z każdym poziomem (dh *= 2^(-roughness)).
     *
     * Wynik: fraktalny, naturalnie wyglądający teren.
     *
     * @param size Rozmiar mapy (size x size), np. 256.
     * @param roughness Chropowatość (0-1, im większa tym bardziej nierówny teren).
     */
    void generatePlasma(int size, float roughness);

    /**
     * @brief Generuje teren algorytmem Particle Deposition (osadzanie cząstek).
     *
     * Działanie:
     *   1. Start z płaską powierzchnią.
     *   2. W każdym skoku (jump) losuje punkt startowy i generuje `particles` cząstek.
     *   3. Każda cząstka wykonuje losowy spacer, zwiększając wysokość w każdej odwiedzonej komórce.
     *   4. Po spacerze cząstka próbuje "wędrować na szczyt" (peakWalk).
     *   5. Po zakończeniu, opcjonalnie tworzy kalderę przez odwrócenie i odcięcie wysokości.
     *
     * Wynik: teren przypominający wulkany tarczowe i pola lawowe.
     *
     * @param size Rozmiar mapy (size x size), np. 256.
     * @param jumps Liczba skoków (serii cząstek).
     * @param peakWalk Liczba kroków wędrówki na szczyt po każdej cząstce.
     * @param minPart Minimalna liczba cząstek na skok.
     * @param maxPart Maksymalna liczba cząstek na skok.
     * @param caldera Współczynnik kaldery (0-1, 0 = brak kaldery).
     */
    void generateDeposition(int size, int jumps, int peakWalk, int minPart, int maxPart, float caldera);

    // =========================================================================
    // Budowa i przesyłanie siatki
    // =========================================================================

    /**
     * @brief Buduje początkową siatkę geometryczną terenu.
     *
     * Proces:
     *   1. Odtwarza strukturę drzewa czwórkowego od nowa.
     *   2. Usuwa stare drzewo (zwalnia pamięć).
     *   3. Tworzy nowy korzeń i buduje drzewo (buildTree).
     *   4. Znajduje sąsiadów (findNeighbors).
     *
     * Wywoływane po zmianie parametrów skalowania lub po generacji proceduralnej.
     */
    void buildMesh();

    /**
     * @brief Wygładza krawędzie terenu, obniżając je stopniowo do poziomu 0.
     *
     * Przydatne przy łączeniu terenu z powierzchnią wody lub przy krawędziach mapy.
     * Modyfikuje 3 zewnętrzne rzędy/kolumny pikseli z każdej strony.
     */
    void smoothEdges();

    /**
     * @brief Wysyła kompletną siatkę terenu do GPU (wierzchołki + indeksy LOD).
     *
     * Proces:
     *   1. Przypisuje unikalne ID do każdego węzła liściowego.
     *   2. Dla każdego liścia generuje wierzchołki i wysyła bufor wierzchołków do GPU.
     *   3. Dla każdej kombinacji LOD (0-4) × konfiguracja zszywania (0-15):
     *      generuje bufor indeksowy i wysyła do GPU.
     *
     * Łącznie tworzonych jest 16 × 5 = 80 buforów indeksowych współdzielonych
     * między wszystkimi węzłami.
     *
     * @param mgr Menadżer siatek odpowiedzialny za alokację buforów GPU.
     */
    void uploadTo(MeshManager& mgr);

    /**
     * @brief Przesyła siatki wszystkich węzłów do GPU.
     *
     * Alternatywna metoda uploadu - przesyła bufory wierzchołków dla każdego węzła.
     *
     * @param meshManager Menadżer siatek GPU.
     * @param meshPrefix Prefiks nazwy siatki.
     */
    void uploadMeshes(MeshManager& meshManager, const std::string& meshPrefix);

    // =========================================================================
    // Aktualizacja per-klatka i renderowanie
    // =========================================================================

    /**
     * @brief Aktualizuje drzewo Quadtree na podstawie pozycji kamery (wywoływane co klatkę).
     *
     * Proces:
     *   1. Dla każdego liścia oblicza optymalny LOD na podstawie odległości od kamery
     *      i nachylenia terenu (updateLeafLods).
     *   2. Wyrównuje LOD sąsiadów (różnica ≤ 1) przez iteracyjne "naprawianie"
     *      (relaksacja ograniczeń).
     *   3. Wyznacza widoczność węzłów (frustum culling + back-face culling)
     *      przez updateNodes.
     *
     * @param cam Aktywna kamera (używana do określenia frustum i pozycji).
     * @param meshManager Menadżer siatek (przekazywany, ale nieużywany w tej metodzie).
     * @param meshPrefix Prefiks nazwy siatki (przekazywany, ale nieużywany w tej metodzie).
     */
    void update(const CameraComponent* cam, MeshManager& meshManager, const std::string& meshPrefix);

    /**
     * @brief Zbiera pakiety renderowania dla wszystkich widocznych węzłów terenu.
     *
     * Dla każdego widocznego węzła:
     *   1. Pobiera bufor wierzchołków węzła z GPU.
     *   2. Określa konfigurację zszywania na podstawie sąsiadów (computeStitchConfig).
     *   3. Pobiera odpowiedni bufor indeksowy (LOD + konfiguracja zszywania).
     *   4. Tworzy RenderPacket z pełną konfiguracją materiału.
     *
     * @param packets Wektor wyjściowy na pakiety renderowania.
     * @param meshManager Menadżer siatek GPU.
     * @param worldTransform Macierz transformacji świata dla całego terenu.
     * @param materialHandle Uchwyt do materiału.
     * @param materialInstance Instancja materiału z właściwościami.
     * @param castShadows Czy teren rzuca cienie.
     * @param layerBlendEnabled Czy włączone mieszanie warstw tekstur (splatting).
     * @param layerCount Liczba aktywnych warstw tekstur.
     * @param layerTextureHandles Uchwyty do tekstur warstw.
     * @param layerMaskHandles Uchwyty do masek warstw.
     * @param layerWorldSize Rozmiar świata dla kafelkowania warstw.
     */
    void collectRenderPackets(
        std::vector<RenderPacket>& packets,
        MeshManager& meshManager,
        const Matrix4& worldTransform,
        MaterialHandle materialHandle,
        std::shared_ptr<MaterialInstance> materialInstance,
        bool castShadows,
        bool layerBlendEnabled,
        uint32_t layerCount,
        const std::array<TextureHandle, kMaxTerrainLayers>& layerTextureHandles,
        const std::array<TextureHandle, kMaxTerrainLayers>& layerMaskHandles,
        float layerWorldSize,
        TextureHandle heightmapTextureHandle,
        bool heightmapEnabled,
        bool tessellationActive = true,
        float tessellationLevel = 4.0f);

    /**
     * @brief Pobiera wysokość terenu Y w zadanym punkcie świata (X, Z).
     *
     * Stosuje interpolację dwuliniową (bilinear) między czterema najbliższymi
     * punktami heightmapy. Używane do kolizji, fizyki i umieszczania obiektów na terenie.
     *
     * @param x Współrzędna X w przestrzeni świata.
     * @param z Współrzędna Z w przestrzeni świata.
     * @return Wysokość Y terenu w zadanym punkcie.
     */
    float getHeight(float x, float z) const;

    /**
     * @brief Czy punkt (x, z) w przestrzeni świata leży nad terenem.
     */
    bool containsXZ(float x, float z) const;

    /**
     * @brief Przybliżona normalna powierzchni terenu w punkcie (różnice centralne).
     */
    Vec3 getNormal(float x, float z) const;

    /**
     * @brief Przecięcie promienia z powierzchnią terenu (ray marching + bisekcja).
     *
     * Używane m.in. do wskazywania punktu na terenie myszą (picking w grach RTS).
     *
     * @param ray Promień w przestrzeni świata (kierunek znormalizowany).
     * @param maxDistance Maksymalna długość promienia.
     * @param outHit Punkt trafienia.
     * @return true, gdy promień trafił w teren.
     */
    bool raycast(const Ray& ray, float maxDistance, Vec3& outHit) const;

    /**
     * @brief Ustawia heightmapę bezpośrednio z pamięci (np. z własnego generatora).
     *
     * Wartości w zakresie 0..255 (jak w heightmapach BMP). Po wywołaniu należy
     * ustawić skalę (setScale) i zbudować siatkę (buildMesh).
     */
    void setHeightmap(const std::vector<float>& data, int size);

    // =========================================================================
    // Gettery
    // =========================================================================

    const std::string& meshName() const { return meshName_; }
    Vec3 worldOffset() const;
    float worldSizeXZ() const { return static_cast<float>(hmSize_) * scaleXZ_; }
    const std::vector<Node*>& getVisibleNodes() const { return visibleNodes_; }
    // Zasięg (od kamery), w którym węzły spoza kadru i tak rzucają cienie.
    void setShadowCasterDistance(float d) { shadowCasterDistance_ = d; }
    const std::vector<float>& getHeightmapData() const { return heightmap_; }
    int getHeightmapSize() const { return hmSize_; }
    float getScaleXZ() const { return scaleXZ_; }
    float getScaleY() const { return scaleY_; }
    int getMaxLodLevels() const { return maxLodLevels_; }

    /**
     * @brief Zwraca całkowitą liczbę węzłów liściowych w drzewie czwórkowym.
     *
     * Węzły liściowe to najmniejsze obszary terenu, dla których generowane są siatki.
     */
    int getTotalLeafCount() const;

    // Pola publiczne dla kompatybilności z istniejącym kodem
    int sizeX = 0, sizeZ = 0;            // Wymiary heightmapy.
    int nodeCount = 0;                    // Liczba węzłów wewnętrznych (nie-liści).
    int visibleCount = 0;                 // Liczba widocznych węzłów w ostatniej klatce.
    std::vector<TerrainLayer> layers;     // Warstwy tekstur (splatting).
    std::string meshName_;                // Nazwa siatki w menadżerze GPU.

private:
    /**
     * @brief Rekurencyjnie buduje drzewo czwórkowe.
     *
     * Zaczynając od korzenia, dzieli każdy węzeł na 4 dzieci, dopóki:
     *   - Rozmiar węzła w pikselach heightmapy < gridRes_ (16), lub
     *   - Osiągnięto maksymalny poziom LOD (maxLodLevels_).
     *
     * Dla każdego dziecka oblicza minY/maxY przez iterację po heightmapie.
     */
    void buildTree(Node* node);

    /**
     * @brief Węzeł zastępczy - w obecnej wersji wierzchołki są generowane
     *        bezpośrednio w uploadNodeMeshes. Zachowane dla kompatybilności.
     */
    void buildNodeVertices(Node* node);

    /**
     * @brief Znajduje 4 sąsiadów dla każdego węzła drzewa.
     *
     * Sąsiad to węzeł tego samego rozmiaru przylegający do danego węzła.
     * Kierunki: [0]=lewo(-X), [1]=góra(+Z), [2]=prawo(+X), [3]=dół(-Z).
     * Sąsiedzi są używani do zszywania krawędzi (stitching).
     */
    void findNeighbors(Node* node);

    /**
     * @brief Znajduje węzeł na zadanej pozycji (cx, cz) o tym samym rozmiarze.
     *
     * Przeszukuje drzewo od korzenia, wybierając odpowiednie dziecko na podstawie
     * porównania pozycji. Zwraca węzeł liściowy zawierający punkt (cx, cz).
     */
    Node* findNodeAt(Node* node, float cx, float cz);

    /**
     * @brief Rekurencyjnie sprawdza widoczność węzłów i ich dzieci.
     *
     * Dla każdego węzła:
     *   1. Sprawdza czy sfera otaczająca węzeł jest w frustum kamery.
     *   2. Sprawdza czy węzeł nie jest za kamerą (back-face culling).
     *   3. Sprawdza czy węzeł nie jest za daleko (far plane).
     *   4. Jeśli węzeł jest widoczny i ma dzieci - sprawdza dzieci.
     *   5. Jeśli węzeł jest widoczny i jest liściem - dodaje do visibleNodes_.
     */
    void updateNodes(Node* node, const CameraComponent* cam);

    /**
     * @brief Zwalnia całą pamięć drzewa czwórkowego (rekurencyjnie).
     *
     * Poprawnie usuwa wszystkie węzły potomne przed usunięciem węzła głównego.
     */
    void deleteTree(Node* node);

    /**
     * @brief Przypisuje unikalne ID (meshId) do każdego węzła liściowego.
     *
     * ID są używane do nazewnictwa buforów GPU: "meshName_node_{meshId}".
     */
    void assignNodeIds(Node* node, int& nextId);

    /**
     * @brief Generuje wierzchołki dla węzła liściowego i wysyła je do GPU.
     *
     * Dla każdego liścia:
     *   1. Generuje siatkę (gridRes_+1) × (gridRes_+1) = 17×17 wierzchołków.
     *   2. Próbkuje wysokości z heightmapy z interpolacją.
     *   3. Oblicza normalne metodą różnic skończonych.
     *   4. Współrzędne tekstury są skalowane względem całej heightmapy.
     *   5. Wysyła bufor wierzchołków do GPU przez MeshManager.
     */
    void uploadNodeMeshes(Node* node, MeshManager& mgr, const std::string& prefix);

    /**
     * @brief Oblicza poziom LOD dla każdego liścia na podstawie odległości od kamery.
     *
     * Algorytm:
     *   1. Oblicza odległość euklidesową od kamery do środka węzła.
     *   2. Oblicza nachylenie terenu w węźle: slope = (maxY - minY) / (sizeXZ + 0.1).
     *   3. Wyznacza mnożnik: multiplier = min(1.0 + slope * 2.0, 4.0).
     *   4. Zaczynając od LOD 0, zwiększa LOD dopóki:
     *      dist < (sizeXZ * 2 * (LOD + 1) + sizeXZ * 2) * multiplier.
     *
     * Efekt: płaskie obszary szybciej przechodzą na niższy LOD,
     *        strome/górzyste obszary dłużej zachowują wyższy LOD.
     */
    void updateLeafLods(Node* node, const Vec3& camPos);

    // Dane wysokościowe
    std::vector<float> heightmap_;     // Surowe wartości wysokości (0-255 z obrazu, skalowane).
    float scaleXZ_ = 1.0f;             // Skala pozioma: jednostki świata / piksel.
    float scaleY_ = 1.0f;              // Skala pionowa: mnożnik wartości heightmapy.
    float texScale_ = 1.0f;            // Skala teksturowania: kafelki / cały teren.
    int maxLodLevels_ = 6;             // Maksymalna liczba poziomów LOD.
    int hmSize_ = 0;                   // Rozmiar heightmapy (szerokość = wysokość).
    int gridRes_ = 0;                  // Rozdzielczość siatki węzła (liczba komórek = 16).

    // Drzewo przestrzenne
    Node* root_ = nullptr;             // Korzeń drzewa czwórkowego (Quadtree).
    std::vector<Node*> visibleNodes_;  // Lista widocznych węzłów w aktualnej klatce.
    // Węzły pominięte przy rysowaniu (np. za kamerą), ale bliskie kamerze - rzucają cień w kadr
    // (wzgórze za kamerą od strony słońca). Trafiają tylko do mapy cieni.
    std::vector<Node*> shadowNodes_;
    float shadowCasterDistance_ = 2000.0f;

    // Zależności
    MeshManager* meshManager_ = nullptr; // Menadżer siatek GPU.
    std::string name_;                   // Nazwa diagnostyczna.
};

} // namespace engine3d
