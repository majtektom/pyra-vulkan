/**
 * world_movement.cpp - ruch jednostek.
 *
 * Ścieżka: setPath wyznacza trasę (linia prosta albo A* na siatce NavGrid), followPath przesuwa
 * jednostkę wzdłuż niej. separateUnits rozpycha jednostki, żeby nie stały jedna w drugiej
 * (stojący ustępują idącym). recoverStall odblokowuje jednostki, które utknęły.
 */
#include "world_internal.h"

namespace settlers {

// Wyznacza ścieżkę do celu. Gdy linia prosta jest wolna od przeszkód, wystarcza jeden punkt;
// inaczej szukamy trasy algorytmem A* na siatce NavGrid (z częściową ścieżką, gdy cel nieosiągalny).
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

// Odblokowanie jednostki stojącej w miejscu od kStallSeconds:
//  krok 1 (6 s): nowa ścieżka i chwilowe przeciskanie się przez innych,
//  krok 2 (12 s): wyjście ze stanu - przy budynku zadanie jest zaliczane, daleko od niego zgłaszany jest brak dojścia.
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
            deliverCarriedResource(u, *b);   // tuż przy budynku - oddajemy surowiec w miejscu
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

// Przesuwa jednostkę wzdłuż ścieżki (na zboczach wolniej) i obraca ją w kierunku marszu.
// Zwraca true, gdy jednostka dotarła do końca ścieżki (z dokładnością reachDist).
bool World::followPath(Unit& u, float dt, float reachDist) {
    if (u.pathIndex >= u.path.size()) return true;
    const UnitDef& def = unitDef(u.type);
    float step = def.speed * dt;
    // Na zboczach jednostki zwalniają, po ubitej drodze idą szybciej.
    const Vec3 nrm = terrain_->getNormal(u.pos.x, u.pos.z);
    step *= std::clamp(0.55f + nrm.y * 0.45f, 0.6f, 1.0f);
    if (roadAtCell(u.pos.x, u.pos.z) >= 0) step *= kRoadSpeedBonus;

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

// Rozpychanie jednostek, które na siebie nachodzą:
//  - pracujący (budują, zbierają) stoją w miejscu, inni ich omijają,
//  - stojący ustępują idącym, odsuwając się na bok względem kierunku marszu,
//  - przeciskający się (ghostTimer) nie zderzają się wcale.
// Na końcu jednostki stojące wewnątrz zablokowanych komórek (np. budynek postawiony na nich) wychodzą na zewnątrz.
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

} // namespace settlers
