/**
 * world_queries.cpp - pytania o stan świata (funkcje niczego nie zmieniają).
 *
 * Wskazywanie myszą (pick...), sprawdzanie, czy można postawić budynek (canPlace),
 * terytorium, wyszukiwanie najbliższego surowca / wolnego osadnika / wroga, opis jednostki dla debugu.
 */
#include "world_internal.h"

namespace settlers {

// Wyrównuje punkt do siatki nawigacji (4 jednostki), żeby obrys budynku pokrywał się z komórkami.
Vec3 World::snapToGrid(const Vec3& p) const {
    Vec3 r = p;
    r.x = std::round(p.x / kNavCell) * kNavCell;
    r.z = std::round(p.z / kNavCell) * kNavCell;
    r.y = heightAt(r.x, r.z);
    return r;
}

// Punkt terenu trafiony promieniem; pod wodą zwracamy punkt na powierzchni wody.
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

// Jednostka trafiona promieniem (test ze sferą wokół postaci); wybiera najbliższą kamerze.
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

// Budynek trafiony promieniem (przybliżenie budynku sferą).
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

// Właściciel terenu: spośród ukończonych zamków i wież, w których zasięgu leży punkt, wygrywa ten,
// w którego zasięgu punkt leży najgłębiej (największe "promień - odległość"). Granica dwóch frakcji
// wypada więc w środku pasa, gdzie ich zasięgi się nakładają - nie ma ziemi wspólnej ani niczyjej.
int World::territoryOwnerAt(const Vec3& p) const {
    int owner = -1;
    float bestDepth = 0.0f;
    for (const auto& b : buildings) {
        if (!b.alive || !b.constructed) continue;
        const float r = buildingDef(b.type).territoryRadius;
        if (r <= 0.0f) continue;
        const float depth = r - dist2D(p, b.pos);
        if (depth > bestDepth) {
            bestDepth = depth;
            owner = b.faction;
        }
    }
    return owner;
}

// Czy punkt leży na terytorium frakcji. Margines sprawdzamy próbkami na dwóch okręgach wokół punktu:
// margin > 0 - cały krąg musi należeć do frakcji, margin < 0 - wystarczy dowolny jego fragment.
bool World::isInTerritory(int faction, const Vec3& p, float margin) const {
    bool any = territoryOwnerAt(p) == faction;
    bool all = any;
    const float m = std::abs(margin);
    if (m > 0.0f) {
        for (int ring = 1; ring <= 2; ++ring) {
            const float rr = m * static_cast<float>(ring) * 0.5f;
            for (int k = 0; k < 8; ++k) {
                const float a = static_cast<float>(k) * PI * 0.25f + static_cast<float>(ring) * 0.39f;
                const bool o = territoryOwnerAt({p.x + std::cos(a) * rr, 0.0f, p.z + std::sin(a) * rr}) == faction;
                any = any || o;
                all = all && o;
            }
        }
    }
    return margin >= 0.0f ? all : any;
}

// Czy obrys budynku (z marginesem 2) leży na samych wolnych komórkach nawigacji.
bool World::buildingAreaFree(BuildingType type, const Vec3& pos) const {
    const float h = buildingDef(type).halfSize + 2.0f;
    return nav_.isAreaFree(pos.x - h, pos.z - h, pos.x + h, pos.z + h);
}

// Sprawdza po kolei wszystkie warunki postawienia budynku i zwraca pierwszy niespełniony:
// mapa, terytorium własne / wroga, woda, nierówność terenu, zajęte miejsce, wejścia innych budynków,
// drzewa w obrysie, zablokowane własne wejście, dojście z zamku.
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
    if (maxH > kSnowLine - 1.0f) return fail("Ośnieżony szczyt - tu nie da się budować");
    if (type == BuildingType::Mine) {
        // Kopalnia wchodzi w zbocze góry - musi stać w górach i może stać na bardziej nierównym terenie.
        if (heightAt(pos.x, pos.z) < kMountainLevel) return fail("Kopalnię stawia się w górach (szare skały)");
        if (maxH - minH > 11.0f) return fail("Zbocze zbyt strome");
    } else if (maxH - minH > 6.5f) {
        return fail("Teren zbyt nierówny");
    }
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

// Odległość od punktu do najbliższej ściany prostokątnego obrysu budynku.
float World::distanceToBuilding(const Vec3& p, const Building& b) const {
    const float h = buildingDef(b.type).halfSize;
    const float dx = std::max(0.0f, std::abs(p.x - b.pos.x) - h);
    const float dz = std::max(0.0f, std::abs(p.z - b.pos.z) - h);
    return std::sqrt(dx * dx + dz * dz);
}

// Najbliższy dostępny surowiec danego rodzaju: niewyczerpany, nie przewracający się,
// osiągalny i niezarezerwowany (drzewo - przez 1 drwala, skała - przez 2 kamieniarzy).
int World::findNearestNode(NodeKind kind, const Vec3& from, float radius) const {
    int best = -1;
    float bestD = radius;
    const int maxReserve = kind == NodeKind::Tree ? 1 : 2;
    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& n = nodes[i];
        if (!n.alive || n.kind != kind || n.amount <= 0 || n.fallTimer >= 0.0f || n.reserved >= maxReserve) continue;
        if (n.blockedUntil > gameTime || n.growth < 1.0f) continue;   // nieosiągalne lub sadzonka jeszcze rośnie
        const float d = dist2D(from, n.pos);
        if (d < bestD) {
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

// Liczba dostępnych surowców w zasięgu (do ostrzeżeń 'kończą się drzewa').
int World::countNodesInRange(NodeKind kind, const Vec3& from, float radius) const {
    int count = 0;
    for (const auto& n : nodes) {
        if (!n.alive || n.kind != kind || n.amount <= 0 || n.fallTimer >= 0.0f || n.blockedUntil > gameTime) continue;
        if (n.growth < 1.0f) continue;
        if (dist2D(from, n.pos) <= radius) ++count;
    }
    return count;
}

// Czy z punktu da się dojść do któregoś boku budynku - porównanie numerów obszarów nawigacji (bez A*).
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

// Rolnik pracujący na polu stoi wewnątrz obrysu swojej farmy - to jedyny dozwolony taki przypadek.
bool World::isWorkerOnFarm(const Unit& u, int id) const {
    if (u.building < 0) return false;
    const Building& b = buildings[u.building];
    return b.alive && b.type == BuildingType::Farm && b.worker == id;
}

// Budynek wymaga naprawy, gdy ma mniej niż 97% PŻ.
bool World::needsRepair(const Building& b) const {
    if (!b.alive || !b.constructed) return false;
    return b.hp < buildingDef(b.type).maxHp * 0.97f;
}

// Ilu osadników idzie naprawiać / naprawia budynek.
int World::repairersOf(int building) const {
    int n = 0;
    for (const auto& u : units) {
        if (u.alive && u.building == building && (u.state == UnitState::GoBuild || u.state == UnitState::Building)) ++n;
    }
    return n;
}

// Najbliższy bezczynny osadnik frakcji (-1 = brak).
// Liczba rosnących sadzonek w promieniu.
int World::countSaplingsInRange(const Vec3& from, float radius) const {
    int count = 0;
    for (const auto& n : nodes) {
        if (n.alive && n.kind == NodeKind::Tree && n.growth < 1.0f && dist2D(from, n.pos) <= radius) ++count;
    }
    return count;
}

// Leśniczy: losuje miejsce na sadzonkę w zasięgu chaty. Miejsce musi być przechodnie, poniżej granicy lasu,
// z dala od innych drzew (6 j.) i budynków (5 j.). Gdy las w okolicy jest już gęsty (45 drzew), nie sadzimy.
bool World::findPlantingSpot(const Building& forester, Vec3& out) {
    const float radius = buildingDef(BuildingType::Forester).workRadius;
    int treesAround = 0;
    for (const auto& n : nodes) {
        if (n.alive && n.kind == NodeKind::Tree && dist2D(forester.pos, n.pos) <= radius) ++treesAround;
    }
    if (treesAround >= 45) return false;

    for (int attempt = 0; attempt < 40; ++attempt) {
        const float angle = randf(0.0f, 2.0f * PI);
        const float r = randf(14.0f, radius);
        const Vec3 p{forester.pos.x + std::cos(angle) * r, 0.0f, forester.pos.z + std::sin(angle) * r};
        if (!nav_.isWalkableWorld(p.x, p.z)) continue;
        const float h = heightAt(p.x, p.z);
        if (h < kWaterLevel + 3.0f || h > kMountainLevel + 6.0f) continue;
        bool free = true;
        for (const auto& n : nodes) {
            if (n.alive && dist2D(p, n.pos) < 6.0f + n.radius) {
                free = false;
                break;
            }
        }
        for (const auto& b : buildings) {
            if (!free) break;
            if (b.alive && (distanceToBuilding(p, b) < 5.0f || dist2D(p, b.door()) < 6.0f)) free = false;
        }
        if (free) {
            out = p;
            return true;
        }
    }
    return false;
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

// Najbliższa wroga jednostka w promieniu; żołnierze mają pierwszeństwo przed osadnikami.
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

// Najbliższy wrogi budynek w promieniu.
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

// Losuje do 120 punktów w pierścieniu wokół center i zwraca pierwszy, w którym można budować.
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

// Nazwa stanu do debugowania i logów.
const char* World::stateName(UnitState s) {
    static const char* names[] = {"Idle", "Moving", "GoBuild", "Building", "GoToWork", "WorkWait",
                                  "GoGather", "Gathering", "ReturnHome", "Attack"};
    return names[static_cast<int>(s)];
}

// Stany, w których jednostka powinna się przemieszczać (do wykrywania zawieszeń).
bool World::isTravelState(UnitState s) {
    return s == UnitState::Moving || s == UnitState::GoBuild || s == UnitState::GoToWork ||
           s == UnitState::GoGather || s == UnitState::ReturnHome;
}

// Pełny opis jednostki do logów: stan, pozycja i cel (z informacją o komórkach nawigacji i obszarach),
// postęp ścieżki, czas stania, budynek i odległość do jego drzwi.
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

} // namespace settlers
