#pragma once

#include "feature/particle/types.h"

namespace engine3d::particles {

struct Particle;
class ParticleEmitter;

class ParticleEvent {
public:
    ~ParticleEvent() {}

    float time;
    EventType type;
    float min_duration = 0, max_duration = 0;
    float min_1 = 0, max_1 = 0;
    float min_2 = 0, max_2 = 0;
    float min_3 = 0, max_3 = 0;
    float min_4 = 0, max_4 = 0;

    float GetTime() const { return time; }
    void RunEvent(Particle* p);
};

} // namespace engine3d::particles
