/**
 * world_update.cpp - jeden krok symulacji (World::update) i elementy wspólne.
 *
 * Kolejność w każdym kroku:
 *   frakcje (populacja, nowi osadnicy, przydział prac) -> jednostki -> rozpychanie ->
 *   budynki (szkolenie, wieże) -> pociski -> efekty -> drzewa i skały -> AI ->
 *   terytorium i obszary nawigacji -> wygląd encji 3D -> sprawdzenie zwycięstwa.
 */
#include "world_internal.h"

namespace settlers {

// Surowiec, do którego pracownik nie mógł dojść, jest pomijany przez 90 s (zwalniamy rezerwację).
void World::markNodeUnreachable(Unit& u) {
    if (u.resourceNode < 0) return;
    ResourceNode& n = nodes[u.resourceNode];
    n.reserved = std::max(0, n.reserved - 1);
    n.blockedUntil = gameTime + 90.0f;   // przez 1,5 min nikt nie próbuje do niego iść
    u.resourceNode = -1;
}

// Drwal/kamieniarz: liczy surowce w zasięgu pracy. Ostrzega, gdy zostają 1-2, oraz gdy nie ma żadnego
// (budynek dostaje stały znacznik 'brak drzew/kamienia').
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

// Natychmiastowe oznaczenie budynku jako niedostępnego (gdy obszary nawigacji się nie łączą).
void World::markUnreachable(int id) {
    Building& b = buildings[id];
    b.accessFailures = std::max(b.accessFailures, 1);
    reportAccessFailure(id);
}

// Osadnik nie doszedł do budynku. Po dwóch takich porażkach budynek dostaje znacznik 'brak dojścia',
// a przydział prac pomija go przez 30 s (potem próbuje ponownie).
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

// Leczenie: po 8 s bez obrażeń i poza walką jednostka odzyskuje 1,5% PŻ/s
// (6%/s w promieniu 70 od własnego zamku lub koszar).
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

// JEDEN KROK SYMULACJI. dt jest ograniczane do 0,1 s, żeby przy zacięciu klatki
// jednostki nie 'przeskakiwały' przez przeszkody.
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

// Dla każdej frakcji: liczy populację i limit ludności, zamek przyjmuje nowych osadników
// (za żywność; za darmo, gdy osadników prawie nie ma), a co 0,5 s przydzielane są prace.
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

// Przydział prac dla wolnych osadników, w kolejności ważności:
//  1. place budowy (do 2 budowniczych),
//  2. warsztaty bez pracownika,
//  3. naprawy budynków, przy których od 8 s nie ma walki.
// Przed wysłaniem sprawdzamy, czy do budynku w ogóle da się dojść.
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

// Wspólna część kroku każdej jednostki: wykrywanie tłoku (przeciskanie się) i zawieszeń,
// potem logika osadnika albo żołnierza, na końcu leczenie.
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

// Efekty: znacznik rozkazu kurczy się, gruzowisko zapada się pod koniec życia.
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

// Drzewa: drżenie przy ścinaniu, przewracanie po ścięciu ostatniej porcji.
// Skały: zmniejszają się w miarę wydobycia; wyczerpana skała znika i przestaje blokować nawigację.
void World::updateNodes(float dt) {
    for (auto& n : nodes) {
        if (!n.alive || !n.entity) continue;
        auto& tr = n.entity->transform();
        if (n.kind == NodeKind::Tree) {
            if (n.growth < 1.0f) {
                // Sadzonka rośnie: od 15% do pełnego rozmiaru w kTreeGrowSeconds.
                n.growth = std::min(1.0f, n.growth + dt / kTreeGrowSeconds);
                const float s = n.baseScale * (0.15f + 0.85f * n.growth);
                tr.scale = {s, s, s};
            } else if (n.fallTimer >= 0.0f) {
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

// Budynki: kontrola surowców warsztatów, szkolenie w koszarach (gdy jest miejsce w limicie ludności),
// strzały z wież i zamku do wrogów w zasięgu.
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

// Przepisuje stan symulacji do encji 3D: pozycja i obrót, animacje (krok, praca, cios),
// pierścienie zaznaczenia, wysokość budynków w budowie.
void World::syncVisuals(float dt) {
    (void)dt;
    for (auto& u : units) {
        if (!u.alive || !u.entity) continue;
        // Górnik przy pracy jest wewnątrz kopalni - chowamy jego model.
        const bool insideMine = u.state == UnitState::Gathering && u.building >= 0 &&
                                buildings[u.building].alive && buildings[u.building].type == BuildingType::Mine;
        u.entity->setActive(!insideMine);
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

// Słupki granicy terytorium. Dwa rodzaje granic:
//  1. zewnętrzna - punkty na okręgach zasięgu, które nie leżą w zasięgu żadnego innego budynku,
//  2. granica z wrogiem - linia, na której własność terenu zmienia się z frakcji f na wrogą.
//     Szukamy jej na siatce co 4 jednostki, a dokładne miejsce na krawędzi siatki - bisekcją.
//     Każda frakcja stawia słupki po swojej stronie linii, więc widać podwójną, dwukolorową granicę.
// Wszystkie słupki frakcji to jedna siatka (jedna encja).
void World::rebuildTerritory() {
    territoryDirty_ = false;
    burnBuildingsOnLostLand();
    auto* mm = engine_.getMeshManager();
    ++territoryMeshVersion_;

    struct Src {
        Vec3 p;
        float r;
        int faction;
    };
    std::vector<Src> sources;
    for (const auto& b : buildings) {
        if (!b.alive || !b.constructed) continue;
        const float r = buildingDef(b.type).territoryRadius;
        if (r > 0.0f) sources.push_back({b.pos, r, b.faction});
    }

    constexpr float kPostSpacing = 8.0f;
    constexpr float kGridStep = 4.0f;
    for (int f = 0; f < kFactionCount; ++f) {
        MeshBuilder mb;
        const Vec3 fc = factionColor(f);
        std::vector<Vec3> posts;
        // Słupek nie stoi w wodzie ani bliżej niż kPostSpacing od innego słupka tej frakcji.
        auto addPost = [&](float x, float z) {
            for (const auto& q : posts) {
                if ((q.x - x) * (q.x - x) + (q.z - z) * (q.z - z) < kPostSpacing * kPostSpacing) return;
            }
            const float h = heightAt(x, z);
            if (h < kWaterLevel) return;
            posts.push_back({x, h, z});
            mb.cylinder({x, h - 0.5f, z}, 0.4f, 3.4f, 5, {0.45f, 0.32f, 0.2f});
            mb.box({x, h + 3.2f, z}, {1.0f, 0.8f, 1.0f}, fc);
        };

        // 1. Zewnętrzna granica.
        float minX = 1e9f, minZ = 1e9f, maxX = -1e9f, maxZ = -1e9f;
        for (size_t i = 0; i < sources.size(); ++i) {
            const auto& s = sources[i];
            if (s.faction != f) continue;
            minX = std::min(minX, s.p.x - s.r);
            maxX = std::max(maxX, s.p.x + s.r);
            minZ = std::min(minZ, s.p.z - s.r);
            maxZ = std::max(maxZ, s.p.z + s.r);
            const int count = std::max(12, static_cast<int>(2.0f * PI * s.r / 10.0f));
            for (int k = 0; k < count; ++k) {
                const float a = 2.0f * PI * static_cast<float>(k) / static_cast<float>(count);
                const Vec3 p{s.p.x + std::cos(a) * s.r, 0.0f, s.p.z + std::sin(a) * s.r};
                bool inside = false;
                for (size_t j = 0; j < sources.size() && !inside; ++j) {
                    if (j != i && dist2D(p, sources[j].p) < sources[j].r - 2.0f) inside = true;
                }
                if (!inside) addPost(p.x, p.z);
            }
        }
        if (posts.empty() && minX > maxX) {
            territoryEntity_[f]->setActive(false);
            continue;
        }

        // 2. Granica z wrogiem: siatka właścicieli w prostokącie obejmującym zasięgi frakcji.
        const int nx = static_cast<int>((maxX - minX) / kGridStep) + 2;
        const int nz = static_cast<int>((maxZ - minZ) / kGridStep) + 2;
        std::vector<int> owner(static_cast<size_t>(nx) * nz);
        for (int iz = 0; iz < nz; ++iz) {
            for (int ix = 0; ix < nx; ++ix) {
                owner[iz * nx + ix] = territoryOwnerAt({minX + ix * kGridStep, 0.0f, minZ + iz * kGridStep});
            }
        }
        for (int iz = 0; iz < nz; ++iz) {
            for (int ix = 0; ix < nx; ++ix) {
                if (owner[iz * nx + ix] != f) continue;
                const int nbr[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (const auto& d : nbr) {
                    const int jx = ix + d[0], jz = iz + d[1];
                    if (jx < 0 || jz < 0 || jx >= nx || jz >= nz) continue;
                    const int o = owner[jz * nx + jx];
                    if (o < 0 || o == f) continue;
                    // Bisekcja na odcinku: t=0 nasz punkt, t=1 punkt wroga.
                    const float ax = minX + ix * kGridStep, az = minZ + iz * kGridStep;
                    float lo = 0.0f, hi = 1.0f;
                    for (int it = 0; it < 6; ++it) {
                        const float mid = (lo + hi) * 0.5f;
                        const Vec3 q{ax + d[0] * kGridStep * mid, 0.0f, az + d[1] * kGridStep * mid};
                        (territoryOwnerAt(q) == f ? lo : hi) = mid;
                    }
                    // Słupek 1,2 jednostki od linii granicy, po naszej stronie.
                    const float t = std::max(0.0f, lo - 1.2f / kGridStep);
                    addPost(ax + d[0] * kGridStep * t, az + d[1] * kGridStep * t);
                }
            }
        }

        auto* mc = territoryEntity_[f]->getComponent<MeshComponent>();
        if (!posts.empty()) {
            const std::string name = "territory_" + std::to_string(f) + "_" + std::to_string(seed_);
            mm->loadMesh(name, mb.build());
            mc->meshName = name;
            territoryEntity_[f]->setActive(true);
        } else {
            territoryEntity_[f]->setActive(false);
        }
    }
}

// Budynki (także w budowie) stojące na terenie, który przejął wróg, płoną - jak w Settlers.
// Ukończone zamki i wieże są chronione: ich własny zasięg zawsze wygrywa tuż przy nich.
void World::burnBuildingsOnLostLand() {
    int burnedPlayer = 0, burnedEnemy = 0;
    for (size_t i = 0; i < buildings.size(); ++i) {
        const Building& b = buildings[i];
        if (!b.alive) continue;
        if (b.constructed && buildingDef(b.type).territoryRadius > 0.0f) continue;
        const int owner = territoryOwnerAt(b.pos);
        if (owner < 0 || owner == b.faction) continue;
        (b.faction == kPlayer ? burnedPlayer : burnedEnemy)++;
        destroyBuilding(static_cast<int>(i), owner);
    }
    if (burnedPlayer > 0) {
        pushMessage("Wróg przejął część terenu - budynki na nim spłonęły (" + std::to_string(burnedPlayer) + ")",
                    {1.0f, 0.5f, 0.4f, 1.0f});
    }
    if (burnedEnemy > 0) {
        pushMessage("Przejęto teren wroga - jego budynki spłonęły (" + std::to_string(burnedEnemy) + ")",
                    {0.6f, 1.0f, 0.6f, 1.0f});
    }
}

// Utrata zamku = przegrana; ustawia zwycięzcę.
void World::checkVictory() {
    if (winner >= 0) return;
    if (factions[kPlayer].defeated) {
        winner = kEnemy;
    } else if (factions[kEnemy].defeated) {
        winner = kPlayer;
    }
}

} // namespace settlers
