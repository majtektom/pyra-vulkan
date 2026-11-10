/**
 * world_save.cpp - zapis i wczytanie stanu gry (JSON).
 *
 * Co jest w zapisie:
 *   - ziarno mapy i poziom trudności - z nich odtwarzamy teren, nawigację, lasy i skały (createLand),
 *   - pełne listy budynków, jednostek, flag, dróg i zleceń transportu RAZEM z martwymi pozycjami: obiekty
 *     odwołują się do siebie indeksami (unit.building, flag.building, road.flagA, order.dest...), więc po
 *     wczytaniu każdy obiekt musi trafić pod ten sam indeks,
 *   - zmiany w surowcach mapy (ścięte drzewa, wydobyte skały, sadzonki posadzone przez leśniczych),
 *   - stan frakcji (limity zapasu, tragarze, liczniki, pamięć AI), czas gry, stan generatora losowego.
 * Czego nie ma (odtwarza się samo): modele 3D, pociski i efekty, terytorium, spójność sieci dróg, komunikaty.
 *
 * Jedna lista pól na strukturę (funkcje fields(...)) służy i do zapisu, i do odczytu: Archive to Writer
 * (pole -> JSON) albo Reader (JSON -> pole). Nowe pole w strukturze = jedna linia w odpowiednim fields(...).
 */
#include <deque>
#include <nlohmann/json.hpp>
#include <sstream>
#include <type_traits>

#include "world_internal.h"

namespace settlers {

using nlohmann::json;

namespace {

constexpr int kSaveVersion = 1;

// --- Kodowanie pojedynczych wartości ---------------------------------------------------

template <class T> struct IsVector : std::false_type {};
template <class T> struct IsVector<std::vector<T>> : std::true_type {};
template <class T> struct IsDeque : std::false_type {};
template <class T> struct IsDeque<std::deque<T>> : std::true_type {};
template <class T> struct IsArray : std::false_type {};
template <class T, size_t N> struct IsArray<std::array<T, N>> : std::true_type {};

template <class T> json enc(const T& v) {
    if constexpr (std::is_enum_v<T>) {
        return static_cast<int>(v);
    } else if constexpr (std::is_same_v<T, Vec3>) {
        return json::array({v.x, v.y, v.z});
    } else if constexpr (std::is_same_v<T, Resources>) {
        return enc(v.v);
    } else if constexpr (IsVector<T>::value || IsDeque<T>::value || IsArray<T>::value) {
        json a = json::array();
        for (const auto& e : v) a.push_back(enc(e));
        return a;
    } else {
        return v;   // bool, liczby, std::string
    }
}

template <class T> void dec(const json& j, T& v) {
    if constexpr (std::is_enum_v<T>) {
        v = static_cast<T>(j.get<int>());
    } else if constexpr (std::is_same_v<T, Vec3>) {
        v = {j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()};
    } else if constexpr (std::is_same_v<T, Resources>) {
        dec(j, v.v);
    } else if constexpr (IsVector<T>::value || IsDeque<T>::value) {
        v.clear();
        for (const auto& e : j) {
            typename T::value_type x{};
            dec(e, x);
            v.push_back(x);
        }
    } else if constexpr (IsArray<T>::value) {
        for (size_t i = 0; i < v.size() && i < j.size(); ++i) dec(j[i], v[i]);
    } else {
        v = j.get<T>();
    }
}

// Zapis: pole -> j[nazwa].
struct Writer {
    json& j;
    template <class T> void operator()(const char* name, const T& v) { j[name] = enc(v); }
};
// Odczyt: j[nazwa] -> pole (brak pola w pliku = zostaje wartość domyślna - starsze zapisy dalej działają).
struct Reader {
    const json& j;
    template <class T> void operator()(const char* name, T& v) {
        auto it = j.find(name);
        if (it != j.end() && !it->is_null()) dec(*it, v);
    }
};

// --- Listy pól (wspólne dla zapisu i odczytu) -------------------------------------------

template <class A> void fields(A& a, Unit& u) {
    a("alive", u.alive);           a("type", u.type);             a("faction", u.faction);
    a("pos", u.pos);               a("yaw", u.yaw);               a("hp", u.hp);
    a("state", u.state);           a("path", u.path);             a("pathIndex", u.pathIndex);
    a("goal", u.goal);             a("attackMove", u.attackMove); a("repathTimer", u.repathTimer);
    a("stuckTimer", u.stuckTimer); a("building", u.building);     a("resourceNode", u.resourceNode);
    a("carry", u.carry);           a("workSpot", u.workSpot);     a("atFlag", u.atFlag);
    a("jobFlag", u.jobFlag);       a("jobOrder", u.jobOrder);     a("jobDest", u.jobDest);
    a("carryCount", u.carryCount); a("targetUnit", u.targetUnit); a("targetBuilding", u.targetBuilding);
    a("attackCooldown", u.attackCooldown); a("guardPos", u.guardPos); a("homeTower", u.homeTower);
    a("timer", u.timer);           a("animTime", u.animTime);     a("lastDamagedTime", u.lastDamagedTime);
    a("meshName", u.meshName);     a("progressPos", u.progressPos); a("progressTimer", u.progressTimer);
    a("ghostTimer", u.ghostTimer); a("stallPos", u.stallPos);     a("stallTime", u.stallTime);
}

template <class A> void fields(A& a, Building& b) {
    a("alive", b.alive);           a("type", b.type);             a("faction", b.faction);
    a("pos", b.pos);               a("rotY", b.rotY);             a("hp", b.hp);
    a("constructed", b.constructed); a("progress", b.progress);   a("worker", b.worker);
    a("produced", b.produced);     a("lastDamagedTime", b.lastDamagedTime);
    a("placedTime", b.placedTime); a("constructedTime", b.constructedTime);
    a("trainQueue", b.trainQueue); a("trainTimer", b.trainTimer); a("trainPaid", b.trainPaid);
    a("autoTrain", b.autoTrain);   a("swordShare", b.swordShare); a("autoSwords", b.autoSwords);
    a("autoArchers", b.autoArchers); a("autoTrainTimer", b.autoTrainTimer);
    a("attackCooldown", b.attackCooldown); a("settlerTimer", b.settlerTimer);
    a("lacksResources", b.lacksResources); a("resourcesInRange", b.resourcesInRange);
    a("resourceCheckTimer", b.resourceCheckTimer);
    a("repairCredit", b.repairCredit); a("repairBlocked", b.repairBlocked);
    a("accessFailures", b.accessFailures); a("unreachable", b.unreachable); a("unreachableTime", b.unreachableTime);
    a("flag", b.flag);             a("stock", b.stock);           a("incoming", b.incoming);
    a("delivered", b.delivered);   a("outputBlocked", b.outputBlocked); a("inputStarved", b.inputStarved);
    a("inputRequestTimer", b.inputRequestTimer);
}

template <class A> void fields(A& a, Flag& f) {
    a("alive", f.alive);           a("faction", f.faction);       a("pos", f.pos);
    a("building", f.building);     a("goods", f.goods);           a("reserved", f.reserved);
    a("roads", f.roads);           a("connected", f.connected);   a("castleLinked", f.castleLinked);
}

template <class A> void fields(A& a, Road& r) {
    a("alive", r.alive);   a("faction", r.faction); a("flagA", r.flagA);
    a("flagB", r.flagB);   a("points", r.points);   a("length", r.length);
}

template <class A> void fields(A& a, TransportOrder& o) {
    a("alive", o.alive);   a("faction", o.faction); a("type", o.type);     a("amount", o.amount);
    a("source", o.source); a("sourceFlag", o.sourceFlag); a("dest", o.dest);
    a("carrier", o.carrier); a("funded", o.funded); a("picked", o.picked);
}

template <class A> void fields(A& a, FactionState& fs) {
    a("defeated", fs.defeated);    a("castle", fs.castle);        a("jobTimer", fs.jobTimer);
    a("garrisonTimer", fs.garrisonTimer); a("unitsLost", fs.unitsLost); a("enemiesKilled", fs.enemiesKilled);
    a("aiThinkTimer", fs.aiThinkTimer); a("aiPlacementFailures", fs.aiPlacementFailures);
    a("aiExpandFor", fs.aiExpandFor); a("waveTimer", fs.waveTimer); a("waveNumber", fs.waveNumber);
    a("soldiersTrained", fs.soldiersTrained); a("carrierTarget", fs.carrierTarget);
    a("carrierAuto", fs.carrierAuto); a("stockLimit", fs.stockLimit);
}

// Stan surowca na mapie. Dla surowców spoza generatora (sadzonki) zapisujemy też rodzaj, miejsce i model.
template <class A> void fields(A& a, ResourceNode& n) {
    a("alive", n.alive);   a("amount", n.amount);  a("reserved", n.reserved);
    a("fallTimer", n.fallTimer); a("blockedUntil", n.blockedUntil); a("growth", n.growth);
}
template <class A> void nodeIdentity(A& a, ResourceNode& n) {
    a("kind", n.kind);     a("pos", n.pos);        a("variant", n.variant); a("baseScale", n.baseScale);
}

// Lista obiektów -> tablica JSON (const_cast: Writer tylko czyta pola, a fields() przyjmuje T&).
template <class T> json writeList(const std::vector<T>& list) {
    json arr = json::array();
    for (const T& item : list) {
        json j = json::object();
        Writer w{j};
        fields(w, const_cast<T&>(item));
        arr.push_back(std::move(j));
    }
    return arr;
}

} // namespace

// =====================================================================================
// Zapis
// =====================================================================================

void World::saveState(json& out) const {
    out = json::object();
    out["version"] = kSaveVersion;
    out["seed"] = seed_;
    out["difficulty"] = static_cast<int>(difficulty);
    out["gameTime"] = gameTime;
    out["winner"] = winner;
    out["lastCombatTime"] = enc(lastCombatTime);
    std::ostringstream rng;
    rng << rng_;   // stan generatora losowego - dalsza gra jest taka sama jak bez zapisu
    out["rng"] = rng.str();

    out["buildings"] = writeList(buildings);
    out["units"] = writeList(units);
    out["flags"] = writeList(flags);
    out["roads"] = writeList(roads);
    out["orders"] = writeList(orders);
    out["factions"] = json::array();
    for (const auto& fs : factions) {
        json j = json::object();
        Writer w{j};
        fields(w, const_cast<FactionState&>(fs));
        out["factions"].push_back(std::move(j));
    }

    // Surowce: wygenerowane z ziarna - tylko stan; posadzone później - też rodzaj, miejsce i model.
    json nodesJ = json::array();
    for (size_t i = 0; i < nodes.size(); ++i) {
        json j = json::object();
        Writer w{j};
        auto& n = const_cast<ResourceNode&>(nodes[i]);
        fields(w, n);
        if (i >= nodesGenerated_) nodeIdentity(w, n);
        nodesJ.push_back(std::move(j));
    }
    out["nodes"] = std::move(nodesJ);
    out["fogOfWar"] = fogOfWar;
    out["fogExplored"] = fogExploredRle();
    out["nodesGenerated"] = nodesGenerated_;
}

// =====================================================================================
// Wczytanie
// =====================================================================================

bool World::loadState(engine3d::Scene* scene, const json& in, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    try {
        if (in.value("version", 0) != kSaveVersion) return fail("nieobsługiwana wersja zapisu");
        const auto seed = in.at("seed").get<uint32_t>();
        const auto diff = static_cast<Difficulty>(in.value("difficulty", 1));

        // 1. Ląd i surowce z ziarna (ten sam generator - te same lasy i skały w tej samej kolejności).
        createLand(scene, seed, diff);
        if (in.value("nodesGenerated", size_t{0}) != nodesGenerated_) {
            return fail("mapa z ziarna różni się od zapisanej (inna wersja generatora mapy)");
        }

        // 2. Surowce: stan wygenerowanych, odtworzenie sadzonek.
        const json& nodesJ = in.at("nodes");
        for (size_t i = 0; i < nodesJ.size(); ++i) {
            Reader r{nodesJ[i]};
            if (i < nodes.size()) {
                ResourceNode& n = nodes[i];
                fields(r, n);
                if (!n.alive) {   // ścięte / wydobyte - usuwamy model (i przeszkodę skały)
                    if (n.kind == NodeKind::Rock) {
                        const float rr = n.radius * 0.7f;
                        nav_.removeObstacle(n.pos.x - rr, n.pos.z - rr, n.pos.x + rr, n.pos.z + rr);
                    }
                    destroyEntity(n.entity);
                }
                continue;
            }
            ResourceNode saved;
            nodeIdentity(r, saved);
            fields(r, saved);
            if (!saved.alive) {   // martwa sadzonka - tylko miejsce w tablicy (indeksy muszą się zgadzać)
                saved.entity = nullptr;
                nodes.push_back(saved);
                continue;
            }
            const int id = addNode(saved.kind, saved.pos, saved.variant, saved.baseScale);
            Reader r2{nodesJ[i]};
            fields(r2, nodes[id]);
        }

        // 3. Budynki: model, rusztowanie (plac budowy), pierścień zaznaczenia i przeszkoda w nawigacji.
        buildings.clear();
        for (const json& jb : in.at("buildings")) {
            Building b;
            Reader r{jb};
            fields(r, b);
            if (b.alive) {
                const BuildingDef& def = buildingDef(b.type);
                b.entity = makeEntity("bld", buildingMeshName(b.type, b.faction));
                b.entity->transform().position = b.pos;
                b.entity->transform().rotation.y = b.rotY;
                b.ring = makeEntity("bring", b.faction == kPlayer ? kRingPlayerMesh : kRingEnemyMesh);
                b.ring->setActive(false);
                b.ring->getComponent<MeshComponent>()->castShadows = false;
                const float o = def.halfSize - 1.0f;
                nav_.addObstacle(b.pos.x - o, b.pos.z - o, b.pos.x + o, b.pos.z + o);
                if (!b.constructed) {
                    b.scaffold = makeEntity("scaf", kScaffoldMesh);
                    b.scaffold->transform().position = b.pos;
                    b.scaffold->transform().scale = {def.halfSize, def.height * 0.75f, def.halfSize};
                    b.scaffold->transform().rotation.y = b.rotY;
                }
            }
            buildings.push_back(std::move(b));
        }

        // 4. Flagi i drogi (model drogi budujemy z zapisanej trasy).
        flags.clear();
        for (const json& jf : in.at("flags")) {
            Flag f;
            Reader r{jf};
            fields(r, f);
            if (f.alive) {
                f.entity = makeEntity("flag", flagMeshName(f.faction));
                f.entity->transform().position = f.pos;
            }
            flags.push_back(std::move(f));
        }
        roads.clear();
        for (const json& jr : in.at("roads")) {
            Road rd;
            Reader r{jr};
            fields(r, rd);
            roads.push_back(std::move(rd));
            if (roads.back().alive) buildRoadMesh(static_cast<int>(roads.size()) - 1);
        }
        rebuildRoadCells();

        // 5. Jednostki (model wg zapisanej nazwy - syncVisuals dalej podmienia go wg stanu i ładunku).
        units.clear();
        for (const json& ju : in.at("units")) {
            Unit u;
            Reader r{ju};
            fields(r, u);
            if (u.alive) {
                if (u.meshName.empty()) u.meshName = unitMeshName(u.type, u.faction, u.carry);
                u.entity = makeEntity("unit", u.meshName);
                u.entity->transform().position = u.pos;
                u.ring = makeEntity("ring", u.faction == kPlayer ? kRingPlayerMesh : kRingEnemyMesh);
                u.ring->setActive(false);
                u.ring->getComponent<MeshComponent>()->castShadows = false;
            }
            units.push_back(std::move(u));
        }

        // 6. Zlecenia transportu, frakcje, czas.
        orders.clear();
        for (const json& jo : in.at("orders")) {
            TransportOrder o;
            Reader r{jo};
            fields(r, o);
            orders.push_back(o);
        }
        factions = {};
        const json& fj = in.at("factions");
        for (size_t f = 0; f < factions.size() && f < fj.size(); ++f) {
            Reader r{fj[f]};
            fields(r, factions[f]);
        }
        fogOfWar = in.value("fogOfWar", false);
        if (in.contains("fogExplored")) setFogExploredRle(in["fogExplored"].get<std::string>());
        gameTime = in.value("gameTime", 0.0f);
        winner = in.value("winner", -1);
        if (in.contains("lastCombatTime")) dec(in["lastCombatTime"], lastCombatTime);
        if (in.contains("rng")) {
            std::istringstream rng(in["rng"].get<std::string>());
            rng >> rng_;
        }

        // 7. To, co wynika z obiektów: terytorium, spójność dróg, obszary nawigacji, surowce na koncie.
        territoryDirty_ = true;
        routingDirty_ = true;
        regionsDirty_ = true;
        finishCreate();
        pushMessage("Wczytano zapis gry", {0.8f, 0.95f, 1.0f, 1.0f});
        return true;
    } catch (const std::exception& e) {
        return fail(std::string("uszkodzony plik zapisu: ") + e.what());
    }
}

} // namespace settlers
