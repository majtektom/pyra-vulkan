#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "core/math.h"

namespace engine3d {

/**
 * @brief Siatka nawigacyjna 2D (płaszczyzna XZ) z wyszukiwaniem ścieżek A*.
 *
 * Świat dzielony jest na kwadratowe komórki. Każda komórka może być:
 * - statycznie zablokowana (woda, strome zbocze) - setStaticBlocked,
 * - dynamicznie zablokowana przez przeszkody (budynki, skały) - addObstacle/removeObstacle
 *   z licznikiem referencji, więc nakładające się przeszkody zwalniają się poprawnie.
 *
 * findPath zwraca listę punktów świata (y = 0, wysokość ustala gra) z wygładzoną
 * ścieżką (usuwane są zbędne punkty pośrednie, gdy linia prosta jest przechodnia).
 * Ruch 8-kierunkowy bez ścinania narożników przeszkód.
 */
class NavGrid {
public:
    /**
     * @param width   liczba komórek w osi X
     * @param height  liczba komórek w osi Z
     * @param cellSize rozmiar komórki w jednostkach świata
     * @param origin  współrzędne świata lewego-dolnego narożnika siatki (x, z)
     */
    void init(int width, int height, float cellSize, const Vec3& origin);

    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }
    [[nodiscard]] float cellSize() const { return cellSize_; }
    [[nodiscard]] Vec3 origin() const { return origin_; }

    [[nodiscard]] bool inBounds(int cx, int cz) const { return cx >= 0 && cz >= 0 && cx < width_ && cz < height_; }
    [[nodiscard]] bool isWalkable(int cx, int cz) const;
    [[nodiscard]] bool isWalkableWorld(float x, float z) const;
    [[nodiscard]] bool isStaticBlocked(int cx, int cz) const;

    void setStaticBlocked(int cx, int cz, bool blocked);

    // Przeszkody dynamiczne (prostokąt w przestrzeni świata), zliczane referencyjnie.
    void addObstacle(float minX, float minZ, float maxX, float maxZ);
    void removeObstacle(float minX, float minZ, float maxX, float maxZ);
    // Czy w prostokącie są wyłącznie komórki przechodnie.
    [[nodiscard]] bool isAreaFree(float minX, float minZ, float maxX, float maxZ) const;

    bool worldToCell(float x, float z, int& cx, int& cz) const;
    [[nodiscard]] Vec3 cellCenter(int cx, int cz) const;

    /**
     * @brief Najbliższa przechodnia komórka w promieniu maxRadius komórek (BFS po pierścieniach).
     */
    bool findNearestWalkable(int cx, int cz, int maxRadius, int& outX, int& outZ) const;

    /**
     * @brief Czy odcinek a-b przechodzi wyłącznie przez przechodnie komórki.
     */
    [[nodiscard]] bool lineWalkable(const Vec3& a, const Vec3& b) const;

    /**
     * @brief Wyszukuje ścieżkę A* od from do to.
     *
     * Gdy cel jest zablokowany, ścieżka prowadzi do najbliższej przechodniej komórki.
     * Gdy cel jest nieosiągalny, zwraca ścieżkę do najbliższego osiągniętego punktu
     * (o ile allowPartial == true).
     *
     * @param outPath wynikowe punkty (bez punktu startowego), y = 0
     * @param maxExpanded limit rozwiniętych węzłów (ochrona przed długimi wyszukiwaniami)
     */
    bool findPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& outPath,
                  bool allowPartial = true, int maxExpanded = 40000) const;

    /**
     * @brief Etykietuje spójne obszary przechodnich komórek (BFS, 4-sąsiedztwo - zgodne z A* bez
     *        ścinania narożników). Pozwala tanio sprawdzić, czy z punktu A da się dojść do B,
     *        np. przed postawieniem budynku w odciętej dolinie.
     * @param includeObstacles true = uwzględnia też przeszkody dynamiczne (budynki, skały).
     */
    void computeRegions(bool includeObstacles = false);
    // Numer obszaru komórki (-1 = zablokowana lub regiony nie policzone).
    [[nodiscard]] int regionAt(float x, float z) const;
    [[nodiscard]] bool sameRegion(const Vec3& a, const Vec3& b) const;

private:
    [[nodiscard]] int index(int cx, int cz) const { return cz * width_ + cx; }
    void forEachCellInRect(float minX, float minZ, float maxX, float maxZ, const std::function<void(int)>& fn) const;

    int width_ = 0;
    int height_ = 0;
    float cellSize_ = 1.0f;
    Vec3 origin_{};
    std::vector<uint8_t> staticBlocked_;
    std::vector<uint16_t> obstacleCount_;
    std::vector<int> region_;

    // Bufory robocze A* (mutable: findPath jest logicznie const).
    mutable std::vector<float> gScore_;
    mutable std::vector<int> parent_;
    mutable std::vector<uint32_t> visitStamp_;
    mutable std::vector<uint32_t> closedStamp_;
    mutable uint32_t stamp_ = 0;
};

} // namespace engine3d
