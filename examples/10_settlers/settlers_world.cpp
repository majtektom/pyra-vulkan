#include "settlers_world.h"

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

using namespace engine3d;

namespace settlers {

namespace {

float dist2D(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

float smoothstep(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Vec3 rotateY(const Vec3& v, float a) {
    const float c = std::cos(a), s = std::sin(a);
    return {c * v.x + s * v.z, v.y, -s * v.x + c * v.z};
}

float angleLerp(float from, float to, float t) {
    float d = std::fmod(to - from + PI, 2.0f * PI);
    if (d < 0.0f) d += 2.0f * PI;
    return from + (d - PI) * t;
}

// --- Szum wartości (value noise) do generowania terenu ---
struct Noise {
    uint32_t seed = 1;

    float hash(int x, int z) const {
        uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;
        return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu) * 2.0f - 1.0f;
    }

    float value(float x, float z) const {
        const int ix = static_cast<int>(std::floor(x));
        const int iz = static_cast<int>(std::floor(z));
        const float fx = x - static_cast<float>(ix);
        const float fz = z - static_cast<float>(iz);
        const float ux = fx * fx * (3.0f - 2.0f * fx);
        const float uz = fz * fz * (3.0f - 2.0f * fz);
        const float a = hash(ix, iz), b = hash(ix + 1, iz);
        const float c = hash(ix, iz + 1), d = hash(ix + 1, iz + 1);
        return (a + (b - a) * ux) * (1.0f - uz) + (c + (d - c) * ux) * uz;
    }

    float fbm(float x, float z, int octaves) const {
        float sum = 0.0f, amp = 0.5f, freq = 1.0f, norm = 0.0f;
        for (int i = 0; i < octaves; ++i) {
            sum += value(x * freq + i * 17.3f, z * freq - i * 9.1f) * amp;
            norm += amp;
            amp *= 0.5f;
            freq *= 2.03f;
        }
        return sum / norm;
    }
};

// Kolejność budowy komputerowego przeciwnika.
const BuildingType kAiBuildOrder[] = {
    BuildingType::Woodcutter, BuildingType::House, BuildingType::Quarry, BuildingType::Woodcutter,
    BuildingType::Farm, BuildingType::Quarry, BuildingType::Barracks, BuildingType::House,
    BuildingType::Woodcutter, BuildingType::Tower, BuildingType::House, BuildingType::Farm,
    BuildingType::Barracks, BuildingType::Quarry, BuildingType::Tower, BuildingType::House,
};
const BuildingType kAiLateCycle[] = {
    BuildingType::House, BuildingType::Woodcutter, BuildingType::Tower, BuildingType::House, BuildingType::Barracks,
    BuildingType::Quarry, BuildingType::Farm,
};

} // namespace

Vec3 Building::door() const {
    const float half = buildingDef(type).halfSize;
    return add(pos, rotateY({0.0f, 0.0f, half + 3.0f}, rotY));
}

World::World(Engine& engine, const GameMaterials& materials) : engine_(engine), materials_(materials) {
    units.reserve(2048);
    buildings.reserve(512);
    nodes.reserve(1024);
}

World::~World() = default;

float World::randf(float a, float b) {
    std::uniform_real_distribution<float> d(a, b);
    return d(rng_);
}

float World::heightAt(float x, float z) const {
    return terrain_ ? terrain_->getHeight(x, z) : 0.0f;
}

void World::emitSound(const char* name, const Vec3& pos, float volume, bool positional) {
    if (soundEvents.size() < 256) soundEvents.push_back({name, pos, volume, positional});
}

void World::pushMessage(const std::string& text, std::array<float, 4> color) {
    messages.push_back({text, color, gameTime});
    while (messages.size() > 6) messages.pop_front();
}

// =====================================================================================
// Tworzenie świata
// =====================================================================================

bool World::create(Scene* scene, uint32_t seed, Difficulty diff) {
    scene_ = scene;
    seed_ = seed;
    difficulty = diff;
    rng_.seed(seed);

    // Bazy po przekątnej mapy (losowo zamieniane stronami)
    const bool flip = (seed & 1u) != 0;
    startPos[kPlayer] = flip ? Vec3{280.0f, 0.0f, -270.0f} : Vec3{-280.0f, 0.0f, 270.0f};
    startPos[kEnemy] = flip ? Vec3{-280.0f, 0.0f, 270.0f} : Vec3{280.0f, 0.0f, -270.0f};

    generateHeightmap(seed);

    terrain_ = std::make_shared<Terrain>();
    terrain_->setHeightmap(heightmap_, kHeightmapSize);
    terrain_->setScale(kScaleXZ, kScaleY, 72.0f, 6);
    terrain_->buildMesh();
    terrain_->uploadTo(*engine_.getMeshManager());
    createTerrainEntity();
    buildNavGrid();

    for (auto& p : startPos) {
        p.y = heightAt(p.x, p.z);
    }

    placeResources();

    for (int f = 0; f < kFactionCount; ++f) {
        territoryEntity_[f] = makeEntity("territory", "");
        territoryEntity_[f]->getComponent<MeshComponent>()->castShadows = false;
    }

    // Startowe zasoby i zamki
    factions = {};
    factions[kPlayer].res = {60, 40, 50};
    factions[kEnemy].res = {60, 40, 50};
    if (diff == Difficulty::Hard) factions[kEnemy].res = {120, 90, 100};
    if (diff == Difficulty::Easy) factions[kPlayer].res = {100, 70, 80};

    for (int f = 0; f < kFactionCount; ++f) {
        const int castle = placeBuilding(BuildingType::Castle, snapToGrid(startPos[f]), f, true);
        factions[f].castle = castle;
        const Vec3 door = buildings[castle].door();
        for (int i = 0; i < 6; ++i) {
            spawnUnit(UnitType::Settler, f, add(door, {randf(-8, 8), 0.0f, randf(2, 10)}));
        }
        const int swords = (f == kEnemy && diff == Difficulty::Hard) ? 4 : 2;
        for (int i = 0; i < swords; ++i) {
            spawnUnit(UnitType::Swordsman, f, add(door, {-10.0f + i * 5.0f, 0.0f, 16.0f}));
        }
        spawnUnit(UnitType::Archer, f, add(door, {12.0f, 0.0f, 14.0f}));
    }

    // Przeciwnik zaczyna z kilkoma budynkami gospodarczymi (zależnie od poziomu trudności).
    {
        const int extra = diff == Difficulty::Easy ? 1 : (diff == Difficulty::Normal ? 2 : 4);
        const BuildingType pre[] = {BuildingType::Woodcutter, BuildingType::Farm, BuildingType::Quarry, BuildingType::Barracks};
        for (int i = 0; i < extra; ++i) {
            Vec3 p;
            if (findPlacementNear(pre[i], kEnemy, startPos[kEnemy], 35.0f, 90.0f, p)) {
                placeBuilding(pre[i], p, kEnemy, true);
            }
        }
        factions[kEnemy].waveTimer = diff == Difficulty::Easy ? 480.0f : (diff == Difficulty::Normal ? 330.0f : 220.0f);
        factions[kEnemy].aiThinkTimer = 5.0f;
    }

    rebuildTerritory();
    buildMinimap();
    nav_.computeRegions(true);
    regionsDirty_ = false;

    pushMessage("Witaj, władco! Rozbuduj osadę i zniszcz zamek wroga.", {1.0f, 0.95f, 0.6f, 1.0f});
    pushMessage("Zacznij od Drwala, Kamieniarza i Farmy.", {0.85f, 0.95f, 1.0f, 1.0f});
    return true;
}

void World::generateHeightmap(uint32_t seed) {
    Noise base{seed * 7u + 1u};
    Noise ridge{seed * 13u + 5u};
    const int n = kHeightmapSize;
    heightmap_.assign(static_cast<size_t>(n) * n, 0.0f);

    const Vec3 a = startPos[kPlayer], b = startPos[kEnemy];
    const Vec3 ab = subtract(b, a);
    const float abLen2 = dot(ab, ab);

    for (int iz = 0; iz < n; ++iz) {
        for (int ix = 0; ix < n; ++ix) {
            const float x = (static_cast<float>(ix) - n * 0.5f) * kScaleXZ;
            const float z = (static_cast<float>(iz) - n * 0.5f) * kScaleXZ;

            float h = 44.0f + base.fbm(x / 230.0f, z / 230.0f, 5) * 40.0f;

            // Grzbiety górskie
            const float r = 1.0f - std::abs(ridge.fbm(x / 340.0f, z / 340.0f, 4)) * 2.2f;
            if (r > 0.0f) h += r * r * r * 60.0f;

            // Pierścień gór na krawędzi mapy
            const float edge = std::max(std::abs(x), std::abs(z)) / kWorldHalf;
            if (edge > 0.8f) {
                const float e = (edge - 0.8f) / 0.2f;
                h += e * e * 95.0f;
            }

            // Płaskowyże pod bazy
            for (const auto& s : startPos) {
                const float d = dist2D({x, 0, z}, s);
                const float w = smoothstep(175.0f, 85.0f, d);
                h = h + (46.0f + base.fbm(x / 60.0f, z / 60.0f, 2) * 3.0f - h) * w;
            }

            // Lądowy korytarz między bazami (gwarantuje drogę do wroga)
            float t = std::clamp(((x - a.x) * ab.x + (z - a.z) * ab.z) / abLen2, 0.0f, 1.0f);
            const Vec3 onSeg{a.x + ab.x * t, 0.0f, a.z + ab.z * t};
            const float ds = dist2D({x, 0, z}, onSeg);
            const float wc = smoothstep(80.0f, 35.0f, ds);
            const float corridorH = 40.0f + base.fbm(x / 90.0f, z / 90.0f, 2) * 6.0f;
            if (h < corridorH) h = h + (corridorH - h) * wc;
            if (h > corridorH + 20.0f) h = h + (corridorH + 20.0f - h) * wc * 0.8f;

            heightmap_[static_cast<size_t>(iz) * n + ix] = std::clamp(h / kScaleY, 0.0f, 255.0f);
        }
    }

    // Wygładzenie (2 przebiegi filtra pudełkowego 3x3)
    std::vector<float> tmp(heightmap_.size());
    for (int pass = 0; pass < 2; ++pass) {
        for (int iz = 0; iz < n; ++iz) {
            for (int ix = 0; ix < n; ++ix) {
                float sum = 0.0f;
                int cnt = 0;
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int x = std::clamp(ix + dx, 0, n - 1), z = std::clamp(iz + dz, 0, n - 1);
                        sum += heightmap_[static_cast<size_t>(z) * n + x];
                        ++cnt;
                    }
                }
                tmp[static_cast<size_t>(iz) * n + ix] = sum / static_cast<float>(cnt);
            }
        }
        heightmap_.swap(tmp);
    }
}

void World::createTerrainEntity() {
    auto* renderer = engine_.getRenderer();
    auto* mats = engine_.getMaterialManager();
    const int n = kHeightmapSize;

    // Maski warstw (splat maps) wyliczane z wysokości i nachylenia
    std::vector<float> maskVariation(static_cast<size_t>(n) * n);
    std::vector<float> maskSand(maskVariation.size());
    std::vector<float> maskRock(maskVariation.size());
    std::vector<float> maskSnow(maskVariation.size());
    Noise patches{seed_ * 31u + 3u};
    for (int iz = 0; iz < n; ++iz) {
        for (int ix = 0; ix < n; ++ix) {
            const size_t i = static_cast<size_t>(iz) * n + ix;
            const float x = (static_cast<float>(ix) - n * 0.5f) * kScaleXZ;
            const float z = (static_cast<float>(iz) - n * 0.5f) * kScaleXZ;
            const float h = heightmap_[i] * kScaleY;
            const Vec3 nrm = terrain_->getNormal(x, z);
            const float slope = 1.0f - nrm.y;

            maskVariation[i] = 255.0f * smoothstep(0.05f, 0.45f, patches.fbm(x / 70.0f, z / 70.0f, 3));
            maskSand[i] = 255.0f * smoothstep(kWaterLevel + 5.0f, kWaterLevel + 1.0f, h);
            maskRock[i] = 255.0f * std::max(smoothstep(0.12f, 0.25f, slope), smoothstep(78.0f, 92.0f, h));
            maskSnow[i] = 255.0f * smoothstep(112.0f, 125.0f, h);
        }
    }

    struct LayerSpec {
        const char* texture;
        const std::vector<float>* mask;
    };
    const LayerSpec layers[] = {
        {"assets/textures/grass.dds", nullptr},
        {"assets/textures/trawa.dds", &maskVariation},
        {"assets/textures/sand.bmp", &maskSand},
        {"assets/teren/wyspa/3_rock_y.bmp", &maskRock},
        {"assets/textures/snieg.bmp", &maskSnow},
    };
    const uint32_t layerCount = static_cast<uint32_t>(std::size(layers));

    Material terrainMaterial;
    terrainMaterial.name = "settlers_terrain_" + std::to_string(seed_);
    terrainMaterial.shaderProgram = "terrain";
    terrainMaterial.cullMode = CullMode::Back;
    terrainMaterial.textureHandle = renderer->loadTexture("settlers_layer_0", layers[0].texture);
    const MaterialHandle terrainHandle = mats->add(terrainMaterial);

    Entity* e = scene_->createEntity("settlers_terrain");
    e->transform().position = terrain_->worldOffset();
    auto* tc = e->addComponent<TerrainComponent>(terrain_);
    tc->materialHandle = terrainHandle;
    tc->castShadows = true;
    tc->layerBlendEnabled = true;
    tc->layerCount = layerCount;
    tc->layerWorldSize = terrain_->worldSizeXZ();
    for (uint32_t i = 0; i < layerCount; ++i) {
        tc->layerTextureHandles[i] = renderer->loadTexture("settlers_layer_" + std::to_string(i), layers[i].texture);
        if (layers[i].mask) {
            tc->layerMaskHandles[i] = renderer->loadTextureFromMemory(
                "settlers_mask_" + std::to_string(i), *layers[i].mask, n, n);
        }
    }
    tc->heightmapTextureHandle = renderer->loadTextureFromMemory("settlers_heightmap", heightmap_, n, n);
    tc->heightmapEnabled = true;
}

void World::buildNavGrid() {
    const int cells = static_cast<int>(kWorldHalf * 2.0f / kNavCell);
    nav_.init(cells, cells, kNavCell, {-kWorldHalf, 0.0f, -kWorldHalf});
    for (int cz = 0; cz < cells; ++cz) {
        for (int cx = 0; cx < cells; ++cx) {
            const Vec3 c = nav_.cellCenter(cx, cz);
            const float h = heightAt(c.x, c.z);
            const Vec3 nrm = terrain_->getNormal(c.x, c.z);
            const bool edge = std::abs(c.x) > kWorldHalf - 16.0f || std::abs(c.z) > kWorldHalf - 16.0f;
            const bool blocked = edge || h < kWaterLevel + 1.2f || nrm.y < 0.8f;
            nav_.setStaticBlocked(cx, cz, blocked);
        }
    }
}

int World::addNode(NodeKind kind, const Vec3& pos, int variant, float scale) {
    ResourceNode node;
    node.alive = true;
    node.kind = kind;
    node.pos = pos;
    node.pos.y = heightAt(pos.x, pos.z);
    node.baseScale = scale;
    if (kind == NodeKind::Tree) {
        node.amount = node.maxAmount = 3;
        node.radius = 2.0f * scale;
        node.entity = makeEntity("tree", treeMeshName(variant));
    } else {
        node.amount = node.maxAmount = 10;
        node.radius = 4.5f * scale;
        node.entity = makeEntity("rock", rockMeshName(variant));
        const float r = node.radius * 0.7f;
        nav_.addObstacle(pos.x - r, pos.z - r, pos.x + r, pos.z + r);
        regionsDirty_ = true;
    }
    node.entity->transform().position = node.pos;
    node.entity->transform().rotation.y = randf(0.0f, 2.0f * PI);
    node.entity->transform().scale = {scale, scale * randf(0.9f, 1.15f), scale};
    nodes.push_back(node);
    return static_cast<int>(nodes.size()) - 1;
}

void World::placeResources() {
    Noise forest{seed_ * 17u + 11u};
    auto suitable = [&](const Vec3& p, float minDistToBase) {
        if (!nav_.isWalkableWorld(p.x, p.z)) return false;
        if (heightAt(p.x, p.z) < kWaterLevel + 3.0f) return false;
        for (const auto& s : startPos) {
            if (dist2D(p, s) < minDistToBase) return false;
        }
        return true;
    };

    // Lasy z szumu
    for (float z = -kWorldHalf + 30.0f; z < kWorldHalf - 30.0f; z += 11.0f) {
        for (float x = -kWorldHalf + 30.0f; x < kWorldHalf - 30.0f; x += 11.0f) {
            const float f = forest.fbm(x / 140.0f, z / 140.0f, 3);
            if (f < 0.12f || randf(0.0f, 1.0f) > (f - 0.12f) * 2.4f) continue;
            const Vec3 p{x + randf(-4.5f, 4.5f), 0.0f, z + randf(-4.5f, 4.5f)};
            if (!suitable(p, 75.0f)) continue;
            addNode(NodeKind::Tree, p, static_cast<int>(randf(0.0f, 3.99f)), randf(0.85f, 1.3f));
            if (nodes.size() > 520) break;
        }
    }

    // Gwarantowane lasy i skały przy każdej bazie
    for (const auto& s : startPos) {
        const float baseAngle = randf(0.0f, 2.0f * PI);
        for (int cluster = 0; cluster < 2; ++cluster) {
            const float a = baseAngle + cluster * 2.3f;
            const Vec3 c{s.x + std::cos(a) * 100.0f, 0.0f, s.z + std::sin(a) * 100.0f};
            for (int i = 0; i < 22; ++i) {
                const float ra = randf(0.0f, 2.0f * PI), rr = std::sqrt(randf(0.0f, 1.0f)) * 32.0f;
                const Vec3 p{c.x + std::cos(ra) * rr, 0.0f, c.z + std::sin(ra) * rr};
                if (suitable(p, 60.0f)) addNode(NodeKind::Tree, p, static_cast<int>(randf(0.0f, 3.99f)), randf(0.85f, 1.25f));
            }
        }
        const float ra = baseAngle + 4.4f;
        const Vec3 c{s.x + std::cos(ra) * 95.0f, 0.0f, s.z + std::sin(ra) * 95.0f};
        for (int i = 0; i < 5; ++i) {
            const Vec3 p{c.x + randf(-18.0f, 18.0f), 0.0f, c.z + randf(-18.0f, 18.0f)};
            if (suitable(p, 60.0f)) addNode(NodeKind::Rock, p, static_cast<int>(randf(0.0f, 2.99f)), randf(0.9f, 1.3f));
        }
    }

    // Skały rozsiane po wzgórzach
    for (int i = 0; i < 400; ++i) {
        const Vec3 p{randf(-kWorldHalf + 40.0f, kWorldHalf - 40.0f), 0.0f, randf(-kWorldHalf + 40.0f, kWorldHalf - 40.0f)};
        if (heightAt(p.x, p.z) < 55.0f || !suitable(p, 120.0f)) continue;
        addNode(NodeKind::Rock, p, static_cast<int>(randf(0.0f, 2.99f)), randf(0.8f, 1.4f));
        int rocks = 0;
        for (const auto& n : nodes) rocks += n.kind == NodeKind::Rock ? 1 : 0;
        if (rocks > 45) break;
    }
}

void World::buildMinimap() {
    minimapColors_.assign(static_cast<size_t>(minimapSize_) * minimapSize_, {});
    for (int y = 0; y < minimapSize_; ++y) {
        for (int x = 0; x < minimapSize_; ++x) {
            const float wx = -kWorldHalf + (static_cast<float>(x) + 0.5f) / minimapSize_ * kWorldHalf * 2.0f;
            const float wz = -kWorldHalf + (static_cast<float>(y) + 0.5f) / minimapSize_ * kWorldHalf * 2.0f;
            const float h = heightAt(wx, wz);
            Vec3 c;
            if (h < kWaterLevel) {
                c = {0.12f, 0.28f, 0.5f};
            } else if (h < kWaterLevel + 4.0f) {
                c = {0.75f, 0.68f, 0.45f};
            } else if (h > 112.0f) {
                c = {0.9f, 0.9f, 0.92f};
            } else if (terrain_->getNormal(wx, wz).y < 0.8f || h > 85.0f) {
                c = {0.45f, 0.43f, 0.4f};
            } else {
                const float t = std::clamp((h - 30.0f) / 60.0f, 0.0f, 1.0f);
                c = {0.22f + 0.15f * t, 0.45f + 0.05f * t, 0.16f + 0.08f * t};
            }
            minimapColors_[static_cast<size_t>(y) * minimapSize_ + x] = c;
        }
    }
}

// =====================================================================================
// Encje
// =====================================================================================

Entity* World::makeEntity(const std::string& prefix, const std::string& mesh) {
    Entity* e = scene_->createEntity(prefix + "_" + std::to_string(++entityCounter_));
    auto* mc = e->addComponent<MeshComponent>(mesh);
    mc->materialHandle = materials_.solid;
    return e;
}

void World::destroyEntity(Entity*& e) {
    if (e && scene_) {
        scene_->removeEntity(e);
    }
    e = nullptr;
}

void World::setUnitMesh(Unit& u, const std::string& mesh) {
    if (u.meshName == mesh || !u.entity) return;
    u.meshName = mesh;
    if (auto* mc = u.entity->getComponent<MeshComponent>()) {
        mc->meshName = mesh;
    }
}

int World::spawnUnit(UnitType type, int faction, const Vec3& pos) {
    const UnitDef& def = unitDef(type);
    Unit u;
    u.alive = true;
    u.type = type;
    u.faction = faction;
    u.hp = def.maxHp;

    // Upewniamy się, że jednostka stoi na przechodnim polu
    Vec3 p = pos;
    int cx, cz, nx, nz;
    if (nav_.worldToCell(p.x, p.z, cx, cz) && !nav_.isWalkable(cx, cz) && nav_.findNearestWalkable(cx, cz, 8, nx, nz)) {
        p = nav_.cellCenter(nx, nz);
    }
    p.y = heightAt(p.x, p.z);
    u.pos = p;
    u.guardPos = p;
    u.yaw = randf(-PI, PI);
    u.timer = randf(0.5f, 3.0f);
    u.animTime = randf(0.0f, 10.0f);

    u.meshName = unitMeshName(type, faction, CarryType::None);
    u.entity = makeEntity("unit", u.meshName);
    u.ring = makeEntity("ring", faction == kPlayer ? kRingPlayerMesh : kRingEnemyMesh);
    u.ring->setActive(false);
    u.ring->getComponent<MeshComponent>()->castShadows = false;
    units.push_back(std::move(u));
    return static_cast<int>(units.size()) - 1;
}

int World::placeBuilding(BuildingType type, const Vec3& posIn, int faction, bool instant, float rotY) {
    const BuildingDef& def = buildingDef(type);
    Building b;
    b.alive = true;
    b.type = type;
    b.faction = faction;
    b.rotY = rotY;
    b.pos = posIn;

    // Wysokość: średnia z narożników i środka obrysu
    const float h = def.halfSize;
    float sum = heightAt(posIn.x, posIn.z);
    sum += heightAt(posIn.x - h, posIn.z - h) + heightAt(posIn.x + h, posIn.z - h);
    sum += heightAt(posIn.x - h, posIn.z + h) + heightAt(posIn.x + h, posIn.z + h);
    b.pos.y = sum / 5.0f;

    if (!instant) {
        factions[faction].res.subtract(def.cost);
    }

    b.entity = makeEntity("bld", buildingMeshName(type, faction));
    b.entity->transform().position = b.pos;
    b.entity->transform().rotation.y = rotY;
    b.ring = makeEntity("bring", faction == kPlayer ? kRingPlayerMesh : kRingEnemyMesh);
    b.ring->setActive(false);
    b.ring->getComponent<MeshComponent>()->castShadows = false;

    const float o = h - 1.0f;
    nav_.addObstacle(b.pos.x - o, b.pos.z - o, b.pos.x + o, b.pos.z + o);
    regionsDirty_ = true;

    if (instant) {
        b.constructed = true;
        b.progress = 1.0f;
        b.hp = def.maxHp;
    } else {
        b.constructed = false;
        b.progress = 0.0f;
        b.hp = def.maxHp * 0.1f;
        b.entity->transform().scale = {1.0f, 0.05f, 1.0f};
        b.scaffold = makeEntity("scaf", kScaffoldMesh);
        b.scaffold->transform().position = b.pos;
        b.scaffold->transform().scale = {h, def.height * 0.75f, h};
        b.scaffold->transform().rotation.y = rotY;
    }
    buildings.push_back(std::move(b));
    const int id = static_cast<int>(buildings.size()) - 1;
    if (instant && def.territoryRadius > 0.0f) territoryDirty_ = true;
    return id;
}

void World::completeBuilding(int id) {
    Building& b = buildings[id];
    const BuildingDef& def = buildingDef(b.type);
    b.constructed = true;
    b.progress = 1.0f;
    b.hp = std::max(b.hp, def.maxHp * 0.9f);
    b.hp = std::min(b.hp, def.maxHp);
    b.entity->transform().scale = {1.0f, 1.0f, 1.0f};
    destroyEntity(b.scaffold);
    if (def.territoryRadius > 0.0f) territoryDirty_ = true;
    if (b.faction == kPlayer) {
        pushMessage(std::string("Budowa ukończona: ") + def.name, {0.6f, 1.0f, 0.6f, 1.0f});
        emitSound("complete", b.pos, 0.7f, false);
    }
}

void World::becomeIdleSettler(Unit& u) {
    if (u.resourceNode >= 0 && u.resourceNode < static_cast<int>(nodes.size())) {
        if (u.state == UnitState::GoGather || u.state == UnitState::Gathering) {
            nodes[u.resourceNode].reserved = std::max(0, nodes[u.resourceNode].reserved - 1);
        }
    }
    if (u.building >= 0 && u.building < static_cast<int>(buildings.size())) {
        Building& b = buildings[u.building];
        if (b.worker >= 0 && &units[b.worker] == &u) b.worker = -1;
    }
    u.resourceNode = -1;
    u.building = -1;
    u.state = UnitState::Idle;
    u.carry = CarryType::None;
    u.path.clear();
    u.stuckTimer = 0.0f;
    u.timer = randf(0.5f, 2.0f);
    setUnitMesh(u, unitMeshName(u.type, u.faction, CarryType::None));
}

void World::killUnit(int id, int killerFaction) {
    Unit& u = units[id];
    if (!u.alive) return;
    if (u.type == UnitType::Settler) becomeIdleSettler(u);
    emitSound("death", u.pos, 0.7f);
    u.alive = false;
    u.selected = false;
    destroyEntity(u.entity);
    destroyEntity(u.ring);
    factions[u.faction].unitsLost++;
    if (killerFaction >= 0 && killerFaction != u.faction) factions[killerFaction].enemiesKilled++;
}

void World::destroyBuilding(int id, int killerFaction) {
    Building& b = buildings[id];
    if (!b.alive) return;
    const BuildingDef& def = buildingDef(b.type);
    b.alive = false;
    b.selected = false;

    if (b.worker >= 0 && units[b.worker].alive) {
        units[b.worker].building = -1;
        becomeIdleSettler(units[b.worker]);
    }
    b.worker = -1;

    const float o = def.halfSize - 1.0f;
    nav_.removeObstacle(b.pos.x - o, b.pos.z - o, b.pos.x + o, b.pos.z + o);
    regionsDirty_ = true;
    emitSound("collapse", b.pos, 1.0f);

    // Gruzowisko jako efekt
    Effect fx;
    fx.alive = true;
    fx.kind = 1;
    fx.duration = 25.0f;
    fx.entity = makeEntity("rubble", kRubbleMesh);
    fx.entity->transform().position = b.pos;
    fx.entity->transform().scale = {def.halfSize * 1.6f, def.halfSize * 1.2f, def.halfSize * 1.6f};
    effects.push_back(fx);

    destroyEntity(b.entity);
    destroyEntity(b.scaffold);
    destroyEntity(b.ring);
    if (def.territoryRadius > 0.0f) territoryDirty_ = true;

    if (b.faction == kPlayer) {
        pushMessage(std::string("Utracono budynek: ") + def.name, {1.0f, 0.5f, 0.4f, 1.0f});
    } else if (killerFaction == kPlayer) {
        pushMessage(std::string("Zniszczono wrogi budynek: ") + def.name, {0.6f, 1.0f, 0.6f, 1.0f});
    }

    if (b.type == BuildingType::Castle) {
        factions[b.faction].defeated = true;
    }
}

// =====================================================================================
// Zapytania
// =====================================================================================

Vec3 World::snapToGrid(const Vec3& p) const {
    Vec3 r = p;
    r.x = std::round(p.x / kNavCell) * kNavCell;
    r.z = std::round(p.z / kNavCell) * kNavCell;
    r.y = heightAt(r.x, r.z);
    return r;
}

bool World::pickGround(const Ray& ray, Vec3& outHit) const {
    if (terrain_ && terrain_->raycast(ray, 5000.0f, outHit)) {
        if (outHit.y < kWaterLevel) {
            Vec3 water;
            if (rayPlaneY(ray, kWaterLevel, water)) outHit = water;
        }
        return true;
    }
    return rayPlaneY(ray, kWaterLevel, outHit);
}

int World::pickUnit(const Ray& ray) const {
    int best = -1;
    float bestT = std::numeric_limits<float>::max();
    for (size_t i = 0; i < units.size(); ++i) {
        const Unit& u = units[i];
        if (!u.alive) continue;
        const UnitDef& def = unitDef(u.type);
        float t;
        if (raySphere(ray, add(u.pos, {0.0f, def.height * 0.5f, 0.0f}), std::max(3.2f, def.radius * 1.5f), t) && t < bestT) {
            bestT = t;
            best = static_cast<int>(i);
        }
    }
    return best;
}

int World::pickBuilding(const Ray& ray) const {
    int best = -1;
    float bestT = std::numeric_limits<float>::max();
    for (size_t i = 0; i < buildings.size(); ++i) {
        const Building& b = buildings[i];
        if (!b.alive) continue;
        const BuildingDef& def = buildingDef(b.type);
        float t;
        const float h = def.height * (b.constructed ? 0.4f : 0.15f);
        if (raySphere(ray, add(b.pos, {0.0f, h, 0.0f}), def.halfSize * 1.05f, t) && t < bestT) {
            bestT = t;
            best = static_cast<int>(i);
        }
    }
    return best;
}

bool World::isInTerritory(int faction, const Vec3& p, float margin) const {
    for (const auto& b : buildings) {
        if (!b.alive || b.faction != faction || !b.constructed) continue;
        const float r = buildingDef(b.type).territoryRadius;
        if (r <= 0.0f) continue;
        if (dist2D(p, b.pos) <= r - margin) return true;
    }
    return false;
}

bool World::buildingAreaFree(BuildingType type, const Vec3& pos) const {
    const float h = buildingDef(type).halfSize + 2.0f;
    return nav_.isAreaFree(pos.x - h, pos.z - h, pos.x + h, pos.z + h);
}

bool World::canPlace(BuildingType type, const Vec3& pos, int faction, std::string* reason, float rotY) const {
    auto fail = [&](const char* r) {
        if (reason) *reason = r;
        return false;
    };
    const BuildingDef& def = buildingDef(type);
    const float h = def.halfSize;
    if (std::abs(pos.x) > kWorldHalf - 30.0f || std::abs(pos.z) > kWorldHalf - 30.0f) return fail("Poza mapą");
    if (type == BuildingType::Castle) return fail("Zamek może być tylko jeden");
    if (!isInTerritory(faction, pos, h * 0.5f)) return fail("Poza terytorium");
    for (int f = 0; f < kFactionCount; ++f) {
        if (f != faction && isInTerritory(f, pos, -h)) return fail("Terytorium wroga");
    }

    float minH = 1e9f, maxH = -1e9f;
    for (int i = 0; i < 9; ++i) {
        const float sx = static_cast<float>(i % 3 - 1), sz = static_cast<float>(i / 3 - 1);
        const float y = heightAt(pos.x + sx * h, pos.z + sz * h);
        minH = std::min(minH, y);
        maxH = std::max(maxH, y);
    }
    if (minH < kWaterLevel + 1.5f) return fail("Za blisko wody");
    if (maxH - minH > 6.5f) return fail("Teren zbyt nierówny");
    if (!buildingAreaFree(type, pos)) return fail("Miejsce zajęte");
    // Nowy budynek nie może zasłonić wejścia innego budynku
    for (const auto& other : buildings) {
        if (!other.alive) continue;
        const Vec3 d = other.door();
        if (std::abs(d.x - pos.x) < h + kNavCell && std::abs(d.z - pos.z) < h + kNavCell) return fail("Zasłania wejście innego budynku");
    }

    for (const auto& n : nodes) {
        if (!n.alive || n.kind != NodeKind::Tree) continue;
        if (std::abs(n.pos.x - pos.x) < h + 1.5f && std::abs(n.pos.z - pos.z) < h + 1.5f) return fail("Drzewa na drodze");
    }

    const Vec3 door = add(pos, rotateY({0.0f, 0.0f, h + 3.0f}, rotY));
    if (!nav_.isWalkableWorld(door.x, door.z)) return fail("Wejście zablokowane");
    // Osadnicy wychodzą z zamku - plac budowy musi leżeć w tym samym spójnym obszarze terenu
    const int castle = factions[faction].castle;
    if (castle >= 0 && buildings[castle].alive && !nav_.sameRegion(buildings[castle].door(), door)) {
        return fail("Brak dojścia - osadnicy nie dotrą tu z zamku");
    }
    return true;
}

float World::distanceToBuilding(const Vec3& p, const Building& b) const {
    const float h = buildingDef(b.type).halfSize;
    const float dx = std::max(0.0f, std::abs(p.x - b.pos.x) - h);
    const float dz = std::max(0.0f, std::abs(p.z - b.pos.z) - h);
    return std::sqrt(dx * dx + dz * dz);
}

int World::findNearestNode(NodeKind kind, const Vec3& from, float radius) const {
    int best = -1;
    float bestD = radius;
    const int maxReserve = kind == NodeKind::Tree ? 1 : 2;
    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& n = nodes[i];
        if (!n.alive || n.kind != kind || n.amount <= 0 || n.fallTimer >= 0.0f || n.reserved >= maxReserve) continue;
        if (n.blockedUntil > gameTime) continue;
        const float d = dist2D(from, n.pos);
        if (d < bestD) {
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

int World::countNodesInRange(NodeKind kind, const Vec3& from, float radius) const {
    int count = 0;
    for (const auto& n : nodes) {
        if (!n.alive || n.kind != kind || n.amount <= 0 || n.fallTimer >= 0.0f || n.blockedUntil > gameTime) continue;
        if (dist2D(from, n.pos) <= radius) ++count;
    }
    return count;
}

void World::markNodeUnreachable(Unit& u) {
    if (u.resourceNode < 0) return;
    ResourceNode& n = nodes[u.resourceNode];
    n.reserved = std::max(0, n.reserved - 1);
    n.blockedUntil = gameTime + 90.0f;   // przez 1,5 min nikt nie próbuje do niego iść
    u.resourceNode = -1;
}

bool World::buildingReachableFrom(const Building& b, const Vec3& from) const {
    // Czy któryś bok budynku leży w tym samym spójnym obszarze co punkt startowy
    const float r = buildingDef(b.type).halfSize + 3.0f;
    for (int i = 0; i < 8; ++i) {
        const float a = static_cast<float>(i) * PI * 0.25f;
        const Vec3 p{b.pos.x + std::cos(a) * r, 0.0f, b.pos.z + std::sin(a) * r};
        if (nav_.sameRegion(from, p)) return true;
    }
    return nav_.sameRegion(from, b.door());
}

void World::checkWorkResources(Building& b) {
    if (b.type != BuildingType::Woodcutter && b.type != BuildingType::Quarry) return;
    const BuildingDef& def = buildingDef(b.type);
    const bool tree = b.type == BuildingType::Woodcutter;
    const int inRange = countNodesInRange(tree ? NodeKind::Tree : NodeKind::Rock, b.pos, def.workRadius);
    const int prevInRange = b.resourcesInRange;
    b.resourcesInRange = inRange;
    // Ostrzeżenie, gdy surowiec się kończy (zostały 1-2 drzewa/skały)
    if (b.faction == kPlayer && inRange > 0 && inRange <= 2 && (prevInRange < 0 || prevInRange > 2) &&
        gameTime - b.lowResourcesMsgTime > 60.0f) {
        b.lowResourcesMsgTime = gameTime;
        pushMessage(std::string(def.name) + (tree ? ": kończą się drzewa w okolicy" : ": kończy się kamień w okolicy"),
                    {1.0f, 0.85f, 0.45f, 1.0f});
    }
    const bool lacks = inRange == 0;
    if (lacks && !b.lacksResources && b.faction == kPlayer) {
        pushMessage(std::string(def.name) + (tree ? ": BRAK DRZEW w zasięgu - przenieś drwala" : ": BRAK KAMIENIA w zasięgu - przenieś kamieniarza"),
                    {1.0f, 0.55f, 0.35f, 1.0f});
        emitSound("ui_error", b.pos, 0.6f, false);
    }
    b.lacksResources = lacks;
}

void World::markUnreachable(int id) {
    Building& b = buildings[id];
    b.accessFailures = std::max(b.accessFailures, 1);
    reportAccessFailure(id);
}

void World::reportAccessFailure(int id) {
    Building& b = buildings[id];
    if (!b.alive) return;
    ++b.accessFailures;
    if (b.accessFailures >= 2 && !b.unreachable) {
        b.unreachable = true;
        b.unreachableTime = gameTime;
        if (b.faction == kPlayer) {
            pushMessage(std::string("Osadnicy nie mogą dojść do: ") + buildingDef(b.type).name +
                            " - udrożnij drogę lub zburz (zwrot 50%)",
                        {1.0f, 0.5f, 0.35f, 1.0f});
            emitSound("ui_error", b.pos, 0.6f, false);
        }
    } else if (b.unreachable) {
        b.unreachableTime = gameTime;
    }
}

bool World::isWorkerOnFarm(const Unit& u, int id) const {
    if (u.building < 0) return false;
    const Building& b = buildings[u.building];
    return b.alive && b.type == BuildingType::Farm && b.worker == id;
}

bool World::needsRepair(const Building& b) const {
    if (!b.alive || !b.constructed) return false;
    return b.hp < buildingDef(b.type).maxHp * 0.97f;
}

int World::repairersOf(int building) const {
    int n = 0;
    for (const auto& u : units) {
        if (u.alive && u.building == building && (u.state == UnitState::GoBuild || u.state == UnitState::Building)) ++n;
    }
    return n;
}

void World::updateHealing(Unit& u, float dt) {
    const float maxHp = unitDef(u.type).maxHp;
    if (u.hp >= maxHp) return;
    // Leczenie dopiero po 8 s bez obrażeń i poza walką
    if (gameTime - u.lastDamagedTime < 8.0f || u.state == UnitState::Attack) return;
    float rate = 0.015f;   // 1,5% PŻ/s w terenie
    for (const auto& b : buildings) {
        if (!b.alive || !b.constructed || b.faction != u.faction) continue;
        if (b.type != BuildingType::Castle && b.type != BuildingType::Barracks) continue;
        if (dist2D(u.pos, b.pos) < 70.0f) {
            rate = 0.06f;  // przy zamku lub koszarach 6% PŻ/s
            break;
        }
    }
    u.hp = std::min(maxHp, u.hp + maxHp * rate * dt);
}

int World::findIdleSettler(int faction, const Vec3& near) const {
    int best = -1;
    float bestD = std::numeric_limits<float>::max();
    for (size_t i = 0; i < units.size(); ++i) {
        const auto& u = units[i];
        if (!u.alive || u.faction != faction || u.type != UnitType::Settler || u.state != UnitState::Idle) continue;
        const float d = dist2D(u.pos, near);
        if (d < bestD) {
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

int World::findEnemyUnitNear(int faction, const Vec3& p, float radius) const {
    int best = -1;
    float bestScore = std::numeric_limits<float>::max();
    for (size_t i = 0; i < units.size(); ++i) {
        const auto& u = units[i];
        if (!u.alive || u.faction == faction) continue;
        const float d = dist2D(u.pos, p);
        if (d > radius) continue;
        // Żołnierze mają pierwszeństwo przed osadnikami
        const float score = d + (u.type == UnitType::Settler ? 40.0f : 0.0f);
        if (score < bestScore) {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }
    return best;
}

int World::findEnemyBuildingNear(int faction, const Vec3& p, float radius) const {
    int best = -1;
    float bestD = radius;
    for (size_t i = 0; i < buildings.size(); ++i) {
        const auto& b = buildings[i];
        if (!b.alive || b.faction == faction) continue;
        const float d = distanceToBuilding(p, b);
        if (d < bestD) {
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

bool World::findPlacementNear(BuildingType type, int faction, const Vec3& center, float minR, float maxR, Vec3& out) {
    for (int attempt = 0; attempt < 120; ++attempt) {
        const float a = randf(0.0f, 2.0f * PI);
        const float r = randf(minR, maxR);
        const Vec3 p = snapToGrid({center.x + std::cos(a) * r, 0.0f, center.z + std::sin(a) * r});
        if (canPlace(type, p, faction)) {
            out = p;
            return true;
        }
    }
    return false;
}

// =====================================================================================
// Rozkazy
// =====================================================================================

void World::setPath(Unit& u, const Vec3& target) {
    u.goal = target;
    u.pathIndex = 0;
    u.path.clear();
    if (nav_.lineWalkable(u.pos, target)) {
        u.path.push_back({target.x, 0.0f, target.z});
        ++debugStats.directPaths;
        return;
    }
    ++debugStats.pathRequests;
    nav_.findPath(u.pos, target, u.path, true, 30000);
}

void World::recoverStall(Unit& u, int id) {
    // Krok 1: nowa ścieżka i chwilowe przeciskanie się przez innych
    if (u.stallTime < kStallSeconds * 2.0f) {
        if (u.stallTime - kStallSeconds < 0.05f) {
            setPath(u, u.goal);
            u.ghostTimer = 3.0f;
        }
        return;
    }
    // Krok 2: wyjście ze stanu, który się nie udaje
    ++debugStats.stallsRecovered;
    LOG_WARN("[zawieszenie] odblokowanie: {}", describeUnit(id));
    u.stallTime = 0.0f;
    u.stallPos = u.pos;
    Building* b = (u.building >= 0 && u.building < static_cast<int>(buildings.size()) && buildings[u.building].alive)
                      ? &buildings[u.building] : nullptr;
    const float nearBuilding = b ? distanceToBuilding(u.pos, *b) : 1e9f;
    switch (u.state) {
    case UnitState::ReturnHome:
        if (b && nearBuilding < 14.0f) {
            // Tuż przy budynku - oddajemy surowiec w miejscu
            FactionState& fs = factions[u.faction];
            if (u.carry == CarryType::Wood) fs.res.wood += 2;
            if (u.carry == CarryType::Stone) fs.res.stone += 2;
            if (u.carry == CarryType::Food) fs.res.food += 2;
            b->produced++;
            u.carry = CarryType::None;
            u.state = UnitState::WorkWait;
            u.timer = 1.5f;
            u.path.clear();
        } else if (b) {
            reportAccessFailure(u.building);
            becomeIdleSettler(u);
        }
        break;
    case UnitState::GoToWork:
        if (b && nearBuilding < 14.0f) {
            u.state = UnitState::WorkWait;
            u.timer = 1.0f;
            u.path.clear();
        } else {
            if (b) reportAccessFailure(u.building);
            becomeIdleSettler(u);
        }
        break;
    case UnitState::GoBuild:
        if (b && nearBuilding < 14.0f) {
            u.state = UnitState::Building;
            u.path.clear();
        } else {
            if (b) reportAccessFailure(u.building);
            becomeIdleSettler(u);
        }
        break;
    case UnitState::GoGather:
        markNodeUnreachable(u);
        u.state = UnitState::WorkWait;
        u.timer = 1.0f;
        u.path.clear();
        break;
    case UnitState::Moving:
        u.state = UnitState::Idle;
        u.guardPos = u.pos;
        u.path.clear();
        break;
    default:
        break;
    }
}

const char* World::stateName(UnitState s) {
    static const char* names[] = {"Idle", "Moving", "GoBuild", "Building", "GoToWork", "WorkWait",
                                  "GoGather", "Gathering", "ReturnHome", "Attack"};
    return names[static_cast<int>(s)];
}

bool World::isTravelState(UnitState s) {
    return s == UnitState::Moving || s == UnitState::GoBuild || s == UnitState::GoToWork ||
           s == UnitState::GoGather || s == UnitState::ReturnHome;
}

std::string World::describeUnit(int id) const {
    static const char* stateNames[] = {"Idle", "Moving", "GoBuild", "Building", "GoToWork", "WorkWait",
                                       "GoGather", "Gathering", "ReturnHome", "Attack"};
    const Unit& u = units[id];
    char buf[512];
    int cx = -1, cz = -1;
    nav_.worldToCell(u.pos.x, u.pos.z, cx, cz);
    int gx = -1, gz = -1;
    nav_.worldToCell(u.goal.x, u.goal.z, gx, gz);
    std::snprintf(buf, sizeof(buf),
                  "#%d %s f=%d stan=%s poz=(%.1f,%.1f) kom=(%d,%d,%s) cel=(%.1f,%.1f) kom=(%d,%d,%s) region %d->%d "
                  "sciezka=%zu/%zu stall=%.1fs bud=%d zasob=%d",
                  id, unitDef(u.type).name, u.faction, stateNames[static_cast<int>(u.state)], u.pos.x, u.pos.z, cx, cz,
                  nav_.isWalkable(cx, cz) ? "wolna" : "ZABL", u.goal.x, u.goal.z, gx, gz, nav_.isWalkable(gx, gz) ? "wolna" : "ZABL",
                  nav_.regionAt(u.pos.x, u.pos.z), nav_.regionAt(u.goal.x, u.goal.z), u.pathIndex, u.path.size(), u.stallTime,
                  u.building, u.resourceNode);
    std::string out = buf;
    if (u.building >= 0 && u.building < static_cast<int>(buildings.size())) {
        const Building& b = buildings[u.building];
        const Vec3 d = b.door();
        int dx = -1, dz = -1;
        nav_.worldToCell(d.x, d.z, dx, dz);
        std::snprintf(buf, sizeof(buf), " | budynek %s poz=(%.1f,%.1f) rot=%.2f drzwi=(%.1f,%.1f) kom=%s odl.do drzwi=%.1f",
                      buildingDef(b.type).name, b.pos.x, b.pos.z, b.rotY, d.x, d.z, nav_.isWalkable(dx, dz) ? "wolna" : "ZABL",
                      dist2D(u.pos, d));
        out += buf;
    }
    return out;
}

bool World::followPath(Unit& u, float dt, float reachDist) {
    if (u.pathIndex >= u.path.size()) return true;
    const UnitDef& def = unitDef(u.type);
    float step = def.speed * dt;
    // Na zboczach jednostki zwalniają
    const Vec3 nrm = terrain_->getNormal(u.pos.x, u.pos.z);
    step *= std::clamp(0.55f + nrm.y * 0.45f, 0.6f, 1.0f);

    while (step > 0.0f && u.pathIndex < u.path.size()) {
        const Vec3& t = u.path[u.pathIndex];
        const bool last = u.pathIndex + 1 == u.path.size();
        const float dx = t.x - u.pos.x, dz = t.z - u.pos.z;
        const float d = std::sqrt(dx * dx + dz * dz);
        const float stopAt = last ? reachDist : 0.0f;
        if (d <= stopAt + 0.001f) {
            ++u.pathIndex;
            continue;
        }
        const float move = std::min(step, d - stopAt);
        u.pos.x += dx / d * move;
        u.pos.z += dz / d * move;
        u.yaw = angleLerp(u.yaw, std::atan2(dx, dz), std::min(1.0f, dt * 10.0f));
        step -= move;
        u.moving = true;
        if (move >= d - stopAt - 0.001f) ++u.pathIndex;
    }
    u.pos.y = heightAt(u.pos.x, u.pos.z);
    return u.pathIndex >= u.path.size();
}

void World::commandMove(const std::vector<int>& ids, const Vec3& point, bool attackMove) {
    // Formacja: siatka wokół punktu docelowego
    const int n = static_cast<int>(ids.size());
    const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(n)))));
    int k = 0;
    for (int id : ids) {
        if (id < 0 || id >= static_cast<int>(units.size())) continue;
        Unit& u = units[id];
        if (!u.alive || u.type == UnitType::Settler) continue;
        const int row = k / cols, col = k % cols;
        ++k;
        Vec3 p{point.x + (col - (cols - 1) * 0.5f) * 6.0f, 0.0f, point.z + (row - (cols - 1) * 0.5f) * 6.0f};
        u.targetUnit = -1;
        u.targetBuilding = -1;
        u.attackMove = attackMove;
        u.state = UnitState::Moving;
        u.guardPos = p;
        setPath(u, p);
    }
}

void World::commandAttackUnit(const std::vector<int>& ids, int target) {
    if (target < 0 || !units[target].alive) return;
    for (int id : ids) {
        Unit& u = units[id];
        if (!u.alive || u.type == UnitType::Settler || u.faction == units[target].faction) continue;
        u.targetUnit = target;
        u.targetBuilding = -1;
        u.state = UnitState::Attack;
        u.attackMove = true;
        u.guardPos = units[target].pos;
        u.path.clear();
        u.repathTimer = 0.0f;
    }
}

void World::commandAttackBuilding(const std::vector<int>& ids, int target) {
    if (target < 0 || !buildings[target].alive) return;
    for (int id : ids) {
        Unit& u = units[id];
        if (!u.alive || u.type == UnitType::Settler || u.faction == buildings[target].faction) continue;
        u.targetUnit = -1;
        u.targetBuilding = target;
        u.state = UnitState::Attack;
        u.attackMove = true;
        u.guardPos = buildings[target].pos;
        u.goal = buildings[target].pos;
        u.path.clear();
        u.repathTimer = 0.0f;
    }
}

bool World::queueTraining(int id, UnitType type, std::string* reason) {
    auto fail = [&](const char* r) {
        if (reason) *reason = r;
        return false;
    };
    if (id < 0 || id >= static_cast<int>(buildings.size())) return fail("Brak budynku");
    Building& b = buildings[id];
    if (!b.alive || b.type != BuildingType::Barracks) return fail("To nie są koszary");
    if (!b.constructed) return fail("Koszary są w budowie");
    if (b.trainQueue.size() >= 5) return fail("Kolejka pełna");
    FactionState& fs = factions[b.faction];
    int queued = 0;
    for (const auto& bb : buildings) {
        if (bb.alive && bb.faction == b.faction) queued += static_cast<int>(bb.trainQueue.size());
    }
    if (fs.population + queued >= fs.popCap) return fail("Za mało miejsca - zbuduj Dom");
    const UnitDef& def = unitDef(type);
    if (!fs.res.canAfford(def.cost)) {
        if (reason) *reason = "brakuje " + missingText(def.cost, fs.res);
        return false;
    }
    fs.res.subtract(def.cost);
    b.trainQueue.push_back(type);
    return true;
}

void World::demolish(int id) {
    if (id < 0 || id >= static_cast<int>(buildings.size())) return;
    Building& b = buildings[id];
    if (!b.alive || b.type == BuildingType::Castle) return;
    const Resources& c = buildingDef(b.type).cost;
    factions[b.faction].res.add({c.wood / 2, c.stone / 2, c.food / 2});
    destroyBuilding(id, -1);
}

void World::spawnMarker(const Vec3& p) {
    Effect fx;
    fx.alive = true;
    fx.kind = 0;
    fx.duration = 0.7f;
    fx.baseScale = 4.0f;
    fx.entity = makeEntity("marker", kMarkerMesh);
    fx.entity->getComponent<MeshComponent>()->castShadows = false;
    fx.entity->transform().position = {p.x, heightAt(p.x, p.z) + 0.6f, p.z};
    effects.push_back(fx);
}

void World::setSelected(int id, bool selected) {
    if (id < 0 || id >= static_cast<int>(units.size())) return;
    units[id].selected = selected && units[id].alive;
}

void World::setBuildingSelected(int id, bool selected) {
    if (id < 0 || id >= static_cast<int>(buildings.size())) return;
    buildings[id].selected = selected && buildings[id].alive;
}

// =====================================================================================
// Symulacja
// =====================================================================================

void World::update(float dt) {
    dt = std::min(dt, 0.1f);
    gameTime += dt;

    updateFactions(dt);
    for (size_t i = 0; i < units.size(); ++i) {
        if (units[i].alive) updateUnit(static_cast<int>(i), dt);
    }
    separateUnits(dt);
    updateBuildings(dt);
    updateProjectiles(dt);
    updateEffects(dt);
    updateNodes(dt);
    if (aiEnabled && winner < 0) updateAI(kEnemy, dt);
    if (aiControlsPlayer && winner < 0) updateAI(kPlayer, dt);
    if (territoryDirty_) rebuildTerritory();
    if (regionsDirty_) {
        nav_.computeRegions(true);
        regionsDirty_ = false;
    }
    syncVisuals(dt);
    checkVictory();

    while (!messages.empty() && gameTime - messages.front().time > 9.0f) messages.pop_front();
}

void World::updateFactions(float dt) {
    for (int f = 0; f < kFactionCount; ++f) {
        FactionState& fs = factions[f];
        fs.population = 0;
        fs.popCap = 0;
        int idleSettlers = 0;
        int settlerCount = 0;
        for (const auto& u : units) {
            if (!u.alive || u.faction != f) continue;
            ++fs.population;
            if (u.type == UnitType::Settler) ++settlerCount;
            if (u.type == UnitType::Settler && u.state == UnitState::Idle) ++idleSettlers;
        }
        for (const auto& b : buildings) {
            if (b.alive && b.faction == f && b.constructed) fs.popCap += buildingDef(b.type).popBonus;
        }
        fs.popCap = std::min(fs.popCap, 120);

        // Zamek przyjmuje nowych osadników, gdy brakuje rąk do pracy
        if (fs.castle >= 0 && buildings[fs.castle].alive) {
            Building& castle = buildings[fs.castle];
            castle.settlerTimer += dt;
            int jobs = 0;
            for (const auto& b : buildings) {
                if (!b.alive || b.faction != f) continue;
                if (!b.constructed) jobs += 1;
                else if (buildingDef(b.type).needsWorker && b.worker < 0) jobs += 1;
                else if (needsRepair(b) && gameTime - b.lastDamagedTime > 8.0f && !b.repairBlocked) jobs += 1;
            }
            const int wantedIdle = std::max(2, jobs + 1);
            // Gdy osadników prawie nie ma, zamek przyjmuje ich za darmo (ochrona przed "spiralą śmierci")
            const int settlerCost = settlerCount < 3 ? 0 : 3;
            if (castle.settlerTimer >= 7.0f && idleSettlers < wantedIdle && fs.population < fs.popCap &&
                fs.res.food >= settlerCost) {
                castle.settlerTimer = 0.0f;
                fs.res.food -= settlerCost;
                const int id = spawnUnit(UnitType::Settler, f, castle.door());
                units[id].timer = 0.5f;
                if (f == kPlayer) {
                    pushMessage("Nowy osadnik przybył do zamku", {0.8f, 0.9f, 1.0f, 1.0f});
                    emitSound("settler", castle.door(), 0.5f, false);
                }
            } else if (f == kPlayer && fs.population >= fs.popCap && idleSettlers < wantedIdle && jobs > 0 &&
                       gameTime - fs.popCapMsgTime > 45.0f) {
                fs.popCapMsgTime = gameTime;
                pushMessage("Limit populacji osiągnięty - zbuduj Dom", {1.0f, 0.85f, 0.4f, 1.0f});
            }
        }

        fs.jobTimer -= dt;
        if (fs.jobTimer <= 0.0f) {
            fs.jobTimer = 0.5f;
            dispatchJobs(f);
        }
    }
}

void World::dispatchJobs(int f) {
    for (size_t bi = 0; bi < buildings.size(); ++bi) {
        Building& b = buildings[bi];
        if (!b.alive || b.faction != f) continue;
        const BuildingDef& def = buildingDef(b.type);
        if (b.unreachable && gameTime - b.unreachableTime < 30.0f) continue;   // ponowna próba co 30 s
        if (!b.constructed) {
            int builders = 0;
            for (const auto& u : units) {
                if (u.alive && u.building == static_cast<int>(bi) &&
                    (u.state == UnitState::GoBuild || u.state == UnitState::Building)) ++builders;
            }
            if (builders < 2) {
                const int s = findIdleSettler(f, b.pos);
                if (s >= 0 && !buildingReachableFrom(b, units[s].pos)) {
                    markUnreachable(static_cast<int>(bi));
                } else if (s >= 0) {
                    Unit& u = units[s];
                    u.state = UnitState::GoBuild;
                    u.building = static_cast<int>(bi);
                    u.path.clear();
                }
            }
        } else if (def.needsWorker && (b.worker < 0 || !units[b.worker].alive)) {
            const int s = findIdleSettler(f, b.pos);
            if (s >= 0 && !buildingReachableFrom(b, units[s].pos)) {
                markUnreachable(static_cast<int>(bi));
            } else if (s >= 0) {
                Unit& u = units[s];
                u.state = UnitState::GoToWork;
                u.building = static_cast<int>(bi);
                u.path.clear();
                b.worker = s;
            }
        }
    }

    // Naprawy: uszkodzone budynki, przy których od 8 s nie ma walki (budowa i pracownicy mają pierwszeństwo)
    for (size_t bi = 0; bi < buildings.size(); ++bi) {
        Building& b = buildings[bi];
        if (b.faction != f || !needsRepair(b) || gameTime - b.lastDamagedTime < 8.0f) continue;
        if (b.repairBlocked) {
            if (factions[f].res.wood < 1) continue;
            b.repairBlocked = false;
        }
        const int limit = b.type == BuildingType::Castle ? 3 : 2;
        if (repairersOf(static_cast<int>(bi)) >= limit) continue;
        const int s = findIdleSettler(f, b.pos);
        if (s < 0) continue;
        Unit& u = units[s];
        u.state = UnitState::GoBuild;
        u.building = static_cast<int>(bi);
        u.path.clear();
    }
}

void World::updateUnit(int id, float dt) {
    Unit& u = units[id];
    u.moving = false;
    u.animTime += dt;
    u.hitFlash = std::max(0.0f, u.hitFlash - dt);

    // Brak postępu w marszu (np. tłum żołnierzy na drodze) - jednostka zaczyna się przeciskać
    u.ghostTimer = std::max(0.0f, u.ghostTimer - dt);
    if (u.pathIndex < u.path.size()) {
        u.progressTimer += dt;
        if (u.progressTimer >= 1.5f) {
            if (dist2D(u.pos, u.progressPos) < 2.0f) u.ghostTimer = 3.0f;
            u.progressTimer = 0.0f;
            u.progressPos = u.pos;
        }
    } else {
        u.progressTimer = 0.0f;
        u.progressPos = u.pos;
    }

    // Wykrywanie zawieszeń: jednostka "w drodze", która nie zmienia położenia
    if (isTravelState(u.state)) {
        if (dist2D(u.pos, u.stallPos) > 2.0f) {
            u.stallPos = u.pos;
            u.stallTime = 0.0f;
        } else {
            u.stallTime += dt;
            if (u.stallTime >= kStallSeconds && u.stallTime - dt < kStallSeconds) {
                ++debugStats.stallsDetected;
                ++u.stallCount;
                LOG_WARN("[zawieszenie] {}", describeUnit(id));
            }
            if (u.stallTime >= kStallSeconds) recoverStall(u, id);
        }
    } else {
        u.stallPos = u.pos;
        u.stallTime = 0.0f;
    }

    if (u.type == UnitType::Settler) {
        updateSettler(u, id, dt);
    } else {
        updateSoldier(u, id, dt);
    }
    if (u.alive) updateHealing(u, dt);
}

void World::updateSettler(Unit& u, int id, float dt) {
    auto validBuilding = [&](bool needConstructed) -> Building* {
        if (u.building < 0 || u.building >= static_cast<int>(buildings.size())) return nullptr;
        Building& b = buildings[u.building];
        if (!b.alive) return nullptr;
        if (needConstructed && !b.constructed) return nullptr;
        return &b;
    };

    switch (u.state) {
    case UnitState::Idle: {
        u.timer -= dt;
        if (u.timer <= 0.0f) {
            u.timer = randf(4.0f, 9.0f);
            Vec3 center = u.pos;
            const int castle = factions[u.faction].castle;
            if (castle >= 0 && buildings[castle].alive) center = buildings[castle].door();
            const float a = randf(0.0f, 2.0f * PI), r = randf(4.0f, 28.0f);
            setPath(u, {center.x + std::cos(a) * r, 0.0f, center.z + std::sin(a) * r});
        }
        followPath(u, dt * 0.45f, 0.5f);
        break;
    }
    case UnitState::GoBuild: {
        Building* b = validBuilding(false);
        // Ten sam stan służy budowie i naprawie ukończonego, uszkodzonego budynku
        // (rozpoczęta naprawa trwa do pełnych PŻ, choć zlecana jest dopiero poniżej 97%)
        if (!b || (b->constructed && (b->hp >= buildingDef(b->type).maxHp || b->repairBlocked))) {
            becomeIdleSettler(u);
            break;
        }
        const float half = buildingDef(b->type).halfSize;
        // Budynek blokuje całe komórki nawigacji (do kNavCell poza obrysem), więc ścieżka może się
        // skończyć dalej niż 3.2 od ściany - koniec ścieżki przy budynku też oznacza dotarcie.
        const bool pathDone = !u.path.empty() && u.pathIndex >= u.path.size();
        const float dBuild = distanceToBuilding(u.pos, *b);
        if (dBuild < 3.2f || (pathDone && dBuild < kNavCell * 2.0f + 1.5f)) {
            u.stuckTimer = 0.0f;
            b->accessFailures = 0;
            b->unreachable = false;
            u.state = UnitState::Building;
            u.path.clear();
            break;
        }
        if (u.path.empty() || u.pathIndex >= u.path.size()) {
            u.stuckTimer += dt;
            if (u.stuckTimer > 4.0f) {
                const int site = u.building;
                becomeIdleSettler(u);
                reportAccessFailure(site);
                break;
            }
            Vec3 dir = subtract(u.pos, b->pos);
            dir.y = 0.0f;
            dir = normalize(dir);
            if (dot(dir, dir) < 0.5f) dir = {0.0f, 0.0f, 1.0f};
            setPath(u, add(b->pos, multiply(dir, half + 1.5f)));
        }
        followPath(u, dt, 0.3f);
        break;
    }
    case UnitState::Building: {
        Building* b = validBuilding(false);
        if (!b || (b->constructed && b->hp >= buildingDef(b->type).maxHp)) {
            becomeIdleSettler(u);
            break;
        }
        const BuildingDef& def = buildingDef(b->type);
        if (b->constructed) {
            // Naprawa: 1 drewno przywraca 10% PŻ, tempo jak połowa szybkości budowy
            if (b->repairCredit <= 0.0f) {
                FactionState& fs = factions[b->faction];
                if (fs.res.wood < 1) {
                    b->repairBlocked = true;
                    if (b->faction == kPlayer && gameTime - b->repairMsgTime > 30.0f) {
                        b->repairMsgTime = gameTime;
                        pushMessage(std::string("Naprawa wstrzymana (") + def.name + ") - brak drewna", {1.0f, 0.75f, 0.4f, 1.0f});
                    }
                    becomeIdleSettler(u);
                    break;
                }
                fs.res.wood -= 1;
                b->repairCredit += def.maxHp * 0.1f;
            }
            const float heal = std::min(b->repairCredit, def.maxHp / (def.buildTime * 2.0f) * dt);
            b->hp = std::min(def.maxHp, b->hp + heal);
            b->repairCredit -= heal;
            u.sfxTimer -= dt;
            if (u.sfxTimer <= 0.0f) {
                u.sfxTimer = randf(0.75f, 1.05f);
                emitSound("hammer", u.pos, 0.5f);
            }
            u.yaw = angleLerp(u.yaw, std::atan2(b->pos.x - u.pos.x, b->pos.z - u.pos.z), std::min(1.0f, dt * 6.0f));
            if (b->hp >= def.maxHp) {
                if (b->faction == kPlayer) pushMessage(std::string("Naprawiono: ") + def.name, {0.6f, 1.0f, 0.6f, 1.0f});
                becomeIdleSettler(u);
            }
            break;
        }
        const float dp = dt / def.buildTime;
        b->progress = std::min(1.0f, b->progress + dp);
        u.sfxTimer -= dt;
        if (u.sfxTimer <= 0.0f) {
            u.sfxTimer = randf(0.65f, 0.95f);
            emitSound("hammer", u.pos, 0.55f);
        }
        b->hp = std::min(def.maxHp, b->hp + def.maxHp * 0.9f * dp);
        u.yaw = angleLerp(u.yaw, std::atan2(b->pos.x - u.pos.x, b->pos.z - u.pos.z), std::min(1.0f, dt * 6.0f));
        if (b->progress >= 1.0f) {
            completeBuilding(u.building);
            becomeIdleSettler(u);
        }
        break;
    }
    case UnitState::GoToWork: {
        Building* b = validBuilding(true);
        if (!b || b->worker != id) {
            becomeIdleSettler(u);
            break;
        }
        const Vec3 door = b->door();
        if (dist2D(u.pos, door) < 2.5f) {
            u.stuckTimer = 0.0f;
            b->accessFailures = 0;
            b->unreachable = false;
            u.state = UnitState::WorkWait;
            u.timer = 1.0f;
            u.path.clear();
            break;
        }
        if (u.path.empty() || u.pathIndex >= u.path.size()) {
            u.stuckTimer += dt;
            if (u.stuckTimer > 4.0f) {
                const int site = u.building;
                becomeIdleSettler(u);
                reportAccessFailure(site);
                break;
            }
            setPath(u, door);
        }
        followPath(u, dt, 0.3f);
        break;
    }
    case UnitState::WorkWait: {
        Building* b = validBuilding(true);
        if (!b || b->worker != id) {
            becomeIdleSettler(u);
            break;
        }
        u.timer -= dt;
        if (u.timer > 0.0f) break;
        const BuildingDef& def = buildingDef(b->type);
        if (b->type == BuildingType::Woodcutter || b->type == BuildingType::Quarry) {
            const NodeKind kind = b->type == BuildingType::Woodcutter ? NodeKind::Tree : NodeKind::Rock;
            checkWorkResources(*b);
            const int n = findNearestNode(kind, b->pos, def.workRadius);
            if (n < 0) {
                u.timer = 6.0f;
                break;
            }
            nodes[n].reserved++;
            u.resourceNode = n;
            u.state = UnitState::GoGather;
            u.path.clear();
        } else if (b->type == BuildingType::Farm) {
            u.workSpot = add(b->pos, rotateY({randf(-9.0f, 9.0f), 0.0f, randf(-1.0f, 9.5f)}, b->rotY));
            u.state = UnitState::GoGather;
            u.resourceNode = -1;
            u.path.clear();
            setPath(u, u.workSpot);
        } else {
            u.timer = 5.0f;
        }
        break;
    }
    case UnitState::GoGather: {
        Building* b = validBuilding(true);
        if (!b || b->worker != id) {
            becomeIdleSettler(u);
            break;
        }
        if (b->type == BuildingType::Farm) {
            // Pole leży wewnątrz obrysu farmy - idziemy bez nawigacji
            const float dx = u.workSpot.x - u.pos.x, dz = u.workSpot.z - u.pos.z;
            const float d = std::sqrt(dx * dx + dz * dz);
            if (d < 0.8f) {
                u.state = UnitState::Gathering;
                u.timer = 6.0f;
                break;
            }
            u.path = {u.workSpot};
            u.pathIndex = 0;
            followPath(u, dt, 0.5f);
            break;
        }
        if (u.resourceNode < 0 || !nodes[u.resourceNode].alive || nodes[u.resourceNode].amount <= 0) {
            if (u.resourceNode >= 0) nodes[u.resourceNode].reserved = std::max(0, nodes[u.resourceNode].reserved - 1);
            u.resourceNode = -1;
            u.state = UnitState::WorkWait;
            u.timer = 0.5f;
            break;
        }
        const ResourceNode& n = nodes[u.resourceNode];
        const float dNode = dist2D(u.pos, n.pos);
        const bool pathDone = !u.path.empty() && u.pathIndex >= u.path.size();
        // Skała blokuje siatkę nawigacji, więc ścieżka kończy się na najbliższej wolnej komórce,
        // która może leżeć dalej niż promień skały - koniec ścieżki w pobliżu też oznacza dotarcie.
        if (dNode < n.radius + 3.0f || (pathDone && dNode < n.radius + 3.0f + kNavCell * 2.0f)) {
            u.state = UnitState::Gathering;
            u.timer = n.kind == NodeKind::Tree ? 4.0f : 5.0f;
            u.stuckTimer = 0.0f;
            u.path.clear();
            break;
        }
        if (pathDone) {
            // Koniec ścieżki daleko od surowca - surowiec nieosiągalny, szukamy innego
            u.stuckTimer += dt;
            if (u.stuckTimer > 2.0f) {
                markNodeUnreachable(u);
                u.stuckTimer = 0.0f;
                u.state = UnitState::WorkWait;
                u.timer = 3.0f;
                break;
            }
        }
        if (u.path.empty() || u.pathIndex >= u.path.size()) {
            setPath(u, n.pos);
            if (u.path.empty()) {
                markNodeUnreachable(u);
                u.state = UnitState::WorkWait;
                u.timer = 3.0f;
                break;
            }
        }
        followPath(u, dt, n.radius + 2.0f);
        break;
    }
    case UnitState::Gathering: {
        Building* b = validBuilding(true);
        if (!b || b->worker != id) {
            becomeIdleSettler(u);
            break;
        }
        u.timer -= dt;
        if (b->type == BuildingType::Farm) {
            if (u.timer <= 0.0f) {
                u.carry = CarryType::Food;
                u.state = UnitState::ReturnHome;
                u.path.clear();
            }
            break;
        }
        if (u.resourceNode < 0 || !nodes[u.resourceNode].alive) {
            u.state = UnitState::WorkWait;
            u.timer = 0.5f;
            u.resourceNode = -1;
            break;
        }
        ResourceNode& n = nodes[u.resourceNode];
        u.yaw = angleLerp(u.yaw, std::atan2(n.pos.x - u.pos.x, n.pos.z - u.pos.z), std::min(1.0f, dt * 6.0f));
        n.shake = 0.4f;
        u.sfxTimer -= dt;
        if (u.sfxTimer <= 0.0f) {
            u.sfxTimer = n.kind == NodeKind::Tree ? randf(0.8f, 1.0f) : randf(0.9f, 1.1f);
            emitSound(n.kind == NodeKind::Tree ? "chop" : "mine", n.pos, 0.6f);
        }
        if (u.timer <= 0.0f) {
            n.amount--;
            n.reserved = std::max(0, n.reserved - 1);
            if (n.kind == NodeKind::Tree) {
                u.carry = CarryType::Wood;
                if (n.amount <= 0) {
                    n.fallTimer = 0.0f;
                    emitSound("hit_building", n.pos, 0.7f);
                }
            } else {
                u.carry = CarryType::Stone;
            }
            u.resourceNode = -1;
            u.state = UnitState::ReturnHome;
            u.path.clear();
        }
        break;
    }
    case UnitState::ReturnHome: {
        Building* b = validBuilding(true);
        if (!b || b->worker != id) {
            becomeIdleSettler(u);
            break;
        }
        const Vec3 door = b->door();
        const bool farm = b->type == BuildingType::Farm;
        const Vec3 target = farm ? add(b->pos, rotateY({0.0f, 0.0f, -2.5f}, b->rotY)) : door;
        if (dist2D(u.pos, target) < 2.5f) {
            FactionState& fs = factions[u.faction];
            // Jeden kurs pracownika = kilka jednostek surowca
            if (u.carry == CarryType::Wood) fs.res.wood += 2;
            if (u.carry == CarryType::Stone) fs.res.stone += 2;
            if (u.carry == CarryType::Food) fs.res.food += 2;
            b->produced++;
            if (u.faction == kPlayer) emitSound("deliver", u.pos, 0.3f);
            u.carry = CarryType::None;
            u.state = UnitState::WorkWait;
            u.timer = 1.5f;
            u.path.clear();
            break;
        }
        if (farm) {
            u.path = {target};
            u.pathIndex = 0;
        } else if (u.path.empty() || u.pathIndex >= u.path.size()) {
            setPath(u, door);
        }
        followPath(u, dt, 0.3f);
        break;
    }
    default:
        u.state = UnitState::Idle;
        break;
    }

    setUnitMesh(u, unitMeshName(u.type, u.faction, u.carry));
}

void World::updateSoldier(Unit& u, int id, float dt) {
    const UnitDef& def = unitDef(u.type);
    u.attackCooldown -= dt;
    u.timer -= dt;

    if (u.targetUnit >= 0 && !units[u.targetUnit].alive) u.targetUnit = -1;
    if (u.targetBuilding >= 0 && !buildings[u.targetBuilding].alive) u.targetBuilding = -1;

    // Automatyczne wykrywanie wrogów
    const bool canAcquire = u.state == UnitState::Idle || (u.state == UnitState::Moving && u.attackMove) ||
                            (u.state == UnitState::Attack && u.targetUnit < 0);
    if (canAcquire && u.timer <= 0.0f) {
        u.timer = 0.35f;
        const int t = findEnemyUnitNear(u.faction, u.pos, def.aggroRange);
        if (t >= 0) {
            u.targetUnit = t;
            u.state = UnitState::Attack;
            u.path.clear();
        } else if (u.state != UnitState::Attack) {
            const int b = findEnemyBuildingNear(u.faction, u.pos, def.aggroRange * 0.8f);
            if (b >= 0) {
                u.targetBuilding = b;
                u.state = UnitState::Attack;
                u.path.clear();
            }
        }
    }

    if (u.state == UnitState::Attack && u.targetUnit < 0 && u.targetBuilding < 0) {
        // Cel zniszczony - kontynuujemy marsz lub wracamy na posterunek
        if (dist2D(u.pos, u.guardPos) > 10.0f) {
            u.state = UnitState::Moving;
            setPath(u, u.guardPos);
        } else {
            u.state = UnitState::Idle;
        }
    }

    switch (u.state) {
    case UnitState::Idle:
        break;
    case UnitState::Moving:
        if (followPath(u, dt, 0.5f)) {
            u.state = UnitState::Idle;
            u.guardPos = u.pos;
        }
        break;
    case UnitState::Attack: {
        Vec3 tp;
        float d;
        float targetRadius = 0.0f;
        if (u.targetUnit >= 0) {
            const Unit& t = units[u.targetUnit];
            tp = t.pos;
            targetRadius = unitDef(t.type).radius;
            d = dist2D(u.pos, tp) - targetRadius;
            // Smycz: nie gonimy w nieskończoność, gdy cel wybrany automatycznie
            if (!u.attackMove && dist2D(u.pos, u.guardPos) > 160.0f) {
                u.targetUnit = -1;
                u.state = UnitState::Moving;
                setPath(u, u.guardPos);
                break;
            }
        } else {
            const Building& b = buildings[u.targetBuilding];
            tp = b.pos;
            d = distanceToBuilding(u.pos, b);
        }

        const bool pathDone = !u.path.empty() && u.pathIndex >= u.path.size();
        const bool reachedBuilding = u.targetBuilding >= 0 && pathDone && d <= def.range + def.radius + kNavCell * 2.0f;
        if (d <= def.range + def.radius || reachedBuilding) {
            u.path.clear();
            u.yaw = angleLerp(u.yaw, std::atan2(tp.x - u.pos.x, tp.z - u.pos.z), std::min(1.0f, dt * 10.0f));
            if (u.attackCooldown <= 0.0f) {
                u.attackCooldown = def.cooldown * randf(0.9f, 1.1f);
                if (def.ranged) {
                    fireArrow(add(u.pos, {0.0f, 4.5f, 0.0f}), u.faction, u.targetUnit, u.targetBuilding, def.damage, id);
                } else if (u.targetUnit >= 0) {
                    emitSound("sword", u.pos, 0.6f);
                    damageUnit(u.targetUnit, def.damage * randf(0.85f, 1.15f), u.faction, id);
                } else {
                    emitSound("hit_building", u.pos, 0.55f);
                    damageBuilding(u.targetBuilding, def.damage * randf(0.85f, 1.15f), u.faction, id);
                }
            }
        } else {
            u.repathTimer -= dt;
            if (u.path.empty() || u.pathIndex >= u.path.size() || u.repathTimer <= 0.0f) {
                u.repathTimer = u.targetUnit >= 0 ? 0.6f : 3.0f;
                Vec3 approach = tp;
                if (u.targetBuilding >= 0) {
                    Vec3 dir = subtract(u.pos, tp);
                    dir.y = 0.0f;
                    dir = normalize(dir);
                    approach = add(tp, multiply(dir, buildingDef(buildings[u.targetBuilding].type).halfSize + 1.0f));
                }
                setPath(u, approach);
                if (u.path.empty()) {
                    // Cel nieosiągalny
                    u.targetUnit = -1;
                    u.targetBuilding = -1;
                    u.state = UnitState::Idle;
                    break;
                }
            }
            followPath(u, dt, std::max(0.5f, def.range * 0.8f));
        }
        break;
    }
    default:
        u.state = UnitState::Idle;
        break;
    }
}

void World::damageUnit(int target, float amount, int attackerFaction, int attackerUnit) {
    Unit& t = units[target];
    if (!t.alive) return;
    t.hp -= amount;
    t.hitFlash = 0.2f;
    t.lastDamagedTime = gameTime;
    lastCombatTime[t.faction] = gameTime;
    if (attackerFaction >= 0) lastCombatTime[attackerFaction] = gameTime;

    if (t.faction == kPlayer && gameTime - factions[kPlayer].underAttackMsgTime > 25.0f) {
        factions[kPlayer].underAttackMsgTime = gameTime;
        pushMessage("Jesteśmy atakowani!", {1.0f, 0.4f, 0.3f, 1.0f});
        emitSound("alarm", t.pos, 0.8f, false);
    }

    // Żołnierz bez celu odpowiada atakiem
    if (t.type != UnitType::Settler && attackerUnit >= 0 && units[attackerUnit].alive && t.targetUnit < 0 &&
        t.state != UnitState::Moving) {
        t.targetUnit = attackerUnit;
        t.state = UnitState::Attack;
    }
    if (attackerUnit >= 0) alertDefenders(t.faction, t.pos, attackerUnit);
    if (t.hp <= 0.0f) killUnit(target, attackerFaction);
}

void World::damageBuilding(int target, float amount, int attackerFaction, int attackerUnit) {
    Building& b = buildings[target];
    if (!b.alive) return;
    b.hp -= amount;
    b.lastDamagedTime = gameTime;
    lastCombatTime[b.faction] = gameTime;
    if (attackerFaction >= 0) lastCombatTime[attackerFaction] = gameTime;
    if (b.faction == kPlayer && gameTime - factions[kPlayer].underAttackMsgTime > 25.0f) {
        factions[kPlayer].underAttackMsgTime = gameTime;
        pushMessage(std::string("Wróg atakuje: ") + buildingDef(b.type).name, {1.0f, 0.4f, 0.3f, 1.0f});
        emitSound("alarm", b.pos, 0.8f, false);
    }
    if (attackerUnit >= 0) alertDefenders(b.faction, b.pos, attackerUnit);
    if (b.hp <= 0.0f) destroyBuilding(target, attackerFaction);
}

void World::alertDefenders(int faction, const Vec3& p, int attackerUnit) {
    if (attackerUnit < 0 || !units[attackerUnit].alive) return;
    for (auto& u : units) {
        if (!u.alive || u.faction != faction || u.type == UnitType::Settler) continue;
        if (u.state != UnitState::Idle || u.targetUnit >= 0) continue;
        if (dist2D(u.pos, p) > 140.0f) continue;
        u.targetUnit = attackerUnit;
        u.state = UnitState::Attack;
        u.path.clear();
    }
}

void World::fireArrow(const Vec3& from, int faction, int targetUnit, int targetBuilding, float damage, int shooter) {
    Projectile p;
    p.alive = true;
    p.start = from;
    p.faction = faction;
    p.targetUnit = targetUnit;
    p.targetBuilding = targetBuilding;
    p.damage = damage;
    p.shooter = shooter;
    if (targetUnit >= 0) {
        p.end = add(units[targetUnit].pos, {0.0f, 3.0f, 0.0f});
    } else {
        p.end = add(buildings[targetBuilding].pos, {0.0f, buildingDef(buildings[targetBuilding].type).height * 0.3f, 0.0f});
    }
    const float d = dist2D(p.start, p.end);
    p.duration = 0.25f + d / 110.0f;
    p.arcHeight = d * 0.12f;
    emitSound("bow", from, 0.5f);
    p.entity = makeEntity("arrow", kArrowMesh);
    p.entity->getComponent<MeshComponent>()->castShadows = false;
    p.entity->transform().position = from;
    projectiles.push_back(p);
}

void World::updateProjectiles(float dt) {
    for (auto& p : projectiles) {
        if (!p.alive) continue;
        // Strzała naprowadzana na aktualną pozycję celu
        if (p.targetUnit >= 0 && units[p.targetUnit].alive) {
            p.end = add(units[p.targetUnit].pos, {0.0f, 3.0f, 0.0f});
        }
        p.t += dt / p.duration;
        const float t = std::min(p.t, 1.0f);
        Vec3 pos = lerp(p.start, p.end, t);
        pos.y += p.arcHeight * 4.0f * t * (1.0f - t);
        Vec3 vel = subtract(p.end, p.start);
        vel.y += p.arcHeight * 4.0f * (1.0f - 2.0f * t);
        const float horiz = std::sqrt(vel.x * vel.x + vel.z * vel.z);
        auto& tr = p.entity->transform();
        tr.position = pos;
        tr.rotation.y = std::atan2(vel.x, vel.z);
        tr.rotation.x = -std::atan2(vel.y, horiz);

        if (p.t >= 1.0f) {
            if (p.targetUnit >= 0 && units[p.targetUnit].alive) {
                const int shooter = (p.shooter >= 0 && units[p.shooter].alive) ? p.shooter : -1;
                emitSound("arrow_hit", p.end, 0.5f);
                damageUnit(p.targetUnit, p.damage * randf(0.85f, 1.15f), p.faction, shooter);
            } else if (p.targetBuilding >= 0 && buildings[p.targetBuilding].alive) {
                const int shooter = (p.shooter >= 0 && units[p.shooter].alive) ? p.shooter : -1;
                damageBuilding(p.targetBuilding, p.damage * 0.6f, p.faction, shooter);
            }
            p.alive = false;
            destroyEntity(p.entity);
        }
    }
    projectiles.erase(std::remove_if(projectiles.begin(), projectiles.end(), [](const Projectile& p) { return !p.alive; }),
                      projectiles.end());
}

void World::updateEffects(float dt) {
    for (auto& fx : effects) {
        if (!fx.alive) continue;
        fx.t += dt;
        const float k = fx.t / fx.duration;
        if (fx.kind == 0 && fx.entity) {
            const float s = fx.baseScale * (1.0f - 0.6f * k);
            fx.entity->transform().scale = {s, 1.0f, s};
        } else if (fx.kind == 1 && fx.entity && k > 0.8f) {
            auto& sc = fx.entity->transform().scale;
            sc.y = std::max(0.01f, sc.y * (1.0f - dt * 2.0f));
        }
        if (fx.t >= fx.duration) {
            fx.alive = false;
            destroyEntity(fx.entity);
        }
    }
    effects.erase(std::remove_if(effects.begin(), effects.end(), [](const Effect& e) { return !e.alive; }), effects.end());
}

void World::updateNodes(float dt) {
    for (auto& n : nodes) {
        if (!n.alive || !n.entity) continue;
        auto& tr = n.entity->transform();
        if (n.kind == NodeKind::Tree) {
            if (n.fallTimer >= 0.0f) {
                n.fallTimer += dt;
                const float k = std::min(1.0f, n.fallTimer / 1.4f);
                tr.rotation.x = k * k * (PI * 0.5f);
                if (n.fallTimer > 2.6f) {
                    tr.scale = multiply(tr.scale, std::max(0.0f, 1.0f - dt * 3.0f));
                }
                if (n.fallTimer > 3.4f) {
                    n.alive = false;
                    destroyEntity(n.entity);
                }
            } else if (n.shake > 0.0f) {
                n.shake = std::max(0.0f, n.shake - dt);
                tr.rotation.z = std::sin(gameTime * 32.0f) * 0.035f;
            } else {
                tr.rotation.z = 0.0f;
            }
        } else {
            // Skała maleje w miarę wydobycia
            const float k = 0.35f + 0.65f * static_cast<float>(n.amount) / static_cast<float>(n.maxAmount);
            const float s = n.baseScale * k;
            tr.scale = {s, s, s};
            if (n.amount <= 0) {
                n.alive = false;
                const float r = n.radius * 0.7f;
                nav_.removeObstacle(n.pos.x - r, n.pos.z - r, n.pos.x + r, n.pos.z + r);
                regionsDirty_ = true;
                destroyEntity(n.entity);
            }
        }
    }
}

void World::updateBuildings(float dt) {
    for (size_t i = 0; i < buildings.size(); ++i) {
        Building& b = buildings[i];
        if (!b.alive || !b.constructed) continue;
        const BuildingDef& def = buildingDef(b.type);

        // Kontrola surowców w zasięgu drwala / kamieniarza (niezależnie od pracownika)
        b.resourceCheckTimer -= dt;
        if (b.resourceCheckTimer <= 0.0f) {
            b.resourceCheckTimer = 2.0f;
            checkWorkResources(b);
        }

        // Szkolenie wojska
        if (b.type == BuildingType::Barracks && !b.trainQueue.empty()) {
            const UnitType type = b.trainQueue.front();
            b.trainTimer += dt;
            if (b.trainTimer >= unitDef(type).trainTime) {
                FactionState& fs = factions[b.faction];
                if (fs.population < fs.popCap) {
                    b.trainTimer = 0.0f;
                    b.trainQueue.pop_front();
                    const Vec3 door = b.door();
                    const int id = spawnUnit(type, b.faction, door);
                    Unit& u = units[id];
                    const Vec3 rally = add(door, rotateY({randf(-8.0f, 8.0f), 0.0f, randf(8.0f, 16.0f)}, b.rotY));
                    u.state = UnitState::Moving;
                    u.guardPos = rally;
                    setPath(u, rally);
                    fs.soldiersTrained++;
                    if (b.faction == kPlayer) {
                        pushMessage(std::string("Wyszkolono: ") + unitDef(type).name, {0.7f, 0.9f, 1.0f, 1.0f});
                        emitSound("trained", door, 0.8f, false);
                    }
                } else {
                    b.trainTimer = unitDef(type).trainTime;  // czeka na miejsce
                }
            }
        }

        // Obrona: wieże i zamek strzelają do wrogów
        if (def.attackRange > 0.0f) {
            b.attackCooldown -= dt;
            if (b.attackCooldown <= 0.0f) {
                const int t = findEnemyUnitNear(b.faction, b.pos, def.attackRange);
                if (t >= 0) {
                    b.attackCooldown = def.attackCooldown;
                    fireArrow(add(b.pos, {0.0f, def.height * 0.75f, 0.0f}), b.faction, t, -1, def.attackDamage);
                } else {
                    b.attackCooldown = 0.3f;
                }
            }
        }
    }
}

void World::separateUnits(float dt) {
    const size_t n = units.size();
    for (size_t i = 0; i < n; ++i) {
        Unit& a = units[i];
        if (!a.alive) continue;
        const float ra = unitDef(a.type).radius;
        for (size_t j = i + 1; j < n; ++j) {
            Unit& b = units[j];
            if (!b.alive) continue;
            const float rb = unitDef(b.type).radius;
            const float dx = b.pos.x - a.pos.x, dz = b.pos.z - a.pos.z;
            const float minD = (ra + rb) * 0.9f;
            if (std::abs(dx) > minD || std::abs(dz) > minD) continue;
            float d = std::sqrt(dx * dx + dz * dz);
            if (d >= minD) continue;
            float nx, nz;
            if (d < 0.01f) {
                nx = 1.0f;
                nz = 0.0f;
                d = 0.0f;
            } else {
                nx = dx / d;
                nz = dz / d;
            }
            // Przeciskająca się jednostka nie zderza się z innymi
            if (a.ghostTimer > 0.0f || b.ghostTimer > 0.0f) continue;
            const float push = (minD - d) * 0.5f;
            const bool aFixed = a.state == UnitState::Building || a.state == UnitState::Gathering;
            const bool bFixed = b.state == UnitState::Building || b.state == UnitState::Gathering;
            if (aFixed && bFixed) continue;
            const bool aMoving = a.pathIndex < a.path.size();
            const bool bMoving = b.pathIndex < b.path.size();

            float wa = 1.0f, wb = 1.0f;
            float bx = nx, bz = nz;      // kierunek odsunięcia b (a przesuwa się przeciwnie)
            if (aFixed || bFixed) {
                // Pracujący stoją w miejscu, inni ich omijają
                wa = aFixed ? 0.0f : 2.0f;
                wb = bFixed ? 0.0f : 2.0f;
            } else if (aMoving != bMoving) {
                // Stojący ustępuje drogi idącemu - odsuwa się na bok względem kierunku marszu
                Unit& mover = aMoving ? a : b;
                const Vec3& target = mover.path[mover.pathIndex];
                Vec3 dir{target.x - mover.pos.x, 0.0f, target.z - mover.pos.z};
                const float dl = std::sqrt(dir.x * dir.x + dir.z * dir.z);
                // n: od idącego do stojącego
                float sx = aMoving ? nx : -nx, sz = aMoving ? nz : -nz;
                if (dl > 0.01f) {
                    dir = {dir.x / dl, 0.0f, dir.z / dl};
                    const float along = sx * dir.x + sz * dir.z;
                    float px = sx - dir.x * along, pz = sz - dir.z * along;
                    float pl = std::sqrt(px * px + pz * pz);
                    if (pl < 0.2f) {
                        px = -dir.z;   // dokładnie na drodze - w bok
                        pz = dir.x;
                        pl = 1.0f;
                    }
                    sx = px / pl;
                    sz = pz / pl;
                }
                if (aMoving) {
                    bx = sx; bz = sz;
                    wa = 0.15f; wb = 2.2f;
                } else {
                    bx = -sx; bz = -sz;     // a jest stojącym: a przesuwa się o -b, więc odwracamy
                    wa = 2.2f; wb = 0.15f;
                }
            }
            const Vec3 na{a.pos.x - bx * push * wa, a.pos.y, a.pos.z - bz * push * wa};
            const Vec3 nb{b.pos.x + bx * push * wb, b.pos.y, b.pos.z + bz * push * wb};
            if (nav_.isWalkableWorld(na.x, na.z)) a.pos = na;
            if (nav_.isWalkableWorld(nb.x, nb.z)) b.pos = nb;
        }
    }

    // Jednostki uwięzione w zablokowanej komórce (np. budynek postawiony na nich) wychodzą na zewnątrz.
    // Wyjątek: rolnik pracujący na polu wewnątrz obrysu swojej farmy.
    for (size_t i = 0; i < n; ++i) {
        Unit& u = units[i];
        if (!u.alive || nav_.isWalkableWorld(u.pos.x, u.pos.z) || isWorkerOnFarm(u, static_cast<int>(i))) continue;
        // Idących nie wypychamy: ścieżka w linii prostej może ściąć róg zablokowanej komórki,
        // a wypychanie walczyłoby z ruchem (jednostka dreptałaby w miejscu).
        if (u.pathIndex < u.path.size()) continue;
        int cx, cz, wx, wz;
        if (!nav_.worldToCell(u.pos.x, u.pos.z, cx, cz) || !nav_.findNearestWalkable(cx, cz, 10, wx, wz)) continue;
        const Vec3 c = nav_.cellCenter(wx, wz);
        const float dx = c.x - u.pos.x, dz = c.z - u.pos.z;
        const float d = std::sqrt(dx * dx + dz * dz);
        const float step = std::min(d, 25.0f * dt);
        if (d > 0.001f) {
            u.pos.x += dx / d * step;
            u.pos.z += dz / d * step;
        }
    }
}

void World::syncVisuals(float dt) {
    (void)dt;
    for (auto& u : units) {
        if (!u.alive || !u.entity) continue;
        u.pos.y = heightAt(u.pos.x, u.pos.z);
        auto& tr = u.entity->transform();
        Vec3 p = u.pos;
        Vec3 rot{0.0f, u.yaw, 0.0f};
        if (u.moving) {
            p.y += std::abs(std::sin(u.animTime * 10.0f)) * 0.55f;
            rot.z = std::sin(u.animTime * 10.0f) * 0.07f;
        } else if (u.state == UnitState::Building || u.state == UnitState::Gathering) {
            rot.x = std::max(0.0f, std::sin(u.animTime * 7.0f)) * 0.35f;
        } else if (u.state == UnitState::Attack && u.type == UnitType::Swordsman) {
            const float since = unitDef(u.type).cooldown - u.attackCooldown;
            if (since >= 0.0f && since < 0.3f) {
                const float k = std::sin(since / 0.3f * PI);
                rot.x = k * 0.45f;
                p = add(p, {std::sin(u.yaw) * k * 0.8f, 0.0f, std::cos(u.yaw) * k * 0.8f});
            }
        }
        if (u.hitFlash > 0.0f) {
            p.y += u.hitFlash * 1.5f;
        }
        tr.position = p;
        tr.rotation = rot;

        if (u.ring) {
            u.ring->setActive(u.selected);
            if (u.selected) {
                const float s = unitDef(u.type).radius * 1.6f;
                u.ring->transform().position = {u.pos.x, u.pos.y + 0.5f, u.pos.z};
                u.ring->transform().scale = {s, 1.0f, s};
            }
        }
    }

    for (auto& b : buildings) {
        if (!b.alive || !b.entity) continue;
        if (!b.constructed) {
            b.entity->transform().scale = {1.0f, 0.05f + 0.95f * b.progress, 1.0f};
        }
        if (b.ring) {
            b.ring->setActive(b.selected);
            if (b.selected) {
                const float s = buildingDef(b.type).halfSize * 1.45f;
                b.ring->transform().position = {b.pos.x, b.pos.y + 0.8f, b.pos.z};
                b.ring->transform().scale = {s, 1.0f, s};
            }
        }
    }
}

void World::rebuildTerritory() {
    territoryDirty_ = false;
    auto* mm = engine_.getMeshManager();
    ++territoryMeshVersion_;
    for (int f = 0; f < kFactionCount; ++f) {
        struct Src {
            Vec3 p;
            float r;
        };
        std::vector<Src> sources;
        for (const auto& b : buildings) {
            if (!b.alive || b.faction != f || !b.constructed) continue;
            const float r = buildingDef(b.type).territoryRadius;
            if (r > 0.0f) sources.push_back({b.pos, r});
        }
        MeshBuilder mb;
        const Vec3 fc = factionColor(f);
        int posts = 0;
        for (size_t i = 0; i < sources.size(); ++i) {
            const auto& s = sources[i];
            const int count = std::max(12, static_cast<int>(2.0f * PI * s.r / 10.0f));
            for (int k = 0; k < count; ++k) {
                const float a = 2.0f * PI * static_cast<float>(k) / static_cast<float>(count);
                const Vec3 p{s.p.x + std::cos(a) * s.r, 0.0f, s.p.z + std::sin(a) * s.r};
                bool inside = false;
                for (size_t j = 0; j < sources.size() && !inside; ++j) {
                    if (j != i && dist2D(p, sources[j].p) < sources[j].r - 2.0f) inside = true;
                }
                if (inside) continue;
                const float h = heightAt(p.x, p.z);
                if (h < kWaterLevel) continue;
                mb.cylinder({p.x, h - 0.5f, p.z}, 0.4f, 3.4f, 5, {0.45f, 0.32f, 0.2f});
                mb.box({p.x, h + 3.2f, p.z}, {1.0f, 0.8f, 1.0f}, fc);
                ++posts;
            }
        }
        auto* mc = territoryEntity_[f]->getComponent<MeshComponent>();
        if (posts > 0) {
            const std::string name = "territory_" + std::to_string(f) + "_" + std::to_string(seed_);
            mm->loadMesh(name, mb.build());
            mc->meshName = name;
            territoryEntity_[f]->setActive(true);
        } else {
            territoryEntity_[f]->setActive(false);
        }
    }
}

void World::updateAI(int f, float dt) {
    FactionState& fs = factions[f];
    if (fs.defeated) return;

    // Dochód bierny rekompensuje prostotę AI (zależny od poziomu trudności)
    const float income = difficulty == Difficulty::Easy ? 0.12f : (difficulty == Difficulty::Normal ? 0.3f : 0.6f);
    fs.incomeAccumulator += dt * income;
    while (fs.incomeAccumulator >= 1.0f) {
        fs.incomeAccumulator -= 1.0f;
        fs.res.add({1, 1, 1});
    }

    fs.aiThinkTimer -= dt;
    if (fs.aiThinkTimer <= 0.0f) {
        fs.aiThinkTimer = 3.0f;
        const Vec3 home = (fs.castle >= 0 && buildings[fs.castle].alive) ? buildings[fs.castle].pos : startPos[f];

        int underConstruction = 0;
        bool houseInProgress = false;
        for (const auto& b : buildings) {
            if (!b.alive || b.faction != f || b.constructed) continue;
            ++underConstruction;
            if (b.type == BuildingType::House) houseInProgress = true;
        }

        if (underConstruction < 2) {
            BuildingType next;
            const size_t orderLen = std::size(kAiBuildOrder);
            if (fs.popCap - fs.population <= 2 && !houseInProgress) {
                next = BuildingType::House;
            } else if (fs.buildOrderIndex < orderLen) {
                next = kAiBuildOrder[fs.buildOrderIndex];
            } else {
                next = kAiLateCycle[(fs.buildOrderIndex - orderLen) % std::size(kAiLateCycle)];
            }
            const BuildingDef& def = buildingDef(next);
            if (fs.res.canAfford(def.cost)) {
                Vec3 p;
                const float maxR = buildingDef(BuildingType::Castle).territoryRadius - 25.0f;
                // Wieże stawiamy na obrzeżach (poszerzają terytorium), resztę bliżej zamku
                const float minR = next == BuildingType::Tower ? maxR * 0.7f : 30.0f;
                if (findPlacementNear(next, f, home, minR, next == BuildingType::Tower ? maxR + 60.0f : maxR, p)) {
                    placeBuilding(next, p, f, false, 0.0f);
                    if (!(next == BuildingType::House && fs.popCap - fs.population <= 2 &&
                          (fs.buildOrderIndex >= orderLen || kAiBuildOrder[fs.buildOrderIndex] != BuildingType::House))) {
                        fs.buildOrderIndex++;
                    }
                    fs.placementFailures = 0;
                } else if (++fs.placementFailures > 4) {
                    fs.buildOrderIndex++;
                    fs.placementFailures = 0;
                }
            }
        }

        // Szkolenie wojska
        for (size_t i = 0; i < buildings.size(); ++i) {
            const Building& b = buildings[i];
            if (!b.alive || b.faction != f || b.type != BuildingType::Barracks || !b.constructed) continue;
            if (b.trainQueue.size() >= 2) continue;
            const UnitType type = (fs.soldiersTrained % 3 == 2) ? UnitType::Archer : UnitType::Swordsman;
            queueTraining(static_cast<int>(i), type);
        }
    }

    // Fale ataku
    fs.waveTimer -= dt;
    if (fs.waveTimer <= 0.0f) {
        std::vector<int> army;
        for (size_t i = 0; i < units.size(); ++i) {
            const Unit& u = units[i];
            if (u.alive && u.faction == f && u.type != UnitType::Settler &&
                (u.state == UnitState::Idle || (u.state == UnitState::Moving && !u.attackMove))) {
                army.push_back(static_cast<int>(i));
            }
        }
        const int minWave = (difficulty == Difficulty::Easy ? 4 : (difficulty == Difficulty::Normal ? 5 : 6)) + fs.waveNumber;
        if (static_cast<int>(army.size()) >= minWave + 2) {
            // Dwóch obrońców zostaje przy zamku
            const Vec3 home = (fs.castle >= 0 && buildings[fs.castle].alive) ? buildings[fs.castle].pos : startPos[f];
            std::sort(army.begin(), army.end(), [&](int a, int b) { return dist2D(units[a].pos, home) < dist2D(units[b].pos, home); });
            army.erase(army.begin(), army.begin() + 2);

            // Cel: najbliższy budynek gracza
            int target = -1;
            float best = std::numeric_limits<float>::max();
            for (size_t i = 0; i < buildings.size(); ++i) {
                const Building& b = buildings[i];
                if (!b.alive || b.faction == f) continue;
                const float d = dist2D(b.pos, home);
                if (d < best) {
                    best = d;
                    target = static_cast<int>(i);
                }
            }
            if (target >= 0) {
                commandAttackBuilding(army, target);
                fs.waveNumber++;
                pushMessage("Armia wroga wyrusza do ataku! (" + std::to_string(army.size()) + " żołnierzy)",
                            {1.0f, 0.35f, 0.3f, 1.0f});
            }
            fs.waveTimer = difficulty == Difficulty::Easy ? 260.0f : (difficulty == Difficulty::Normal ? 180.0f : 120.0f);
        } else {
            fs.waveTimer = 20.0f;
        }
    }
}

void World::checkVictory() {
    if (winner >= 0) return;
    if (factions[kPlayer].defeated) {
        winner = kEnemy;
    } else if (factions[kEnemy].defeated) {
        winner = kPlayer;
    }
}

} // namespace settlers
