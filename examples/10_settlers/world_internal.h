#pragma once
/**
 * world_internal.h - wspólne nagłówki i drobne funkcje matematyczne dla plików world_*.cpp.
 *
 * To nagłówek "wewnętrzny": dołączają go tylko implementacje klasy World,
 * reszta gry korzysta z settlers_world.h.
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "core/engine.h"
#include "core/entity.h"
#include "core/logger.h"
#include "core/mesh_builder.h"
#include "core/mesh_component.h"
#include "core/mesh_manager.h"
#include "object/terrain/terrain.h"
#include "object/terrain/terrain_component.h"
#include "render/material_manager.h"
#include "render/renderer.h"
#include "scene/scene.h"
#include "settlers_world.h"

namespace settlers {

using namespace engine3d;

// Odległość w poziomie (płaszczyzna XZ) - wysokość terenu nie ma znaczenia dla zasięgów.
inline float dist2D(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

// Płynne przejście 0..1, gdy x rośnie od e0 do e1 (działa też dla e0 > e1).
inline float smoothstep(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Obrót wektora wokół osi Y (zgodny z TransformComponent::rotation.y).
inline Vec3 rotateY(const Vec3& v, float a) {
    const float c = std::cos(a), s = std::sin(a);
    return {c * v.x + s * v.z, v.y, -s * v.x + c * v.z};
}

// Interpolacja kąta najkrótszą drogą (np. 350° -> 10° obraca się o 20°, a nie o 340°).
inline float angleLerp(float from, float to, float t) {
    float d = std::fmod(to - from + PI, 2.0f * PI);
    if (d < 0.0f) d += 2.0f * PI;
    return from + (d - PI) * t;
}

} // namespace settlers
