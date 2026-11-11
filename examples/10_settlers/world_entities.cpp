/**
 * world_entities.cpp - powstawanie i znikanie obiektów gry.
 *
 * Każdy obiekt gry (jednostka, budynek, drzewo, strzała) ma swoją encję 3D w scenie silnika
 * (Entity + MeshComponent). Tutaj są funkcje, które tworzą i usuwają te encje razem z danymi gry,
 * oraz zmiany stanu "na całe życie" obiektu: ukończenie budowy, śmierć, zburzenie.
 */
#include "world_internal.h"

namespace settlers {

World::World(Engine& engine, const GameMaterials& materials) : engine_(engine), materials_(materials) {
    // Rezerwacja z zapasem - indeksy do wektorów są używane jako identyfikatory obiektów.
    units.reserve(2048);
    buildings.reserve(512);
    nodes.reserve(1024);
    flags.reserve(1024);
    roads.reserve(1024);
    static int worldCounter = 0;   // każdy świat ma własny prefiks siatek (stary i nowy istnieją chwilę naraz)
    meshPrefix_ = "w" + std::to_string(++worldCounter) + "_";
    activate();
}

// Ten świat jest "bieżący" dla nowych przypisań Ref<T> (slot_ref.h). Wołane w konstruktorze i na wejściu do
// kroku gry, zapisu i wczytania - przy kilku światach naraz każdy przypisuje odwołania do swoich wektorów.
void World::activate() {
    SlotRegistry<Unit>::items = &units;
    SlotRegistry<Building>::items = &buildings;
    SlotRegistry<Flag>::items = &flags;
    SlotRegistry<Road>::items = &roads;
    SlotRegistry<TransportOrder>::items = &orders;
    SlotRegistry<ResourceNode>::items = &nodes;
}

// Siatki tworzone przez ten świat (teren, drogi, granice terytorium) - zwalniane, inaczej każda nowa gra
// i każde wczytanie zapisu zostawiałyby je w pamięci GPU.
World::~World() {
    if (SlotRegistry<Unit>::items == &units) {
        SlotRegistry<Unit>::items = nullptr;
        SlotRegistry<Building>::items = nullptr;
        SlotRegistry<Flag>::items = nullptr;
        SlotRegistry<Road>::items = nullptr;
        SlotRegistry<TransportOrder>::items = nullptr;
        SlotRegistry<ResourceNode>::items = nullptr;
    }
    auto* mm = engine_.getMeshManager();
    if (!mm) return;
    if (terrain_) terrain_->releaseMeshes(*mm);
    mm->removeMeshesWithPrefix(meshPrefix_);
}

// Punkt przed wejściem: środek budynku + (halfSize + 3) w kierunku przodu (oś +Z obrócona o rotY).
Vec3 Building::door() const {
    const float half = buildingDef(type).halfSize;
    return add(pos, rotateY({0.0f, 0.0f, half + 3.0f}, rotY));
}

// Liczba losowa z przedziału [a, b].
float World::randf(float a, float b) {
    std::uniform_real_distribution<float> d(a, b);
    return d(rng_);
}

// Wysokość terenu w punkcie (x, z).
float World::heightAt(float x, float z) const {
    return terrain_ ? terrain_->getHeight(x, z) : 0.0f;
}

// Zgłasza dźwięk do odtworzenia przez aplikację (limit 256 zdarzeń na klatkę).
void World::emitSound(const char* name, const Vec3& pos, float volume, bool positional) {
    if (soundEvents.size() < 256) soundEvents.push_back({name, pos, volume, positional});
}

// Dodaje komunikat dla gracza (pamiętamy najwyżej 6 ostatnich).
void World::pushMessage(const std::string& text, std::array<float, 4> color) {
    messages.push_back({text, color, gameTime});
    while (messages.size() > 6) messages.pop_front();
}

// Tworzy encję 3D z siatką i wspólnym materiałem. Nazwy encji w scenie muszą być unikalne.
Entity* World::makeEntity(const std::string& prefix, const std::string& mesh) {
    Entity* e = scene_->createEntity(prefix + "_" + std::to_string(++entityCounter_));
    auto* mc = e->addComponent<MeshComponent>(mesh);
    mc->materialHandle = materials_.solid;
    return e;
}

// Usuwa encję ze sceny i zeruje wskaźnik.
void World::destroyEntity(Entity*& e) {
    if (e && scene_) {
        scene_->removeEntity(e);
    }
    e = nullptr;
}

// Podmienia model jednostki (tylko gdy się zmienia - np. osadnik podnosi drewno).
void World::setUnitMesh(Unit& u, const std::string& mesh) {
    if (u.meshName == mesh || !u.entity) return;
    u.meshName = mesh;
    if (auto* mc = u.entity->getComponent<MeshComponent>()) {
        mc->meshName = mesh;
    }
}

// Tworzy jednostkę (jeśli punkt jest nieprzechodni - na najbliższej wolnej komórce) wraz z encją i pierścieniem zaznaczenia.
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
    const int id = freeUnits_.allocate(units, gameTime);   // ponownie użyty slot poległej jednostki albo nowy
    u.gen = units[id].gen;
    units[id] = std::move(u);
    return id;
}

// Stawia budynek: wysokość = średnia terenu pod obrysem, obrys blokuje nawigację.
// Zwykły budynek zaczyna jako plac budowy (rusztowanie, niski model rosnący z postępem budowy).
int World::placeBuilding(BuildingType type, const Vec3& posIn, int faction, bool instant, float rotY) {
    const BuildingDef& def = buildingDef(type);
    Building b;
    b.alive = true;
    b.type = type;
    b.faction = faction;
    b.rotY = rotY;
    b.pos = posIn;
    b.placedTime = gameTime;

    // Wysokość: średnia z narożników i środka obrysu
    const float h = def.halfSize;
    float sum = heightAt(posIn.x, posIn.z);
    sum += heightAt(posIn.x - h, posIn.z - h) + heightAt(posIn.x + h, posIn.z - h);
    sum += heightAt(posIn.x - h, posIn.z + h) + heightAt(posIn.x + h, posIn.z + h);
    b.pos.y = sum / 5.0f;


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
        b.constructedTime = gameTime;
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
    const int id = freeBuildings_.allocate(buildings, gameTime);
    b.gen = buildings[id].gen;
    buildings[id] = std::move(b);

    // Flaga przed wejściem: istniejąca flaga tuż przy drzwiach, podział drogi biegnącej pod drzwiami
    // (budynek postawiony przy drodze od razu jest z nią połączony) albo nowa flaga.
    const Vec3 door = buildings[id].door();
    int flag = flagNear(faction, door, 2.5f);
    if (flag < 0) {
        const int road = roadAtCell(door.x, door.z);
        if (road >= 0 && roads[road].alive && roads[road].faction == faction) {
            const Road& r = roads[road];
            size_t k = 0;
            float bestD = 1e9f;
            for (size_t i = 0; i < r.points.size(); ++i) {
                const float d = distanceXZ(r.points[i], door);
                if (d < bestD) {
                    bestD = d;
                    k = i;
                }
            }
            if (bestD < 2.5f && k > 0 && k + 1 < r.points.size() && distanceXZ(r.points[k], flags[r.flagA].pos) >= 3.0f &&
                distanceXZ(r.points[k], flags[r.flagB].pos) >= 3.0f) {
                flag = splitRoad(road, k);
            }
        }
    }
    if (flag < 0) flag = createFlag(faction, door, id);
    flags[flag].building = id;
    buildings[id].flag = flag;
    routingDirty_ = true;
    // Materiały na budowę: zdejmowane z magazynów od razu, na plac donoszą je tragarze.
    if (!instant) orderMaterials(id, def.cost);
    if (instant && def.territoryRadius > 0.0f) markTerritoryChanged();
    return id;
}

// Koniec budowy: pełny model, usunięcie rusztowania, komunikat; zamek/wieża poszerzają terytorium.
void World::completeBuilding(int id) {
    Building& b = buildings[id];
    const BuildingDef& def = buildingDef(b.type);
    b.constructed = true;
    b.constructedTime = gameTime;
    b.progress = 1.0f;
    b.hp = std::max(b.hp, def.maxHp * 0.9f);
    b.hp = std::min(b.hp, def.maxHp);
    b.delivered = {};   // materiały budowy zużyte
    b.entity->transform().scale = {1.0f, 1.0f, 1.0f};
    destroyEntity(b.scaffold);
    if (def.territoryRadius > 0.0f) markTerritoryChanged();
    routingDirty_ = true;   // nowy magazyn przyjmuje towary
    refreshFactionResources();
    if (b.faction == kPlayer) {
        pushMessage(std::string("Budowa ukończona: ") + def.name, {0.6f, 1.0f, 0.6f, 1.0f});
        emitSound("complete", b.pos, 0.7f, false);
    }
}

// Osadnik porzuca bieżące zadanie: zwalnia rezerwację surowca, przestaje być pracownikiem
// warsztatu, odkłada niesiony surowiec i wraca do stanu Idle.
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
    // Tragarz porzuca zlecenie (rezerwacje są zwalniane, niesiony towar zostaje na fladze).
    if (isCarrierState(u.state)) transport_.releaseCarrierJob(u);
    u.atFlag = -1;
    u.jobFlag = -1;
    u.jobOrder = -1;
    u.jobDest = -1;
    u.carryCount = 0;
    u.resourceNode = -1;
    u.building = -1;
    u.state = UnitState::Idle;
    u.carry = CarryType::None;
    u.path.clear();
    u.stuckTimer = 0.0f;
    u.timer = randf(0.5f, 2.0f);
    setUnitMesh(u, unitMeshName(u.type, u.faction, CarryType::None));
}

// Śmierć jednostki: zwolnienie zadań, usunięcie encji, statystyki strat i zabitych.
void World::killUnit(int id, int killerFaction) {
    Unit& u = units[id];
    if (!u.alive) return;
    if (u.type == UnitType::Settler) becomeIdleSettler(u);
    emitSound("death", u.pos, 0.7f);
    u.alive = false;
    u.selected = false;
    destroyEntity(u.entity);
    destroyEntity(u.ring);
    freeUnits_.release(id, gameTime);
    factions[u.faction].unitsLost++;
    if (killerFaction >= 0 && killerFaction != u.faction) factions[killerFaction].enemiesKilled++;
}

// Zniszczenie budynku: pracownik wraca do Idle, obrys przestaje blokować nawigację,
// zostaje gruzowisko. Utrata zamku oznacza przegraną frakcji.
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
    freeBuildings_.release(id, gameTime);
    if (def.territoryRadius > 0.0f) markTerritoryChanged();

    // Zapasy magazynu przepadają. Flaga zostaje, jeśli prowadzą do niej drogi (to już zwykła flaga).
    b.stock = {};
    b.incoming = 0;
    b.delivered = {};
    if (b.flag >= 0 && b.flag < static_cast<int>(flags.size()) && flags[b.flag].alive) {
        flags[b.flag].building = -1;
        if (flags[b.flag].roads.empty()) removeFlag(b.flag);
    }
    b.flag = -1;
    routingDirty_ = true;
    refreshFactionResources();

    if (b.faction == kPlayer) {
        pushMessage(std::string("Utracono budynek: ") + def.name, {1.0f, 0.5f, 0.4f, 1.0f});
    } else if (killerFaction == kPlayer) {
        pushMessage(std::string("Zniszczono wrogi budynek: ") + def.name, {0.6f, 1.0f, 0.6f, 1.0f});
    }

    if (b.type == BuildingType::Castle) {
        factions[b.faction].defeated = true;
    }
}

// Zburzenie budynku przez gracza - zwrot połowy kosztów.
void World::demolish(int id) {
    if (id < 0 || id >= static_cast<int>(buildings.size())) return;
    Building& b = buildings[id];
    if (!b.alive || b.type == BuildingType::Castle) return;
    const Resources& c = buildingDef(b.type).cost;
    Resources refund;
    for (int t = 1; t <= kGoodTypeCount; ++t) refund.v[t] = c.v[t] / 2;
    deposit(b.faction, refund);
    destroyBuilding(id, -1);
}

// Zielony pierścień w miejscu rozkazu - kurczy się i znika (updateEffects).
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

// Zaznaczenie jednostki (widoczny pierścień pod stopami).
void World::setSelected(int id, bool selected) {
    if (id < 0 || id >= static_cast<int>(units.size())) return;
    units[id].selected = selected && units[id].alive;
}

// Zaznaczenie budynku (widoczny pierścień wokół).
void World::setBuildingSelected(int id, bool selected) {
    if (id < 0 || id >= static_cast<int>(buildings.size())) return;
    buildings[id].selected = selected && buildings[id].alive;
}

} // namespace settlers
