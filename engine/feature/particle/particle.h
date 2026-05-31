#pragma once

#include <array>
#include <vector>
#include "core/math.h"

namespace engine3d::particles {

class ParticleEvent;
class ParticleEmitter;

struct Particle {
    Vec3 pos;
    Vec3 pos_step;
    float pos_time = 0;
    Vec3 velocity;
    Vec3 velocity_step;
    float velocity_time = 0;
    std::array<float, 4> color{1, 1, 1, 1};
    std::array<float, 4> color_step{0, 0, 0, 0};
    float color_time = 0;
    float size = 1;
    float size_step = 0;
    float size_time = 0;
    float mass = 1;
    float mass_step = 0;
    float mass_time = 0;
    float lifeTime = 3;
    float age = 0;
    float ageEvent = 0;

    using EventList = std::vector<ParticleEvent*>;
    EventList::iterator iter_event;
};

} // namespace engine3d::particles
