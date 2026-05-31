#pragma once

#include <array>
#include "core/math.h"

namespace engine3d::particles {

enum EventType {
    EVENT_SIZE = 0,
    EVENT_MASS,
    EVENT_LIFETIME,
    EVENT_COLOR,
    EVENT_POSITION,
    EVENT_VELOCITY
};

struct RealNumber { float max, min; };
struct IntNumber { int max, min; };
struct Vec3Number { Vec3 min, max; };
struct Vec4Number { std::array<float, 4> min, max; };

} // namespace engine3d::particles
