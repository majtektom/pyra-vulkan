/**
 * world_ai.cpp - komputerowy przeciwnik.
 *
 * AI korzysta z tych samych reguł co gracz (koszty, budowa przez osadników, szkolenie).
 * Co 3 sekundy "myśli": stawia kolejny budynek z listy i zleca szkolenie wojska.
 * Co kilka minut zbiera armię i wysyła falę ataku na najbliższy budynek przeciwnika.
 * AI gra na tych samych zasadach co gracz (bez dochodu biernego) - poziom trudności to zapasy startowe i tempo fal.
 * Drogi AI buduje automatycznie: każdy warsztat i magazyn łączy z najbliższą flagą sieci (autoConnectFlag),
 * a gdy magazyny są zapełnione w 75%, stawia nowy Magazyn blisko warsztatów.
 */
#include "world_internal.h"

namespace settlers {

namespace {

// Kolejność budowy komputerowego przeciwnika.
const BuildingType kAiBuildOrder[] = {
    // podstawy: deski, kamień, mieszkańcy, żywność, koszary
    BuildingType::Woodcutter, BuildingType::Sawmill, BuildingType::Quarry, BuildingType::House,
    BuildingType::Woodcutter, BuildingType::Fisher, BuildingType::Barracks, BuildingType::Hunter,
    BuildingType::Quarry, BuildingType::House,
    // łańcuch broni (kopalnie zjadają żywność z rybaka i myśliwego)
    BuildingType::CoalMine, BuildingType::OreMine, BuildingType::Smelter, BuildingType::Smithy,
    // chleb, więcej surowców, terytorium
    BuildingType::Farm, BuildingType::Mill, BuildingType::Bakery, BuildingType::Mine, BuildingType::House,
    BuildingType::Forester, BuildingType::Tower, BuildingType::Farm, BuildingType::CoalMine, BuildingType::Woodcutter,
    BuildingType::Tower, BuildingType::House,
};
// Po wyczerpaniu listy AI powtarza ten cykl. Kopalnie, dla których nie ma gór w terytorium, i rybak
// bez wody są pomijane po kilku nieudanych próbach (placementFailures).
const BuildingType kAiLateCycle[] = {
    BuildingType::House, BuildingType::Woodcutter, BuildingType::Tower, BuildingType::Fisher, BuildingType::Barracks,
    BuildingType::Quarry, BuildingType::Hunter, BuildingType::Forester, BuildingType::CoalMine, BuildingType::OreMine,
};

} // namespace

// Jeden krok AI frakcji f: co 3 s decyzje (budowa, szkolenie), fale ataku.
void World::updateAI(int f, float dt) {
    FactionState& fs = factions[f];
    if (fs.defeated) return;

    fs.aiThinkTimer -= dt;
    if (fs.aiThinkTimer <= 0.0f) {
        fs.aiThinkTimer = 3.0f;
        aiConnectRoads(f);
        aiBuildNext(f);
        aiTrainArmy(f);
    }

    aiLaunchAttackWave(f, dt);
}

// Pozycja zamku frakcji (albo pozycja startowa, gdy zamku już nie ma).
Vec3 World::aiHome(int f) const {
    const FactionState& fs = factions[f];
    return (fs.castle >= 0 && buildings[fs.castle].alive) ? buildings[fs.castle].pos : startPos[f];
}

// Stawia kolejny budynek z listy kAiBuildOrder (najwyżej 2 budowy naraz).
// Gdy brakuje miejsca dla ludności, najpierw stawia Dom.
void World::aiBuildNext(int f) {
    FactionState& fs = factions[f];

    int underConstruction = 0;
    bool houseInProgress = false;
    bool storeInProgress = false;
    bool hasBarracks = false;   // także w budowie
    int mines = 0, foodMakers = 0, hunters = 0;   // kopalnie (jedzą żywność) i jej producenci
    bool foodInProgress = false;
    for (const auto& b : buildings) {
        if (b.alive && b.faction == f && b.type == BuildingType::Barracks) hasBarracks = true;
        if (b.alive && b.faction == f) {
            const WorkKind k = productionDef(b.type).kind;
            mines += k == WorkKind::Mine ? 1 : 0;
            const bool food = k == WorkKind::Fisher || k == WorkKind::Hunter || b.type == BuildingType::Bakery;
            foodMakers += food ? 1 : 0;
            hunters += k == WorkKind::Hunter ? 1 : 0;
            foodInProgress |= food && !b.constructed;
        }
        if (!b.alive || b.faction != f || b.constructed) continue;
        ++underConstruction;
        if (b.type == BuildingType::House) houseInProgress = true;
        if (b.type == BuildingType::Warehouse) storeInProgress = true;
    }
    if (underConstruction >= 2) return;

    // Co budować: pilny Dom, kolejna pozycja z listy albo (po jej wyczerpaniu) cykl późnej gry.
    const size_t orderLen = std::size(kAiBuildOrder);
    const bool needHouse = fs.popCap - fs.population <= 2 && !houseInProgress;
    // Nowy Magazyn: magazyny zapełnione w 75% (bez miejsca warsztaty staną) albo towary zalegają
    // na flagach (drogi do zamku są zakorkowane - magazyn bliżej warsztatów skraca trasy).
    int onFlags = 0, stores = 0;
    for (const auto& fl : flags) {
        if (fl.alive && fl.faction == f) onFlags += static_cast<int>(fl.goods.size());
    }
    for (const auto& b : buildings) {
        if (b.alive && b.faction == f && b.type == BuildingType::Warehouse) ++stores;
    }
    const bool needStore = !storeInProgress && (storageUsedOf(f) * 4 >= storageCapacityOf(f) * 3 || (onFlags > 30 && stores < 3));
    // Koszary: gdy lista je minęła (np. pominięte, bo zabrakło miejsca), a żadnych nie ma - bez nich AI nie ma wojska.
    const size_t barracksPos = static_cast<size_t>(std::find(std::begin(kAiBuildOrder), std::end(kAiBuildOrder), BuildingType::Barracks) -
                                                   std::begin(kAiBuildOrder));
    const bool needBarracks = !hasBarracks && fs.buildOrderIndex > barracksPos;
    // Żywność: kopalnie bez jedzenia stoją (a z nimi kamień, węgiel i ruda).
    const bool needFood = mines > 0 && !foodInProgress && spendable(f).food() < 15 && foodMakers < mines + 2;
    BuildingType next;
    if (needHouse) {
        next = BuildingType::House;
    } else if (needStore) {
        next = BuildingType::Warehouse;
    } else if (needFood) {
        next = hunters * 2 < foodMakers ? BuildingType::Hunter : BuildingType::Fisher;   // na zmianę
    } else if (needBarracks) {
        next = BuildingType::Barracks;
    } else if (fs.buildOrderIndex < orderLen) {
        next = kAiBuildOrder[fs.buildOrderIndex];
    } else {
        next = kAiLateCycle[(fs.buildOrderIndex - orderLen) % std::size(kAiLateCycle)];
    }
    // Brakuje kamienia albo desek na kolejny budynek: zamiast czekać, dobudowujemy ich producenta
    // (bez tego AI potrafiło stać całą grę - jeden kamieniarz wyczerpuje okoliczne skały).
    bool filler = false;
    const Resources have = spendable(f);
    if (!have.canAfford(buildingDef(next).cost)) {
        const BuildingType alt = aiProducerFor(f, buildingDef(next).cost, have);
        if (alt == BuildingType::Count || !have.canAfford(buildingDef(alt).cost)) return;   // czekamy na surowce
        next = alt;
        filler = true;
    }

    // Gdzie: wieże na obrzeżach terytorium (poszerzają je), reszta bliżej zamku.
    const float maxR = buildingDef(BuildingType::Castle).territoryRadius - 25.0f;
    const bool tower = next == BuildingType::Tower;
    const float minR = tower ? maxR * 0.7f : 30.0f;
    Vec3 spot;
    bool found = false;
    if (next == BuildingType::Warehouse) {
        // Magazyn blisko produkcji: w okolicy środka ciężkości warsztatów.
        Vec3 c{};
        int n = 0;
        for (const auto& b : buildings) {
            if (b.alive && b.faction == f && producesGoods(b.type)) {
                c = add(c, b.pos);
                ++n;
            }
        }
        if (n > 0) found = findPlacementNear(next, f, multiply(c, 1.0f / static_cast<float>(n)), 10.0f, 70.0f, spot);
    }
    if (!found) found = findPlacementNear(next, f, aiHome(f), minR, tower ? maxR + 60.0f : maxR, spot);
    // Rybak potrzebuje wody - gdy jej nie ma, myśliwy (ten sam koszt).
    if (!found && next == BuildingType::Fisher) {
        next = BuildingType::Hunter;
        found = findPlacementNear(next, f, aiHome(f), minR, maxR, spot);
    }
    // Duże budynki (koszary) mogą się nie zmieścić blisko zamku - szukamy dalej (teren poszerzony wieżami).
    if (!found && next == BuildingType::Barracks) found = findPlacementNear(next, f, aiHome(f), maxR, maxR + 120.0f, spot);
    if (found) {
        const int id = placeBuilding(next, spot, f, false, 0.0f);
        autoConnectFlag(buildings[id].flag);   // bez drogi tragarze nie dowiozą materiałów
        // Pilny Dom spoza listy nie przesuwa listy budowy (chyba że lista i tak wskazywała Dom).
        const bool listSaysHouse = fs.buildOrderIndex < orderLen && kAiBuildOrder[fs.buildOrderIndex] == BuildingType::House;
        const bool extra = filler || needFood || (next == BuildingType::House && needHouse && !listSaysHouse) ||
                           (next == BuildingType::Barracks && needBarracks);
        if (!extra) {
            fs.buildOrderIndex++;
        }
        fs.placementFailures = 0;
    } else if (!filler && !needFood && !(needBarracks && next == BuildingType::Barracks) && ++fs.placementFailures > 4) {
        fs.buildOrderIndex++;   // nie ma gdzie postawić - pomijamy tę pozycję listy
        fs.placementFailures = 0;
    }
}

// Producent brakującego materiału budowlanego (BuildingType::Count = nic nie trzeba / już się buduje):
//  - kamień: kopalnia kamienia w górach (nie wyczerpuje się), a gdy nie ma gdzie - kamieniarz,
//  - deski: tartak (gdy jest drewno), inaczej drwal.
BuildingType World::aiProducerFor(int f, const Resources& cost, const Resources& have) {
    std::array<int, kBuildingTypeCount> count{};   // ukończone i w budowie
    bool building = false;
    for (const auto& b : buildings) {
        if (!b.alive || b.faction != f) continue;
        if (b.lacksResources) continue;   // kamieniarz bez skał / drwal bez drzew się nie liczy
        count[static_cast<size_t>(b.type)]++;
        building |= !b.constructed && (b.type == BuildingType::Quarry || b.type == BuildingType::Mine ||
                                       b.type == BuildingType::Sawmill || b.type == BuildingType::Woodcutter);
    }
    if (building) return BuildingType::Count;   // producent już powstaje
    if (factions[f].popCap - factions[f].population <= 2) return BuildingType::Count;   // najpierw Dom (ludzie do pracy)
    auto n = [&](BuildingType t) { return count[static_cast<size_t>(t)]; };
    if (have[CarryType::Stone] < cost[CarryType::Stone] && n(BuildingType::Quarry) + n(BuildingType::Mine) < 4) {
        // Skały w okolicy (kamieniarz jest tańszy i nie je) - inaczej kopalnia w górach.
        if (countNodesInRange(NodeKind::Rock, aiHome(f), buildingDef(BuildingType::Castle).territoryRadius) >= 3 &&
            n(BuildingType::Quarry) < 3) {
            return BuildingType::Quarry;
        }
        Vec3 spot;
        if (findPlacementNear(BuildingType::Mine, f, aiHome(f), 30.0f, buildingDef(BuildingType::Castle).territoryRadius, spot)) {
            return BuildingType::Mine;
        }
        return BuildingType::Quarry;
    }
    if (have[CarryType::Planks] < cost[CarryType::Planks]) {
        // Drwal daje 2 kłody na kurs, tartak robi z 1 kłody 2 deski - na tartak wystarczy ok. 2 drwali.
        if (have[CarryType::Wood] >= 6 && n(BuildingType::Sawmill) < 2) return BuildingType::Sawmill;
        if (n(BuildingType::Woodcutter) < 2 * n(BuildingType::Sawmill) + 1) return BuildingType::Woodcutter;
    }
    return BuildingType::Count;
}

// Budynki bez połączenia z siecią dróg: AI dobudowuje drogę (najwyżej 2 na raz,
// żeby jeden "myślący" krok nie trwał za długo).
void World::aiConnectRoads(int f) {
    int tries = 0;
    for (const auto& b : buildings) {
        if (tries >= 2) break;
        if (!b.alive || b.faction != f || b.flag < 0 || b.type == BuildingType::Castle) continue;
        if (!b.constructed && b.type == BuildingType::Warehouse && flagConnected(b.flag)) continue;
        if (buildingLinked(b)) continue;
        ++tries;
        autoConnectFlag(b.flag);
    }
}

// Każde koszary szkolą wojsko (kolejka do 2): dwóch mieczników na jednego łucznika.
void World::aiTrainArmy(int f) {
    FactionState& fs = factions[f];
    for (size_t i = 0; i < buildings.size(); ++i) {
        const Building& b = buildings[i];
        if (!b.alive || b.faction != f || b.type != BuildingType::Barracks || !b.constructed) continue;
        if (b.trainQueue.size() >= 2) continue;
        const UnitType type = (fs.soldiersTrained % 3 == 2) ? UnitType::Archer : UnitType::Swordsman;
        queueTraining(static_cast<int>(i), type);
    }
}

// Fala ataku: gdy minie czas i armia jest dość liczna, wszyscy oprócz 2 obrońców
// ruszają na najbliższy budynek przeciwnika. Kolejne fale wymagają większej armii.
void World::aiLaunchAttackWave(int f, float dt) {
    FactionState& fs = factions[f];
    fs.waveTimer -= dt;
    if (fs.waveTimer > 0.0f) return;

    // Wolni żołnierze (nie walczą i nie są w marszu bojowym).
    std::vector<int> army;
    for (size_t i = 0; i < units.size(); ++i) {
        const Unit& u = units[i];
        if (u.alive && u.faction == f && u.type != UnitType::Settler &&
            (u.state == UnitState::Idle || (u.state == UnitState::Moving && !u.attackMove))) {
            army.push_back(static_cast<int>(i));
        }
    }
    const int minWave = (difficulty == Difficulty::Easy ? 4 : (difficulty == Difficulty::Normal ? 5 : 6)) + fs.waveNumber;
    if (static_cast<int>(army.size()) < minWave + 2) {
        fs.waveTimer = 20.0f;   // za mało wojska - sprawdzimy za 20 s
        return;
    }

    // Dwóch żołnierzy najbliżej zamku zostaje na obronie.
    const Vec3 home = aiHome(f);
    std::sort(army.begin(), army.end(), [&](int a, int b) { return dist2D(units[a].pos, home) < dist2D(units[b].pos, home); });
    army.erase(army.begin(), army.begin() + 2);

    // Cel: budynek przeciwnika najbliższy naszemu zamkowi.
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
}

} // namespace settlers
