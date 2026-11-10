/**
 * app_tests.cpp - testy automatyczne.
 *
 * SETTLERS_UITEST=1  - scenariusz testu interfejsu: wstrzykuje zdarzenia klawiatury i myszy
 *                      (tak jak prawdziwy gracz) i sprawdza efekty; wynik w logu "[uitest]".
 * SETTLERS_AUTOPLAY  - AI gra za obie strony, stan partii jest logowany co 30 s gry.
 */
#include "settlers_app.h"

namespace settlers {

// --- Test interfejsu (zdarzenia SDL trafiają do Platform jak prawdziwe wejście) ---
void SettlersApp::pushMouseMotion(float x, float y) {
    SDL_Event ev{};
    ev.type = SDL_EVENT_MOUSE_MOTION;
    ev.motion.x = x;
    ev.motion.y = y;
    SDL_PushEvent(&ev);
}

// Wstrzykuje naciśnięcie/puszczenie przycisku myszy.
void SettlersApp::pushMouseButton(Uint8 button, bool down) {
    SDL_Event ev{};
    ev.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    ev.button.button = button;
    SDL_PushEvent(&ev);
}

// Wstrzykuje naciśnięcie/puszczenie klawisza.
void SettlersApp::pushKey(SDL_Scancode sc, bool down) {
    SDL_Event ev{};
    ev.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    ev.key.scancode = sc;
    SDL_PushEvent(&ev);
}

// Zapisuje wynik sprawdzenia w logu i zlicza błędy.
void SettlersApp::uiCheck(const char* name, bool ok) {
    LOG_INFO("[uitest] {}: {}", name, ok ? "OK" : "BŁĄD");
    if (!ok) ++uiFailures_;
}

// Tłum wojska na drodze budowniczych, plac bez dojścia, budynek postawiony na jednostkach.
void SettlersApp::crowdAndAccessTest() {
    World& w = *world_;
    w.aiEnabled = false;
    w.debugSetStock(kPlayer, Resources::filled(150));
    const int castle = w.factions[kPlayer].castle;
    const Vec3 door = w.buildings[castle].door();

    // 1) Plac budowy ~45 jednostek przed bramą, a między nimi zwarty blok 20 żołnierzy
    Vec3 site{};
    bool found = false;
    for (int i = 0; i < 200 && !found; ++i) {
        const float a = i * 0.31f;
        const Vec3 p = w.snapToGrid(add(door, {std::sin(a) * 45.0f, 0.0f, std::cos(a) * 45.0f}));
        if (w.canPlace(BuildingType::House, p, kPlayer)) {
            site = p;
            found = true;
        }
    }
    uiCheck("znaleziono miejsce na test tłumu", found);
    if (found) {
        const Vec3 mid = lerp(door, site, 0.55f);
        std::vector<int> soldiers;
        for (int i = 0; i < 20; ++i) {
            soldiers.push_back(w.debugSpawnUnit(UnitType::Swordsman, kPlayer,
                                                {mid.x + (i % 5 - 2) * 3.5f, 0.0f, mid.z + (i / 5 - 2) * 3.5f}));
        }
        const int b = w.placeBuilding(BuildingType::House, site, kPlayer, false);
        w.autoConnectFlag(w.buildings[b].flag);   // tragarze donoszą materiały drogą
        float t = 0.0f;
        while (t < 180.0f && w.buildings[b].alive && !w.buildings[b].constructed) {
            w.update(0.1f);
            t += 0.1f;
        }
        LOG_INFO("[uitest] dom za tłumem wojska ukończony po {:.0f} s", t);
        uiCheck("budowniczowie i tragarze przechodzą przez tłum wojska", w.buildings[b].constructed && t < 120.0f);
    }

    // 2) Teren bez dojścia z zamku: szukamy przechodniej komórki w innym obszarze niż brama zamku
    const auto& nav = w.nav();
    const int castleRegion = nav.regionAt(door.x, door.z);
    Vec3 isolated{};
    bool haveIsolated = false;
    for (int cz = 10; cz < nav.height() - 10 && !haveIsolated; cz += 2) {
        for (int cx = 10; cx < nav.width() - 10 && !haveIsolated; cx += 2) {
            const Vec3 c = nav.cellCenter(cx, cz);
            const int r = nav.regionAt(c.x, c.z);
            if (r < 0 || r == castleRegion) continue;
            // obszar musi pomieścić dom z wejściem
            if (!nav.isAreaFree(c.x - 9, c.z - 9, c.x + 9, c.z + 13) || nav.regionAt(c.x, c.z + 10) != r) continue;
            isolated = w.snapToGrid(c);
            haveIsolated = true;
        }
    }
    if (!haveIsolated) {
        LOG_INFO("[uitest] na tej mapie nie ma odciętych obszarów - test dojścia pominięty");
    } else {
        std::string reason;
        // canPlace odrzuca też "poza terytorium" - sprawdzamy sam warunek dojścia
        const bool sameRegion = nav.sameRegion(door, add(isolated, {0.0f, 0.0f, 10.0f}));
        uiCheck("obszar odcięty od zamku wykryty", !sameRegion);
        const int b = w.placeBuilding(BuildingType::House, isolated, kPlayer, false);
        for (int i = 0; i < 400 && !w.buildings[b].unreachable; ++i) w.update(0.1f);
        uiCheck("plac budowy bez dojścia jest oznaczony", w.buildings[b].unreachable);
        w.demolish(b);
    }
    // canPlace z powodem "Brak dojścia" (dla obszaru odciętego w zasięgu terytorium)
    {
        std::string reason;
        bool sawAccess = false;
        const Vec3 c = w.buildings[castle].pos;
        for (int i = 0; i < 4000 && !sawAccess; ++i) {
            const float a = i * 0.0731f, r = 40.0f + (i % 37) * 4.0f;
            const Vec3 p = w.snapToGrid(add(c, {std::cos(a) * r, 0.0f, std::sin(a) * r}));
            if (!w.canPlace(BuildingType::House, p, kPlayer, &reason)) sawAccess = sawAccess || reason.find("dojścia") != std::string::npos;
        }
        LOG_INFO("[uitest] canPlace zgłosił 'Brak dojścia' w terytorium: {}", sawAccess ? "tak" : "nie (brak odciętych miejsc w zasięgu)");
    }

    // 3) Budynek postawiony na stojącej jednostce - jednostka wychodzi na zewnątrz
    Vec3 spot{};
    bool ok = false;
    for (int i = 0; i < 300 && !ok; ++i) {
        const float a = i * 0.53f, r = 50.0f + (i % 9) * 8.0f;
        const Vec3 p = w.snapToGrid(add(w.buildings[castle].pos, {std::cos(a) * r, 0.0f, std::sin(a) * r}));
        if (w.canPlace(BuildingType::Barracks, p, kPlayer)) {
            spot = p;
            ok = true;
        }
    }
    if (ok) {
        const int u = w.debugSpawnUnit(UnitType::Archer, kPlayer, spot);
        w.placeBuilding(BuildingType::Barracks, spot, kPlayer, true);
        for (int i = 0; i < 40; ++i) w.update(0.1f);
        uiCheck("jednostka wychodzi z budynku postawionego na niej", w.nav().isWalkableWorld(w.units[u].pos.x, w.units[u].pos.z));
    }
}

// Naprawa budynku, leczenie jednostki i sygnalizacja braku surowca.
void SettlersApp::repairHealResourceTest() {
    world_->debugSetStock(kPlayer, Resources::filled(100));   // deski na naprawę
    World& w = *world_;
    // Naprawa: uszkodzony dom
    int house = -1;
    for (size_t i = 0; i < w.buildings.size(); ++i) {
        const Building& b = w.buildings[i];
        if (b.alive && b.faction == kPlayer && b.constructed && b.type == BuildingType::House) house = static_cast<int>(i);
    }
    int soldier = -1;
    for (size_t i = 0; i < w.units.size(); ++i) {
        if (w.units[i].alive && w.units[i].faction == kPlayer && w.units[i].type == UnitType::Swordsman) soldier = static_cast<int>(i);
    }
    const float houseMax = buildingDef(BuildingType::House).maxHp;
    w.buildings[house].hp = houseMax * 0.35f;
    w.buildings[house].lastDamagedTime = w.gameTime;
    w.units[soldier].hp = 20.0f;
    w.units[soldier].lastDamagedTime = w.gameTime;
    const int woodBefore = w.factions[kPlayer].res[CarryType::Planks];
    bool sawRepairer = false;
    for (int i = 0; i < 1500; ++i) {
        w.update(0.1f);
        sawRepairer = sawRepairer || w.repairersOf(house) > 0;
    }
    LOG_INFO("[uitest] dom: {:.0f}/{:.0f} PŻ, zużyte deski: {}, żołnierz: {:.0f} PŻ", w.buildings[house].hp, houseMax,
             woodBefore - w.factions[kPlayer].res[CarryType::Planks], w.units[soldier].hp);
    uiCheck("osadnicy naprawiają uszkodzony budynek", sawRepairer && w.buildings[house].hp >= houseMax * 0.99f);
    uiCheck("jednostka poza walką się leczy", w.units[soldier].hp >= unitDef(UnitType::Swordsman).maxHp * 0.99f);

    // Brak surowca: drwal bez drzew w zasięgu
    int woodcutter = -1;
    for (size_t i = 0; i < w.buildings.size(); ++i) {
        const Building& b = w.buildings[i];
        if (b.alive && b.faction == kPlayer && b.constructed && b.type == BuildingType::Woodcutter) woodcutter = static_cast<int>(i);
    }
    for (auto& n : w.nodes) {
        if (n.kind == NodeKind::Tree) n.amount = 0;
    }
    bool msg = false;
    for (int i = 0; i < 600; ++i) {
        w.update(0.1f);
        for (const auto& m : w.messages) msg = msg || m.text.find("BRAK DRZEW") != std::string::npos;
    }
    uiCheck("drwal sygnalizuje brak drzew", woodcutter >= 0 && w.buildings[woodcutter].lacksResources);
    uiCheck("komunikat o braku drzew", msg);
}

// SETTLERS_SHOWCASE=1: nowa gra z gotowymi budynkami łańcuchów produkcji przy bazie (do oglądania modeli
// i pracy osadników); kamera nad środkiem ciężkości postawionych budynków.
void SettlersApp::showcaseBuildings() {
    World& w = *world_;
    const Vec3 castlePos = w.buildings[w.factions[kPlayer].castle].pos;
    Vec3 sum{};
    int count = 0;
    const BuildingType types[] = {BuildingType::Sawmill, BuildingType::Mill,  BuildingType::Bakery, BuildingType::Smelter,
                                  BuildingType::Smithy,  BuildingType::Hunter, BuildingType::Fisher, BuildingType::Forester,
                                  BuildingType::Mine,    BuildingType::CoalMine, BuildingType::OreMine};
    for (BuildingType type : types) {
        bool placed = false;
        for (float r = 35.0f; r < 175.0f && !placed; r += 8.0f) {
            for (int k = 0; k < 48 && !placed; ++k) {
                const float a = static_cast<float>(k) * 2.0f * PI / 48.0f;
                const Vec3 p = w.snapToGrid(add(castlePos, {std::cos(a) * r, 0.0f, std::sin(a) * r}));
                if (w.canPlace(type, p, kPlayer)) {
                    const int id = w.placeBuilding(type, p, kPlayer, true);
                    w.autoConnectFlag(w.buildings[id].flag);
                    if (r < 120.0f) {
                        sum = add(sum, p);
                        ++count;
                    }
                    placed = true;
                }
            }
        }
    }
    cam_.setFocus(count > 0 ? multiply(sum, 1.0f / static_cast<float>(count)) : castlePos);
    cam_.setDistance(160.0f);
}

// Góry, kopalnia i leśniczy:
//  1. ośnieżone szczyty są nieprzechodnie, a zbocza poniżej linii śniegu w większości dostępne,
//  2. kopalnia w górach (w terytorium gracza) daje kamień bez skał na powierzchni,
//  3. leśniczy sadzi drzewa, które po kTreeGrowSeconds wyrastają.
void SettlersApp::mountainBuildingsTest() {
    World& w = *world_;
    w.aiEnabled = false;
    w.debugSetStock(kPlayer, Resources::filled(150));
    const auto& nav = w.nav();

    // 1. Śnieg i zbocza
    int snowCells = 0, snowWalkable = 0, slopeCells = 0, slopeWalkable = 0;
    for (int cz = 0; cz < nav.height(); ++cz) {
        for (int cx = 0; cx < nav.width(); ++cx) {
            const Vec3 c = nav.cellCenter(cx, cz);
            const float h = w.heightAt(c.x, c.z);
            if (h > kSnowLine) {
                ++snowCells;
                if (nav.isWalkable(cx, cz)) ++snowWalkable;
            } else if (h > kMountainLevel) {
                ++slopeCells;
                if (!nav.isStaticBlocked(cx, cz)) ++slopeWalkable;
            }
        }
    }
    LOG_INFO("[uitest] teren: śnieg {} komórek (dostępnych {}), góry poniżej śniegu {} komórek (dostępnych {}%)", snowCells,
             snowWalkable, slopeCells, slopeCells > 0 ? slopeWalkable * 100 / slopeCells : 0);
    uiCheck("ośnieżone szczyty są niedostępne", snowWalkable == 0);
    uiCheck("zbocza gór w większości dostępne", slopeCells > 0 && slopeWalkable * 100 / slopeCells >= 60);

    // Miejsce na budynek w terytorium gracza (pierścienie wokół zamku)
    const Vec3 castlePos = w.buildings[w.factions[kPlayer].castle].pos;
    auto findSpot = [&](BuildingType type, Vec3& out) {
        for (float r = 40.0f; r < 175.0f; r += 8.0f) {
            for (int k = 0; k < 48; ++k) {
                const float a = static_cast<float>(k) * 2.0f * PI / 48.0f;
                const Vec3 p = w.snapToGrid(add(castlePos, {std::cos(a) * r, 0.0f, std::sin(a) * r}));
                if (w.canPlace(type, p, kPlayer)) {
                    out = p;
                    return true;
                }
            }
        }
        return false;
    };

    // 2. Kopalnia
    Vec3 mineSpot;
    const bool haveMine = findSpot(BuildingType::Mine, mineSpot);
    uiCheck("jest miejsce na kopalnię w górach przy bazie", haveMine);
    std::string reason;
    w.canPlace(BuildingType::Mine, add(castlePos, {0.0f, 0.0f, 40.0f}), kPlayer, &reason);   // nizina przy zamku
    LOG_INFO("[uitest] kopalnia na nizinie: \"{}\"", reason);
    if (haveMine) {
        const int mine = w.placeBuilding(BuildingType::Mine, mineSpot, kPlayer, true);
        w.autoConnectFlag(w.buildings[mine].flag);   // górnik potrzebuje żywności dowiezionej drogą
        for (int i = 0; i < 2400 && w.buildings[mine].produced < 2; ++i) w.update(0.1f);
        LOG_INFO("[uitest] kopalnia: wysokość {:.0f}, dostaw kamienia: {}", w.buildings[mine].pos.y, w.buildings[mine].produced);
        uiCheck("kopalnia wydobywa kamień", w.buildings[mine].produced >= 2);
    }

    // 3. Leśniczy
    Vec3 forestSpot;
    if (findSpot(BuildingType::Forester, forestSpot)) {
        const int forester = w.placeBuilding(BuildingType::Forester, forestSpot, kPlayer, true);
        const float until = w.gameTime + 60.0f + kTreeGrowSeconds + 10.0f;
        while (w.gameTime < until) w.update(0.1f);
        int growing = 0;
        for (const auto& n : w.nodes) {
            if (n.alive && n.kind == NodeKind::Tree && n.growth < 1.0f) ++growing;
        }
        const int planted = w.buildings[forester].produced;
        LOG_INFO("[uitest] leśniczy: posadzone {}, nadal rośnie {}, wyrosło {}", planted, growing, planted - growing);
        uiCheck("leśniczy sadzi drzewa", planted >= 3);
        uiCheck("posadzone drzewa wyrastają", planted - growing >= 1);
    } else {
        uiCheck("jest miejsce na leśniczego", false);
    }
}

// Przejmowanie terenu: wieża gracza postawiona tuż za granicą wroga odbiera mu pas ziemi,
// a wrogi dom na tym pasie płonie. Zburzenie wieży oddaje teren z powrotem.
void SettlersApp::territoryCaptureTest() {
    World& w = *world_;
    w.aiEnabled = false;
    const Vec3 enemyCastle = w.buildings[w.factions[kEnemy].castle].pos;
    const Vec3 playerCastle = w.buildings[w.factions[kPlayer].castle].pos;
    Vec3 dir = subtract(playerCastle, enemyCastle);
    dir.y = 0.0f;
    dir = normalize(dir);
    const float castleR = buildingDef(BuildingType::Castle).territoryRadius;
    const Vec3 housePos = w.snapToGrid(add(enemyCastle, multiply(dir, castleR - 30.0f)));
    const Vec3 towerPos = w.snapToGrid(add(enemyCastle, multiply(dir, castleR + 20.0f)));

    const int house = w.placeBuilding(BuildingType::House, housePos, kEnemy, true);
    w.update(0.1f);
    uiCheck("dom wroga stoi na jego terenie", w.buildings[house].alive && w.territoryOwnerAt(housePos) == kEnemy);
    const int ownerBefore = w.territoryOwnerAt(towerPos);

    const int tower = w.placeBuilding(BuildingType::Tower, towerPos, kPlayer, true);
    w.update(0.1f);
    LOG_INFO("[uitest] przejęcie terenu: właściciel pola wieży przed {} / po {}, dom wroga {}", ownerBefore,
             w.territoryOwnerAt(towerPos), w.buildings[house].alive ? "stoi" : "spłonął");
    uiCheck("wieża przejmuje pas terenu wroga", w.territoryOwnerAt(housePos) == kPlayer);
    uiCheck("wrogi dom na przejętym terenie płonie", !w.buildings[house].alive);
    uiCheck("zamek wroga zostaje na jego terenie", w.territoryOwnerAt(enemyCastle) == kEnemy);
    // Granica w środku nakładania się zasięgów: punkt w połowie drogi wieża-zamek należy do kogoś (brak ziemi niczyjej).
    uiCheck("brak ziemi niczyjej między wieżą a zamkiem wroga",
            w.territoryOwnerAt(multiply(add(towerPos, enemyCastle), 0.5f)) >= 0);

    w.demolish(tower);
    w.update(0.1f);
    uiCheck("po zburzeniu wieży teren wraca do wroga", w.territoryOwnerAt(housePos) == kEnemy);
}

// Szturm wieży: wrogi żołnierz obsadza wieżę stojącą między zamkami; trzech mieczników gracza atakuje -
// obrońca wychodzi z wieży, ginie, a pierwszy napastnik przy drzwiach przejmuje wieżę razem z terenem.
void SettlersApp::towerAssaultTest() {
    World& w = *world_;
    w.aiEnabled = false;
    for (size_t i = 0; i < w.units.size(); ++i) {   // tylko nasz obrońca po stronie wroga
        if (w.units[i].alive && w.units[i].faction == kEnemy && w.units[i].type != UnitType::Settler) {
            w.debugKillUnit(static_cast<int>(i));
        }
    }
    const Vec3 pc = w.buildings[w.factions[kPlayer].castle].pos;
    const Vec3 ec = w.buildings[w.factions[kEnemy].castle].pos;
    Vec3 dir = subtract(ec, pc);
    dir.y = 0.0f;
    dir = normalize(dir);
    Vec3 spot{};
    bool found = false;
    // Szukamy w środkowej części odcinka zamek-zamek (mapy są losowe, więc zakres względem ich odległości),
    // coraz dalej w bok od prostej łączącej zamki.
    const float span = distanceXZ(pc, ec);
    for (int k = 0; k <= 16 && !found; ++k) {
        const int side = (k % 2 == 0) ? k / 2 : -(k + 1) / 2;   // 0, -1, 1, -2, 2...
        for (float d = span * 0.3f; d < span * 0.7f && !found; d += 12.0f) {
            const int kk = side;
            const Vec3 p = w.snapToGrid(add(add(pc, multiply(dir, d)), multiply(Vec3{-dir.z, 0.0f, dir.x}, kk * 15.0f)));
            if (w.nav().isAreaFree(p.x - 9, p.z - 9, p.x + 9, p.z + 9) && w.nav().sameRegion(p, w.buildings[w.factions[kPlayer].castle].door()) &&
                w.heightAt(p.x, p.z) > kWaterLevel + 3.0f && w.heightAt(p.x, p.z) < kMountainLevel) {
                spot = p;
                found = true;
            }
        }
    }
    if (!found) {
        LOG_INFO("[uitest] szturm: brak miejsca na wieżę (zamki {:.0f} j. od siebie) - test pominięty", span);
        return;
    }
    const int tower = w.placeBuilding(BuildingType::Tower, spot, kEnemy, true);
    const int defender = w.debugSpawnUnit(UnitType::Swordsman, kEnemy, w.buildings[tower].door());
    w.commandGarrison({defender}, tower);
    for (int i = 0; i < 300 && w.garrisonOf(tower) == 0; ++i) w.update(0.1f);
    uiCheck("żołnierz obsadza wieżę", w.garrisonOf(tower) == 1);

    std::vector<int> attackers;
    const Vec3 start = add(spot, multiply(dir, -45.0f));
    for (int k = 0; k < 3; ++k) attackers.push_back(w.debugSpawnUnit(UnitType::Swordsman, kPlayer, add(start, {k * 4.0f, 0.0f, 0.0f})));
    w.commandAttackBuilding(attackers, tower);
    bool sally = false;
    float t = 0.0f;
    while (t < 150.0f && w.buildings[tower].faction != kPlayer) {
        w.update(0.1f);
        t += 0.1f;
        sally = sally || (w.units[defender].alive && w.units[defender].state == UnitState::Attack);
    }
    LOG_INFO("[uitest] szturm: po {:.0f} s właściciel wieży {}, załoga {}, obrońca {}, PŻ wieży {:.0f}", t,
             factionName(w.buildings[tower].faction), w.garrisonOf(tower), w.units[defender].alive ? "żyje" : "poległ",
             w.buildings[tower].hp);
    uiCheck("obrońca wychodzi z wieży do walki", sally);
    uiCheck("wieża zdobyta szturmem (nie zburzona)", w.buildings[tower].alive && w.buildings[tower].faction == kPlayer);
    uiCheck("zdobywca wchodzi do załogi", w.garrisonOf(tower) >= 1);
    for (int i = 0; i < 5; ++i) w.update(0.1f);
    uiCheck("teren wokół zdobytej wieży należy do gracza", w.territoryOwnerAt(spot) == kPlayer);
}

// Transport towarów (wspólna pula tragarzy):
//  1. drwal bez drogi zapełnia flagę (kFlagCapacity) i wstrzymuje produkcję,
//  2. droga do zamku (autoConnectFlag) - tragarze niosą drewno całą trasą do zamku,
//  3. ręczna liczba tragarzy jest utrzymywana,
//  4. dostawy: materiały na plac budowy (dom zostaje ukończony) i koszt szkolenia do koszar,
//  5. pełny zamek - towary trafiają do Magazynu; zamek nie przekracza pojemności.
void SettlersApp::economyRoadTest() {
    World& w = *world_;
    w.aiEnabled = false;
    w.debugSetStock(kPlayer, Resources{{CarryType::Planks, 40}, {CarryType::Stone, 40}, {CarryType::Bread, 40}});
    const int castle = w.factions[kPlayer].castle;
    const Vec3 c = w.buildings[castle].pos;
    for (int i = 0; i < 8; ++i) w.debugSpawnUnit(UnitType::Settler, kPlayer, w.buildings[castle].door());
    FactionState& fs = w.factions[kPlayer];

    auto findSpot = [&](BuildingType type, float minR, float maxR, Vec3& out) {
        for (float r = minR; r < maxR; r += 6.0f) {
            for (int k = 0; k < 40; ++k) {
                const float a = static_cast<float>(k) * 2.0f * PI / 40.0f + r * 0.01f;
                const Vec3 p = w.snapToGrid(add(c, {std::cos(a) * r, 0.0f, std::sin(a) * r}));
                if (w.canPlace(type, p, kPlayer)) {
                    out = p;
                    return true;
                }
            }
        }
        return false;
    };
    auto carriers = [&]() {
        int n = 0;
        for (const auto& u : w.units) {
            if (u.alive && u.faction == kPlayer &&
                (u.state == UnitState::GoToRoad || u.state == UnitState::CarrierWait || u.state == UnitState::CarrierWalk)) ++n;
        }
        return n;
    };
    Vec3 spot;
    if (!findSpot(BuildingType::Woodcutter, 75.0f, 150.0f, spot)) {
        uiCheck("jest miejsce na drwala do testu transportu", false);
        return;
    }
    const int wc = w.placeBuilding(BuildingType::Woodcutter, spot, kPlayer, true);
    const int wcFlag = w.buildings[wc].flag;

    // 1. Bez drogi
    for (int i = 0; i < 6000 && !w.buildings[wc].outputBlocked; ++i) w.update(0.1f);
    LOG_INFO("[uitest] drwal bez drogi: na fladze {} towarów, wstrzymany: {}", w.flags[wcFlag].goods.size(),
             w.buildings[wc].outputBlocked ? "tak" : "nie");
    uiCheck("warsztat bez drogi zapełnia flagę i wstrzymuje produkcję",
            w.buildings[wc].outputBlocked && w.flags[wcFlag].goods.size() + kGoodsPerTrip > static_cast<size_t>(kFlagCapacity));

    // 2. Droga do zamku. Zamek opróżniamy - warsztaty z wcześniejszych testów też dowożą do niego towary.
    //    Limit zapasu drewna wyłączony (test transportu, limity sprawdza storageJamTest).
    fs.stockLimit[static_cast<int>(CarryType::Wood)] = kNoStockLimit;
    w.debugSetStock(kPlayer, Resources{});
    const int producedBefore = w.buildings[wc].produced;
    std::string reason;
    const bool connected = w.autoConnectFlag(wcFlag, &reason);
    LOG_INFO("[uitest] droga drwal-zamek: {} {}", connected ? "zbudowana" : "BRAK", reason);
    uiCheck("autoConnectFlag łączy warsztat drogą z zamkiem", connected);
    for (int i = 0; i < 2400; ++i) w.update(0.1f);
    const int delivered = w.buildings[castle].stock[CarryType::Wood];
    const int produced = w.buildings[wc].produced - producedBefore;
    LOG_INFO("[uitest] po 240 s: drewno w zamku {}, drwal wznowił produkcję: {} kursów, tragarzy {}", delivered, produced,
             carriers());
    uiCheck("tragarze niosą drewno całą trasą do zamku", delivered >= 10);
    uiCheck("odbierane towary wznawiają produkcję warsztatu", produced >= 3);

    // 3. Ręczna liczba tragarzy
    fs.carrierAuto = false;
    fs.carrierTarget = 3;
    for (int i = 0; i < 300; ++i) w.update(0.1f);
    LOG_INFO("[uitest] ręcznie 3 tragarzy -> jest {}", carriers());
    uiCheck("liczba tragarzy ustawiona ręcznie jest utrzymywana", carriers() == 3);
    fs.carrierAuto = true;

    // 4a. Dostawa materiałów na plac budowy
    w.debugSetStock(kPlayer, Resources{{CarryType::Planks, 60}, {CarryType::Stone, 40}, {CarryType::Bread, 10}});
    Vec3 houseSpot;
    if (findSpot(BuildingType::House, 40.0f, 120.0f, houseSpot)) {
        const int house = w.placeBuilding(BuildingType::House, houseSpot, kPlayer, false);
        w.autoConnectFlag(w.buildings[house].flag);
        int maxDelivered = 0;
        for (int i = 0; i < 3000 && !w.buildings[house].constructed; ++i) {
            w.update(0.1f);
            if (w.buildings[house].alive) maxDelivered = std::max(maxDelivered, w.buildings[house].delivered.total());
        }
        LOG_INFO("[uitest] dom: dowiezione materiały (max) {}/{}, ukończony: {}", maxDelivered,
                 buildingDef(BuildingType::House).cost.total(), w.buildings[house].constructed ? "tak" : "nie");
        uiCheck("tragarze dowożą materiały i dom zostaje ukończony", w.buildings[house].constructed);
    } else {
        uiCheck("jest miejsce na dom do testu dostaw", false);
    }

    // 4b. Koszt szkolenia dowożony do koszar
    Vec3 barracksSpot;
    if (findSpot(BuildingType::Barracks, 40.0f, 140.0f, barracksSpot)) {
        const int bar = w.placeBuilding(BuildingType::Barracks, barracksSpot, kPlayer, true);
        for (int k = 0; k < 4; ++k) {   // miejsce w limicie ludności na nowego żołnierza
            Vec3 hs;
            if (findSpot(BuildingType::House, 30.0f, 160.0f, hs)) w.placeBuilding(BuildingType::House, hs, kPlayer, true);
        }
        w.debugSetStock(kPlayer, Resources{{CarryType::Planks, 60}, {CarryType::Stone, 40}, {CarryType::Bread, 30}, {CarryType::Fish, 30}, {CarryType::Weapons, 10}});
        for (size_t i = 0; i < w.units.size(); ++i) {   // żołnierze z wcześniejszych testów zajmują limit ludności
            if (w.units[i].alive && w.units[i].faction == kPlayer && w.units[i].type != UnitType::Settler) w.debugKillUnit(static_cast<int>(i));
        }
        w.update(0.1f);
        w.autoConnectFlag(w.buildings[bar].flag);
        int soldiersBefore = 0;
        for (const auto& u : w.units) soldiersBefore += (u.alive && u.faction == kPlayer && u.type == UnitType::Swordsman) ? 1 : 0;
        std::string why;
        const bool queued = w.queueTraining(bar, UnitType::Swordsman, &why);
        int soldiersAfter = soldiersBefore;
        for (int i = 0; i < 2400 && soldiersAfter == soldiersBefore; ++i) {
            w.update(0.1f);
            soldiersAfter = 0;
            for (const auto& u : w.units) soldiersAfter += (u.alive && u.faction == kPlayer && u.type == UnitType::Swordsman) ? 1 : 0;
        }
        LOG_INFO("[uitest] koszary: kolejka {} ({}), wyszkolono: {}, ludność {}/{}", queued ? "tak" : "nie", why,
                 soldiersAfter - soldiersBefore, w.factions[kPlayer].population, w.factions[kPlayer].popCap);
        uiCheck("koszt szkolenia trafia do koszar i żołnierz zostaje wyszkolony", queued && soldiersAfter > soldiersBefore);
    }

    // 5. Pełny zamek -> Magazyn
    w.debugSetStock(kPlayer, Resources{{CarryType::Stone, storageCapacity(BuildingType::Castle)}});
    Vec3 storeSpot;
    if (!findSpot(BuildingType::Warehouse, 40.0f, 140.0f, storeSpot)) {
        uiCheck("jest miejsce na magazyn", false);
        return;
    }
    const int store = w.placeBuilding(BuildingType::Warehouse, storeSpot, kPlayer, true);
    const bool storeConnected = w.autoConnectFlag(w.buildings[store].flag, &reason);
    for (int i = 0; i < 2400; ++i) w.update(0.1f);
    LOG_INFO("[uitest] magazyn: połączony {}, zapas {} / zamek {}", storeConnected ? "tak" : "nie",
             w.buildings[store].stock.total(), w.buildings[castle].stock.total());
    uiCheck("przy pełnym zamku towary trafiają do Magazynu", storeConnected && w.buildings[store].stock[CarryType::Wood] > 0);
    uiCheck("zamek nie przekracza pojemności", w.buildings[castle].stock.total() <= storageCapacity(BuildingType::Castle));
    uiCheck("surowce gracza = suma magazynów", w.factions[kPlayer].res.total() == w.storageUsedOf(kPlayer));
    fs.stockLimit = defaultStockLimits();
}

// Zapchane magazyny (zgłoszenie gracza: magazyny pełne kamienia, deski tylko na fladze tartaku, nic nie da się
// zbudować):
//  1. dostawy biorą towar prosto z flagi warsztatu - dom zostaje ukończony mimo pełnych magazynów,
//  2. towary na flagach liczą się do "stać nas" (pasek budowy),
//  3. limit zapasu: towaru powyżej limitu tragarze nie wnoszą do magazynu (zostaje na fladze, przyczyna "limit"),
//  4. przyciski limitu: krok 10, powyżej 200 - bez limitu.
void SettlersApp::storageJamTest() {
    World& w = *world_;
    w.aiEnabled = false;
    FactionState& fs = w.factions[kPlayer];
    const Vec3 c = w.buildings[fs.castle].pos;
    for (int i = 0; i < 6; ++i) w.debugSpawnUnit(UnitType::Settler, kPlayer, w.buildings[fs.castle].door());
    auto findSpot = [&](BuildingType type, Vec3& out) {
        for (float r = 40.0f; r < 170.0f; r += 6.0f) {
            for (int k = 0; k < 40; ++k) {
                const float a = static_cast<float>(k) * 2.0f * PI / 40.0f + r * 0.013f;
                const Vec3 p = w.snapToGrid(add(c, {std::cos(a) * r, 0.0f, std::sin(a) * r}));
                if (w.canPlace(type, p, kPlayer)) {
                    out = p;
                    return true;
                }
            }
        }
        return false;
    };
    // Wszystkie magazyny gracza pełne kamienia.
    auto fillStores = [&]() {
        for (auto& b : w.buildings) {
            if (b.alive && b.faction == kPlayer && storageCapacity(b.type) > 0) b.stock = Resources{{CarryType::Stone, storageCapacity(b.type)}};
        }
        w.refreshFactionResources();
    };
    fillStores();

    Vec3 sawSpot, houseSpot;
    if (!findSpot(BuildingType::Sawmill, sawSpot)) {
        uiCheck("jest miejsce na tartak do testu pełnych magazynów", false);
        return;
    }
    const int saw = w.placeBuilding(BuildingType::Sawmill, sawSpot, kPlayer, true);
    w.autoConnectFlag(w.buildings[saw].flag);
    const int planksNeeded = buildingDef(BuildingType::House).cost[CarryType::Planks];
    Flag& sawFlag = w.flags[w.buildings[saw].flag];
    for (int k = 0; k < planksNeeded; ++k) sawFlag.goods.push_back(CarryType::Planks);
    w.update(0.1f);
    uiCheck("deski na fladze liczą się do dostępnych towarów", w.spendable(kPlayer)[CarryType::Planks] >= planksNeeded &&
                                                                    w.factions[kPlayer].res[CarryType::Planks] == 0);
    uiCheck("przyczyna czekania desek: magazyny pełne", w.flagBlockReason(w.buildings[saw].flag) == World::FlagBlock::StorageFull);

    if (findSpot(BuildingType::House, houseSpot)) {
        const int house = w.placeBuilding(BuildingType::House, houseSpot, kPlayer, false);
        w.autoConnectFlag(w.buildings[house].flag);
        for (int i = 0; i < 3000 && !w.buildings[house].constructed; ++i) w.update(0.1f);
        LOG_INFO("[uitest] pełne magazyny: dom {}, deski na fladze tartaku {}, magazyny {}/{}",
                 w.buildings[house].constructed ? "ukończony" : "NIE ukończony", w.flags[w.buildings[saw].flag].goods.size(),
                 w.storageUsedOf(kPlayer), w.storageCapacityOf(kPlayer));
        uiCheck("przy pełnych magazynach deski idą z flagi tartaku prosto na budowę", w.buildings[house].constructed);
    } else {
        uiCheck("jest miejsce na dom do testu pełnych magazynów", false);
    }

    // 3. Limit zapasu: puste magazyny, limit desek 0 -> deski zostają na fladze.
    for (auto& b : w.buildings) {
        if (b.alive && b.faction == kPlayer && storageCapacity(b.type) > 0) b.stock = Resources{};
    }
    w.refreshFactionResources();
    fs.stockLimit[static_cast<int>(CarryType::Planks)] = 0;
    // Tragarze, którzy już nieśli deski do magazynu (przydział sprzed limitu), kończą kurs - potem zerujemy deski.
    for (int i = 0; i < 300; ++i) w.update(0.1f);
    for (auto& b : w.buildings) {
        if (b.alive && b.faction == kPlayer && storageCapacity(b.type) > 0) b.stock[CarryType::Planks] = 0;
    }
    w.refreshFactionResources();
    Flag& sf = w.flags[w.buildings[saw].flag];
    sf.goods.clear();
    for (int k = 0; k < 4; ++k) sf.goods.push_back(CarryType::Planks);   // tartak może dorobić więcej (drewno z flag drwali)
    for (int i = 0; i < 600; ++i) w.update(0.1f);
    const int planksLeft = static_cast<int>(std::count(w.flags[w.buildings[saw].flag].goods.begin(),
                                                       w.flags[w.buildings[saw].flag].goods.end(), CarryType::Planks));
    LOG_INFO("[uitest] limit desek 0: na fladze {}, w magazynach {}", planksLeft, w.factions[kPlayer].res[CarryType::Planks]);
    // Deski mogą zabrać odbiorcy (dostawy z flagi) - ważne, że żadna nie trafiła do magazynu.
    uiCheck("towar powyżej limitu nie trafia do magazynu", w.factions[kPlayer].res[CarryType::Planks] == 0);
    uiCheck("przyczyna czekania: limit zapasu",
            planksLeft == 0 || w.flagBlockReason(w.buildings[saw].flag) == World::FlagBlock::StockLimit);
    fs.stockLimit[static_cast<int>(CarryType::Planks)] = 10;
    for (int i = 0; i < 900; ++i) w.update(0.1f);
    LOG_INFO("[uitest] limit desek 10: w magazynach {}", w.factions[kPlayer].res[CarryType::Planks]);
    uiCheck("po podniesieniu limitu tragarze zanoszą towar do magazynu", w.factions[kPlayer].res[CarryType::Planks] >= std::min(planksLeft, 4));

    // 4. Przyciski limitu
    fs.stockLimit[static_cast<int>(CarryType::Planks)] = kNoStockLimit;
    changeStockLimit(CarryType::Planks, -1);
    const int a = fs.stockLimit[static_cast<int>(CarryType::Planks)];
    changeStockLimit(CarryType::Planks, -1);
    const int b = fs.stockLimit[static_cast<int>(CarryType::Planks)];
    changeStockLimit(CarryType::Planks, +1);
    changeStockLimit(CarryType::Planks, +1);
    uiCheck("przyciski limitu: bez -> 200 -> 190, 200 + 10 -> bez",
            a == kStockLimitMax && b == kStockLimitMax - kStockLimitStep && fs.stockLimit[static_cast<int>(CarryType::Planks)] == kNoStockLimit);
    fs.stockLimit = defaultStockLimits();
}

// Rekrutacja automatyczna koszar (zgłoszenie gracza: dużo broni, a nie da się wyklikać szkolenia):
//  1. okno koszar widoczne po zaznaczeniu, nad oknem akcji, oba nad paskiem budowy (nic się nie zasłania),
//  2. auto 50%: kolejka uzupełnia się sama, powstają miecznicy i łucznicy,
//  3. auto 100%: same miecznicy.
void SettlersApp::autoRecruitTest() {
    World& w = *world_;
    w.aiEnabled = false;
    FactionState& fs = w.factions[kPlayer];
    const Vec3 c = w.buildings[fs.castle].pos;
    int bar = -1;
    for (float r = 45.0f; r < 230.0f && bar < 0; r += 5.0f) {   // mapy są losowe - szukamy szeroko
        for (int k = 0; k < 64 && bar < 0; ++k) {
            const float a = static_cast<float>(k) * 2.0f * PI / 64.0f + r * 0.01f;
            const Vec3 p = w.snapToGrid(add(c, {std::cos(a) * r, 0.0f, std::sin(a) * r}));
            if (w.canPlace(BuildingType::Barracks, p, kPlayer)) bar = w.placeBuilding(BuildingType::Barracks, p, kPlayer, true);
        }
    }
    if (bar < 0) {
        uiCheck("jest miejsce na koszary do testu rekrutacji", false);
        return;
    }
    w.autoConnectFlag(w.buildings[bar].flag);
    // Miejsce w limicie ludności: usuwamy wojsko z wcześniejszych testów i podnosimy limit domami.
    for (size_t i = 0; i < w.units.size(); ++i) {
        if (w.units[i].alive && w.units[i].faction == kPlayer && w.units[i].type != UnitType::Settler) w.debugKillUnit(static_cast<int>(i));
    }
    for (int k = 0; k < 4; ++k) {
        for (float r = 40.0f; r < 200.0f; r += 9.0f) {
            const float a = static_cast<float>(k) * 1.7f + r * 0.05f;
            const Vec3 p = w.snapToGrid(add(c, {std::cos(a) * r, 0.0f, std::sin(a) * r}));
            if (w.canPlace(BuildingType::House, p, kPlayer)) {
                w.placeBuilding(BuildingType::House, p, kPlayer, true);
                break;
            }
        }
    }
    w.debugSetStock(kPlayer, Resources{{CarryType::Weapons, 40}, {CarryType::Bread, 40}, {CarryType::Fish, 40}, {CarryType::Planks, 60}});

    // 1. Okna
    selectedBuilding_ = bar;
    updateContextWindows();
    const bool visible = barracksWindow_->isVisible() && actionsWindow_->isVisible();
    const bool stacked = barracksWindow_->y() + barracksWindow_->height() <= actionsWindow_->y();
    const bool aboveBar = !buildBarVisible() || buildBar_.x() > 14.0f + actionsWindow_->width() ||
                          actionsWindow_->y() + actionsWindow_->height() <= static_cast<int>(buildBar_.y());
    LOG_INFO("[uitest] okna koszar: widoczne {}, koszary y={}..{}, akcje y={}..{}, pasek y={:.0f}", visible ? "tak" : "nie",
             barracksWindow_->y(), barracksWindow_->y() + barracksWindow_->height(), actionsWindow_->y(),
             actionsWindow_->y() + actionsWindow_->height(), buildBar_.y());
    uiCheck("okno koszar widoczne i nie zasłonięte przez okno akcji", visible && stacked && aboveBar);

    // 2. Auto 50%
    toggleAutoTrainSelected();
    changeSwordShareSelected(-20);   // 70% -> 50%
    auto countType = [&](UnitType t) {
        int n = 0;
        for (const auto& u : w.units) n += (u.alive && u.faction == kPlayer && u.type == t) ? 1 : 0;
        return n;
    };
    for (int i = 0; i < 4000 && countType(UnitType::Swordsman) + countType(UnitType::Archer) < 4; ++i) w.update(0.1f);
    const int s1 = countType(UnitType::Swordsman), a1 = countType(UnitType::Archer);
    LOG_INFO("[uitest] auto-rekrutacja 50%: miecznicy {}, łucznicy {}, ludność {}/{}", s1, a1, fs.population, fs.popCap);
    uiCheck("auto-rekrutacja szkoli sama, obu typów wg proporcji", s1 >= 1 && a1 >= 1 && s1 + a1 >= 4);

    // 3. Auto 100% - same miecznicy
    changeSwordShareSelected(+100);
    w.buildings[bar].trainQueue.clear();
    w.buildings[bar].trainPaid = false;
    w.buildings[bar].trainTimer = 0.0f;
    for (int i = 0; i < 3000 && countType(UnitType::Swordsman) < s1 + 2; ++i) w.update(0.1f);
    LOG_INFO("[uitest] auto-rekrutacja 100%: miecznicy {} (+{}), łucznicy {}", countType(UnitType::Swordsman),
             countType(UnitType::Swordsman) - s1, countType(UnitType::Archer));
    uiCheck("auto 100% mieczników: przybywa tylko mieczników", countType(UnitType::Swordsman) >= s1 + 2 && countType(UnitType::Archer) <= a1 + 1);
    w.buildings[bar].autoTrain = false;
    selectedBuilding_ = -1;
}

// Łańcuchy produkcji: każdy warsztat dostaje drogą swoje towary wejściowe z zamku i oddaje wyrób.
// Zamek startuje z surowcami (kłody, zboże, mąka, ruda, węgiel, żelazo, żywność); po kilku minutach gry
// sprawdzamy, że każdy budynek coś wyprodukował, a kopalnia zużyła żywność.
void SettlersApp::productionChainTest() {
    World& w = *world_;
    w.aiEnabled = false;
    using C = CarryType;
    w.debugSetStock(kPlayer, Resources{{C::Wood, 20}, {C::Grain, 16}, {C::Flour, 8}, {C::Ore, 12}, {C::Coal, 20},
                                       {C::Iron, 8}, {C::Bread, 20}, {C::Fish, 10}, {C::Planks, 20}, {C::Stone, 10}});
    const int castle = w.factions[kPlayer].castle;
    const Vec3 c = w.buildings[castle].pos;
    for (int i = 0; i < 16; ++i) w.debugSpawnUnit(UnitType::Settler, kPlayer, w.buildings[castle].door());

    auto findSpot = [&](BuildingType type, Vec3& out) {
        for (float r = 30.0f; r < 175.0f; r += 6.0f) {
            for (int k = 0; k < 48; ++k) {
                const float a = static_cast<float>(k) * 2.0f * PI / 48.0f + r * 0.013f;
                const Vec3 p = w.snapToGrid(add(c, {std::cos(a) * r, 0.0f, std::sin(a) * r}));
                if (w.canPlace(type, p, kPlayer)) {
                    out = p;
                    return true;
                }
            }
        }
        return false;
    };
    const BuildingType chain[] = {BuildingType::Sawmill, BuildingType::Mill,     BuildingType::Bakery,
                                  BuildingType::Smelter, BuildingType::Smithy,   BuildingType::CoalMine,
                                  BuildingType::Hunter,  BuildingType::Fisher};
    std::vector<std::pair<BuildingType, int>> placed;
    for (BuildingType t : chain) {
        Vec3 p;
        if (!findSpot(t, p)) {
            LOG_INFO("[uitest] łańcuch: brak miejsca na {} - pominięty", buildingDef(t).name);
            continue;
        }
        const int id = w.placeBuilding(t, p, kPlayer, true);
        std::string why;
        if (!w.autoConnectFlag(w.buildings[id].flag, &why)) LOG_INFO("[uitest] łańcuch: {} bez drogi ({})", buildingDef(t).name, why);
        placed.push_back({t, id});
    }
    const Resources before = w.factions[kPlayer].res;
    for (int i = 0; i < 3600; ++i) w.update(0.1f);
    const Resources after = w.factions[kPlayer].res;
    for (const auto& [t, id] : placed) {
        const Building& b = w.buildings[id];
        LOG_INFO("[uitest] łańcuch: {} - wyprodukował {} razy, wejścia: {}", buildingDef(t).name, b.produced,
                 stockSummary(b.delivered, 4));
        const bool required = t != BuildingType::Fisher && t != BuildingType::CoalMine;   // zależne od mapy
        if (required || b.produced > 0) {
            uiCheck((std::string(buildingDef(t).name) + " produkuje").c_str(), b.produced >= 1);
        }
    }
    LOG_INFO("[uitest] łańcuch: magazyn przed: {}", stockSummary(before, 12));
    LOG_INFO("[uitest] łańcuch: magazyn po:    {}", stockSummary(after, 12));
    uiCheck("kłody zamienione na deski", after[C::Planks] > before[C::Planks]);
    uiCheck("kuźnia dostarcza broń", after[C::Weapons] > before[C::Weapons]);
}

// Stawia po kilka placów budowy każdego typu i symuluje świat - wszystkie muszą zostać ukończone.
bool SettlersApp::constructionStressTest() {
    World& w = *world_;
    w.aiEnabled = false;
    w.debugSetStock(kPlayer, Resources::filled(1000));
    for (int i = 0; i < 12; ++i) w.debugSpawnUnit(UnitType::Settler, kPlayer, w.buildings[w.factions[kPlayer].castle].door());
    const BuildingType types[] = {BuildingType::Tower, BuildingType::House, BuildingType::Woodcutter, BuildingType::Quarry,
                                  BuildingType::Farm, BuildingType::Barracks};
    std::vector<int> sites;
    const Vec3 c = w.buildings[w.factions[kPlayer].castle].pos;
    int attempt = 0;
    for (int round = 0; round < 3; ++round) {
        for (BuildingType t : types) {
            for (int i = 0; i < 300; ++i, ++attempt) {
                const float a = attempt * 2.399f, r = 40.0f + static_cast<float>((attempt * 7) % 120);
                const Vec3 p = w.snapToGrid(add(c, {std::cos(a) * r, 0.0f, std::sin(a) * r}));
                if (w.canPlace(t, p, kPlayer)) {
                    sites.push_back(w.placeBuilding(t, p, kPlayer, false, (attempt % 4) * PI * 0.5f));
                    w.autoConnectFlag(w.buildings[sites.back()].flag);   // materiały dowożą tragarze
                    break;
                }
            }
        }
    }
    for (int step = 0; step < 12000; ++step) {
        w.update(0.1f);
        bool done = true;
        for (int id : sites) done = done && (!w.buildings[id].alive || w.buildings[id].constructed);
        if (done) break;
    }
    int unfinished = 0;
    for (int id : sites) {
        const Building& b = w.buildings[id];
        if (b.alive && !b.constructed) {
            ++unfinished;
            const TransportStats ts = w.transportStats(kPlayer);
            LOG_INFO("[uitest]   nieukończony: {} postęp={}% materiały={}/{} droga={} brakDojścia={} | tragarze {}/{} zajęci {} dostawy {} bez towaru {}",
                     buildingDef(b.type).name, static_cast<int>(b.progress * 100), b.delivered.total(),
                     buildingDef(b.type).cost.total(), w.flagConnected(b.flag), b.unreachable, ts.carriers, ts.target, ts.busy,
                     ts.ordersWaiting, ts.ordersUnfunded);
        }
    }
    LOG_INFO("[uitest] place budowy: {}, nieukończone po {} s gry: {}", sites.size(), static_cast<int>(w.gameTime), unfinished);
    return !sites.empty() && unfinished == 0;
}

// Scenariusz testu UI. Każda klatka (f = numer klatki) wykonuje jeden krok: wstrzyknięcie zdarzenia
// albo sprawdzenie jego efektu kilka klatek później (silnik potrzebuje klatki na przetworzenie wejścia).
void SettlersApp::runUiTestStep() {
    const int f = uiFrame_++;
    World& w = *world_;
    const int castle = w.factions[kPlayer].castle;
    auto project = [&](const Vec3& p, float& sx, float& sy) { return worldToScreen(*camera_, p, screenW_, screenH_, sx, sy); };

    if (f == 20) {
        // Picking: promień z ekranowej pozycji zamku musi trafić w zamek i w teren obok niego
        float sx, sy;
        const Vec3 c = add(w.buildings[castle].pos, {0.0f, 8.0f, 0.0f});
        const bool vis = project(c, sx, sy);
        const Ray ray = screenPointToRay(*camera_, sx, sy, screenW_, screenH_);
        uiCheck("worldToScreen/screenPointToRay -> pickBuilding(zamek)", vis && w.pickBuilding(ray) == castle);
        Vec3 hit;
        float gx, gy;
        const Vec3 g = add(w.buildings[castle].pos, {40.0f, 0.0f, 40.0f});
        project({g.x, w.heightAt(g.x, g.z), g.z}, gx, gy);
        const bool ground = w.pickGround(screenPointToRay(*camera_, gx, gy, screenW_, screenH_), hit);
        uiCheck("Terrain::raycast trafia w punkt terenu", ground && std::abs(hit.x - g.x) < 3.0f && std::abs(hit.z - g.z) < 3.0f);

        // Szukamy miejsca na Dom i wchodzimy w tryb budowy klawiszem "1"
        uiTarget_ = {};
        for (int i = 0; i < 400 && uiTarget_.y == 0.0f; ++i) {
            const float a = i * 0.37f, r = 35.0f + (i % 10) * 6.0f;
            const Vec3 p = w.snapToGrid(add(w.buildings[castle].pos, {std::cos(a) * r, 0.0f, std::sin(a) * r}));
            float px, py;
            if (w.canPlace(BuildingType::House, p, kPlayer) && project(p, px, py) && px > 300 && px < screenW_ - 300 &&
                py > 100 && py < screenH_ - 250) {
                uiTarget_ = p;
                uiTarget_.y = std::max(0.01f, p.y);
            }
        }
        uiSitesBefore_ = 0;
        for (const auto& b : w.buildings) uiSitesBefore_ += (b.alive && b.faction == kPlayer) ? 1 : 0;
        pushKey(SDL_SCANCODE_1, true);
    } else if (f == 22) {
        pushKey(SDL_SCANCODE_1, false);
    } else if (f == 24) {
        uiCheck("klawisz 1 włącza tryb budowy", placing_ && placeType_ == BuildingType::House);
        float sx, sy;
        project(uiTarget_, sx, sy);
        pushMouseMotion(sx, sy);
    } else if (f == 28) {
        pushMouseButton(SDL_BUTTON_LEFT, true);
    } else if (f == 30) {
        pushMouseButton(SDL_BUTTON_LEFT, false);
    } else if (f == 34) {
        int sites = 0;
        for (const auto& b : w.buildings) sites += (b.alive && b.faction == kPlayer) ? 1 : 0;
        uiCheck("LPM stawia plac budowy", sites == uiSitesBefore_ + 1 && !placing_);
    } else if (f == 40) {
        // Ramka zaznaczenia wokół żołnierzy gracza
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        for (const auto& u : w.units) {
            if (!u.alive || u.faction != kPlayer || u.type == UnitType::Settler) continue;
            float sx, sy;
            if (!project(add(u.pos, {0.0f, 3.0f, 0.0f}), sx, sy)) continue;
            minX = std::min(minX, sx); minY = std::min(minY, sy);
            maxX = std::max(maxX, sx); maxY = std::max(maxY, sy);
        }
        uiBox_ = {minX - 15.0f, minY - 15.0f, maxX + 15.0f, maxY + 15.0f};
        pushMouseMotion(uiBox_[0], uiBox_[1]);
    } else if (f == 43) {
        pushMouseButton(SDL_BUTTON_LEFT, true);
    } else if (f == 46) {
        pushMouseMotion(uiBox_[2], uiBox_[3]);
    } else if (f == 49) {
        pushMouseButton(SDL_BUTTON_LEFT, false);
    } else if (f == 53) {
        uiCheck("ramka zaznacza wojsko", !selectedSoldiers().empty());
        // Rozkaz ruchu PPM
        uiMoveTarget_ = add(w.buildings[castle].pos, {0.0f, 0.0f, -45.0f});
        uiMoveTarget_.y = w.heightAt(uiMoveTarget_.x, uiMoveTarget_.z);
        float sx, sy;
        project(uiMoveTarget_, sx, sy);
        pushMouseMotion(sx, sy);
    } else if (f == 56) {
        pushMouseButton(SDL_BUTTON_RIGHT, true);
    } else if (f == 58) {
        pushMouseButton(SDL_BUTTON_RIGHT, false);
    } else if (f == 62) {
        bool ordered = !selectedSoldiers().empty();
        for (int id : selectedSoldiers()) {
            const Unit& u = w.units[id];
            if (std::abs(u.guardPos.x - uiMoveTarget_.x) > 25.0f || std::abs(u.guardPos.z - uiMoveTarget_.z) > 25.0f) ordered = false;
        }
        uiCheck("PPM wydaje rozkaz marszu", ordered);
        // Kliknięcie w zamek zaznacza budynek
        float sx, sy;
        project(add(w.buildings[castle].pos, {0.0f, 8.0f, 0.0f}), sx, sy);
        pushMouseMotion(sx, sy);
    } else if (f == 65) {
        pushMouseButton(SDL_BUTTON_LEFT, true);
    } else if (f == 67) {
        pushMouseButton(SDL_BUTTON_LEFT, false);
    } else if (f == 70) {
        uiCheck("LPM na zamku zaznacza budynek", selectedBuilding_ == castle);
        // Kółko myszy z ułamkowymi wartościami (precyzyjna mysz / touchpad)
        float sx, sy;
        project(add(w.buildings[castle].pos, {60.0f, 0.0f, 60.0f}), sx, sy);
        pushMouseMotion(std::clamp(sx, 400.0f, screenW_ - 400.0f), std::clamp(sy, 200.0f, screenH_ - 300.0f));
        uiDistBefore_ = cam_.distance();
        for (int i = 0; i < 4; ++i) {
            SDL_Event ev{};
            ev.type = SDL_EVENT_MOUSE_WHEEL;
            ev.wheel.y = 0.4f;
            SDL_PushEvent(&ev);
        }
    } else if (f == 90) {
        uiCheck("kółko myszy przybliża kamerę", cam_.distance() < uiDistBefore_ * 0.9f);
        // Podpowiedź brakujących surowców: zerujemy drewno i najeżdżamy na przycisk "Dom"
        {
            Resources r = world_->factions[kPlayer].res;
            r[CarryType::Planks] = 0;
            world_->debugSetStock(kPlayer, r);
        }
        float hx = 0.0f, hy = 0.0f;
        buildBarSlotCenter(0, hx, hy);   // przycisk "Dom" na dolnym pasku
        pushMouseMotion(hx, hy);
    } else if (f == 94) {
        uiCheck("podpowiedź pokazuje brakujące surowce", hoverMissing_.find("20 desek") != std::string::npos);
        LOG_INFO("[uitest] podpowiedź: \"{}\"", hoverMissing_);
        crowdAndAccessTest();
        uiCheck("każdy typ budynku zostaje ukończony przez osadników", constructionStressTest());
        repairHealResourceTest();
        mountainBuildingsTest();
        territoryCaptureTest();
        towerAssaultTest();
        economyRoadTest();
        productionChainTest();
        storageJamTest();
        autoRecruitTest();
        showPause();
        uiOrigW_ = static_cast<int>(renderer_->width());
        uiOrigH_ = static_cast<int>(renderer_->height());
    } else if (f == 98) {
        // Pauza -> "Ustawienia"
        pushMouseMotion(static_cast<float>(pauseWindow_->x() + 140), static_cast<float>(pauseWindow_->y() + 138));
    } else if (f == 100) {
        pushMouseButton(SDL_BUTTON_LEFT, true);
    } else if (f == 102) {
        pushMouseButton(SDL_BUTTON_LEFT, false);
    } else if (f == 105) {
        uiCheck("przycisk Ustawienia otwiera okno ustawień", subWindow_ == settingsWindow_ && settingsWindow_->isVisible());
        uiMusicBefore_ = settings_.musicOn;
        // checkbox "Muzyka"
        pushMouseMotion(static_cast<float>(settingsWindow_->x() + 20), static_cast<float>(settingsWindow_->y() + 45));
    } else if (f == 107) {
        pushMouseButton(SDL_BUTTON_LEFT, true);
    } else if (f == 109) {
        pushMouseButton(SDL_BUTTON_LEFT, false);
    } else if (f == 112) {
        uiCheck("checkbox wyłącza/włącza muzykę", settings_.musicOn != uiMusicBefore_ && musicEnabled_ == settings_.musicOn);
        // suwak głośności muzyki na ~20%
        pushMouseMotion(static_cast<float>(settingsWindow_->x() + 300), static_cast<float>(settingsWindow_->y() + 77));
    } else if (f == 114) {
        pushMouseButton(SDL_BUTTON_LEFT, true);
    } else if (f == 116) {
        pushMouseButton(SDL_BUTTON_LEFT, false);
    } else if (f == 119) {
        const bool ok = settings_.musicVolume >= 15 && settings_.musicVolume <= 25 &&
                        (!audio_ || std::abs(audio_->musicVolume() - settings_.musicVolume / 100.0f) < 0.001f);
        LOG_INFO("[uitest] głośność muzyki po kliknięciu suwaka: {}%", settings_.musicVolume);
        uiCheck("suwak zmienia głośność muzyki", ok);
        // combobox rozdzielczości - otwarcie
        pushMouseMotion(static_cast<float>(settingsWindow_->x() + 60), static_cast<float>(settingsWindow_->y() + 266));
    } else if (f == 121) {
        pushMouseButton(SDL_BUTTON_LEFT, true);
    } else if (f == 123) {
        pushMouseButton(SDL_BUTTON_LEFT, false);
    } else if (f == 126) {
        // wybór innej rozdzielczości niż obecna
        uiResIdx_ = -1;
        for (size_t i = 0; i < resolutions_.size(); ++i) {
            if (resolutions_[i][0] != uiOrigW_ && resolutions_[i][0] <= 1280) uiResIdx_ = static_cast<int>(i);
        }
        if (uiResIdx_ < 0) uiResIdx_ = 0;
        pushMouseMotion(static_cast<float>(settingsWindow_->x() + 60),
                        static_cast<float>(settingsWindow_->y() + 256 + 25 + uiResIdx_ * 25 + 12));
    } else if (f == 128) {
        pushMouseButton(SDL_BUTTON_LEFT, true);
    } else if (f == 130) {
        pushMouseButton(SDL_BUTTON_LEFT, false);
    } else if (f == 170) {
        const auto want = resolutions_[uiResIdx_];
        LOG_INFO("[uitest] rozdzielczość: wybrano {}x{}, okno {}x{}", want[0], want[1], renderer_->width(), renderer_->height());
        uiCheck("wybór rozdzielczości zmienia rozmiar okna",
                settings_.windowW == want[0] && static_cast<int>(renderer_->width()) == want[0] &&
                    static_cast<int>(renderer_->height()) == want[1]);
        pushKey(SDL_SCANCODE_ESCAPE, true);
    } else if (f == 172) {
        pushKey(SDL_SCANCODE_ESCAPE, false);
    } else if (f == 175) {
        uiCheck("ESC wraca z ustawień do pauzy", subWindow_ == nullptr && pauseWindow_->isVisible() && mode_ == Mode::Pause);
        // "Sterowanie" (wiersze okna pauzy przesunęły się po zmianie rozdzielczości - liczymy od okna)
        pushMouseMotion(static_cast<float>(pauseWindow_->x() + 140), static_cast<float>(pauseWindow_->y() + 188));
    } else if (f == 177) {
        pushMouseButton(SDL_BUTTON_LEFT, true);
    } else if (f == 179) {
        pushMouseButton(SDL_BUTTON_LEFT, false);
    } else if (f == 182) {
        uiCheck("przycisk Sterowanie otwiera listę sterowania", subWindow_ == controlsWindow_ && controlsWindow_->isVisible());
        pushKey(SDL_SCANCODE_ESCAPE, true);
    } else if (f == 184) {
        pushKey(SDL_SCANCODE_ESCAPE, false);
    } else if (f == 187) {
        uiCheck("ESC zamyka sterowanie", subWindow_ == nullptr && mode_ == Mode::Pause);
        engine_.setWindowSize(static_cast<uint32_t>(uiOrigW_), static_cast<uint32_t>(uiOrigH_));
        // Przycisk prędkości w pauzie
        uiSpeedBefore_ = gameSpeed_;
    } else if (f == 215) {
        // po zmianie rozmiaru okna pozycje okien GUI są już przeliczone
        pushMouseMotion(static_cast<float>(pauseWindow_->x() + 140), static_cast<float>(pauseWindow_->y() + 88));
    } else if (f == 220) {
        pushMouseButton(SDL_BUTTON_LEFT, true);
    } else if (f == 222) {
        pushMouseButton(SDL_BUTTON_LEFT, false);
    } else if (f == 225) {
        uiCheck("przycisk prędkości w pauzie zmienia prędkość", gameSpeed_ == uiSpeedBefore_ * 2);
        enterGame();
        gameSpeed_ = 1;
        pushKey(SDL_SCANCODE_RIGHTBRACKET, true);
    } else if (f == 227) {
        pushKey(SDL_SCANCODE_RIGHTBRACKET, false);
    } else if (f == 229) {
        pushKey(SDL_SCANCODE_RIGHTBRACKET, true);
    } else if (f == 231) {
        pushKey(SDL_SCANCODE_RIGHTBRACKET, false);
    } else if (f == 233) {
        uiCheck("klawisz ] przyspiesza czas (x1 -> x4)", gameSpeed_ == 4);
        uiTimeBefore_ = world_->gameTime;
        uiRealBefore_ = std::chrono::steady_clock::now();
    } else if (f == 236) {
        // Budowa drogi myszą: T, klik na zamku (flaga początkowa), klik na terenie (koniec drogi), PPM.
        clearSelection();
        cam_.setFocus(w.buildings[castle].pos);
        cam_.setDistance(130.0f);
    } else if (f == 246) {
        const int castleFlag = w.buildings[castle].flag;
        uiTarget_ = {};
        std::vector<Vec3> pts;
        for (int i = 0; i < 300 && uiTarget_.y == 0.0f; ++i) {
            const float a = i * 0.41f, r = 30.0f + (i % 8) * 6.0f;
            Vec3 p = add(w.flags[castleFlag].pos, {std::cos(a) * r, 0.0f, std::sin(a) * r});
            p.y = w.heightAt(p.x, p.z);
            float px, py;
            if (w.planRoad(kPlayer, castleFlag, p, pts) && project(p, px, py) && px > 350 && px < screenW_ - 350 && py > 120 &&
                py < screenH_ - 260 && w.pickBuilding(screenPointToRay(*camera_, px, py, screenW_, screenH_)) < 0 &&
                w.flagNear(kPlayer, p, 8.0f) < 0) {
                uiTarget_ = p;
                uiTarget_.y = std::max(0.01f, p.y);
            }
        }
        pushKey(SDL_SCANCODE_T, true);
    } else if (f == 248) {
        pushKey(SDL_SCANCODE_T, false);
    } else if (f == 250) {
        uiCheck("klawisz T włącza tryb drogi", roadMode_ && roadStart_ < 0);
        float sx, sy;
        project(add(w.buildings[castle].pos, {0.0f, 8.0f, 0.0f}), sx, sy);
        pushMouseMotion(sx, sy);
    } else if (f == 252) {
        pushMouseButton(SDL_BUTTON_LEFT, true);
    } else if (f == 254) {
        pushMouseButton(SDL_BUTTON_LEFT, false);
    } else if (f == 256) {
        uiCheck("LPM na zamku wybiera jego flagę jako początek drogi", roadStart_ == w.buildings[castle].flag);
        float sx, sy;
        project(uiTarget_, sx, sy);
        pushMouseMotion(sx, sy);
    } else if (f == 260) {
        LOG_INFO("[uitest] podgląd drogi: cel ({:.0f},{:.0f}) znaleziony={} punkty={} powód=\"{}\"", uiTarget_.x, uiTarget_.z,
                 uiTarget_.y != 0.0f, roadPreview_.size(), roadReason_);
        uiCheck("podgląd trasy drogi jest poprawny", uiTarget_.y != 0.0f && roadPreviewOk_ && roadPreview_.size() > 2);
        uiSitesBefore_ = 0;
        for (const auto& r : w.roads) uiSitesBefore_ += (r.alive && r.faction == kPlayer) ? 1 : 0;
        pushMouseButton(SDL_BUTTON_LEFT, true);
    } else if (f == 262) {
        pushMouseButton(SDL_BUTTON_LEFT, false);
    } else if (f == 265) {
        int roadsNow = 0;
        for (const auto& r : w.roads) roadsNow += (r.alive && r.faction == kPlayer) ? 1 : 0;
        uiCheck("LPM buduje drogę i prowadzi ją dalej od nowej flagi",
                roadsNow > uiSitesBefore_ && roadMode_ && roadStart_ >= 0 && roadStart_ != w.buildings[castle].flag);
        pushMouseButton(SDL_BUTTON_RIGHT, true);
    } else if (f == 267) {
        pushMouseButton(SDL_BUTTON_RIGHT, false);
    } else if (f == 270) {
        uiCheck("PPM kończy budowę drogi", !roadMode_);
    } else if (f == 293) {
        const float simDt = world_->gameTime - uiTimeBefore_;
        const float realDt = std::chrono::duration<float>(std::chrono::steady_clock::now() - uiRealBefore_).count();
        LOG_INFO("[uitest] przy x4: {:.1f} s gry w {:.1f} s rzeczywistych", simDt, realDt);
        uiCheck("czas gry płynie ~4x szybciej", simDt > realDt * 3.0f);
        pushKey(SDL_SCANCODE_LEFTBRACKET, true);
    } else if (f == 295) {
        pushKey(SDL_SCANCODE_LEFTBRACKET, false);
    } else if (f == 297) {
        uiCheck("klawisz [ zwalnia czas", gameSpeed_ == 2);
        // Pokaz AI kontra AI z menu głównego
        showMenu();
        pushMouseMotion(static_cast<float>(menuWindow_->x() + 110), static_cast<float>(menuWindow_->y() + 178));
    } else if (f == 300) {
        pushMouseButton(SDL_BUTTON_LEFT, true);
    } else if (f == 302) {
        pushMouseButton(SDL_BUTTON_LEFT, false);
    } else if (f == 306) {
        uiCheck("przycisk Pokaz uruchamia AI kontra AI",
                mode_ == Mode::Game && spectator_ && world_->aiControlsPlayer && !buildBarVisible() && gameSpeed_ == 4);
    } else if (f == 600) {
        int playerSites = 0;
        for (const auto& b : world_->buildings) playerSites += (b.alive && b.faction == kPlayer) ? 1 : 0;
        LOG_INFO("[uitest] pokaz: {:.0f} s gry, budynki AI gracza: {}", world_->gameTime, playerSites);
        uiCheck("w pokazie AI rozbudowuje stronę gracza", playerSites > 1);
        LOG_INFO("[uitest] koniec, błędów: {}", uiFailures_);
        uiTest_ = false;
        if (!std::getenv("SETTLERS_UITEST_KEEP")) pushQuitEvent();
    }
}

// SETTLERS_AUTOPLAY: co 30 s gry wypisuje stan obu stron, a po zadanym czasie (lub końcu partii) zamyka program.
void SettlersApp::updateAutoplay() {
    const World& w = *world_;
    if (w.gameTime >= nextAutoplayLog_) {
        nextAutoplayLog_ += 30.0f;
        for (int f = 0; f < kFactionCount; ++f) {
            const FactionState& fs = w.factions[f];
            int built = 0, sites = 0, soldiers = 0, workers = 0;
            for (const auto& b : w.buildings) {
                if (!b.alive || b.faction != f) continue;
                (b.constructed ? built : sites)++;
            }
            for (const auto& u : w.units) {
                if (!u.alive || u.faction != f) continue;
                if (u.type != UnitType::Settler) ++soldiers;
                else if (u.building >= 0) ++workers;
            }
            LOG_INFO("[autoplay] t={}s {}: deski={} kamień={} żywność={} pop={}/{} bud={} plac={} wojsko={} pracuje={} zabici={} straty={} fale={}",
                     static_cast<int>(w.gameTime), factionName(f), fs.res[CarryType::Planks], fs.res[CarryType::Stone], fs.res.food(),
                     fs.population, fs.popCap, built, sites, soldiers, workers, fs.enemiesKilled, fs.unitsLost,
                     fs.waveNumber);
        }
        for (int f = 0; f < kFactionCount; ++f) {
            // Transport: drogi, tragarze, towary na flagach, dostawy, magazyny, przestoje warsztatów.
            int roadCount = 0, onFlags = 0, producers = 0, blocked = 0, unlinked = 0;
            for (const auto& r : w.roads) roadCount += (r.alive && r.faction == f) ? 1 : 0;
            for (const auto& fl : w.flags) {
                if (fl.alive && fl.faction == f) onFlags += static_cast<int>(fl.goods.size());
            }
            for (const auto& b : w.buildings) {
                if (!b.alive || b.faction != f || !b.constructed || !producesGoods(b.type)) continue;
                ++producers;
                if (!w.buildingLinked(b)) ++unlinked;
                else if (b.outputBlocked) ++blocked;
            }
            const TransportStats ts = w.transportStats(f);
            Resources unfunded;   // na jakie towary czekają dostawy bez pokrycia w magazynach
            for (const auto& o : w.orders) {
                if (o.alive && o.faction == f && !o.funded) unfunded[o.type] += o.amount;
            }
            if (unfunded.total() > 0) LOG_INFO("[autoplay] {}: dostawy czekają na: {}", factionName(f), stockSummary(unfunded, 6));
            int barracks = 0, queued = 0, garrisoned = 0, towers = 0;   // wojsko: koszary, kolejka, wieże i ich załogi
            for (const auto& b : w.buildings) {
                towers += (b.alive && b.faction == f && w.isCapturable(b)) ? 1 : 0;
                if (!b.alive || b.faction != f || b.type != BuildingType::Barracks) continue;
                barracks += b.constructed ? 1 : 0;
                queued += static_cast<int>(b.trainQueue.size());
            }
            for (const auto& u : w.units) garrisoned += (u.alive && u.faction == f && u.state == UnitState::Garrison) ? 1 : 0;
            {   // Budynki wg typu (ukończone; w nawiasie place budowy) - gdzie urywa się łańcuch produkcji.
                std::array<int, static_cast<size_t>(BuildingType::Count)> done{}, sites{};
                for (const auto& b : w.buildings) {
                    if (b.alive && b.faction == f) (b.constructed ? done : sites)[static_cast<size_t>(b.type)]++;
                }
                std::string list;
                for (size_t t = 0; t < done.size(); ++t) {
                    if (done[t] + sites[t] == 0) continue;
                    list += std::string(list.empty() ? "" : ", ") + buildingDef(static_cast<BuildingType>(t)).name + " " +
                            std::to_string(done[t]) + (sites[t] ? "(+" + std::to_string(sites[t]) + ")" : "");
                }
                LOG_INFO("[autoplay] budynki {}: {} | magazyny: {}", factionName(f), list, stockSummary(w.factions[f].res, 12));
            }
            LOG_INFO("[autoplay] wojsko {}: broń={} koszary={} w kolejce={} wieże={} w załogach={}", factionName(f),
                     w.factions[f].res[CarryType::Weapons], barracks, queued, towers, garrisoned);
            LOG_INFO("[autoplay] transport {}: drogi={} tragarze={}/{} zajęci={} na flagach={} dostawy={} (bez towaru {}) "
                     "magazyn={}/{} warsztaty={} bez drogi={} wstrzymane={}",
                     factionName(f), roadCount, ts.carriers, ts.target, ts.busy, onFlags, ts.ordersWaiting, ts.ordersUnfunded,
                     w.storageUsedOf(f), w.storageCapacityOf(f), producers, unlinked, blocked);
        }
        if (audio_) LOG_INFO("[autoplay] audio: muzyka={} grająca={} szczyt={:.3f} głosy={}", audio_->currentMusic(),
                             audio_->isMusicPlaying(), audio_->peakLevel(), audio_->activeVoices());
    }
    if (w.winner >= 0 || w.gameTime >= autoplaySeconds_) {
        if (std::getenv("SETTLERS_FLAGDUMP")) {
            for (size_t i = 0; i < w.flags.size(); ++i) {
                const Flag& fl = w.flags[i];
                if (!fl.alive || fl.goods.empty()) continue;
                LOG_INFO("[flagi] {} f={} bud={} towary={} wolne={} conn={}", i, fl.faction,
                         fl.building >= 0 ? buildingDef(w.buildings[fl.building].type).name : "-", fl.goods.size(),
                         w.flagAvailableGoods(fl), fl.connected);
            }
        }
        LOG_INFO("[autoplay] koniec: t={}s zwycięzca={}", static_cast<int>(w.gameTime),
                 w.winner >= 0 ? factionName(w.winner) : "brak");
        autoplaySeconds_ = 0.0f;
        pushQuitEvent();
    }
}

} // namespace settlers
