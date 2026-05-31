#pragma once

#include <string>
#include <vector>
#include "core/math.h"
#include "feature/particle/types.h"
#include "feature/particle/emitter.h"
#include "render/render_feature.h"

namespace engine3d {

class Engine;

namespace particles {

class ParticleSystem : public IRenderFeature {
public:
    ParticleSystem();
    ~ParticleSystem();

    bool load(const std::string& file);
    bool destroy();
    void update(float time, const Vec3& camRight, const Vec3& camUp);
    void updateParallel(float time, int threadCount);
    std::string infoParticles(int index);

    void initialize(Engine& engine);
    void update(float dt, Engine& engine);

    void setCameraVectors(const Vec3& right, const Vec3& up) { camRight_ = right; camUp_ = up; }
    void setCameraPosition(const Vec3& pos) { camPos_ = pos; }
    ParticleEmitter* addEmitter(const std::string& name);
    ParticleEmitter* getEmitter(int index);
    int emitterCount() const { return static_cast<int>(emitters_.size()); }
    void setPosition(const Vec3& pos) { position_ = pos; }
    const Vec3& position() const { return position_; }

    void init(Renderer*) override {}
    void render(VkCommandBuffer, uint32_t, const RenderView&) override {}
    bool isActive() const override { return initialized_; }
    const std::string& getName() const override { static std::string n = "ParticleSystem"; return n; }

private:
    static std::string extractParen(const std::string& s);
    RealNumber processRealNumber(const std::string& str);
    IntNumber processIntNumber(const std::string& str);
    Vec3Number processVector3(const std::string& str);
    Vec4Number processVector4(const std::string& str);
    bool processEvent(ParticleEmitter* emitter, float fadeMin, float fadeMax, const std::string& line);

    std::vector<ParticleEmitter*> emitters_;
    using EmitterIter = std::vector<ParticleEmitter*>::iterator;
    EmitterIter emittersBegin_, emittersEnd_;
    Vec3 position_{0, 0, 0};
    Vec3 camRight_{1, 0, 0};
    Vec3 camUp_{0, 1, 0};
    Vec3 camPos_{0, 0, 0};
    std::vector<std::string> meshNames_;
    int numThreads_ = 1;
    bool initialized_ = false;
};

} // namespace particles
} // namespace engine3d
