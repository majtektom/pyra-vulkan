#include "core/mesh_builder.h"

#include <cmath>
#include <utility>

namespace engine3d {

namespace {

Vertex makeVertex(const Vec3& p, const Vec3& n, const Vec3& c, float u, float v) {
    Vertex vert{};
    vert.position = {p.x, p.y, p.z};
    vert.normal = {n.x, n.y, n.z};
    vert.color = {c.x, c.y, c.z};
    vert.texCoord = {u, v};
    return vert;
}

} // namespace

void MeshBuilder::addTriangleFacing(const Vec3& a, const Vec3& b, const Vec3& c,
                                    const Vec3& normal, const Vec3& color) {
    Vec3 pa = a, pb = b, pc = c;
    const Vec3 n = normalize(normal);
    // Konwencja silnika: cross(b - a, c - a) wskazuje na zewnątrz (strona widoczna).
    const Vec3 geo = cross(subtract(pb, pa), subtract(pc, pa));
    if (dot(geo, n) < 0.0f) {
        std::swap(pb, pc);
    }
    const auto base = static_cast<uint32_t>(mesh_.vertices.size());
    mesh_.vertices.push_back(makeVertex(pa, n, color, 0.0f, 0.0f));
    mesh_.vertices.push_back(makeVertex(pb, n, color, 1.0f, 0.0f));
    mesh_.vertices.push_back(makeVertex(pc, n, color, 0.0f, 1.0f));
    mesh_.indices.push_back(base);
    mesh_.indices.push_back(base + 1);
    mesh_.indices.push_back(base + 2);
}

void MeshBuilder::addTriangle(const Vec3& a, const Vec3& b, const Vec3& c,
                              const Vec3& insidePoint, const Vec3& color) {
    Vec3 n = normalize(cross(subtract(b, a), subtract(c, a)));
    const Vec3 centroid = multiply(add(add(a, b), c), 1.0f / 3.0f);
    if (dot(n, subtract(centroid, insidePoint)) < 0.0f) {
        n = multiply(n, -1.0f);
    }
    addTriangleFacing(a, b, c, n, color);
}

MeshBuilder& MeshBuilder::box(const Vec3& center, const Vec3& size, const Vec3& color) {
    const float hx = size.x * 0.5f, hy = size.y * 0.5f, hz = size.z * 0.5f;
    const Vec3 p[8] = {
        {center.x - hx, center.y - hy, center.z - hz}, {center.x + hx, center.y - hy, center.z - hz},
        {center.x + hx, center.y + hy, center.z - hz}, {center.x - hx, center.y + hy, center.z - hz},
        {center.x - hx, center.y - hy, center.z + hz}, {center.x + hx, center.y - hy, center.z + hz},
        {center.x + hx, center.y + hy, center.z + hz}, {center.x - hx, center.y + hy, center.z + hz},
    };
    const int faces[6][4] = {
        {0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7}, {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0},
    };
    for (const auto& f : faces) {
        addTriangle(p[f[0]], p[f[1]], p[f[2]], center, color);
        addTriangle(p[f[0]], p[f[2]], p[f[3]], center, color);
    }
    return *this;
}

MeshBuilder& MeshBuilder::cylinder(const Vec3& baseCenter, float radius, float height, int segments,
                                   const Vec3& color, float topRadius) {
    if (segments < 3) segments = 3;
    if (topRadius < 0.0f) topRadius = radius;
    const Vec3 top{baseCenter.x, baseCenter.y + height, baseCenter.z};
    const Vec3 inside{baseCenter.x, baseCenter.y + height * 0.5f, baseCenter.z};
    for (int i = 0; i < segments; ++i) {
        const float a0 = 2.0f * PI * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = 2.0f * PI * static_cast<float>(i + 1) / static_cast<float>(segments);
        const Vec3 b0{baseCenter.x + std::cos(a0) * radius, baseCenter.y, baseCenter.z + std::sin(a0) * radius};
        const Vec3 b1{baseCenter.x + std::cos(a1) * radius, baseCenter.y, baseCenter.z + std::sin(a1) * radius};
        const Vec3 t0{top.x + std::cos(a0) * topRadius, top.y, top.z + std::sin(a0) * topRadius};
        const Vec3 t1{top.x + std::cos(a1) * topRadius, top.y, top.z + std::sin(a1) * topRadius};

        addTriangle(b0, b1, t1, inside, color);
        if (topRadius > 0.0001f) {
            addTriangle(b0, t1, t0, inside, color);
            addTriangleFacing(top, t0, t1, {0.0f, 1.0f, 0.0f}, color);
        }
        addTriangleFacing(baseCenter, b1, b0, {0.0f, -1.0f, 0.0f}, color);
    }
    return *this;
}

MeshBuilder& MeshBuilder::cone(const Vec3& baseCenter, float radius, float height, int segments, const Vec3& color) {
    return cylinder(baseCenter, radius, height, segments, color, 0.0f);
}

MeshBuilder& MeshBuilder::sphere(const Vec3& center, float radius, int rings, int segments, const Vec3& color) {
    if (rings < 2) rings = 2;
    if (segments < 3) segments = 3;
    auto point = [&](int r, int s) {
        const float phi = PI * static_cast<float>(r) / static_cast<float>(rings);
        const float theta = 2.0f * PI * static_cast<float>(s) / static_cast<float>(segments);
        return Vec3{
            center.x + radius * std::sin(phi) * std::cos(theta),
            center.y + radius * std::cos(phi),
            center.z + radius * std::sin(phi) * std::sin(theta),
        };
    };
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            const Vec3 p00 = point(r, s), p01 = point(r, s + 1);
            const Vec3 p10 = point(r + 1, s), p11 = point(r + 1, s + 1);
            if (r == 0) {
                addTriangle(p00, p11, p10, center, color);          // biegun północny
            } else if (r == rings - 1) {
                addTriangle(p00, p01, p10, center, color);          // biegun południowy
            } else {
                addTriangle(p00, p01, p11, center, color);
                addTriangle(p00, p11, p10, center, color);
            }
        }
    }
    return *this;
}

MeshBuilder& MeshBuilder::gableRoof(const Vec3& baseCenter, float width, float depth, float height, const Vec3& color) {
    const float hx = width * 0.5f, hz = depth * 0.5f;
    const float y0 = baseCenter.y, y1 = baseCenter.y + height;
    const Vec3 a{baseCenter.x - hx, y0, baseCenter.z - hz};
    const Vec3 b{baseCenter.x + hx, y0, baseCenter.z - hz};
    const Vec3 c{baseCenter.x + hx, y0, baseCenter.z + hz};
    const Vec3 d{baseCenter.x - hx, y0, baseCenter.z + hz};
    const Vec3 r0{baseCenter.x - hx, y1, baseCenter.z};
    const Vec3 r1{baseCenter.x + hx, y1, baseCenter.z};
    const Vec3 inside{baseCenter.x, y0 + height * 0.3f, baseCenter.z};
    // Połacie dachu
    addTriangle(a, b, r1, inside, color);
    addTriangle(a, r1, r0, inside, color);
    addTriangle(d, c, r1, inside, color);
    addTriangle(d, r1, r0, inside, color);
    // Szczyty (trójkątne ściany) - lekko ciemniejsze
    const Vec3 gable = multiply(color, 0.85f);
    addTriangle(a, d, r0, inside, gable);
    addTriangle(b, c, r1, inside, gable);
    return *this;
}

MeshBuilder& MeshBuilder::pyramid(const Vec3& baseCenter, float width, float depth, float height, const Vec3& color) {
    const float hx = width * 0.5f, hz = depth * 0.5f;
    const Vec3 a{baseCenter.x - hx, baseCenter.y, baseCenter.z - hz};
    const Vec3 b{baseCenter.x + hx, baseCenter.y, baseCenter.z - hz};
    const Vec3 c{baseCenter.x + hx, baseCenter.y, baseCenter.z + hz};
    const Vec3 d{baseCenter.x - hx, baseCenter.y, baseCenter.z + hz};
    const Vec3 apex{baseCenter.x, baseCenter.y + height, baseCenter.z};
    const Vec3 inside{baseCenter.x, baseCenter.y + height * 0.25f, baseCenter.z};
    addTriangle(a, b, apex, inside, color);
    addTriangle(b, c, apex, inside, color);
    addTriangle(c, d, apex, inside, color);
    addTriangle(d, a, apex, inside, color);
    addTriangle(a, b, c, inside, color);
    addTriangle(a, c, d, inside, color);
    return *this;
}

MeshBuilder& MeshBuilder::ring(const Vec3& center, float innerRadius, float outerRadius, int segments, const Vec3& color) {
    if (segments < 3) segments = 3;
    const Vec3 up{0.0f, 1.0f, 0.0f};
    for (int i = 0; i < segments; ++i) {
        const float a0 = 2.0f * PI * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = 2.0f * PI * static_cast<float>(i + 1) / static_cast<float>(segments);
        const Vec3 i0{center.x + std::cos(a0) * innerRadius, center.y, center.z + std::sin(a0) * innerRadius};
        const Vec3 i1{center.x + std::cos(a1) * innerRadius, center.y, center.z + std::sin(a1) * innerRadius};
        const Vec3 o0{center.x + std::cos(a0) * outerRadius, center.y, center.z + std::sin(a0) * outerRadius};
        const Vec3 o1{center.x + std::cos(a1) * outerRadius, center.y, center.z + std::sin(a1) * outerRadius};
        addTriangleFacing(i0, o0, o1, up, color);
        addTriangleFacing(i0, o1, i1, up, color);
    }
    return *this;
}

MeshBuilder& MeshBuilder::disk(const Vec3& center, float radius, int segments, const Vec3& color) {
    if (segments < 3) segments = 3;
    const Vec3 up{0.0f, 1.0f, 0.0f};
    for (int i = 0; i < segments; ++i) {
        const float a0 = 2.0f * PI * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = 2.0f * PI * static_cast<float>(i + 1) / static_cast<float>(segments);
        const Vec3 o0{center.x + std::cos(a0) * radius, center.y, center.z + std::sin(a0) * radius};
        const Vec3 o1{center.x + std::cos(a1) * radius, center.y, center.z + std::sin(a1) * radius};
        addTriangleFacing(center, o0, o1, up, color);
    }
    return *this;
}

MeshBuilder& MeshBuilder::polygon(std::initializer_list<Vec3> points, const Vec3& insidePoint, const Vec3& color) {
    if (points.size() < 3) return *this;
    const Vec3* p = points.begin();
    for (size_t i = 1; i + 1 < points.size(); ++i) {
        addTriangle(p[0], p[i], p[i + 1], insidePoint, color);
    }
    return *this;
}

MeshBuilder& MeshBuilder::append(const MeshData& mesh) {
    const auto base = static_cast<uint32_t>(mesh_.vertices.size());
    for (const auto& v : mesh.vertices) {
        Vertex out = v;
        const Vec3 n = normalize(Vec3{v.normal[0], v.normal[1], v.normal[2]});
        out.normal = {n.x, n.y, n.z};
        mesh_.vertices.push_back(out);
    }
    for (uint32_t idx : mesh.indices) {
        mesh_.indices.push_back(base + idx);
    }
    return *this;
}

MeshData MeshBuilder::makeBox(const Vec3& size, const Vec3& color) {
    MeshBuilder b;
    b.box({0.0f, 0.0f, 0.0f}, size, color);
    return b.build();
}

MeshData MeshBuilder::makeRing(float innerRadius, float outerRadius, int segments, const Vec3& color) {
    MeshBuilder b;
    b.ring({0.0f, 0.0f, 0.0f}, innerRadius, outerRadius, segments, color);
    return b.build();
}

} // namespace engine3d
