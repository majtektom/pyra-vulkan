#include "feature/particle/system.h"
#include "feature/particle/particle_event.h"
#include "feature/particle/particle.h"
#include "core/asset_registry.h"
#include "core/entity.h"
#include "core/mesh_component.h"
#include "core/mesh_manager.h"
#include "render/material_manager.h"
#include "render/material_system.h"
#include "render/renderer.h"
#include "scene/scene.h"
#include "core/types.h"
#include "core/logger.h"
#include "core/job_system.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
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

void ParticleSystem::updateParallel(float time) {
    for (auto* e : emitters_) e->updateParallel(time, jobSystem_);
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

// Następna niepusta linia pliku (bez komentarzy i białych znaków na brzegach).
static bool nextContentLine(std::istream& in, std::string& out) {
    while (std::getline(in, out)) {
        out = stripLineComments(out);
        out.erase(0, out.find_first_not_of(" \t\r\n"));
        out.erase(out.find_last_not_of(" \t\r\n") + 1);
        if (!out.empty()) return true;
    }
    return false;
}

/*
 * Format pliku .particle:
 *
 *   ParticleSystem 1.0 nazwa
 *   particles "ogien" {
 *       numparticles = 200          liczba cząsteczek
 *       emitrate = 30               nowych cząsteczek na sekundę
 *       texture = "plik.dds"
 *       gravity = (0, -2, 0)
 *       position = (x,y,z), (x,y,z) losowo między min a max (podobnie size, velocity, lifeTime, mass, color)
 *       fade 2 at 1 color = ...     zdarzenie: od 1. sekundy życia przez 2 s płynnie zmieniaj kolor
 *   }
 *
 * Każdy blok "particles" to jeden emiter.
 */
bool ParticleSystem::load(const std::string& file) {
    std::ifstream in(file);
    if (!in.is_open()) {
        LOG_ERROR("[ParticleSystem] nie można otworzyć pliku: {}", file);
        return false;
    }

    std::string line;
    if (!nextContentLine(in, line)) return false;
    std::stringstream header(line);
    std::string magic;
    header >> magic;
    if (magic != "ParticleSystem") {
        LOG_ERROR("[ParticleSystem] to nie jest plik systemu cząsteczek: {}", file);
        return false;
    }

    destroy();
    while (nextContentLine(in, line)) {
        std::stringstream ss(line);
        std::string keyword, name;
        ss >> keyword >> name;
        if (keyword != "particles") continue;
        // Nazwa w cudzysłowie: "ogien" -> ogien
        if (!name.empty() && name.front() == '"') name.erase(0, 1);
        if (!name.empty() && name.back() == '"') name.pop_back();

        if (!nextContentLine(in, line) || line != "{") {
            LOG_ERROR("[ParticleSystem] brak '{{' po nazwie cząsteczek w {}", file);
            return false;
        }
        auto* emitter = new ParticleEmitter();
        emitter->name_ = name;
        readEmitterBlock(in, emitter);
        if (!emitter->createParticles()) {
            LOG_ERROR("[ParticleSystem] błąd tworzenia cząsteczek emitera {} w {}", name, file);
            delete emitter;
            return false;
        }
        emitters_.push_back(emitter);
    }

    emittersBegin_ = emitters_.begin();
    emittersEnd_ = emitters_.end();
    return true;
}

// Linie emitera aż do "}": właściwości "nazwa = wartość" oraz zdarzenia "fade ... at ..." / "at ...".
void ParticleSystem::readEmitterBlock(std::istream& in, ParticleEmitter* emitter) {
    float fadeTime = 0.0f;   // czas przejścia z ostatniego "fade" (dla następnego "at")
    bool pendingFade = false;
    std::string line;
    while (nextContentLine(in, line) && line != "}") {
        std::string name;
        std::stringstream(line) >> name;
        // "nazwa=wartość" bez spacji: nazwa kończy się na '='.
        if (const size_t eq = name.find('='); eq != std::string::npos) name.erase(eq);
        std::string value;
        if (const size_t eq = line.find('='); eq != std::string::npos) {
            value = line.substr(eq + 1);
            value.erase(0, value.find_first_not_of(" \t"));
        }

        if (name == "fade") {
            // "fade <czas> [at <zdarzenie>]" - płynna zmiana trwająca <czas> sekund.
            std::string rest = line.substr(line.find("fade") + 4);
            rest.erase(0, rest.find_first_not_of(" \t"));
            std::stringstream(rest) >> fadeTime;
            pendingFade = true;
            if (const size_t at = rest.find("at"); at != std::string::npos) {
                processEvent(emitter, fadeTime, fadeTime, rest.substr(at + 2));
                pendingFade = false;
            }
        } else if (name == "at") {
            // "at <czas> <właściwość> = ..." - zmiana w danej chwili życia cząsteczki (płynna po "fade").
            const float t = pendingFade ? fadeTime : 0.0f;
            processEvent(emitter, t, t, line.substr(line.find("at") + 2));
            pendingFade = false;
        } else {
            applyEmitterProperty(emitter, name, value);
        }
    }
}

void ParticleSystem::applyEmitterProperty(ParticleEmitter* e, const std::string& name, const std::string& value) {
    if (name == "numparticles") {
        e->numParticles_ = processIntNumber(value).min;
    } else if (name == "emitrate") {
        e->emitRate_ = processIntNumber(value).min;
    } else if (name == "gravity") {
        e->gravity_ = processVector3(value).min;
    } else if (name == "texture") {
        std::string path = value;
        path.erase(0, path.find_first_not_of(" \t\""));
        path.erase(path.find_last_not_of(" \t\"") + 1);
        e->texturePath_ = path;
    } else if (name == "size") {
        const auto n = processRealNumber(value);
        e->setSize(n.min, n.max);
    } else if (name == "position") {
        const auto n = processVector3(value);
        e->setPosition(n.min, n.max);
    } else if (name == "velocity") {
        const auto n = processVector3(value);
        e->setVelocity(n.min, n.max);
    } else if (name == "lifeTime") {
        const auto n = processRealNumber(value);
        e->setLifeTime(n.min, n.max);
    } else if (name == "mass") {
        const auto n = processRealNumber(value);
        e->setMass(n.min, n.max);
    } else if (name == "color") {
        const auto n = processVector4(value);
        e->setColor(n.min, n.max);
    }
}

void ParticleSystem::initialize(Scene& scene,
                                MeshManager& meshManager,
                                MaterialManager& materialManager,
                                MaterialSystem& materialSystem,
                                Renderer& renderer,
                                AssetRegistry& assetRegistry,
                                JobSystem* jobSystem) {
    if (initialized_) return;

    scene_ = &scene;
    meshManager_ = &meshManager;
    materialManager_ = &materialManager;
    materialSystem_ = &materialSystem;
    renderer_ = &renderer;
    assetRegistry_ = &assetRegistry;

    meshNames_.clear();
    jobSystem_ = jobSystem;

    for (int ei = 0; ei < static_cast<int>(emitters_.size()); ei++) {
        auto* e = emitters_[ei];
        if (!e) continue;

        int numP = e->numParticles();
        if (numP <= 0) continue;

        std::string meshName = "particles_" + std::to_string(ei);
        meshNames_.push_back(meshName);

        auto textureHandle = e->textureHandle();
        if (!textureHandle.isValid()) {
            std::string texPath = e->texturePath();
            if (texPath.empty()) texPath = "assets/textures/particles/particle.dds";
            std::string texName = "particle_tex_" + std::to_string(ei);
            textureHandle = renderer_->loadTexture(texName, texPath);
            e->setTextureHandle(textureHandle);
        }

        Material mat;
        mat.name = "particleMat_" + std::to_string(ei);
        mat.textureHandle = textureHandle;
        mat.shaderProgram = "particle";
        mat.blendMode = BlendMode::Additive;
        mat.renderPriority = 1;
        mat.depthWrite = false;
        mat.cullMode = CullMode::Disabled;
        const auto materialHandle = materialManager_->add(mat);
        auto matInst = materialSystem_->getOrCreateMaterialInstance(mat);

        MeshData quadMesh;
        quadMesh.vertices = {
            {{{-0.5f, -0.5f, 0.0f}}, {{0,0,1}}, {{1,1,1}}, {{0,0}}},
            {{{ 0.5f, -0.5f, 0.0f}}, {{0,0,1}}, {{1,1,1}}, {{1,0}}},
            {{{ 0.5f,  0.5f, 0.0f}}, {{0,0,1}}, {{1,1,1}}, {{1,1}}},
            {{{-0.5f,  0.5f, 0.0f}}, {{0,0,1}}, {{1,1,1}}, {{0,1}}},
        };
        quadMesh.indices = {0, 1, 2, 2, 3, 0};
        meshManager_->loadMesh(meshName, quadMesh);

        VkDeviceSize instBufSize = static_cast<VkDeviceSize>(numP) * sizeof(ParticleInstanceData);
        meshManager_->loadInstanceBuffer(meshName, instBufSize);

        auto* entity = scene_->createEntity("emitter_" + std::to_string(ei));
        auto* mc = entity->addComponent<MeshComponent>(meshName);
        mc->materialHandle = materialHandle;
        mc->materialInstance = matInst;
    }

    initialized_ = true;
}

void ParticleSystem::update(float dt) {
    if (!initialized_) return;

    updateParallel(dt);

    int count = static_cast<int>(meshNames_.size());
    for (int ei = 0; ei < count; ei++) {
        std::vector<ParticleInstanceData>& instData = instanceScratch_;
        emitters_[ei]->fillInstanceDataParallel(instData, jobSystem_);

        uint32_t instanceCount = static_cast<uint32_t>(instData.size());
        if (instanceCount > 0) {
            meshManager_->updateInstanceBuffer(meshNames_[ei],
                instData.data(), instData.size() * sizeof(ParticleInstanceData), instanceCount);
        } else {
            meshManager_->updateInstanceBuffer(meshNames_[ei], nullptr, 0, 0);
        }
    }
}

} // namespace engine3d::particles
