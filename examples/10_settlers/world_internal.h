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

// Odległość w poziomie (płaszczyzna XZ) - krótsza nazwa dla engine3d::distanceXZ.
// smoothstep, rotateY i angleLerp pochodzą z silnika (core/math.h).
inline float dist2D(const Vec3& a, const Vec3& b) {
    return distanceXZ(a, b);
}

} // namespace settlers
