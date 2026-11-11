/**
 * world_roads.cpp - drogi, flagi, tragarze i magazyny (transport towarów).
 *
 * JAK PŁYNĄ TOWARY
 * ----------------
 *   1. Pracownik warsztatu (drwal, kamieniarz, farma, kopalnia) odkłada towary na FLADZE przed
 *      swoim budynkiem. Flaga mieści kFlagCapacity towarów - gdy jest pełna, warsztat stoi.
 *   2. DROGI łączą flagi budynków w sieć. Po drodze każdy idzie szybciej (kRoadSpeedBonus),
 *      a tragarze chodzą wyłącznie po drogach - budynek bez drogi do magazynu nie jest obsługiwany.
 *   3. TRAGARZE to wspólna pula osadników (liczbę ustawia gracz w oknie gospodarki albo tryb Auto).
 *      Wolny tragarz czeka przy magazynie i dostaje ZLECENIE:
 *        - odbiór: idzie po drogach do flagi warsztatu, zabiera do kCarrierLoad towarów i niesie
 *          je CAŁĄ TRASĄ do najbliższego magazynu, który ma wolne miejsce,
 *        - dostawa: zabiera z magazynu materiały i niesie je na plac budowy albo do koszar.
 *      Do jednego magazynu może iść naraz wielu tragarzy - przepustowość zależy tylko od ich liczby.
 *   4. Towar w magazynie (zamek, Magazyn) można wydać: FactionState::res to suma zapasów magazynów.
 *      Wydatek na budowę/szkolenie zdejmuje towar z magazynu od razu (zlecenie "opłacone"),
 *      ale budowa/szkolenie postępuje dopiero, gdy tragarz go dowiezie.
 */
#include <queue>

#include "world_internal.h"

#include <unordered_map>

namespace settlers {

namespace {

// Długość łamanej w poziomie.
float polylineLength(const std::vector<Vec3>& pts) {
    float len = 0.0f;
    for (size_t i = 1; i < pts.size(); ++i) len += distanceXZ(pts[i - 1], pts[i]);
    return len;
}

// Zagęszcza łamaną: punkty co najwyżej co step jednostek (do znaczenia komórek drogi i podziału flagami).
std::vector<Vec3> densify(const std::vector<Vec3>& pts, float step) {
    std::vector<Vec3> out;
    if (pts.empty()) return out;
    out.push_back(pts[0]);
    for (size_t i = 1; i < pts.size(); ++i) {
        const Vec3 a = pts[i - 1], b = pts[i];
        const float d = distanceXZ(a, b);
        const int n = std::max(1, static_cast<int>(std::ceil(d / step)));
        for (int k = 1; k <= n; ++k) {
            const float t = static_cast<float>(k) / static_cast<float>(n);
            out.push_back({a.x + (b.x - a.x) * t, 0.0f, a.z + (b.z - a.z) * t});
        }
    }
    return out;
}

// Indeks punktu łamanej najbliższego p.
size_t nearestIndex(const std::vector<Vec3>& pts, const Vec3& p) {
    size_t best = 0;
    float bestD = std::numeric_limits<float>::max();
    for (size_t i = 0; i < pts.size(); ++i) {
        const float d = distanceXZ(pts[i], p);
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

} // namespace

// =====================================================================================
// Magazyny i wydatki
// =====================================================================================

// Surowce frakcji = suma zapasów ukończonych magazynów (zamek, Magazyn).
void World::refreshFactionResources() {
    for (auto& fs : factions) fs.res = {};
    for (const auto& b : buildings) {
        if (!b.alive || !b.constructed || storageCapacity(b.type) <= 0) continue;
        factions[b.faction].res.add(b.stock);
    }
}

int World::storageCapacityOf(int f) const {
    int cap = 0;
    for (const auto& b : buildings) {
        if (b.alive && b.constructed && b.faction == f) cap += storageCapacity(b.type);
    }
    return cap;
}

int World::storageUsedOf(int f) const {
    int used = 0;
    for (const auto& b : buildings) {
        if (b.alive && b.constructed && b.faction == f && storageCapacity(b.type) > 0) used += b.stock.total();
    }
    return used;
}

// Płatność: najpierw z zamku, potem z kolejnych magazynów. Zapas, który się zmniejszył, zwalnia
// miejsce w magazynie - routing przeliczamy, bo pełny magazyn mógł znów zacząć przyjmować towary.
bool World::spend(int f, const Resources& cost) {
    refreshFactionResources();
    if (!factions[f].res.canAfford(cost)) return false;
    Resources left = cost;
    auto takeFrom = [&](Building& b) {
        for (int t = 1; t <= kGoodTypeCount; ++t) {
            const auto type = static_cast<CarryType>(t);
            const int n = std::min(b.stock[type], left[type]);
            b.stock[type] -= n;
            left[type] -= n;
        }
        // Dowolna żywność: z tego rodzaju, którego magazyn ma najwięcej.
        while (left[CarryType::AnyFood] > 0 && b.stock.food() > 0) {
            CarryType best = CarryType::Bread;
            for (CarryType fd : kFoods) {
                if (b.stock[fd] > b.stock[best]) best = fd;
            }
            b.stock[best] -= 1;
            left[CarryType::AnyFood] -= 1;
        }
    };
    const int castle = factions[f].castle;
    if (castle >= 0 && buildings[castle].alive) takeFrom(buildings[castle]);
    for (auto& b : buildings) {
        if (left.total() <= 0) break;
        if (b.alive && b.constructed && b.faction == f && storageCapacity(b.type) > 0) takeFrom(b);
    }
    refreshFactionResources();
    routingDirty_ = true;
    return true;
}

// Dodaje surowce do magazynów z wolnym miejscem (zamek pierwszy). Nadmiar przepada.
int World::deposit(int f, const Resources& r) {
    int stored = 0;
    auto putInto = [&](Building& b, CarryType type, int& amount) {
        const int room = storageCapacity(b.type) - b.stock.total();
        const int n = std::clamp(room, 0, amount);
        resourceRef(b.stock, type) += n;
        amount -= n;
        stored += n;
    };
    for (int t = 1; t <= kGoodTypeCount; ++t) {
        const auto type = static_cast<CarryType>(t);
        int amount = resourceOf(r, type);
        const int castle = factions[f].castle;
        if (castle >= 0 && buildings[castle].alive) putInto(buildings[castle], type, amount);
        for (auto& b : buildings) {
            if (amount <= 0) break;
            if (b.alive && b.constructed && b.faction == f && storageCapacity(b.type) > 0) putInto(b, type, amount);
        }
    }
    refreshFactionResources();
    return stored;
}

void World::debugSetStock(int f, const Resources& r) {
    const int castle = factions[f].castle;
    if (castle >= 0) buildings[castle].stock = r;
    refreshFactionResources();
    routingDirty_ = true;
}

// =====================================================================================
// Flagi
// =====================================================================================

int World::createFlag(int f, const Vec3& p, int building) {
    Flag fl;
    fl.alive = true;
    fl.faction = f;
    fl.pos = {p.x, heightAt(p.x, p.z), p.z};
    fl.building = building;
    fl.entity = makeEntity("flag", flagMeshName(f));
    fl.entity->transform().position = fl.pos;
    const int id = freeFlags_.allocate(flags, gameTime);
    fl.gen = flags[id].gen;
    flags[id] = std::move(fl);
    transport_.roadGraphChanged();
    routingDirty_ = true;
    return id;
}

int World::flagNear(int f, const Vec3& p, float radius) const {
    int best = -1;
    float bestD = radius;
    for (size_t i = 0; i < flags.size(); ++i) {
        const Flag& fl = flags[i];
        if (!fl.alive || fl.faction != f) continue;
        const float d = distanceXZ(fl.pos, p);
        if (d <= bestD) {
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

bool World::canPlaceFlag(int f, const Vec3& p, std::string* reason) const {
    auto fail = [&](const char* r) {
        if (reason) *reason = r;
        return false;
    };
    if (!nav_.isWalkableWorld(p.x, p.z)) return fail("Tu nie można postawić flagi");
    if (heightAt(p.x, p.z) < kWaterLevel + 0.5f) return fail("Flaga w wodzie");
    if (!isInTerritory(f, p)) return fail("Poza terytorium");
    for (const auto& fl : flags) {
        if (fl.alive && distanceXZ(fl.pos, p) < kFlagMinSpacing) return fail("Za blisko innej flagi");
    }
    return true;
}

bool World::flagConnected(int fl) const {
    return fl >= 0 && fl < static_cast<int>(flags.size()) && flags[fl].alive && flags[fl].connected;
}

bool World::buildingLinked(const Building& b) const {
    if (b.flag < 0 || b.flag >= static_cast<int>(flags.size()) || !flags[b.flag].alive) return false;
    // Magazyn sam jest magazynem - liczy się, czy drogi łączą go z resztą sieci (z zamkiem).
    return storageCapacity(b.type) > 0 ? flags[b.flag].castleLinked : flags[b.flag].connected;
}

void World::setFlagSelected(int fl, bool selected) {
    if (fl >= 0 && fl < static_cast<int>(flags.size())) flags[fl].selected = selected;
}

// Usuwa flagę z drogami. Flaga przed budynkiem zostaje (budynek musi mieć gdzie odkładać towary).
void World::removeFlag(int id) {
    if (id < 0 || id >= static_cast<int>(flags.size()) || !flags[id].alive) return;
    const std::vector<int> attached = flags[id].roads;
    for (int r : attached) removeRoad(r);
    Flag& fl = flags[id];
    if (fl.building >= 0 && buildings[fl.building].alive) return;
    fl.alive = false;
    fl.selected = false;
    fl.goods.clear();
    destroyEntity(fl.entity);
    for (auto*& e : fl.goodEntities) destroyEntity(e);
    freeFlags_.release(id, gameTime);
    transport_.roadGraphChanged();   // graf dróg bez tej flagi (removeRoad też to woła, ale flaga mogła nie mieć dróg)
    routingDirty_ = true;
}

// =====================================================================================
// Drogi
// =====================================================================================

int World::createRoad(int f, int a, int b, const std::vector<Vec3>& points) {
    Road r;
    r.alive = true;
    r.faction = f;
    r.flagA = a;
    r.flagB = b;
    r.points = points;
    r.points.front() = {flags[a].pos.x, 0.0f, flags[a].pos.z};
    r.points.back() = {flags[b].pos.x, 0.0f, flags[b].pos.z};
    r.length = polylineLength(r.points);
    const int id = freeRoads_.allocate(roads, gameTime);
    r.gen = roads[id].gen;
    roads[id] = std::move(r);
    flags[a].roads.push_back(id);
    flags[b].roads.push_back(id);
    buildRoadMesh(id);
    transport_.roadGraphChanged();
    markRoadCellsDirty();
    routingDirty_ = true;
    return id;
}

void World::removeRoad(int id) {
    if (id < 0 || id >= static_cast<int>(roads.size()) || !roads[id].alive) return;
    Road& r = roads[id];
    r.alive = false;
    for (int f : {r.flagA, r.flagB}) {
        auto& list = flags[f].roads;
        list.erase(std::remove(list.begin(), list.end(), id), list.end());
    }
    destroyEntity(r.entity);
    freeRoads_.release(id, gameTime);
    engine_.getMeshManager()->removeMesh(roadMeshName(id));   // siatka drogi zwolniona (inaczej zostaje w VRAM)
    transport_.roadGraphChanged();
    markRoadCellsDirty();
    routingDirty_ = true;
}

// Dzieli odcinek w punkcie points[index]. Pierwsza połowa zostaje pod starym indeksem,
// druga staje się nowym odcinkiem. Zwraca nową flagę.
int World::splitRoad(int id, size_t index) {
    const std::vector<Vec3> pts = roads[id].points;
    const int f = roads[id].faction;
    const int oldB = roads[id].flagB;
    const int nf = createFlag(f, pts[index]);

    roads[id].points.assign(pts.begin(), pts.begin() + static_cast<long>(index) + 1);
    roads[id].flagB = nf;
    roads[id].points.back() = {flags[nf].pos.x, 0.0f, flags[nf].pos.z};
    roads[id].length = polylineLength(roads[id].points);
    auto& listB = flags[oldB].roads;
    listB.erase(std::remove(listB.begin(), listB.end(), id), listB.end());
    flags[nf].roads.push_back(id);
    buildRoadMesh(id);

    std::vector<Vec3> second(pts.begin() + static_cast<long>(index), pts.end());
    createRoad(f, nf, oldB, second);

    return nf;
}

// Ubity trakt: dwa pasy (ciemniejsze pobocze i jaśniejszy środek) układane na terenie odcinek po odcinku.
void World::buildRoadMesh(int id) {
    Road& r = roads[id];
    MeshBuilder mb;
    const Vec3 down{0.0f, -1.0f, 0.0f};
    auto strip = [&](float halfWidth, float lift, const Vec3& color) {
        for (size_t i = 1; i < r.points.size(); ++i) {
            const Vec3 a = r.points[i - 1], b = r.points[i];
            Vec3 dir{b.x - a.x, 0.0f, b.z - a.z};
            const float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
            if (len < 0.01f) continue;
            // Odcinek lekko wydłużony, żeby nie było szczelin na zakrętach.
            dir = {dir.x / len, 0.0f, dir.z / len};
            const Vec3 a2{a.x - dir.x * halfWidth * 0.5f, 0.0f, a.z - dir.z * halfWidth * 0.5f};
            const Vec3 b2{b.x + dir.x * halfWidth * 0.5f, 0.0f, b.z + dir.z * halfWidth * 0.5f};
            const Vec3 side{-dir.z * halfWidth, 0.0f, dir.x * halfWidth};
            auto ground = [&](float x, float z) { return Vec3{x, heightAt(x, z) + lift, z}; };
            const Vec3 p0 = ground(a2.x + side.x, a2.z + side.z);
            const Vec3 p1 = ground(b2.x + side.x, b2.z + side.z);
            const Vec3 p2 = ground(b2.x - side.x, b2.z - side.z);
            const Vec3 p3 = ground(a2.x - side.x, a2.z - side.z);
            const Vec3 mid = multiply(add(add(p0, p1), add(p2, p3)), 0.25f);
            mb.polygon({p0, p1, p2, p3}, add(mid, down), color);
        }
    };
    strip(1.9f, 0.25f, {0.43f, 0.35f, 0.24f});
    strip(1.2f, 0.32f, {0.6f, 0.5f, 0.35f});

    // Stała nazwa na odcinek drogi: przebudowa (np. podział drogi) nadpisuje tę samą siatkę - MeshManager
    // bezpiecznie odkłada stare bufory - a removeRoad ją zwalnia.
    const std::string name = roadMeshName(id);
    engine_.getMeshManager()->loadMesh(name, mb.build());
    if (!r.entity) {
        r.entity = makeEntity("road", name);
        r.entity->getComponent<MeshComponent>()->castShadows = false;
    } else {
        r.entity->getComponent<MeshComponent>()->meshName = name;
    }
}

void World::rebuildRoadCells() const {
    roadCellsDirty_ = false;
    roadCells_.assign(static_cast<size_t>(nav_.width()) * nav_.height(), 0);
    for (size_t i = 0; i < roads.size(); ++i) {
        if (!roads[i].alive) continue;
        for (const auto& p : roads[i].points) {
            int cx, cz;
            if (nav_.worldToCell(p.x, p.z, cx, cz)) roadCells_[static_cast<size_t>(cz) * nav_.width() + cx] = static_cast<int>(i) + 1;
        }
    }
}

int World::roadAtCell(float x, float z) const {
    int cx, cz;
    if (roadCellsDirty_) rebuildRoadCells();
    if (roadCells_.empty() || !nav_.worldToCell(x, z, cx, cz)) return -1;
    return roadCells_[static_cast<size_t>(cz) * nav_.width() + cx] - 1;
}

// Trasa nowej drogi: A* po siatce nawigacji, cała na własnym terytorium i na lądzie.
bool World::planRoad(int f, int fromFlag, const Vec3& toIn, std::vector<Vec3>& out, std::string* reason) const {
    auto fail = [&](const char* r) {
        if (reason) *reason = r;
        return false;
    };
    if (fromFlag < 0 || fromFlag >= static_cast<int>(flags.size()) || !flags[fromFlag].alive) return fail("Brak flagi początkowej");
    const Vec3 from = flags[fromFlag].pos;
    Vec3 to = toIn;
    const int endFlag = flagNear(f, to, 5.0f);
    if (endFlag == fromFlag) return fail("Wskaż miejsce dalej od flagi początkowej");
    if (endFlag >= 0) {
        to = flags[endFlag].pos;
        for (int r : flags[fromFlag].roads) {
            if (otherEnd(roads[r], fromFlag) == endFlag) return fail("Te flagi są już połączone");
        }
    } else if (!canPlaceFlag(f, to, reason)) {
        return false;
    }
    if (distanceXZ(from, to) < 4.0f) return fail("Za krótka droga");

    std::vector<Vec3> path;
    if (nav_.lineWalkable(from, to)) {
        path.push_back(to);
    } else if (!nav_.findPath(from, to, path, false, 40000) || path.empty()) {
        return fail("Brak przejścia");
    }
    path.insert(path.begin(), {from.x, 0.0f, from.z});
    path.back() = {to.x, 0.0f, to.z};
    out = densify(path, 2.0f);
    for (const auto& p : out) {
        if (!isInTerritory(f, p)) return fail("Droga wychodzi poza terytorium");
        if (heightAt(p.x, p.z) < kWaterLevel + 0.3f) return fail("Droga przez wodę");
    }
    return true;
}

// Buduje drogę po trasie z planRoad. Idąc wzdłuż trasy dzieli ją na odcinki:
//  - na istniejącej fladze, którą trasa mija,
//  - na przecięciu z inną drogą (powstaje tam flaga - skrzyżowanie).
int World::buildRoad(int f, int fromFlag, const Vec3& to, std::string* reason) {
    std::vector<Vec3> pts;
    if (!planRoad(f, fromFlag, to, pts, reason)) return -1;

    const int roadsBefore = static_cast<int>(roads.size());
    int segStart = fromFlag;
    std::vector<Vec3> seg{pts[0]};
    float segLen = 0.0f;
    auto finish = [&](int endFlag) {
        seg.back() = {flags[endFlag].pos.x, 0.0f, flags[endFlag].pos.z};
        bool duplicate = endFlag == segStart || seg.size() < 2;
        for (int r : flags[segStart].roads) {
            if (otherEnd(roads[r], segStart) == endFlag) duplicate = true;
        }
        if (!duplicate) createRoad(f, segStart, endFlag, seg);
        segStart = endFlag;
        seg = {{flags[endFlag].pos.x, 0.0f, flags[endFlag].pos.z}};
        segLen = 0.0f;
    };

    for (size_t i = 1; i + 1 < pts.size(); ++i) {
        const Vec3 p = pts[i];
        segLen += distanceXZ(seg.back(), p);
        seg.push_back(p);

        // 1. Trasa mija istniejącą flagę.
        const int near = flagNear(f, p, 2.0f);
        if (near >= 0 && near != segStart) {
            finish(near);
            continue;
        }
        // 2. Przecięcie z drogą zbudowaną wcześniej (nie z odcinkami tej samej budowy).
        const int cross = roadAtCell(p.x, p.z);
        if (cross >= 0 && cross < roadsBefore && roads[cross].alive && roads[cross].faction == f && segLen > 3.0f &&
            roads[cross].flagA != segStart && roads[cross].flagB != segStart) {
            const Road& cr = roads[cross];
            const size_t k = nearestIndex(cr.points, p);
            int at;
            if (distanceXZ(cr.points[k], flags[cr.flagA].pos) < kFlagMinSpacing) {
                at = cr.flagA;   // przecięcie tuż przy fladze - łączymy się z nią
            } else if (distanceXZ(cr.points[k], flags[cr.flagB].pos) < kFlagMinSpacing) {
                at = cr.flagB;
            } else {
                at = splitRoad(cross, k);
            }
            if (at != segStart) finish(at);
            continue;
        }
    }

    seg.push_back(pts.back());
    int endFlag = flagNear(f, pts.back(), 5.0f);
    if (endFlag < 0) endFlag = createFlag(f, pts.back());
    if (endFlag != segStart) finish(endFlag);
    updateRouting();
    return endFlag;
}

// Łączy flagę z siecią: próbuje zbudować drogę do kilku najbliższych flag, które mają już
// połączenie z magazynem (albo same są flagą magazynu).
bool World::autoConnectFlag(int id, std::string* reason) {
    if (id < 0 || id >= static_cast<int>(flags.size()) || !flags[id].alive) return false;
    updateRouting();
    // Flaga magazynu: celem jest połączenie z zamkiem; flaga warsztatu: z dowolnym magazynem.
    const bool storageFlag = flags[id].building >= 0 && storageCapacity(buildings[flags[id].building].type) > 0;
    auto linked = [&](int fl) { return storageFlag ? flags[fl].castleLinked : flags[fl].connected; };
    if (linked(id)) return true;
    const int f = flags[id].faction;
    std::vector<int> candidates;
    for (size_t i = 0; i < flags.size(); ++i) {
        if (static_cast<int>(i) == id || !flags[i].alive || flags[i].faction != f || !linked(static_cast<int>(i))) continue;
        if (distanceXZ(flags[i].pos, flags[id].pos) < 220.0f) candidates.push_back(static_cast<int>(i));
    }
    std::sort(candidates.begin(), candidates.end(), [&](int a, int b) {
        return distanceXZ(flags[a].pos, flags[id].pos) < distanceXZ(flags[b].pos, flags[id].pos);
    });
    std::string why = "Brak połączonej z magazynem flagi w pobliżu";
    for (size_t i = 0; i < candidates.size() && i < 6; ++i) {
        if (buildRoad(f, id, flags[candidates[i]].pos, &why) >= 0) {
            updateRouting();
            if (linked(id)) return true;
        }
    }
    if (reason) *reason = why;
    return false;
}

// =====================================================================================
// Sieć dróg
// =====================================================================================

// Spójność sieci (Dijkstra od flag magazynów):
//  0. "castleLinked": czy z flagi da się dojść drogami do zamku,
//  1. "connected": czy z flagi da się dojść do jakiegokolwiek magazynu.
void World::updateRouting() {
    routingDirty_ = false;
    for (auto& fl : flags) {
        fl.connected = false;
        fl.castleLinked = false;
    }
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<int> stack;
        std::vector<char> seen(flags.size(), 0);
        for (size_t i = 0; i < flags.size(); ++i) {
            const Flag& fl = flags[i];
            if (!fl.alive || fl.building < 0) continue;
            const Building& b = buildings[fl.building];
            if (!b.alive || !b.constructed || storageCapacity(b.type) <= 0) continue;
            if (pass == 0 && b.type != BuildingType::Castle) continue;
            seen[i] = 1;
            stack.push_back(static_cast<int>(i));
        }
        while (!stack.empty()) {
            const int cur = stack.back();
            stack.pop_back();
            (pass == 0 ? flags[cur].castleLinked : flags[cur].connected) = true;
            for (int r : flags[cur].roads) {
                if (!roads[r].alive) continue;
                const int next = otherEnd(roads[r], cur);
                if (!seen[next]) {
                    seen[next] = 1;
                    stack.push_back(next);
                }
            }
        }
    }
}

// =====================================================================================
// Zlecenia dostaw (magazyn -> plac budowy / koszary)
// =====================================================================================

// Zamawia materiały: każda sztuka (po kCarrierLoad tego samego rodzaju) to osobne zlecenie,
// od razu opłacone z najbliższego magazynu, który ma towar.
void World::orderMaterials(int building, const Resources& r) {
    const int f = buildings[building].faction;
    for (int t = 1; t < kResourceSlots; ++t) {   // także AnyFood - konkretny rodzaj wybiera fundOrder
        const auto type = static_cast<CarryType>(t);
        int left = resourceOf(r, type);
        while (left > 0) {
            TransportOrder o;
            o.alive = true;
            o.faction = f;
            o.type = type;
            o.amount = std::min(left, kCarrierLoad);
            o.dest = building;
            left -= o.amount;
            transport_.fundOrder(o);
            // Slot po wykonanym zleceniu (Ref<TransportOrder> tragarza rozpozna, że to już inne zlecenie) albo nowy.
            const int id = freeOrders_.allocate(orders, gameTime);
            o.gen = orders[id].gen;
            orders[id] = o;
        }
    }
    refreshFactionResources();
}

// Warsztat z towarami wejściowymi (kopalnia - żywność, tartak - kłody, huta - ruda i węgiel...) zamawia je
// z magazynów, gdy ma ich (razem z zamówionymi w drodze) mniej niż kInputBuffer. Tragarze dowożą zamówienia.
void World::requestInputs(int id) {
    Building& b = buildings[id];
    if (!b.alive || !b.constructed || !flagConnected(b.flag)) return;
    for (CarryType in : productionDef(b.type).inputs) {
        if (in == CarryType::None) continue;
        int have = in == CarryType::AnyFood ? b.delivered.food() : b.delivered[in];
        for (const auto& o : orders) {
            if (!o.alive || o.dest != id) continue;
            const bool match = in == CarryType::AnyFood ? (o.type == CarryType::AnyFood || isFood(o.type)) : o.type == in;
            if (match) have += o.amount;
        }
        if (have < kInputBuffer) orderMaterials(id, Resources{{in, kInputBuffer - have}});
    }
}

int World::flagAvailableGoods(const Flag& fl) const {
    int reserved = 0;
    for (int r : fl.reserved) reserved += r;
    return static_cast<int>(fl.goods.size()) - reserved;
}

Resources World::spendable(int f) const {
    Resources r = factions[f].res;
    for (const auto& fl : flags) {
        if (!fl.alive || fl.faction != f || !fl.connected) continue;
        for (int t = 1; t <= kGoodTypeCount; ++t) {
            const int n = static_cast<int>(std::count(fl.goods.begin(), fl.goods.end(), static_cast<CarryType>(t))) - fl.reserved[t];
            r.v[t] += std::max(0, n);
        }
    }
    return r;
}

Resources World::storeBound(int f) const {
    Resources r = factions[f].res;
    for (const auto& u : units) {
        if (u.alive && u.faction == f && isCarrierState(u.state) && u.jobFlag >= 0 && u.carry != CarryType::None) {
            resourceRef(r, u.carry) += u.carryCount;
        }
    }
    return r;
}

int World::pickupableGoods(const Flag& fl, const Resources& bound) const {
    int n = 0;
    for (int t = 1; t <= kGoodTypeCount; ++t) {
        if (bound.v[t] >= factions[fl.faction].stockLimit[t]) continue;
        n += std::max(0, static_cast<int>(std::count(fl.goods.begin(), fl.goods.end(), static_cast<CarryType>(t))) - fl.reserved[t]);
    }
    return n;
}

// Przyczyna czekania towarów: brak drogi, wszystkie magazyny pełne, limit zapasu, albo po prostu brak tragarzy.
World::FlagBlock World::flagBlockReason(int flag) const {
    if (flag < 0 || !flags[flag].alive) return FlagBlock::None;
    const Flag& fl = flags[flag];
    if (!fl.connected) return FlagBlock::NoRoad;
    if (flagAvailableGoods(fl) <= 0) return FlagBlock::None;
    if (pickupableGoods(fl, storeBound(fl.faction)) <= 0) return FlagBlock::StockLimit;
    for (const auto& b : buildings) {
        if (b.alive && b.constructed && b.faction == fl.faction && storageCapacity(b.type) > 0 &&
            b.stock.total() + b.incoming < storageCapacity(b.type)) {
            return FlagBlock::Carriers;
        }
    }
    return FlagBlock::StorageFull;
}

// =====================================================================================
// Ruch towarów
// =====================================================================================

// Co klatkę: towary na flagach magazynów trafiają do środka (poza zarezerwowanymi przez tragarzy).
// Co 0,5 s: zlecenia odbiorców, którzy zniknęli, są anulowane (towar wraca do magazynu),
// nieopłacone zlecenia dostają towar, a wolni tragarze - zlecenia.
void World::updateFlags(float dt) {
    if (routingDirty_) updateRouting();
    bool changed = false;
    for (auto& fl : flags) {
        if (!fl.alive || fl.building < 0 || fl.goods.empty()) continue;
        Building& b = buildings[fl.building];
        if (!b.alive || !b.constructed) continue;
        const int cap = storageCapacity(b.type);
        if (cap <= 0) continue;
        for (int t = 1; t <= kGoodTypeCount; ++t) {
            const auto type = static_cast<CarryType>(t);
            int free = static_cast<int>(std::count(fl.goods.begin(), fl.goods.end(), type)) - fl.reserved[t];
            for (auto it = fl.goods.begin(); it != fl.goods.end() && free > 0 && b.stock.total() < cap;) {
                if (*it == type) {
                    resourceRef(b.stock, type) += 1;
                    it = fl.goods.erase(it);
                    --free;
                    changed = true;
                } else {
                    ++it;
                }
            }
        }
    }
    if (changed) refreshFactionResources();

    transportTimer_ -= dt;
    if (transportTimer_ > 0.0f) return;
    transportTimer_ = 0.5f;
    for (auto& o : orders) {
        if (!o.alive) continue;
        const bool destOk = o.dest >= 0 && buildings[o.dest].alive;
        if (!destOk && !o.picked) {
            // Odbiorca zniknął - opłacony towar wraca do magazynu, z którego go zdjęto (albo zwalniamy rezerwację na fladze).
            transport_.unfundOrder(o);
            if (o.carrier >= 0 && units[o.carrier].alive && units[o.carrier].jobOrder >= 0 &&
                &orders[units[o.carrier].jobOrder] == &o) {
                units[o.carrier].jobOrder = -1;
            }
            releaseOrder(o);
            continue;
        }
        if (o.funded && !o.picked) {
            // Magazyn albo flaga zniszczone razem z towarem - trzeba opłacić od nowa.
            const bool storeOk = o.source >= 0 && buildings[o.source].alive;
            const bool flagOk = o.sourceFlag >= 0 && flags[o.sourceFlag].alive;
            if (!storeOk && !flagOk) {
                o.funded = false;
                o.source = -1;
                o.sourceFlag = -1;
            }
        }
        if (o.funded && !o.picked && o.carrier < 0 && o.dest >= 0) {
            // Źródło przestało być osiągalne (zburzona droga) - cofamy opłatę, zlecenie opłaci się z innego miejsca.
            const int dflag = buildings[o.dest].flag;
            const int src = o.source >= 0 ? buildings[o.source].flag : o.sourceFlag;
            if (src < 0 || dflag < 0 || transport_.networkFrom(dflag).dist[src] >= 1e8f) transport_.unfundOrder(o);
        }
        if (!o.funded && !o.picked) transport_.fundOrder(o);
    }
    refreshFactionResources();
    for (int f = 0; f < kFactionCount; ++f) transport_.assignTransportJobs(f);
}

// Pracownik odkłada wyrób (ProductionDef::outputCount sztuk) na fladze swojego warsztatu.
bool World::depositAtWorkshopFlag(Unit& u, Building& b) {
    if (u.carry == CarryType::None) return true;
    const bool hasFlag = b.flag >= 0 && b.flag < static_cast<int>(flags.size()) && flags[b.flag].alive;
    const int count = productionDef(b.type).outputCount;
    if (hasFlag && flags[b.flag].goods.size() + static_cast<size_t>(count) <= static_cast<size_t>(kFlagCapacity)) {
        for (int k = 0; k < count; ++k) flags[b.flag].goods.push_back(u.carry);
        b.outputBlocked = false;
        return true;
    }
    // Flaga pełna - produkcja stoi, dopóki tragarz nie odbierze towarów.
    if (!b.outputBlocked && b.faction == kPlayer && gameTime - b.outputMsgTime > 90.0f) {
        b.outputMsgTime = gameTime;
        const char* why = ": towary czekają na tragarzy - produkcja wstrzymana (dodaj tragarzy: I)";
        switch (hasFlag ? flagBlockReason(b.flag) : FlagBlock::NoRoad) {
        case FlagBlock::NoRoad: why = ": brak drogi do magazynu - produkcja wstrzymana (T - buduj drogę)"; break;
        case FlagBlock::StorageFull: why = ": magazyny pełne - produkcja wstrzymana (zbuduj Magazyn albo obniż limity: I)"; break;
        case FlagBlock::StockLimit: why = nullptr; break;   // limit ustawiony przez gracza - to nie problem
        default: break;
        }
        if (why) pushMessage(std::string(buildingDef(b.type).name) + why, {1.0f, 0.75f, 0.4f, 1.0f});
    }
    b.outputBlocked = true;
    return false;
}

// =====================================================================================
// Tragarze
// =====================================================================================

int World::carrierTargetOf(int f) const {
    const FactionState& fs = factions[f];
    if (!fs.carrierAuto) return fs.carrierTarget;
    // Auto: 2 + po jednym na podłączony warsztat + po jednym na 3 dostawy w kolejce
    //       + po jednym na 4 towary czekające na flagach warsztatów.
    //       (liczymy tylko towary, które da się zanieść - limit zapasu albo pełne magazyny nie wymagają tragarzy)
    int producers = 0, waiting = 0;
    const Resources bound = storeBound(f);
    for (const auto& b : buildings) {
        if (!b.alive || b.faction != f || !b.constructed || !producesGoods(b.type) || !buildingLinked(b)) continue;
        ++producers;
        waiting += pickupableGoods(flags[b.flag], bound);
    }
    int pending = 0;
    for (const auto& o : orders) {
        if (o.alive && o.faction == f && o.carrier < 0) ++pending;
    }
    return std::clamp(2 + producers + pending / 3 + waiting / 4, 2, 80);   // duże osady potrzebują wielu tragarzy
}

TransportStats World::transportStats(int f) const {
    TransportStats s;
    s.target = carrierTargetOf(f);
    for (const auto& u : units) {
        if (!u.alive || u.faction != f || !isCarrierState(u.state)) continue;
        ++s.carriers;
        if (u.jobFlag >= 0 || u.jobOrder >= 0) ++s.busy;
    }
    for (const auto& fl : flags) {
        if (fl.alive && fl.faction == f && fl.connected) {
            const Building* b = fl.building >= 0 ? &buildings[fl.building] : nullptr;
            const bool storageFlag = b && b->alive && b->constructed && storageCapacity(b->type) > 0;
            if (!storageFlag) s.goodsWaiting += flagAvailableGoods(fl);
        }
    }
    for (const auto& o : orders) {
        if (!o.alive || o.faction != f) continue;
        if (!o.funded) ++s.ordersUnfunded;
        else if (o.carrier < 0) ++s.ordersWaiting;
    }
    return s;
}

// Towary leżące przy flagach: do kFlagCapacity modeli na flagę, ułożonych w krąg wokół masztu.
void World::syncFlagVisuals() {
    // Mgła wojny: drogi i flagi wroga widać na odkrytym terenie.
    for (auto& r : roads) {
        if (r.alive && r.entity && !r.points.empty()) {
            r.entity->setActive(r.faction == kPlayer || exploredByPlayer(r.points[r.points.size() / 2]));
        }
    }
    for (auto& fl : flags) {
        if (!fl.alive) continue;
        const bool known = fl.faction == kPlayer || exploredByPlayer(fl.pos);
        if (fl.entity) fl.entity->setActive(known);
        for (int k = 0; k < kFlagCapacity; ++k) {
            Entity*& e = fl.goodEntities[k];
            const bool show = known && k < static_cast<int>(fl.goods.size());
            if (!show) {
                if (e) e->setActive(false);
                continue;
            }
            const std::string mesh = goodMeshName(fl.goods[k]);
            if (!e) e = makeEntity("good", mesh);
            auto* mc = e->getComponent<MeshComponent>();
            if (mc->meshName != mesh) mc->meshName = mesh;
            const float a = static_cast<float>(k) * (2.0f * PI / kFlagCapacity) + 0.4f;
            const float x = fl.pos.x + std::cos(a) * 2.0f, z = fl.pos.z + std::sin(a) * 2.0f;
            e->transform().position = {x, heightAt(x, z), z};
            e->transform().rotation.y = a;
            e->setActive(true);
        }
    }
}

// Flagi i drogi, które znalazły się na terenie innej frakcji, są niszczone.
void World::removeRoadsOnLostLand() {
    for (size_t i = 0; i < roads.size(); ++i) {
        const Road& r = roads[i];
        if (!r.alive) continue;
        for (size_t k = 0; k < r.points.size(); k += 3) {
            const int owner = territoryOwnerAt(r.points[k]);
            if (owner >= 0 && owner != r.faction) {
                removeRoad(static_cast<int>(i));
                break;
            }
        }
    }
    for (size_t i = 0; i < flags.size(); ++i) {
        const Flag& fl = flags[i];
        if (!fl.alive) continue;
        const int owner = territoryOwnerAt(fl.pos);
        if (owner >= 0 && owner != fl.faction) removeFlag(static_cast<int>(i));
    }
}

} // namespace settlers

namespace settlers {

void World::releaseOrder(TransportOrder& o) {
    if (!o.alive) return;
    o.alive = false;
    freeOrders_.release(static_cast<int>(&o - orders.data()), gameTime);
}

// Wolne sloty po wczytaniu zapisu: wszystkie martwe obiekty (czas śmierci "dawno" - bez kwarantanny).
void World::rebuildFreeLists() {
    auto fill = [](SlotFreeList& list, const auto& items, size_t first) {
        list.clear();
        for (size_t i = first; i < items.size(); ++i) {
            if (!items[i].alive) list.release(static_cast<int>(i), -1e9f);
        }
    };
    fill(freeUnits_, units, 0);
    fill(freeBuildings_, buildings, 0);
    fill(freeFlags_, flags, 0);
    fill(freeRoads_, roads, 0);
    fill(freeOrders_, orders, 0);
    fill(freeNodes_, nodes, nodesGenerated_);
}

} // namespace settlers
