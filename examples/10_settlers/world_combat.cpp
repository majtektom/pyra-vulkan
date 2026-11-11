/**
 * world_combat.cpp - walka.
 *
 * Żołnierze (miecznik, łucznik) same wykrywają wrogów w zasięgu, gonią ich i atakują.
 * Łucznicy i wieże strzelają strzałami lecącymi po łuku (Projectile). Zaatakowana jednostka
 * odpowiada atakiem i wzywa pobliskich obrońców. Tu są też rozkazy wydawane wojsku przez gracza.
 */
#include "world_internal.h"

namespace settlers {

// Marsz wojska w szyku: kolejni żołnierze dostają punkty siatki wokół celu (odstęp 6).
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

// Rozkaz ataku na wskazaną jednostkę (pościg bez ograniczenia odległości).
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

// Rozkaz ataku na budynek. Po drodze żołnierze walczą z napotkanymi wrogami (attackMove).
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

// =====================================================================================
// Wieże: załoga i zdobywanie
// =====================================================================================

int World::garrisonOf(int tower) const {
    int n = 0;
    for (const auto& u : units) n += (u.alive && u.homeTower == tower && u.state == UnitState::Garrison) ? 1 : 0;
    return n;
}

int World::garrisonAssigned(int tower) const {
    int n = 0;
    for (const auto& u : units) n += (u.alive && u.homeTower == tower) ? 1 : 0;
    return n;
}

// Wysyła żołnierzy do własnej wieży (do jej pojemności). Zwraca, ilu wysłano.
int World::commandGarrison(const std::vector<int>& ids, int tower) {
    if (tower < 0 || !isCapturable(buildings[tower])) return 0;
    const Building& t = buildings[tower];
    int free = garrisonCapacity(t.type) - garrisonAssigned(tower);
    int sent = 0;
    for (int id : ids) {
        if (free <= 0) break;
        Unit& u = units[id];
        if (!u.alive || u.type == UnitType::Settler || u.faction != t.faction || u.homeTower == tower) continue;
        u.homeTower = tower;
        u.state = UnitState::GoGarrison;
        u.targetUnit = -1;
        u.targetBuilding = -1;
        u.attackMove = false;
        u.path.clear();
        --free;
        ++sent;
    }
    return sent;
}

void World::releaseGarrison(int tower) {
    const Vec3 door = buildings[tower].door();
    int k = 0;
    for (auto& u : units) {
        if (!u.alive || u.homeTower != tower) continue;
        u.homeTower = -1;
        if (u.state == UnitState::Garrison || u.state == UnitState::GoGarrison) {
            u.state = UnitState::Moving;
            u.pos = door;
            const Vec3 spot = add(door, rotateY({-6.0f + 6.0f * static_cast<float>(k++), 0.0f, 8.0f}, buildings[tower].rotY));
            u.guardPos = spot;
            setPath(u, spot);
        }
    }
    // Wieża nie zostanie od razu obsadzona ponownie przez tych samych żołnierzy (autoGarrison co 2 s
    // wybierze najbliższego wolnego - zwykle jednego z nich; to minimalna załoga, żeby wieża strzelała).
}

// Marsz do drzwi wieży; na miejscu żołnierz wchodzi do załogi (model ukrywa syncVisuals).
void World::soldierGoGarrison(Unit& u, float dt) {
    if (u.homeTower < 0) {   // wieży już nie ma (odwołanie nieaktualne) - żołnierz jest wolny
        u.state = UnitState::Idle;
        u.guardPos = u.pos;
        return;
    }
    const Building& t = buildings[u.homeTower];
    const Vec3 door = t.door();
    const bool pathDone = !u.path.empty() && u.pathIndex >= u.path.size();
    if (dist2D(u.pos, door) < 3.0f || (pathDone && dist2D(u.pos, door) < kNavCell * 2.5f)) {
        u.state = UnitState::Garrison;
        u.pos = door;
        u.path.clear();
        u.guardPos = door;
        return;
    }
    if (pathFinished(u)) {
        setPath(u, door);
        if (u.path.empty()) {   // nie da się dojść - rezygnacja
            u.homeTower = -1;
            u.state = UnitState::Idle;
            return;
        }
    }
    followPath(u, dt, 0.5f);
}

// Każda ukończona wieża bez załogi dostaje wolnego żołnierza (najbliższego, do 500 jednostek).
void World::autoGarrison(int f) {
    for (size_t ti = 0; ti < buildings.size(); ++ti) {
        const Building& t = buildings[ti];
        if (t.faction != f || !isCapturable(t) || garrisonAssigned(static_cast<int>(ti)) > 0) continue;
        int best = -1;
        float bestD = 500.0f;
        for (size_t i = 0; i < units.size(); ++i) {
            const Unit& u = units[i];
            if (!u.alive || u.faction != f || u.type == UnitType::Settler || u.state != UnitState::Idle || u.homeTower >= 0) continue;
            const float d = dist2D(u.pos, t.pos);
            if (d < bestD) {
                bestD = d;
                best = static_cast<int>(i);
            }
        }
        if (best >= 0) commandGarrison({best}, static_cast<int>(ti));
    }
}

// Szturm wieży wroga:
//  - załoga w środku: jeden z obrońców wychodzi i walczy z napastnikiem (napastnik czeka przy drzwiach),
//  - obrońca na zewnątrz: napastnik walczy z nim,
//  - wieża pusta: napastnik wchodzi przez drzwi i przejmuje wieżę razem z terenem.
void World::soldierAssaultTower(Unit& u, int id, float dt) {
    const int ti = u.targetBuilding;
    const Building& t = buildings[ti];
    const Vec3 door = t.door();

    // Obrońca już na zewnątrz - walczymy z nim.
    for (size_t i = 0; i < units.size(); ++i) {
        const Unit& d = units[i];
        if (d.alive && d.homeTower == ti && d.state != UnitState::Garrison && dist2D(d.pos, t.pos) < 90.0f) {
            u.targetUnit = static_cast<int>(i);
            return;   // następna klatka: zwykła walka z jednostką
        }
    }

    const float toDoor = dist2D(u.pos, door);
    if (toDoor < 14.0f) {
        if (garrisonOf(ti) > 0) {
            // Wywołanie obrońcy: pierwszy z załogi wychodzi przed drzwi i atakuje.
            for (size_t i = 0; i < units.size(); ++i) {
                Unit& d = units[i];
                if (!d.alive || d.homeTower != ti || d.state != UnitState::Garrison) continue;
                d.state = UnitState::Attack;
                d.targetUnit = id;
                d.pos = door;
                d.path.clear();
                emitSound("sword", door, 0.6f);
                break;
            }
            u.targetUnit = -1;
            return;
        }
        if (toDoor < 3.5f) {
            captureTower(ti, id);
            return;
        }
    }
    // Marsz do drzwi wieży.
    u.repathTimer -= dt;
    if (pathFinished(u) || u.repathTimer <= 0.0f) {
        u.repathTimer = 2.0f;
        setPath(u, door);
        if (u.path.empty()) {
            u.targetBuilding = -1;
            u.state = UnitState::Idle;
            return;
        }
    }
    followPath(u, dt, 0.5f);
}

// Przejęcie wieży: zmiana właściciela, modelu i flagi; drogi poprzedniego właściciela przy wieży znikają,
// napastnik wchodzi do załogi. Nowy zasięg terenu (rebuildTerritory) pali wrogie budynki w okolicy.
void World::captureTower(int ti, int unitId) {
    Building& t = buildings[ti];
    Unit& u = units[unitId];
    const int oldFaction = t.faction;
    t.faction = u.faction;
    t.hp = std::max(t.hp, buildingDef(t.type).maxHp * 0.5f);
    t.selected = false;
    if (auto* mc = t.entity->getComponent<MeshComponent>()) mc->meshName = buildingMeshName(t.type, t.faction);
    destroyEntity(t.ring);
    t.ring = makeEntity("bring", t.faction == kPlayer ? kRingPlayerMesh : kRingEnemyMesh);
    t.ring->setActive(false);
    t.ring->getComponent<MeshComponent>()->castShadows = false;
    t.delivered = {};
    for (auto& o : orders) {
        if (o.alive && o.dest == ti) releaseOrder(o);   // dostawy dawnego właściciela przepadają
    }
    if (t.flag >= 0 && flags[t.flag].alive) {
        removeFlag(t.flag);   // drogi poprzedniego właściciela; flaga przy budynku zostaje
        Flag& fl = flags[t.flag];
        fl.faction = t.faction;
        fl.goods.clear();
        fl.reserved = {};
        if (fl.entity) fl.entity->getComponent<MeshComponent>()->meshName = flagMeshName(t.faction);
    }
    u.homeTower = ti;
    u.state = UnitState::Garrison;
    u.targetBuilding = -1;
    u.targetUnit = -1;
    u.attackMove = false;
    u.pos = t.door();
    u.path.clear();
    markTerritoryChanged();
    routingDirty_ = true;
    emitSound("complete", t.pos, 1.0f, false);
    if (t.faction == kPlayer) {
        pushMessage("Zdobyto wrogą wieżę! Teren wokół niej jest nasz.", {0.6f, 1.0f, 0.6f, 1.0f});
    } else if (oldFaction == kPlayer) {
        pushMessage("Wróg zdobył naszą wieżę!", {1.0f, 0.4f, 0.3f, 1.0f});
    }
}

// Dodaje żołnierza do kolejki koszar. Koszt zdejmowany z magazynu od razu, a szkolenie rusza, gdy
// tragarze dowiozą go do koszar; wymaga miejsca w limicie ludności.
// Wybór typu: ten, którego jest za mało względem ustawionej proporcji (liczone od włączenia trybu auto).
// Gdy na wybrany typ nie stać (brak broni/desek/żywności), próbujemy drugiego - kolejka nie stoi bez potrzeby.
void World::autoRecruit(int id) {
    Building& b = buildings[id];
    if (!b.alive || !b.constructed || !b.autoTrain || b.trainQueue.size() >= 2) return;
    const float share = static_cast<float>(b.swordShare) / 100.0f;
    const float total = static_cast<float>(b.autoSwords + b.autoArchers + 1);
    const float swordDeficit = share * total - static_cast<float>(b.autoSwords);
    const float archerDeficit = (1.0f - share) * total - static_cast<float>(b.autoArchers);
    const UnitType first = swordDeficit >= archerDeficit ? UnitType::Swordsman : UnitType::Archer;
    const UnitType second = first == UnitType::Swordsman ? UnitType::Archer : UnitType::Swordsman;
    const bool secondAllowed = second == UnitType::Swordsman ? b.swordShare > 0 : b.swordShare < 100;
    std::string why;
    UnitType queued = first;
    bool ok = queueTraining(id, first, &why);
    if (!ok && secondAllowed) {
        std::string why2;
        ok = queueTraining(id, second, &why2);
        queued = second;
    }
    if (ok) {
        (queued == UnitType::Swordsman ? b.autoSwords : b.autoArchers)++;
    } else if (b.faction == kPlayer && b.trainQueue.empty() && gameTime - b.autoTrainMsgTime > 60.0f) {
        b.autoTrainMsgTime = gameTime;
        pushMessage("Koszary (auto): " + why, {1.0f, 0.75f, 0.4f, 1.0f});
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
    const Resources have = spendable(b.faction);
    if (!have.canAfford(def.cost)) {
        if (reason) *reason = "brakuje " + missingText(def.cost, have);
        return false;
    }
    orderMaterials(id, def.cost);   // koszt zdjęty z magazynu, tragarze dowiozą go do koszar
    b.trainQueue.push_back(type);
    return true;
}

// =====================================================================================
// Żołnierze
// =====================================================================================

// Jeden krok żołnierza: wykrycie wroga, reakcja na utratę celu, potem marsz albo walka.
void World::updateSoldier(Unit& u, int id, float dt) {
    u.attackCooldown -= dt;
    u.timer -= dt;

    // Cel mógł zginąć w poprzedniej klatce.
    if (u.targetUnit >= 0 && !units[u.targetUnit].alive) u.targetUnit = -1;
    // Budynek-cel zniknął albo jest już nasz (np. wieżę zdobył inny żołnierz) - koniec ataku.
    if (u.targetBuilding >= 0 && (!buildings[u.targetBuilding].alive || buildings[u.targetBuilding].faction == u.faction)) {
        u.targetBuilding = -1;
    }
    if (u.targetUnit >= 0 && units[u.targetUnit].faction == u.faction) u.targetUnit = -1;

    soldierAcquireTarget(u);

    // Wieża macierzysta przestała być nasza (zdobyta / zburzona / jej slot zajął nowy budynek) - żołnierz jest wolny.
    if ((u.homeTower < 0 && (u.state == UnitState::Garrison || u.state == UnitState::GoGarrison)) ||
        (u.homeTower >= 0 && (!buildings[u.homeTower].alive || buildings[u.homeTower].faction != u.faction))) {
        u.homeTower = -1;
        if (u.state == UnitState::Garrison || u.state == UnitState::GoGarrison) {
            u.state = UnitState::Idle;
            u.guardPos = u.pos;
        }
    }

    if (u.state == UnitState::Attack && u.targetUnit < 0 && u.targetBuilding < 0) {
        // Cel zniszczony - obrońca wraca do wieży, inni kontynuują marsz albo wracają na posterunek.
        if (u.homeTower >= 0) {
            u.state = UnitState::GoGarrison;
            u.path.clear();
        } else if (dist2D(u.pos, u.guardPos) > 10.0f) {
            u.state = UnitState::Moving;
            setPath(u, u.guardPos);
        } else {
            u.state = UnitState::Idle;
        }
    }

    switch (u.state) {
    case UnitState::Idle:
        break;   // stoi na posterunku (wrogów wypatruje soldierAcquireTarget)
    case UnitState::Moving:
        if (followPath(u, dt, 0.5f)) {
            u.state = UnitState::Idle;
            u.guardPos = u.pos;   // miejsce dojścia staje się nowym posterunkiem
        }
        break;
    case UnitState::Attack:
        soldierAttack(u, id, dt);
        break;
    case UnitState::GoGarrison:
        soldierGoGarrison(u, dt);
        break;
    case UnitState::Garrison:
        break;   // w środku wieży - wieża strzela, a wróg może wywołać obrońcę (soldierAssaultTower)
    default:
        u.state = UnitState::Idle;
        break;
    }
}

// Automatyczne wykrywanie wrogów (co 0,35 s, żeby nie przeszukiwać wszystkich jednostek w każdej klatce).
// Działa, gdy żołnierz stoi, idzie w marszu bojowym albo atakuje budynek (jednostki mają pierwszeństwo).
void World::soldierAcquireTarget(Unit& u) {
    const bool canAcquire = u.state == UnitState::Idle || (u.state == UnitState::Moving && u.attackMove) ||
                            (u.state == UnitState::Attack && u.targetUnit < 0);
    if (!canAcquire || u.timer > 0.0f) return;
    u.timer = 0.35f;

    const UnitDef& def = unitDef(u.type);
    const int enemy = findEnemyUnitNear(u.faction, u.pos, def.aggroRange);
    if (enemy >= 0) {
        u.targetUnit = enemy;
        u.state = UnitState::Attack;
        u.path.clear();
        return;
    }
    if (u.state != UnitState::Attack) {
        const int building = findEnemyBuildingNear(u.faction, u.pos, def.aggroRange * 0.8f);
        if (building >= 0) {
            u.targetBuilding = building;
            u.state = UnitState::Attack;
            u.path.clear();
        }
    }
}

// Walka z wybranym celem: w zasięgu - cios/strzał, poza zasięgiem - pościg.
void World::soldierAttack(Unit& u, int id, float dt) {
    const UnitDef& def = unitDef(u.type);

    // Położenie celu i odległość od jego krawędzi (dla budynku - od ściany).
    Vec3 targetPos;
    float distance;
    if (u.targetUnit >= 0) {
        const Unit& t = units[u.targetUnit];
        targetPos = t.pos;
        distance = dist2D(u.pos, targetPos) - unitDef(t.type).radius;
        // "Smycz": cel wybrany automatycznie nie jest ścigany dalej niż 160 od posterunku.
        if (!u.attackMove && dist2D(u.pos, u.guardPos) > 160.0f) {
            u.targetUnit = -1;
            u.state = UnitState::Moving;
            setPath(u, u.guardPos);
            return;
        }
    } else {
        if (u.targetBuilding < 0) {   // bez celu (sprawdzone wyżej w updateSoldier - tu tylko zabezpieczenie)
            u.state = UnitState::Idle;
            return;
        }
        const Building& b = buildings[u.targetBuilding];
        if (isCapturable(b)) {   // ukończonej wieży się nie burzy - szturmuje się ją
            soldierAssaultTower(u, id, dt);
            return;
        }
        targetPos = b.pos;
        distance = distanceToBuilding(u.pos, b);
    }

    // Przy budynku ścieżka może się skończyć dalej (budynek blokuje całe komórki nawigacji).
    const bool pathDone = !u.path.empty() && u.pathIndex >= u.path.size();
    const bool reachedBuilding = u.targetBuilding >= 0 && pathDone && distance <= def.range + def.radius + kNavCell * 2.0f;
    if (distance <= def.range + def.radius || reachedBuilding) {
        soldierStrike(u, id, targetPos, dt);
    } else {
        soldierChase(u, targetPos, dt);
    }
}

// Cel w zasięgu: zatrzymanie, obrót do celu i atak, gdy minął czas odnowienia.
void World::soldierStrike(Unit& u, int id, const Vec3& targetPos, float dt) {
    const UnitDef& def = unitDef(u.type);
    u.path.clear();
    u.yaw = angleLerp(u.yaw, std::atan2(targetPos.x - u.pos.x, targetPos.z - u.pos.z), std::min(1.0f, dt * 10.0f));
    if (u.attackCooldown > 0.0f) return;

    u.attackCooldown = def.cooldown * randf(0.9f, 1.1f);
    if (def.ranged) {
        // Łucznik: strzała leci po łuku, obrażenia zadaje dopiero po trafieniu (updateProjectiles).
        fireArrow(add(u.pos, {0.0f, 4.5f, 0.0f}), u.faction, u.targetUnit, u.targetBuilding, def.damage, id);
    } else if (u.targetUnit >= 0) {
        emitSound("sword", u.pos, 0.6f);
        damageUnit(u.targetUnit, def.damage * randf(0.85f, 1.15f), u.faction, id);
    } else {
        emitSound("hit_building", u.pos, 0.55f);
        damageBuilding(u.targetBuilding, def.damage * randf(0.85f, 1.15f), u.faction, id);
    }
}

// Pościg: ścieżka do celu jest odświeżana co 0,6 s (jednostka się rusza) albo co 3 s (budynek stoi).
void World::soldierChase(Unit& u, const Vec3& targetPos, float dt) {
    const UnitDef& def = unitDef(u.type);
    u.repathTimer -= dt;
    if (pathFinished(u) || u.repathTimer <= 0.0f) {
        u.repathTimer = u.targetUnit >= 0 ? 0.6f : 3.0f;
        Vec3 approach = targetPos;
        if (u.targetBuilding >= 0) {
            // Do budynku podchodzimy do ściany od naszej strony, nie do jego środka.
            Vec3 dir = subtract(u.pos, targetPos);
            dir.y = 0.0f;
            dir = normalize(dir);
            approach = add(targetPos, multiply(dir, buildingDef(buildings[u.targetBuilding].type).halfSize + 1.0f));
        }
        setPath(u, approach);
        if (u.path.empty()) {
            // Cel nieosiągalny (np. za wodą) - rezygnujemy.
            u.targetUnit = -1;
            u.targetBuilding = -1;
            u.state = UnitState::Idle;
            return;
        }
    }
    followPath(u, dt, std::max(0.5f, def.range * 0.8f));
}

// =====================================================================================
// Obrażenia i pociski
// =====================================================================================

void World::damageUnit(int target, float amount, int attackerFaction, int attackerUnit) {
    Unit& t = units[target];
    if (!t.alive) return;
    t.hp -= amount;
    t.hitFlash = 0.2f;
    t.lastDamagedTime = gameTime;
    lastCombatTime[t.faction] = gameTime;
    if (attackerFaction >= 0) lastCombatTime[attackerFaction] = gameTime;
    if (attackerFaction >= 0 && attackerFaction != t.faction) {   // skąd przyszedł atak (front obrony AI)
        factions[t.faction].lastAttackPos = attackerUnit >= 0 && units[attackerUnit].alive ? units[attackerUnit].pos : t.pos;
        factions[t.faction].lastAttackTime = gameTime;
    }

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

// Obrażenia budynku; przy 0 PŻ budynek jest niszczony. Zaatakowana frakcja wzywa obrońców.
void World::damageBuilding(int target, float amount, int attackerFaction, int attackerUnit) {
    Building& b = buildings[target];
    if (!b.alive || isCapturable(b)) return;   // ukończoną wieżę się zdobywa, nie burzy
    b.hp -= amount;
    b.lastDamagedTime = gameTime;
    lastCombatTime[b.faction] = gameTime;
    if (attackerFaction >= 0) lastCombatTime[attackerFaction] = gameTime;
    if (attackerFaction >= 0 && attackerFaction != b.faction) {
        factions[b.faction].lastAttackPos = attackerUnit >= 0 && units[attackerUnit].alive ? units[attackerUnit].pos : b.pos;
        factions[b.faction].lastAttackTime = gameTime;
    }
    if (b.faction == kPlayer && gameTime - factions[kPlayer].underAttackMsgTime > 25.0f) {
        factions[kPlayer].underAttackMsgTime = gameTime;
        pushMessage(std::string("Wróg atakuje: ") + buildingDef(b.type).name, {1.0f, 0.4f, 0.3f, 1.0f});
        emitSound("alarm", b.pos, 0.8f, false);
    }
    if (attackerUnit >= 0) alertDefenders(b.faction, b.pos, attackerUnit);
    if (b.hp <= 0.0f) destroyBuilding(target, attackerFaction);
}

// Wolni żołnierze frakcji w promieniu 140 od miejsca ataku ruszają na napastnika.
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

// Wystrzeliwuje strzałę: czas lotu zależy od odległości, wysokość łuku też.
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

// Lot strzał po paraboli (naprowadzanych na ruchomy cel); trafienie zadaje obrażenia.
// Strzała jest obracana wzdłuż wektora prędkości, żeby leciała grotem do przodu.
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
        p.entity->setActive(visibleToPlayer(pos));   // mgła wojny: strzał widać tylko w zasięgu wzroku
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

} // namespace settlers
