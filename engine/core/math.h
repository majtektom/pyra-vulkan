#pragma once

#include <array>
#include <cmath>

namespace engine3d {

constexpr float PI	= 3.14159265358979323846264338327950288f;
constexpr float G	= -32.174f;		/// acceleration due to gravity, 

// Prosty wektor 3D uzywany do pozycji, kierunkow i skalowania.
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Quaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

// Macierz 4x4 w ukladzie zgodnym z tym, czego oczekuja shadery Vulkan.
struct Matrix4 {
    alignas(16) std::array<float, 16> values{};

    // Zwraca macierz jednostkowa, czyli taka, ktora nie zmienia transformowanego punktu.
    [[nodiscard]] static Matrix4 identity() {
        Matrix4 matrix{};
        matrix.values = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
        return matrix;
    }
};

// Odejmowanie wektorow daje wektor od punktu right do punktu left.
[[nodiscard]] inline Vec3 subtract(const Vec3& left, const Vec3& right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] inline Vec3 add(const Vec3& left, const Vec3& right) {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] inline Vec3 multiply(const Vec3& vec, float scalar) {
    return {vec.x * scalar, vec.y * scalar, vec.z * scalar};
}

[[nodiscard]] inline Vec3 lerp(const Vec3& left, const Vec3& right, float t) {
    return {
        left.x + (right.x - left.x) * t,
        left.y + (right.y - left.y) * t,
        left.z + (right.z - left.z) * t,
    };
}

// Iloczyn skalarny przydaje sie m.in. do projekcji i liczenia dlugosci wektora.
[[nodiscard]] inline float dot(const Vec3& left, const Vec3& right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

// Iloczyn wektorowy wyznacza wektor prostopadly do obu argumentow.
[[nodiscard]] inline Vec3 cross(const Vec3& left, const Vec3& right) {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

// Normalizacja sprowadza wektor do dlugosci 1, zachowujac jego kierunek.
[[nodiscard]] inline Vec3 normalize(const Vec3& vector) {
    const float length = std::sqrt(dot(vector, vector));
    if (length == 0.0f) {
        return {0.0f, 0.0f, 0.0f};
    }
    return {vector.x / length, vector.y / length, vector.z / length};
}

[[nodiscard]] inline float radians(float degrees) {
    return degrees * PI / 180.0f;
}

[[nodiscard]] inline Quaternion normalize(const Quaternion& quaternion) {
    const float length = std::sqrt(
        quaternion.x * quaternion.x +
        quaternion.y * quaternion.y +
        quaternion.z * quaternion.z +
        quaternion.w * quaternion.w
    );
    if (length == 0.0f) {
        return {};
    }

    return {
        quaternion.x / length,
        quaternion.y / length,
        quaternion.z / length,
        quaternion.w / length,
    };
}

[[nodiscard]] inline Quaternion conjugate(const Quaternion& quaternion) {
    return {-quaternion.x, -quaternion.y, -quaternion.z, quaternion.w};
}

[[nodiscard]] inline Quaternion multiply(const Quaternion& left, const Quaternion& right) {
    return {
        left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
        left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z,
    };
}

[[nodiscard]] inline Quaternion quaternionFromAxisAngle(const Vec3& axis, float angleRadians) {
    const Vec3 unitAxis = normalize(axis);
    const float halfAngle = angleRadians * 0.5f;
    const float sine = std::sin(halfAngle);
    return normalize({
        unitAxis.x * sine,
        unitAxis.y * sine,
        unitAxis.z * sine,
        std::cos(halfAngle),
    });
}

[[nodiscard]] inline Vec3 rotateVector(const Quaternion& rotation, const Vec3& vector) {
    const Quaternion vectorQuaternion{vector.x, vector.y, vector.z, 0.0f};
    const Quaternion rotated = multiply(multiply(rotation, vectorQuaternion), conjugate(rotation));
    return {rotated.x, rotated.y, rotated.z};
}

// Mnozy dwie macierze 4x4, aby laczyc kolejne transformacje w jedna.
[[nodiscard]] inline Matrix4 multiply(const Matrix4& left, const Matrix4& right) {
    Matrix4 result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float value = 0.0f;
            for (int index = 0; index < 4; ++index) {
                value += left.values[index * 4 + row] * right.values[column * 4 + index];
            }
            result.values[column * 4 + row] = value;
        }
    }
    return result;
}

// Tworzy macierz przesuniecia o zadany wektor.
[[nodiscard]] inline Matrix4 translation(float x, float y, float z) {
    Matrix4 matrix = Matrix4::identity();
    matrix.values[12] = x;
    matrix.values[13] = y;
    matrix.values[14] = z;
    return matrix;
}

// Tworzy macierz obrotu wokol osi X.
[[nodiscard]] inline Matrix4 rotationX(float angle) {
    Matrix4 matrix = Matrix4::identity();
    const float sine = std::sin(angle);
    const float cosine = std::cos(angle);
    matrix.values[5] = cosine;
    matrix.values[6] = sine;
    matrix.values[9] = -sine;
    matrix.values[10] = cosine;
    return matrix;
}

// Tworzy macierz skali niezaleznej dla kazdej osi.
[[nodiscard]] inline Matrix4 scale(float x, float y, float z) {
    Matrix4 matrix = Matrix4::identity();
    matrix.values[0] = x;
    matrix.values[5] = y;
    matrix.values[10] = z;
    return matrix;
}

// Tworzy macierz obrotu wokol osi Y.
[[nodiscard]] inline Matrix4 rotationY(float angle) {
    Matrix4 matrix = Matrix4::identity();
    const float sine = std::sin(angle);
    const float cosine = std::cos(angle);
    matrix.values[0] = cosine;
    matrix.values[2] = -sine;
    matrix.values[8] = sine;
    matrix.values[10] = cosine;
    return matrix;
}

// Tworzy macierz obrotu wokol osi Z.
[[nodiscard]] inline Matrix4 rotationZ(float angle) {
    Matrix4 matrix = Matrix4::identity();
    const float sine = std::sin(angle);
    const float cosine = std::cos(angle);
    matrix.values[0] = cosine;
    matrix.values[1] = sine;
    matrix.values[4] = -sine;
    matrix.values[5] = cosine;
    return matrix;
}

// Buduje macierz perspektywy, ktora zamienia frustum kamery na przestrzen clip-space.
[[nodiscard]] inline Matrix4 perspective(
    float fieldOfViewRadians,
    float aspectRatio,
    float nearPlane,
    float farPlane
) {
    const float inverseTanHalfFov = 1.0f / std::tan(fieldOfViewRadians * 0.5f);

    Matrix4 matrix{};
    // Vulkan NDC: Y points DOWN, so negate Y-scale vs. OpenGL
    matrix.values = {
        inverseTanHalfFov / aspectRatio, 0.0f, 0.0f, 0.0f,
        0.0f, -inverseTanHalfFov, 0.0f, 0.0f,
        0.0f, 0.0f, farPlane / (nearPlane - farPlane), -1.0f,
        0.0f, 0.0f, (farPlane * nearPlane) / (nearPlane - farPlane), 0.0f,
    };
    return matrix;
}

[[nodiscard]] inline Matrix4 ortho(float left, float right, float bottom, float top, float nearPlane, float farPlane) {
    Matrix4 matrix = Matrix4::identity();
    matrix.values[0] = 2.0f / (right - left);
    matrix.values[5] = 2.0f / (bottom - top); // Flip Y for Vulkan
    matrix.values[10] = -1.0f / (farPlane - nearPlane); // Map negative Z to [0,1]
    matrix.values[12] = -(right + left) / (right - left);
    matrix.values[13] = -(bottom + top) / (bottom - top);
    matrix.values[14] = -nearPlane / (farPlane - nearPlane);
    return matrix;
}

// Buduje macierz widoku kamery na podstawie pozycji oka, celu patrzenia i wektora "gora".
[[nodiscard]] inline Matrix4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    const Vec3 forward = normalize(subtract(center, eye));
    const Vec3 side = normalize(cross(forward, up));
    const Vec3 upVector = cross(side, forward);

    Matrix4 matrix = Matrix4::identity();
    matrix.values = std::array<float, 16>{
        side.x, upVector.x, -forward.x, 0.0f,
        side.y, upVector.y, -forward.y, 0.0f,
        side.z, upVector.z, -forward.z, 0.0f,
        -dot(side, eye), -dot(upVector, eye), dot(forward, eye), 1.0f,
    };
    return matrix;
}

[[nodiscard]] inline Matrix4 inverse(const Matrix4& m) {
    const auto& v = m.values;
    float s[6];
    s[0] = v[0]*v[5] - v[4]*v[1];
    s[1] = v[0]*v[6] - v[4]*v[2];
    s[2] = v[0]*v[7] - v[4]*v[3];
    s[3] = v[1]*v[6] - v[5]*v[2];
    s[4] = v[1]*v[7] - v[5]*v[3];
    s[5] = v[2]*v[7] - v[6]*v[3];

    float c[6];
    c[0] = v[8]*v[13] - v[12]*v[9];
    c[1] = v[8]*v[14] - v[12]*v[10];
    c[2] = v[8]*v[15] - v[12]*v[11];
    c[3] = v[9]*v[14] - v[13]*v[10];
    c[4] = v[9]*v[15] - v[13]*v[11];
    c[5] = v[10]*v[15] - v[14]*v[11];

    float det = s[0]*c[5] - s[1]*c[4] + s[2]*c[3] + s[3]*c[2] - s[4]*c[1] + s[5]*c[0];
    if (std::abs(det) < 1e-30f) return Matrix4::identity();
    float inv = 1.0f / det;

    Matrix4 r;
    r.values[0] = ( v[5]*c[5] - v[6]*c[4] + v[7]*c[3]) * inv;
    r.values[1] = (-v[1]*c[5] + v[2]*c[4] - v[3]*c[3]) * inv;
    r.values[2] = ( v[13]*s[5] - v[14]*s[4] + v[15]*s[3]) * inv;
    r.values[3] = (-v[9]*s[5] + v[10]*s[4] - v[11]*s[3]) * inv;
    r.values[4] = (-v[4]*c[5] + v[6]*c[2] - v[7]*c[1]) * inv;
    r.values[5] = ( v[0]*c[5] - v[2]*c[2] + v[3]*c[1]) * inv;
    r.values[6] = (-v[12]*s[5] + v[14]*s[2] - v[15]*s[1]) * inv;
    r.values[7] = ( v[8]*s[5] - v[10]*s[2] + v[11]*s[1]) * inv;
    r.values[8] = ( v[4]*c[4] - v[5]*c[2] + v[7]*c[0]) * inv;
    r.values[9] = (-v[0]*c[4] + v[1]*c[2] - v[3]*c[0]) * inv;
    r.values[10] = ( v[12]*s[4] - v[13]*s[2] + v[15]*s[0]) * inv;
    r.values[11] = (-v[8]*s[4] + v[9]*s[2] - v[11]*s[0]) * inv;
    r.values[12] = (-v[4]*c[3] + v[5]*c[1] - v[6]*c[0]) * inv;
    r.values[13] = ( v[0]*c[3] - v[1]*c[1] + v[2]*c[0]) * inv;
    r.values[14] = (-v[12]*s[3] + v[13]*s[1] - v[14]*s[0]) * inv;
    r.values[15] = ( v[8]*s[3] - v[9]*s[1] + v[10]*s[0]) * inv;
    return r;
}

[[nodiscard]] inline Matrix4 removeTranslation(const Matrix4& matrix) {
    Matrix4 result = matrix;
    result.values[12] = 0.0f;
    result.values[13] = 0.0f;
    result.values[14] = 0.0f;
    return result;
}

[[nodiscard]] inline Vec3 transformPoint(const Matrix4& matrix, const Vec3& point, float w = 1.0f) {
    return {
        matrix.values[0] * point.x + matrix.values[4] * point.y + matrix.values[8] * point.z + matrix.values[12] * w,
        matrix.values[1] * point.x + matrix.values[5] * point.y + matrix.values[9] * point.z + matrix.values[13] * w,
        matrix.values[2] * point.x + matrix.values[6] * point.y + matrix.values[10] * point.z + matrix.values[14] * w,
    };
}

[[nodiscard]] inline Vec3 projectPoint(const Matrix4& matrix, const Vec3& point, float w = 1.0f) {
    const float clipX = matrix.values[0] * point.x + matrix.values[4] * point.y + matrix.values[8] * point.z + matrix.values[12] * w;
    const float clipY = matrix.values[1] * point.x + matrix.values[5] * point.y + matrix.values[9] * point.z + matrix.values[13] * w;
    const float clipZ = matrix.values[2] * point.x + matrix.values[6] * point.y + matrix.values[10] * point.z + matrix.values[14] * w;
    const float clipW = matrix.values[3] * point.x + matrix.values[7] * point.y + matrix.values[11] * point.z + matrix.values[15] * w;
    if (std::abs(clipW) < 1e-6f) {
        return {clipX, clipY, clipZ};
    }
    return {clipX / clipW, clipY / clipW, clipZ / clipW};
}

// --- Pomocnicze funkcje dla gier (płaszczyzna XZ = poziom, Y = wysokość) ---

// Odległość w poziomie (bez wysokości) - zasięgi, wyszukiwanie najbliższych obiektów na mapie.
[[nodiscard]] inline float distanceXZ(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

// Płynne przejście 0..1, gdy x rośnie od e0 do e1 (działa też dla e0 > e1).
[[nodiscard]] inline float smoothstep(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

// Obrót wektora wokół osi Y o kąt a (zgodny z TransformComponent::rotation.y).
[[nodiscard]] inline Vec3 rotateY(const Vec3& v, float a) {
    const float c = std::cos(a), s = std::sin(a);
    return {c * v.x + s * v.z, v.y, -s * v.x + c * v.z};
}

// Interpolacja kąta najkrótszą drogą (350° -> 10° obraca się o 20°, a nie o 340°).
[[nodiscard]] inline float angleLerp(float from, float to, float t) {
    float d = std::fmod(to - from + PI, 2.0f * PI);
    if (d < 0.0f) d += 2.0f * PI;
    return from + (d - PI) * t;
}

}  // namespace engine3d
