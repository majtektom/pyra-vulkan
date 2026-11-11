#pragma once

#include <string>
#include <vector>
#include <array>
#include "core/asset_registry.h"
#include "core/math.h"
#include "core/types.h"
#include "feature/particle/types.h"
#include "feature/particle/particle.h"
#include "feature/particle/recycling_array.h"

namespace engine3d {
class JobSystem;
}

namespace engine3d::particles {

class ParticleSystem;

class ParticleEmitter {
public:
    using EventList = std::vector<ParticleEvent*>;
    using EventIter = EventList::iterator;

    ParticleEmitter();
    ~ParticleEmitter();

    bool createParticles();
    bool destroy();
    void update(float time);
    void updateParallel(float time, JobSystem* jobs);   // jobs == nullptr: jednowątkowo
    void fillInstanceData(std::vector<ParticleInstanceData>& out);
    void fillInstanceDataParallel(std::vector<ParticleInstanceData>& out, JobSystem* jobs);
    void sortEvents();
    void addEvent(ParticleEvent* e) { events_.push_back(e); }

    void setName(const std::string& n) { name_ = n; }
    void setTextureHandle(TextureHandle handle) { textureHandle_ = handle; }
    void setNumParticles(int n) { numParticles_ = n; }
    void setEmitRate(int r) { emitRate_ = r; }
    void setGravity(const Vec3& g) { gravity_ = g; }
    void setPosition(const Vec3& min, const Vec3& max) { minPos_ = min; maxPos_ = max; }
    void setVelocity(const Vec3& min, const Vec3& max) { minVel_ = min; maxVel_ = max; }
    void setLifeTime(float min, float max) { minLife_ = min; maxLife_ = max; }
    void setSize(float min, float max) { minSize_ = min; maxSize_ = max; }
    void setMass(float min, float max) { minMass_ = min; maxMass_ = max; }
    void setColor(const std::array<float, 4>& min, const std::array<float, 4>& max) { minColor_ = min; maxColor_ = max; }

    const std::string& name() const { return name_; }
    int numParticles() const { return numParticles_; }
    const std::string& texturePath() const { return texturePath_; }
    TextureHandle textureHandle() const { return textureHandle_; }

private:
    friend class ParticleSystem;

    EventList events_;
    EventIter beginEvents_, endEvents_;
    std::string name_;
    std::string texturePath_;
    TextureHandle textureHandle_{};
    int numParticles_ = 1;
    float newPartsExcess_ = 0;
    Vec3 gravity_{0, 0, 0};
    int emitRate_ = 1;
    float minLife_ = 3, maxLife_ = 3;
    float minSize_ = 1, maxSize_ = 1;
    Vec3 minPos_{0, 0, 0}, maxPos_{0, 0, 0};
    Vec3 minVel_{0, 0, 0}, maxVel_{0, 0, 0};
    std::array<float, 4> minColor_{1, 1, 1, 1}, maxColor_{1, 1, 1, 1};
    float minMass_ = 1, maxMass_ = 1;
    RecyclingArray<Particle>* particles_ = nullptr;
    int lastActiveCount_ = -1;
    std::vector<int> activeIdx_;   // indeksy żywych cząsteczek (fillInstanceDataParallel)
    void stepParticle(Particle* par, float time, const Vec3& grav) const;
    void stepRange(size_t begin, size_t end, float time);
    void removeExpired();
    void spawn(float time);
};

} // namespace engine3d::particles
