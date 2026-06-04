#pragma once

#include <string>
#include <vector>
#include <array>
#include "core/types.h"
#include "core/math.h"

namespace engine3d {

class MeshManager;
class CameraComponent;

/**
 * @brief Warstwa tekstury terenu (Terrain Layer).
 * Umożliwia nakładanie wielu tekstur na teren (splatting) za pomocą mapy wag (maskPath).
 */
struct TerrainLayer {
    std::string maskPath;      // Ścieżka do mapy wag (określa intensywność tej warstwy na teranie).
    std::string texturePath;   // Ścieżka do tekstury podłoża (np. trawa, skała, piasek).
    TextureHandle textureHandle{};
    TextureHandle maskHandle{};
};

/**
 * @brief Klasa reprezentująca trójwymiarowy teren (Terrain).
 *
 * Klasa implementuje zaawansowany system generowania i renderowania terenu z poziomami szczegółowości (LOD):
 * - Ładowanie map wysokości (Heightmaps) z plików graficznych.
 * - Algorytmy generowania proceduralnego (Fault Formation, Midpoint Displacement / Plasma, Particle Deposition).
 * - Podział przestrzenny terenu oparty na drzewie czwórkowym (Quadtree) w celu dynamicznego wyboru LOD.
 * - Zszywanie siatek (Stitching) zapobiegające powstawaniu szczelin (cracks) na granicach węzłów o różnej rozdzielczości.
 * - Obliczanie fizycznej wysokości terenu pod podaną pozycją (getHeight), niezbędne do kolizji i fizyki gry.
 */
class Terrain {
public:
    Terrain() : name_("Terrain") {}
    ~Terrain();

    /**
     * @brief Ładuje wysokość terenu z pliku graficznego (np. szarej mapy wysokości).
     * @param path Ścieżka do pliku graficznego.
     * @param scaleXZ Skala terenu w płaszczyźnie poziomej.
     * @param scaleY Skala wysokości terenu.
     * @param texScale Skala kafelkowania (tiling) tekstury.
     * @param maxLodLevels Maksymalna liczba poziomów szczegółowości Quadtree.
     */
    bool loadFromImage(const std::string& path, float scaleXZ = 10.0f, float scaleY = 2.0f,
                       float texScale = 20.0f, int maxLodLevels = 5);
    
    /**
     * @brief Ładuje parametry i warstwy terenu z pliku konfiguracyjnego JSON.
     */
    bool loadFromJson(const std::string& path);

        /**
     * @brief Ustawia skalowanie terenu.
     * @param scaleXZ Skala terenu w płaszczyźnie poziomej.
     * @param scaleY Skala wysokości terenu.
     * @param texScale Skala kafelkowania (tiling) tekstury.
     * @param maxLodLevels Maksymalna liczba poziomów szczegółowości Quadtree.
     */
    void setScale(float scaleXZ = 10.0f, float scaleY = 2.0f,
                       float texScale = 20.0f, int maxLodLevels = 5);

    // Proceduralne algorytmy generowania terenu:
    
    /**
     * @brief Algorytm Fault Formation.
     * Przesuwa wysokości wzdłuż losowych linii uskoków tektonicznych, tworząc górskie grzbiety.
     */
    void generateFault(int size, int iterations, int maxDelta, int minDelta, int itersPerFilter, float filter);
    
    /**
     * @brief Algorytm Plasma (Diamond-Square / Midpoint Displacement).
     * Tworzy fraktalny, naturalnie wyglądający teren.
     */
    void generatePlasma(int size, float roughness);
    
    /**
     * @brief Algorytm Deposition (Particle Deposition / Opad wulkaniczny).
     * Symuluje osadzanie się cząstek lawy, tworząc kratery i wulkany tarczowe.
     */
    void generateDeposition(int size, int jumps, int peakWalk, int minPart, int maxPart, float caldera);
    
    /**
     * @brief Buduje początkową siatkę geometryczną na podstawie wysokości w pamięci CPU.
     */
    void buildMesh();
    
    /**
     * @brief Wygładza krawędzie terenu do poziomu zerowego (ułatwia łączenie terenu z wodą/przepaścią).
     */
    void smoothEdges();

    /**
     * @brief Wysyła siatkę terenu do GPU.
     */
    void uploadTo(MeshManager& mgr);
    
    const std::string& meshName() const { return meshName_; }
    Vec3 worldOffset() const;
    float worldSizeXZ() const { return static_cast<float>(hmSize_) * scaleXZ_; }

    /**
     * @brief Aktualizuje drzewo Quadtree terenu na podstawie pozycji kamery.
     * Wyznacza, które węzły są widoczne oraz dobiera odpowiedni poziom szczegółowości (LOD).
     */
    void update(const CameraComponent* cam, MeshManager& meshManager, const std::string& meshPrefix);
    void uploadMeshes(MeshManager& meshManager, const std::string& meshPrefix);

    /**
     * @brief Pobiera fizyczną wysokość terenu Y w zadanych współrzędnych świata X, Z.
     * Wykorzystuje dwuliniową interpolację (bilinear interpolation) wysokości wierzchołków.
     */
    float getHeight(float x, float z) const;

    int sizeX = 0, sizeZ = 0;
    int nodeCount = 0;
    int visibleCount = 0;
    float scaleX; //skalownie mamy w funkcji buildMesh, ale przyda się do obliczeń w getHeight i update
    float scaleY; //skalowanie wysokości, przyda się do obliczeń w getHeight i update
    float texScale; //skalowanie tekstury, przyda się  update
    MeshData mesh;                     // Surowe dane siatki terenu.
    std::vector<TerrainLayer> layers;  // Warstwy tekstur terenu.
    std::string meshName_;

private:
    /**
     * @brief Węzeł drzewa czwórkowego terenu (Quadtree Node).
     * Reprezentuje prostokątny obszar terenu. Może być podzielony na 4 mniejsze dzieci.
     */
    struct Node {
        float cx, cz;               // Współrzędne środka węzła.
        float sizeXZ;               // Długość boku węzła w płaszczyźnie poziomej.
        float minY, maxY;           // Minimalna i maksymalna wysokość wewnątrz tego węzła (używana do frustum culling).
        int lodLevel = 0;           // Aktualny poziom szczegółowości węzła.
        Node* children[4]{};        // Czterej synowie (dzieci) w strukturze Quadtree.
        Node* parent = nullptr;     // Ojciec węzła.
        Node* neighbor[4]{};        // Sąsiedzi węzła w czterech kierunkach (lewo, prawo, góra, dół).
        bool visible = false;       // Czy węzeł znajduje się wewnątrz ostrosłupa widokowego kamery (Frustum Culling).
        int meshId = -1;
        int neighborLodDiff[4]{};   // Różnica poziomów szczegółowości z sąsiadami (używana do zszywania krawędzi).
        int vertexOffset = 0;
        int indexOffset = 0;
        int indexCount = 0;
    };

    void buildTree(Node* node);
    void buildNodeVertices(Node* node);
    void findNeighbors(Node* node);
    Node* findNodeAt(Node* node, float cx, float cz);
    void updateNodes(Node* node, const CameraComponent* cam);
    void generateStitchingIndices(Node* node);
    void collectAndUpload(Node* node, MeshManager& mgr, const std::string& prefix, int& slot);
    void releaseTree(Node* node);
    void deleteTree(Node* node);

    std::vector<float> heightmap_; // Tablica przechowująca surowe wartości wysokości wierzchołków terenu.
    float scaleXZ_ = 1.0f, scaleY_ = 1.0f, texScale_ = 1.0f;
    int maxLodLevels_ = 5;
    int hmSize_ = 0;
    int gridRes_ = 0;
    Node* root_ = nullptr; // Korzeń drzewa Quadtree.
    std::vector<Node*> visibleNodes_;
    MeshManager* meshManager_ = nullptr;
    std::string name_;
};

} // namespace engine3d
