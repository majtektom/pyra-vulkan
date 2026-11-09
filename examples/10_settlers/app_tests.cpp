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
    w.factions[kPlayer].res = {2000, 2000, 2000};
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
        float t = 0.0f;
        while (t < 120.0f && w.buildings[b].alive && !w.buildings[b].constructed) {
            w.update(0.1f);
            t += 0.1f;
        }
        LOG_INFO("[uitest] dom za tłumem wojska ukończony po {:.0f} s", t);
        uiCheck("budowniczowie przechodzą przez tłum wojska", w.buildings[b].constructed && t < 60.0f);
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
    const int woodBefore = w.factions[kPlayer].res.wood;
    bool sawRepairer = false;
    for (int i = 0; i < 1500; ++i) {
        w.update(0.1f);
        sawRepairer = sawRepairer || w.repairersOf(house) > 0;
    }
    LOG_INFO("[uitest] dom: {:.0f}/{:.0f} PŻ, zużyte drewno: {}, żołnierz: {:.0f} PŻ", w.buildings[house].hp, houseMax,
             woodBefore - w.factions[kPlayer].res.wood, w.units[soldier].hp);
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

// SETTLERS_SHOWCASE=1: nowa gra z gotową kopalnią i leśniczym przy bazie, kamera skierowana na kopalnię
// (do oglądania modeli i pracy górnika/leśniczego).
void SettlersApp::showcaseBuildings() {
    World& w = *world_;
    const Vec3 castlePos = w.buildings[w.factions[kPlayer].castle].pos;
    Vec3 focus = castlePos;
    for (BuildingType type : {BuildingType::Forester, BuildingType::Mine}) {
        bool placed = false;
        for (float r = 40.0f; r < 175.0f && !placed; r += 8.0f) {
            for (int k = 0; k < 48 && !placed; ++k) {
                const float a = static_cast<float>(k) * 2.0f * PI / 48.0f;
                const Vec3 p = w.snapToGrid(add(castlePos, {std::cos(a) * r, 0.0f, std::sin(a) * r}));
                if (w.canPlace(type, p, kPlayer)) {
                    w.placeBuilding(type, p, kPlayer, true);
                    if (type == BuildingType::Mine) focus = p;
                    placed = true;
                }
            }
        }
    }
    cam_.setFocus(focus);
    cam_.setDistance(90.0f);
}

// Góry, kopalnia i leśniczy:
//  1. ośnieżone szczyty są nieprzechodnie, a zbocza poniżej linii śniegu w większości dostępne,
//  2. kopalnia w górach (w terytorium gracza) daje kamień bez skał na powierzchni,
//  3. leśniczy sadzi drzewa, które po kTreeGrowSeconds wyrastają.
void SettlersApp::mountainBuildingsTest() {
    World& w = *world_;
    w.aiEnabled = false;
    w.factions[kPlayer].res = {5000, 5000, 5000};
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

// Stawia po kilka placów budowy każdego typu i symuluje świat - wszystkie muszą zostać ukończone.
bool SettlersApp::constructionStressTest() {
    World& w = *world_;
    w.aiEnabled = false;
    w.factions[kPlayer].res = {5000, 5000, 5000};
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
            LOG_INFO("[uitest]   nieukończony: {} postęp={}%", buildingDef(b.type).name, static_cast<int>(b.progress * 100));
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
        world_->factions[kPlayer].res.wood = 0;
        pushMouseMotion(static_cast<float>(buildWindow_->x() + 60), static_cast<float>(buildWindow_->y() + 20));
    } else if (f == 94) {
        uiCheck("podpowiedź pokazuje brakujące surowce", hoverMissing_.find("20 drewna") != std::string::npos);
        LOG_INFO("[uitest] podpowiedź: \"{}\"", hoverMissing_);
        crowdAndAccessTest();
        uiCheck("każdy typ budynku zostaje ukończony przez osadników", constructionStressTest());
        repairHealResourceTest();
        mountainBuildingsTest();
        territoryCaptureTest();
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
                mode_ == Mode::Game && spectator_ && world_->aiControlsPlayer && !buildWindow_->isVisible() && gameSpeed_ == 4);
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
            LOG_INFO("[autoplay] t={}s {}: D={} K={} Z={} pop={}/{} bud={} plac={} wojsko={} pracuje={} zabici={} straty={} fale={}",
                     static_cast<int>(w.gameTime), factionName(f), fs.res.wood, fs.res.stone, fs.res.food,
                     fs.population, fs.popCap, built, sites, soldiers, workers, fs.enemiesKilled, fs.unitsLost,
                     fs.waveNumber);
        }
        for (int f = 0; f < kFactionCount; ++f) {
            int states[10] = {};
            int delivered = 0;
            for (const auto& b : w.buildings) {
                if (b.alive && b.faction == f && b.type == BuildingType::Quarry) delivered += b.produced;
            }
            for (const auto& u : w.units) {
                if (u.alive && u.faction == f && u.building >= 0 && w.buildings[u.building].type == BuildingType::Quarry)
                    states[static_cast<int>(u.state)]++;
            }
            if (f == 0 && audio_) LOG_INFO("[autoplay] audio: muzyka={} grająca={} szczyt={:.3f} głosy={}",
                                        audio_->currentMusic(), audio_->isMusicPlaying(), audio_->peakLevel(), audio_->activeVoices());
        LOG_INFO("[autoplay] kamieniarze {}: idzie={} kuje={} niesie={} czeka={} dostarczono={}", factionName(f),
                     states[static_cast<int>(UnitState::GoGather)], states[static_cast<int>(UnitState::Gathering)],
                     states[static_cast<int>(UnitState::ReturnHome)], states[static_cast<int>(UnitState::WorkWait)], delivered);
        }
    }
    if (w.winner >= 0 || w.gameTime >= autoplaySeconds_) {
        LOG_INFO("[autoplay] koniec: t={}s zwycięzca={}", static_cast<int>(w.gameTime),
                 w.winner >= 0 ? factionName(w.winner) : "brak");
        autoplaySeconds_ = 0.0f;
        pushQuitEvent();
    }
}

} // namespace settlers
