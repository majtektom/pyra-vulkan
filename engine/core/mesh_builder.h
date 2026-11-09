#pragma once

#include <initializer_list>
#include <vector>

#include "core/math.h"
#include "core/types.h"

namespace engine3d {

/**
 * @brief Budowniczy proceduralnych siatek złożonych z prostych brył (low-poly).
 *
 * Pozwala szybko składać modele (budynki, postacie, drzewa, pociski) z prymitywów:
 * prostopadłościanów, walców, stożków, sfer, dachów dwuspadowych, pierścieni.
 * Każda bryła ma własny kolor zapisany w kolorze wierzchołka (shader mnoży go przez
 * kolor materiału i teksturę), dzięki czemu cały model może używać jednego materiału.
 *
 * Ściany są cieniowane płasko (flat shading): każdy trójkąt ma własne wierzchołki
 * i normalną ściany. Orientacja trójkątów jest automatycznie ustawiana na zewnątrz
 * bryły (względem jej środka), co pasuje do back-face cullingu.
 *
 * Przykład:
 * @code
 *   MeshBuilder b;
 *   b.box({0, 1, 0}, {2, 2, 2}, {0.8f, 0.7f, 0.5f});
 *   b.gableRoof({0, 2, 0}, 2.4f, 2.4f, 1.2f, {0.7f, 0.2f, 0.1f});
 *   meshManager->loadMesh("house", b.build());
 * @endcode
 */
class MeshBuilder {
public:
    /**
     * @brief Ustawia transformację stosowaną do kolejnych brył (obrót wokół Y + przesunięcie + skala).
     */
    MeshBuilder& setTransform(const Vec3& offset, float rotationYRadians = 0.0f, float uniformScale = 1.0f);
    MeshBuilder& resetTransform();

    // Prostopadłościan o środku center i wymiarach size.
    MeshBuilder& box(const Vec3& center, const Vec3& size, const Vec3& color);

    // Walec / ścięty stożek z podstawą w baseCenter (oś Y). topRadius < 0 => walec.
    MeshBuilder& cylinder(const Vec3& baseCenter, float radius, float height, int segments,
                          const Vec3& color, float topRadius = -1.0f);

    // Stożek (szczyt na górze).
    MeshBuilder& cone(const Vec3& baseCenter, float radius, float height, int segments, const Vec3& color);

    // Sfera (UV sphere).
    MeshBuilder& sphere(const Vec3& center, float radius, int rings, int segments, const Vec3& color);

    // Dach dwuspadowy: podstawa na wysokości base.y, kalenica wzdłuż osi X.
    MeshBuilder& gableRoof(const Vec3& baseCenter, float width, float depth, float height, const Vec3& color);

    // Ostrosłup o podstawie prostokąta (dach namiotowy, wieża).
    MeshBuilder& pyramid(const Vec3& baseCenter, float width, float depth, float height, const Vec3& color);

    // Płaski pierścień w płaszczyźnie XZ (np. znacznik zaznaczenia), widoczny z góry.
    MeshBuilder& ring(const Vec3& center, float innerRadius, float outerRadius, int segments, const Vec3& color);

    // Płaski dysk w płaszczyźnie XZ, widoczny z góry.
    MeshBuilder& disk(const Vec3& center, float radius, int segments, const Vec3& color);

    // Dowolny wypukły wielokąt (wierzchołki w kolejności obwodu). Normalna skierowana od insidePoint.
    MeshBuilder& polygon(std::initializer_list<Vec3> points, const Vec3& insidePoint, const Vec3& color);

    // Dokleja istniejącą siatkę (z aktualną transformacją).
    MeshBuilder& append(const MeshData& mesh);

    [[nodiscard]] MeshData build() const { return mesh_; }
    [[nodiscard]] const MeshData& data() const { return mesh_; }
    void clear() { mesh_ = {}; }

    // Pomocnicze gotowe siatki
    static MeshData makeBox(const Vec3& size, const Vec3& color);
    static MeshData makeRing(float innerRadius, float outerRadius, int segments, const Vec3& color);

private:
    Vec3 applyPoint(const Vec3& p) const;
    Vec3 applyDir(const Vec3& d) const;
    // Trójkąt z płaską normalną skierowaną od insidePoint.
    void addTriangle(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& insidePoint, const Vec3& color);
    // Trójkąt z zadaną normalną (np. dla płaskich elementów widocznych z góry).
    void addTriangleFacing(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& normal, const Vec3& color);

    MeshData mesh_;
    Vec3 offset_{};
    float rotY_ = 0.0f;
    float scale_ = 1.0f;
};

} // namespace engine3d
