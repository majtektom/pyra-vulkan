#include "feature/particle/system.h"
#include "feature/particle/particle_event.h"
#include "feature/particle/particle.h"
#include "core/engine.h"
#include "core/mesh_component.h"
#include "core/mesh_manager.h"
#include "render/renderer.h"
#include "core/types.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

namespace engine3d::particles {

template<class T>
static void deleteAll(T& t) {
    for (auto i = t.begin(); i != t.end(); ++i) delete (*i);
    t.clear();
}

static std::string stripLineComments(std::string line) {
    size_t start = line.find("/*");
    while (start != std::string::npos) {
        size_t end = line.find("*/", start + 2);
        if (end != std::string::npos) {
            line.erase(start, end - start + 2);
        } else {
            line.erase(start);
            break;
        }
        start = line.find("/*");
    }
    size_t sl = line.find("//");
    if (sl != std::string::npos) line = line.substr(0, sl);
    return line;
}

static std::vector<std::string> splitByCommaOutsideParen(const std::string& s) {
    std::vector<std::string> parts;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '(') depth++;
        else if (s[i] == ')') depth--;
        else if (s[i] == ',' && depth == 0) {
            parts.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    parts.push_back(s.substr(start));
    return parts;
}

std::string ParticleSystem::extractParen(const std::string& s) {
    size_t start = s.find('(');
    size_t end = s.rfind(')');
    if (start == std::string::npos || end == std::string::npos || end <= start)
        return "";
    return s.substr(start + 1, end - start - 1);
}

RealNumber ParticleSystem::processRealNumber(const std::string& str) {
    RealNumber n{0, 0};
    std::string s = str;
    s.erase(0, s.find_first_not_of(" \t"));
    s.erase(s.find_last_not_of(" \t") + 1);
    if (s.find("RealRandom") == 0) {
        std::string inside = extractParen(s);
        std::stringstream ss(inside);
        float a, b; char comma;
        ss >> a >> comma >> b;
        n.min = a; n.max = b;
    } else {
        n.min = n.max = std::stof(s);
    }
    return n;
}

IntNumber ParticleSystem::processIntNumber(const std::string& str) {
    IntNumber n{0, 0};
    std::string s = str;
    s.erase(0, s.find_first_not_of(" \t"));
    s.erase(s.find_last_not_of(" \t") + 1);
    if (s.find("IntRandom") == 0) {
        std::string inside = extractParen(s);
        std::stringstream ss(inside);
        int a, b; char comma;
        ss >> a >> comma >> b;
        n.min = a; n.max = b;
    } else {
        n.min = n.max = std::stoi(s);
    }
    return n;
}

Vec3Number ParticleSystem::processVector3(const std::string& str) {
    Vec3Number v;
    v.min = v.max = {0, 0, 0};
    std::string inside = extractParen(str);
    auto parts = splitByCommaOutsideParen(inside);
    for (size_t c = 0; c < parts.size() && c < 3; c++) {
        RealNumber r = processRealNumber(parts[c]);
        if (c == 0) { v.min.x = r.min; v.max.x = r.max; }
        else if (c == 1) { v.min.y = r.min; v.max.y = r.max; }
        else if (c == 2) { v.min.z = r.min; v.max.z = r.max; }
    }
    return v;
}

Vec4Number ParticleSystem::processVector4(const std::string& str) {
    Vec4Number v;
    v.min = v.max = {0, 0, 0, 0};
    std::string inside = extractParen(str);
    auto parts = splitByCommaOutsideParen(inside);
    for (size_t c = 0; c < parts.size() && c < 4; c++) {
        RealNumber r = processRealNumber(parts[c]);
        if (c == 0) { v.min[0] = r.min; v.max[0] = r.max; }
        else if (c == 1) { v.min[1] = r.min; v.max[1] = r.max; }
        else if (c == 2) { v.min[2] = r.min; v.max[2] = r.max; }
        else if (c == 3) { v.min[3] = r.min; v.max[3] = r.max; }
    }
    return v;
}

bool ParticleSystem::processEvent(ParticleEmitter* emitter, float fadeMin, float fadeMax, const std::string& line) {
    ParticleEvent* e = nullptr;
    std::string s = line;
    s.erase(0, s.find_first_not_of(" \t"));

    std::stringstream ss(s);
    float time;
    ss >> time;

    std::string rest;
    std::getline(ss, rest);
    rest.erase(0, rest.find_first_not_of(" \t"));

    size_t eqPos = rest.find('=');
    if (eqPos == std::string::npos) return false;
    std::string varName = rest.substr(0, eqPos);
    std::string varValue = rest.substr(eqPos + 1);
    varValue.erase(0, varValue.find_first_not_of(" \t"));

    if (varName.empty()) return false;

         if (varName == "size")     { auto n = processRealNumber(varValue); e = new ParticleEvent(); e->type = EVENT_SIZE; e->min_1 = n.min; e->max_1 = n.max; }
    else if (varName == "lifeTime") { auto n = processRealNumber(varValue); e = new ParticleEvent(); e->type = EVENT_LIFETIME; e->min_1 = n.min; e->max_1 = n.max; }
    else if (varName == "mass")     { auto n = processRealNumber(varValue); e = new ParticleEvent(); e->type = EVENT_MASS; e->min_1 = n.min; e->max_1 = n.max; }
    else if (varName == "position") { auto n = processVector3(varValue); e = new ParticleEvent(); e->type = EVENT_POSITION;
        e->min_1 = n.min.x; e->min_2 = n.min.y; e->min_3 = n.min.z;
        e->max_1 = n.max.x; e->max_2 = n.max.y; e->max_3 = n.max.z; }
    else if (varName == "velocity") { auto n = processVector3(varValue); e = new ParticleEvent(); e->type = EVENT_VELOCITY;
        e->min_1 = n.min.x; e->min_2 = n.min.y; e->min_3 = n.min.z;
        e->max_1 = n.max.x; e->max_2 = n.max.y; e->max_3 = n.max.z; }
    else if (varName == "color")    { auto n = processVector4(varValue); e = new ParticleEvent(); e->type = EVENT_COLOR;
        e->min_1 = n.min[0]; e->min_2 = n.min[1]; e->min_3 = n.min[2]; e->min_4 = n.min[3];
        e->max_1 = n.max[0]; e->max_2 = n.max[1]; e->max_3 = n.max[2]; e->max_4 = n.max[3]; }

    if (!e) return false;
    e->time = time;
    e->min_duration = fadeMin;
    e->max_duration = fadeMax;
    emitter->addEvent(e);
    return true;
}

ParticleSystem::ParticleSystem() {}
ParticleSystem::~ParticleSystem() { destroy(); }

bool ParticleSystem::destroy() {
    deleteAll(emitters_);
    emittersBegin_ = emitters_.begin();
    emittersEnd_ = emitters_.end();
    return true;
}

std::string ParticleSystem::infoParticles(int index) {
    if (emitters_.size() > static_cast<size_t>(index)) {
        ParticleEmitter* e = *(emitters_.begin() + index);
        return e->name() + ", maks=" + std::to_string(e->numParticles())
            + ", aktywne=" + std::to_string(e->particles_ ? e->particles_->GetNumUsedElements() : 0);
    }
    return "";
}

void ParticleSystem::update(float time, const Vec3&, const Vec3&) {
    for (auto* e : emitters_) e->update(time);
}

void ParticleSystem::updateParallel(float time, int threadCount) {
    int perEmitterThreads = std::max(1, threadCount / std::max(1, static_cast<int>(emitters_.size())));
    for (auto* e : emitters_) e->updateParallel(time, perEmitterThreads);
}

ParticleEmitter* ParticleSystem::addEmitter(const std::string& name) {
    auto* e = new ParticleEmitter();
    e->setName(name);
    emitters_.push_back(e);
    return e;
}

ParticleEmitter* ParticleSystem::getEmitter(int index) {
    if (index < 0 || index >= static_cast<int>(emitters_.size())) return nullptr;
    return emitters_[index];
}

bool ParticleSystem::load(const std::string& file) {
    std::ifstream f(file);
    if (!f.is_open()) {
        std::cerr << "[ParticleSystem] Cannot open file: " << file << std::endl;
        return false;
    }

    auto nextLine = [&](std::string& out) -> bool {
        while (std::getline(f, out)) {
            out = stripLineComments(out);
            out.erase(0, out.find_first_not_of(" \t\r\n"));
            out.erase(out.find_last_not_of(" \t\r\n") + 1);
            if (!out.empty()) return true;
        }
        return false;
    };

    std::string line;
    if (!nextLine(line)) return false;
    std::stringstream header(line);
    std::string magic;
    float version;
    std::string sysName;
    header >> magic >> version >> sysName;
    if (magic != "ParticleSystem") {
        std::cerr << "[ParticleSystem] Not a particle system file" << std::endl;
        return false;
    }

    destroy();

    while (nextLine(line)) {
        std::stringstream ss(line);
        std::string keyword;
        ss >> keyword;

        if (keyword != "particles") continue;

        std::string name;
        ss >> name;
        if (!name.empty() && name[0] == '"') {
            name = name.substr(1);
            if (!name.empty() && name.back() == '"') name.pop_back();
        }

        if (!nextLine(line) || line != "{") {
            std::cerr << "[ParticleSystem] Expected '{' after particles name" << std::endl;
            return false;
        }

        ParticleEmitter* emitter = new ParticleEmitter();
        emitter->name_ = name;

        float fadeMin = 0, fadeMax = 0;
        bool inFade = false;

        while (nextLine(line)) {
            if (line == "}") break;

            std::stringstream ls(line);
            std::string varName;
            ls >> varName;

            std::string lineVal;
            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos) {
                lineVal = line.substr(eqPos + 1);
                lineVal.erase(0, lineVal.find_first_not_of(" \t"));
            }

            size_t eqInVar = varName.find('=');
            if (eqInVar != std::string::npos) {
                if (lineVal.empty()) lineVal = varName.substr(eqInVar + 1);
                varName = varName.substr(0, eqInVar);
            }

            auto getVal = [&]() -> std::string {
                if (!lineVal.empty()) return lineVal;
                size_t e = line.find('=');
                if (e == std::string::npos) return "";
                std::string v = line.substr(e + 1);
                v.erase(0, v.find_first_not_of(" \t"));
                return v;
            };

            if (varName == "fade") {
                std::string rest = line.substr(line.find("fade") + 4);
                rest.erase(0, rest.find_first_not_of(" \t"));
                std::stringstream fs(rest);
                float fadeTime;
                fs >> fadeTime;
                fadeMin = fadeMax = fadeTime;
                inFade = true;
                size_t atPos = rest.find("at");
                if (atPos != std::string::npos) {
                    processEvent(emitter, fadeMin, fadeMax, rest.substr(atPos + 2));
                    inFade = false;
                }
            } else if (varName == "at") {
                processEvent(emitter, inFade ? fadeMin : 0, inFade ? fadeMax : 0,
                            line.substr(line.find("at") + 2));
                inFade = false;
            } else if (varName == "numparticles") { emitter->numParticles_ = processIntNumber(getVal()).min; }
            else if (varName == "emitrate")   { emitter->emitRate_ = processIntNumber(getVal()).min; }
            else if (varName == "gravity")    { emitter->gravity_ = processVector3(getVal()).min; }
            else if (varName == "texture")    { std::string v = getVal();
                v.erase(0, v.find_first_not_of(" \t\""));
                v.erase(v.find_last_not_of(" \t\"") + 1);
                emitter->textureName_ = v; }
            else if (varName == "size")       { auto n = processRealNumber(getVal()); emitter->minSize_ = n.min; emitter->maxSize_ = n.max; }
            else if (varName == "position")   { auto n = processVector3(getVal()); emitter->minPos_ = n.min; emitter->maxPos_ = n.max; }
            else if (varName == "velocity")   { auto n = processVector3(getVal()); emitter->minVel_ = n.min; emitter->maxVel_ = n.max; }
            else if (varName == "lifeTime")   { auto n = processRealNumber(getVal()); emitter->minLife_ = n.min; emitter->maxLife_ = n.max; }
            else if (varName == "mass")       { auto n = processRealNumber(getVal()); emitter->minMass_ = n.min; emitter->maxMass_ = n.max; }
            else if (varName == "color")      { auto n = processVector4(getVal()); emitter->minColor_ = n.min; emitter->maxColor_ = n.max; }
        }

        if (!emitter->createParticles()) {
            std::cerr << "[ParticleSystem] Error creating particles" << std::endl;
            delete emitter;
            return false;
        }
        emitters_.push_back(emitter);
    }

    emittersBegin_ = emitters_.begin();
    emittersEnd_ = emitters_.end();
    return true;
}

void ParticleSystem::initialize(Engine& engine) {
    if (initialized_) return;

    meshNames_.clear();
    numThreads_ = std::max(1u, std::thread::hardware_concurrency());

    auto* renderer = engine.getRenderer();
    for (int ei = 0; ei < static_cast<int>(emitters_.size()); ei++) {
        auto* e = emitters_[ei];
        if (!e) continue;

        int numP = e->numParticles();
        if (numP <= 0) continue;

        std::string meshName = "particles_" + std::to_string(ei);
        meshNames_.push_back(meshName);

        std::string texPath = e->textureName();
        if (texPath.empty()) texPath = "assets/textures/particles/particle.dds";
        std::string texName = "particle_tex_" + std::to_string(ei);
        renderer->loadTexture(texName, texPath);

        Material mat;
        mat.name = "particleMat_" + std::to_string(ei);
        mat.textureName = texName;
        mat.shaderProgram = "particle";
        mat.blendMode = BlendMode::Additive;
        mat.renderPriority = 1;
        mat.depthWrite = false;
        mat.cullMode = CullMode::Disabled;
        engine.getMaterialManager()->add(mat);
        auto matInst = engine.getMaterialSystem()->getOrCreateMaterialInstance(mat);

        MeshData quadMesh;
        quadMesh.vertices = {
            {{{-0.5f, -0.5f, 0.0f}}, {{0,0,1}}, {{1,1,1}}, {{0,0}}},
            {{{ 0.5f, -0.5f, 0.0f}}, {{0,0,1}}, {{1,1,1}}, {{1,0}}},
            {{{ 0.5f,  0.5f, 0.0f}}, {{0,0,1}}, {{1,1,1}}, {{1,1}}},
            {{{-0.5f,  0.5f, 0.0f}}, {{0,0,1}}, {{1,1,1}}, {{0,1}}},
        };
        quadMesh.indices = {0, 1, 2, 2, 3, 0};
        engine.getMeshManager()->loadMesh(meshName, quadMesh);

        VkDeviceSize instBufSize = static_cast<VkDeviceSize>(numP) * sizeof(ParticleInstanceData);
        engine.getMeshManager()->loadInstanceBuffer(meshName, instBufSize);

        auto* entity = engine.createEntity("emitter_" + std::to_string(ei));
        auto* mc = entity->addComponent<MeshComponent>(meshName);
        mc->materialInstance = matInst;
    }

    initialized_ = true;
}

void ParticleSystem::update(float dt, Engine& engine) {
    if (!initialized_) return;

    updateParallel(dt, numThreads_);

    int count = static_cast<int>(meshNames_.size());
    for (int ei = 0; ei < count; ei++) {
        std::vector<ParticleInstanceData> instData;
        emitters_[ei]->fillInstanceDataParallel(instData, numThreads_);

        uint32_t instanceCount = static_cast<uint32_t>(instData.size());
        if (instanceCount > 0) {
            engine.getMeshManager()->updateInstanceBuffer(meshNames_[ei],
                instData.data(), instData.size() * sizeof(ParticleInstanceData), instanceCount);
        } else {
            engine.getMeshManager()->updateInstanceBuffer(meshNames_[ei], nullptr, 0, 0);
        }
    }
}

} // namespace engine3d::particles
