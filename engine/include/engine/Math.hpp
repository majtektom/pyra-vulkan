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
    matrix.values = {
        inverseTanHalfFov / aspectRatio, 0.0f, 0.0f, 0.0f,
        0.0f, inverseTanHalfFov, 0.0f, 0.0f,
        0.0f, 0.0f, farPlane / (nearPlane - farPlane), -1.0f,
        0.0f, 0.0f, (farPlane * nearPlane) / (nearPlane - farPlane), 0.0f,
    };
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

}  // namespace engine3d
