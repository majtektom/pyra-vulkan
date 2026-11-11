/**
 * world_create.cpp - tworzenie nowego świata.
 *
 * Kolejność (World::create = createLand + createStartingBases + finishCreate; wczytanie zapisu - world_save.cpp -
 * zastępuje createStartingBases odtworzeniem obiektów z pliku):
 *   1. heightmapa z szumu (value noise / fBm) z płaskowyżami pod bazy i lądowym korytarzem między nimi,
 *   2. teren 3D z warstwami tekstur (trawa, piasek, skały, śnieg) mieszanymi maskami,
 *   3. siatka nawigacji: woda, strome zbocza i krawędź mapy są nieprzechodnie,
 *   4. lasy i skały (surowce),
 *   5. zamki, startowi osadnicy i żołnierze obu stron.
 */
#include "core/noise.h"
#include "world_internal.h"

namespace settlers {

// Szum do generowania terenu: engine3d::ValueNoise (core/noise.h).
using Noise = ValueNoise;

// Tworzy nową partię: teren, nawigację, surowce, zamki z jednostkami startowymi
// i (zależnie od poziomu trudności) gotowe budynki przeciwnika.
bool World::create(Scene* scene, uint32_t seed, Difficulty diff) {
    activate();
    createLand(scene, seed, diff);
    createStartingBases(diff);
    finishCreate();

    pushMessage("Witaj, władco! Rozbuduj osadę i zniszcz zamek wroga.", {1.0f, 0.95f, 0.6f, 1.0f});
    pushMessage("Zacznij od Drwala z Tartakiem (deski) i Kamieniarza; żywność: Rybak, Myśliwy, Farma+Młyn+Piekarnia.",
                {0.85f, 0.95f, 1.0f, 1.0f});
    pushMessage("Połącz warsztaty drogą (T) z zamkiem - towary liczą się dopiero w magazynie.", {0.85f, 0.95f, 1.0f, 1.0f});
    return true;
}

// Ląd i surowce - wszystko, co wynika z samego ziarna mapy (nowa gra i wczytanie zapisu): heightmapa, teren 3D,
// nawigacja, lasy i skały. Generator losowy jest ustawiany ziarnem, więc wynik jest zawsze taki sam.
void World::createLand(Scene* scene, uint32_t seed, Difficulty diff) {
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
    rebuildRoadCells();

    for (auto& p : startPos) {
        p.y = heightAt(p.x, p.z);
    }

    placeResources();

    for (int f = 0; f < kFactionCount; ++f) {
        territoryEntity_[f] = makeEntity("territory", "");
        territoryEntity_[f]->getComponent<MeshComponent>()->castShadows = false;
    }

    nodesGenerated_ = nodes.size();
}

// Zamki, startowi osadnicy i żołnierze oraz (zależnie od poziomu) gotowe budynki przeciwnika.
void World::createStartingBases(Difficulty diff) {
    // Startowe zasoby leżą w zamku (zamek jest pierwszym magazynem).
    factions = {};
    using C = CarryType;
    const Resources normalStart{{C::Planks, 60}, {C::Stone, 40}, {C::Wood, 10}, {C::Bread, 20}, {C::Fish, 15}, {C::Weapons, 4}};
    // Bogaty start: więcej materiałów i żywności, ale broń ta sama - wojsko trzeba wyprodukować (kuźnia).
    const Resources richStart{{C::Planks, 110}, {C::Stone, 80}, {C::Wood, 20}, {C::Bread, 40}, {C::Fish, 30}, {C::Weapons, 4}};
    Resources startRes[kFactionCount] = {normalStart, normalStart};
    if (diff == Difficulty::Hard) startRes[kEnemy] = richStart;
    if (diff == Difficulty::Easy) startRes[kPlayer] = richStart;
    // Przeciwnik nie dostaje broni na start - każdego żołnierza ponad oddział startowy musi wykuć w kuźni.
    startRes[kEnemy][C::Weapons] = 0;

    for (int f = 0; f < kFactionCount; ++f) {
        const int castle = placeBuilding(BuildingType::Castle, snapToGrid(startPos[f]), f, true);
        factions[f].castle = castle;
        buildings[castle].stock = startRes[f];
        const Vec3 door = buildings[castle].door();
        for (int i = 0; i < 6; ++i) {
            spawnUnit(UnitType::Settler, f, add(door, {randf(-8, 8), 0.0f, randf(2, 10)}));
        }
        for (int i = 0; i < 2; ++i) {   // ten sam oddział startowy dla obu stron
            spawnUnit(UnitType::Swordsman, f, add(door, {-10.0f + i * 5.0f, 0.0f, 16.0f}));
        }
        spawnUnit(UnitType::Archer, f, add(door, {12.0f, 0.0f, 14.0f}));
    }

    // Przeciwnik zaczyna z kilkoma budynkami gospodarczymi (zależnie od poziomu trudności).
    {
        const int extra = diff == Difficulty::Easy ? 1 : (diff == Difficulty::Normal ? 2 : 4);
        const BuildingType pre[] = {BuildingType::Woodcutter, BuildingType::Sawmill, BuildingType::Quarry, BuildingType::Hunter};
        for (int i = 0; i < extra; ++i) {
            Vec3 p;
            if (findPlacementNear(pre[i], kEnemy, startPos[kEnemy], 35.0f, 90.0f, p)) {
                const int id = placeBuilding(pre[i], p, kEnemy, true);
                autoConnectFlag(buildings[id].flag);   // AI od razu łączy budynek drogą z zamkiem
            }
        }
        factions[kEnemy].waveTimer = diff == Difficulty::Easy ? 480.0f : (diff == Difficulty::Normal ? 330.0f : 220.0f);
        factions[kEnemy].aiThinkTimer = 5.0f;
    }

}

// Wspólne zakończenie (nowa gra i wczytanie): terytorium, minimapa, obszary nawigacji, surowce na koncie.
void World::finishCreate() {
    rebuildTerritory();
    buildMinimap();
    nav_.computeRegions(true);
    regionsDirty_ = false;
    refreshFactionResources();
}

// Heightmapa 256x256 z szumu fBm: wzgórza + grzbiety górskie + pierścień gór na krawędzi.
// Pod bazami teren jest spłaszczany, a między bazami powstaje lądowy korytarz (zawsze da się dojść do wroga).
// Na końcu dwa przebiegi wygładzania.
void World::generateHeightmap(uint32_t seed) {
    Noise base{seed * 7u + 1u};
    Noise mountains{seed * 13u + 5u};
    Noise detail{seed * 29u + 9u};
    const int n = kHeightmapSize;
    heightmap_.assign(static_cast<size_t>(n) * n, 0.0f);

    const Vec3 a = startPos[kPlayer], b = startPos[kEnemy];
    const Vec3 ab = subtract(b, a);
    const float abLen2 = dot(ab, ab);

    for (int iz = 0; iz < n; ++iz) {
        for (int ix = 0; ix < n; ++ix) {
            const float x = (static_cast<float>(ix) - n * 0.5f) * kScaleXZ;
            const float z = (static_cast<float>(iz) - n * 0.5f) * kScaleXZ;

            // Niziny: łagodne pagórki 20..48 (najniższe miejsca poniżej poziomu wody to jeziora).
            float h = 34.0f + base.fbm(x / 230.0f, z / 230.0f, 5) * 14.0f;

            // Masywy górskie z szumu o niskiej częstotliwości (raw ok. -0,5..0,5). Profil góry:
            //  - zbocza i wyżyny (raw -0,12..0,25): szeroki, łagodny wzrost do wyżyny ~88, zawsze poniżej
            //    linii śniegu - tu da się chodzić i budować, a powyżej kMountainLevel stawia się kopalnie,
            //  - szczyty (raw > 0,3, rzadkie): wyrastają ponad linię śniegu - ośnieżone i niedostępne.
            const float raw = mountains.fbm(x / 320.0f, z / 320.0f, 3);
            const float m = smoothstep(-0.12f, 0.25f, raw);
            if (m > 0.0f) {
                const float upland = 88.0f + detail.fbm(x / 60.0f, z / 60.0f, 3) * 5.0f;
                h = h + (upland - h) * m;
                const float peak = smoothstep(0.3f, 0.45f, raw);
                h += 34.0f * peak * peak;
            }

            // Płaskowyże pod bazy
            for (const auto& s : startPos) {
                const float d = dist2D({x, 0, z}, s);
                const float w = smoothstep(175.0f, 85.0f, d);
                h = h + (46.0f + base.fbm(x / 60.0f, z / 60.0f, 2) * 3.0f - h) * w;
            }

            // Gwarantowany masyw przy każdej bazie (miejsce na kopalnię): dodawany PO spłaszczeniu płaskowyżu,
            // w bok od drogi do wroga, tak by jego skaliste zbocze leżało w zasięgu terytorium zamku.
            for (const auto& s : startPos) {
                const Vec3 toCenter = normalize(Vec3{-s.x, 0.0f, -s.z});
                const Vec3 dir = rotateY(toCenter, 65.0f * PI / 180.0f);
                const Vec3 c{s.x + dir.x * 170.0f, 0.0f, s.z + dir.z * 170.0f};
                const float d = dist2D({x, 0, z}, c);
                // Szeroki (sigma 110), łagodny masyw: nachylenie zboczy <= ok. 0,55, więc kopalnia zmieści się na zboczu.
                h = std::max(h, 30.0f + 70.0f * std::exp(-(d * d) / (110.0f * 110.0f)));
            }

            // Lądowy korytarz między bazami (gwarantuje drogę do wroga)
            float t = std::clamp(((x - a.x) * ab.x + (z - a.z) * ab.z) / abLen2, 0.0f, 1.0f);
            const Vec3 onSeg{a.x + ab.x * t, 0.0f, a.z + ab.z * t};
            const float ds = dist2D({x, 0, z}, onSeg);
            const float wc = smoothstep(80.0f, 35.0f, ds);
            const float corridorH = 40.0f + base.fbm(x / 90.0f, z / 90.0f, 2) * 6.0f;
            if (h < corridorH) h = h + (corridorH - h) * wc;
            if (h > corridorH + 20.0f) h = h + (corridorH + 20.0f - h) * wc * 0.8f;

            // Wyspa: przy krawędzi mapy teren łagodnie schodzi pod wodę (morze dookoła).
            // Odległość od środka to mieszanka "kwadratowej" i okrągłej, zaburzona szumem - brzeg jest
            // zaokrąglony i nieregularny (zatoki, półwyspy), a narożniki mapy to otwarte morze.
            const float square = std::max(std::abs(x), std::abs(z)) / kWorldHalf;
            const float round = std::sqrt(x * x + z * z) / kWorldHalf * 0.8f;
            const float edge = std::max(square, round) + detail.fbm(x / 140.0f, z / 140.0f, 3) * 0.12f;
            const float coast = smoothstep(0.72f, 0.93f, edge);
            h = h + (8.0f - h) * coast;

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

// Encja terenu z pięcioma warstwami tekstur. Maski warstw (0..255) liczymy z wysokości i nachylenia:
// trawa (baza), druga trawa (plamy z szumu), piasek (przy wodzie), skały (strome/wysokie), śnieg (szczyty).
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
            // Skała: strome zbocza i góry powyżej linii lasu; śnieg: szczyty powyżej kSnowLine (niedostępne).
            maskRock[i] = 255.0f * std::max(smoothstep(0.15f, 0.3f, slope), smoothstep(kMountainLevel - 6.0f, kMountainLevel + 2.0f, h));
            maskSnow[i] = 255.0f * smoothstep(kSnowLine - 5.0f, kSnowLine + 1.0f, h);
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

// Siatka nawigacji 4x4 jednostki: woda, strome zbocza i pas przy krawędzi mapy są nieprzechodnie.
void World::buildNavGrid() {
    const int cells = static_cast<int>(kWorldHalf * 2.0f / kNavCell);
    nav_.init(cells, cells, kNavCell, {-kWorldHalf, 0.0f, -kWorldHalf});
    for (int cz = 0; cz < cells; ++cz) {
        for (int cx = 0; cx < cells; ++cx) {
            const Vec3 c = nav_.cellCenter(cx, cz);
            const float h = heightAt(c.x, c.z);
            const Vec3 nrm = terrain_->getNormal(c.x, c.z);
            const bool edge = std::abs(c.x) > kWorldHalf - 16.0f || std::abs(c.z) > kWorldHalf - 16.0f;
            // Nieprzechodnie: krawędź mapy, woda, ośnieżone szczyty i bardzo strome zbocza (> ok. 48°).
            const bool blocked = edge || h < kWaterLevel + 1.2f || h > kSnowLine || nrm.y < 0.66f;
            nav_.setStaticBlocked(cx, cz, blocked);
        }
    }
}

// Dodaje drzewo lub skałę. Skała jest przeszkodą w nawigacji, drzewo nie (osadnicy chodzą przez las).
int World::addNode(NodeKind kind, const Vec3& pos, int variant, float scale) {
    ResourceNode node;
    node.alive = true;
    node.kind = kind;
    node.pos = pos;
    node.pos.y = heightAt(pos.x, pos.z);
    node.baseScale = scale;
    node.variant = variant;
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
    node.heightJitter = randf(0.9f, 1.15f);
    node.entity->transform().scale = {scale, scale * node.heightJitter, scale};
    // Sloty surowców z generatora mapy (indeksy < nodesGenerated_) nie są używane ponownie - zapis gry odtwarza
    // je z ziarna mapy; sadzonki leśniczego zajmują sloty po wyczerpanych sadzonkach.
    const int id = nodesGenerated_ > 0 ? freeNodes_.allocate(nodes, gameTime) : static_cast<int>(nodes.size());
    if (id == static_cast<int>(nodes.size())) nodes.emplace_back();
    node.gen = nodes[id].gen;
    nodes[id] = node;
    return id;
}

// Rozmieszcza surowce: lasy wg szumu, po dwa lasy i grupę skał przy każdej bazie, skały na wzgórzach.
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
    // Drzewa rosną tylko poniżej granicy lasu (wyżej są nagie skały i śnieg).
    auto treeSpot = [&](const Vec3& p, float minDistToBase) {
        return suitable(p, minDistToBase) && heightAt(p.x, p.z) < kMountainLevel + 6.0f;
    };

    // Lasy z szumu
    for (float z = -kWorldHalf + 30.0f; z < kWorldHalf - 30.0f; z += 11.0f) {
        for (float x = -kWorldHalf + 30.0f; x < kWorldHalf - 30.0f; x += 11.0f) {
            const float f = forest.fbm(x / 140.0f, z / 140.0f, 3);
            if (f < 0.12f || randf(0.0f, 1.0f) > (f - 0.12f) * 2.4f) continue;
            const Vec3 p{x + randf(-4.5f, 4.5f), 0.0f, z + randf(-4.5f, 4.5f)};
            if (!treeSpot(p, 75.0f)) continue;
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
                if (treeSpot(p, 60.0f)) addNode(NodeKind::Tree, p, static_cast<int>(randf(0.0f, 3.99f)), randf(0.85f, 1.25f));
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

// Liczy kolory minimapy (woda, piasek, trawa wg wysokości, skały, śnieg).
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
            } else if (h > kSnowLine) {
                c = {0.9f, 0.9f, 0.92f};
            } else if (terrain_->getNormal(wx, wz).y < 0.75f || h > kMountainLevel) {
                c = {0.45f, 0.43f, 0.4f};
            } else {
                const float t = std::clamp((h - 30.0f) / 60.0f, 0.0f, 1.0f);
                c = {0.22f + 0.15f * t, 0.45f + 0.05f * t, 0.16f + 0.08f * t};
            }
            minimapColors_[static_cast<size_t>(y) * minimapSize_ + x] = c;
        }
    }
}

} // namespace settlers
