#pragma once

#include <array>
#include <cmath>

namespace engine3d {

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
    return degrees * 3.1415926535f / 180.0f;
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

[[nodiscard]] inline Matrix4 removeTranslation(const Matrix4& matrix) {
    Matrix4 result = matrix;
    result.values[12] = 0.0f;
    result.values[13] = 0.0f;
    result.values[14] = 0.0f;
    return result;
}

}  // namespace engine3d
