/**
 * app_debug.cpp - narzędzia debugowania (F3 / F9).
 *
 * Tryb 1: ścieżki jednostek, linie do celów, etykiety stanów, wejścia budynków, panel statystyk.
 * Tryb 2: dodatkowo siatka kolizji (zablokowane komórki NavGrid) wokół kamery.
 * F9: pełny opis zaznaczonych i zawieszonych jednostek w konsoli.
 */
#include "settlers_app.h"

namespace settlers {

// Linia ekranowa: GuiMgr::addOverlayLine z silnika (gap > 0 = linia przerywana z kropek).
void SettlersApp::debugLine(float x0, float y0, float x1, float y1, const Color& c, float thick, float gap) {
    if (gap <= 0.0f) {
        gui_->addOverlayLine(x0, y0, x1, y1, thick, c);
        return;
    }
    const float dx = x1 - x0, dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5f || len > 4000.0f) return;
    const int n = std::min(800, static_cast<int>(len / (thick + gap)) + 1);
    for (int i = 0; i <= n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n);
        gui_->addOverlayRect(x0 + dx * t - thick * 0.5f, y0 + dy * t - thick * 0.5f, thick, thick, c, 0.949f);
    }
}

// Rzutuje punkt świata na ekran; false, gdy punkt jest za kamerą lub daleko poza ekranem.
bool SettlersApp::project(const Vec3& p, float& sx, float& sy) const {
    return worldToScreen(*camera_, p, screenW_, screenH_, sx, sy) && sx > -200 && sy > -200 && sx < screenW_ + 200 &&
           sy < screenH_ + 200;
}

// Kolor stanu jednostki w trybie debug (ścieżki i etykiety mają kolor stanu).
Color SettlersApp::stateColor(UnitState s) {
    switch (s) {
    case UnitState::Idle: return {0.75f, 0.75f, 0.75f, 1.0f};
    case UnitState::Moving: return {0.4f, 0.8f, 1.0f, 1.0f};
    case UnitState::GoBuild: case UnitState::Building: return {1.0f, 0.85f, 0.3f, 1.0f};
    case UnitState::GoToWork: case UnitState::WorkWait: return {0.6f, 1.0f, 0.6f, 1.0f};
    case UnitState::GoGather: case UnitState::Gathering: return {0.4f, 1.0f, 0.8f, 1.0f};
    case UnitState::ReturnHome: return {0.95f, 0.65f, 1.0f, 1.0f};
    case UnitState::Attack: return {1.0f, 0.35f, 0.3f, 1.0f};
    case UnitState::GoToRoad: case UnitState::CarrierWait: case UnitState::CarrierWalk: return {1.0f, 0.75f, 0.45f, 1.0f};
    case UnitState::GoGarrison: case UnitState::Garrison: return {0.75f, 0.55f, 1.0f, 1.0f};
    }
    return {1, 1, 1, 1};
}

// Rysuje informacje debug dla obiektów w pobliżu kamery:
//  - jednostki: ścieżka (linia w kolorze stanu), linia do celu, etykieta '#id Stan', czerwona ramka gdy stoi,
//  - budynki: ramka wejścia (zielona = wolne, czerwona = zablokowane), opis,
//  - surowce: rezerwacje i nieosiągalne,
//  - panel statystyk w lewym górnym rogu.
void SettlersApp::drawDebug(const InputSnapshot& in) {
    const World& w = *world_;
    const Vec3 focus = cam_.focus();
    const float range = cam_.distance() * 1.3f + 120.0f;
    const bool detailed = cam_.distance() < 380.0f;
    int counts[kUnitStateCount] = {};
    int stalledNow = 0;

    // --- Jednostki: ścieżki, cele, etykiety ---
    for (size_t i = 0; i < w.units.size(); ++i) {
        const Unit& u = w.units[i];
        if (!u.alive) continue;
        counts[static_cast<int>(u.state)]++;
        if (u.stallTime > 2.0f) ++stalledNow;
        const float dx = u.pos.x - focus.x, dz = u.pos.z - focus.z;
        if (dx * dx + dz * dz > range * range) continue;
        float ux, uy;
        if (!project(onGround(u.pos, 1.0f), ux, uy)) continue;
        const Color sc = stateColor(u.state);

        // Ścieżka (od bieżącej pozycji przez kolejne punkty)
        float px = ux, py = uy;
        for (size_t k = u.pathIndex; k < u.path.size(); ++k) {
            float qx, qy;
            if (!project(onGround(u.path[k], 1.0f), qx, qy)) break;
            debugLine(px, py, qx, qy, {sc[0], sc[1], sc[2], 0.85f}, 2.0f);
            gui_->addOverlayRect(qx - 3, qy - 3, 6, 6, sc, 0.948f);
            px = qx;
            py = qy;
        }
        // Cel logiczny: zasób / budynek / atakowany
        Vec3 target{};
        bool hasTarget = false;
        if (u.resourceNode >= 0 && w.nodes[u.resourceNode].alive) {
            target = w.nodes[u.resourceNode].pos;
            hasTarget = true;
        } else if (u.targetUnit >= 0 && w.units[u.targetUnit].alive) {
            target = w.units[u.targetUnit].pos;
            hasTarget = true;
        } else if (u.targetBuilding >= 0 && w.buildings[u.targetBuilding].alive) {
            target = w.buildings[u.targetBuilding].pos;
            hasTarget = true;
        } else if (u.building >= 0 && w.buildings[u.building].alive) {
            target = u.state == UnitState::ReturnHome || u.state == UnitState::GoToWork ? w.buildings[u.building].door()
                                                                                      : w.buildings[u.building].pos;
            hasTarget = true;
        }
        if (hasTarget) {
            float tx, ty;
            if (project(onGround(target, 2.0f), tx, ty)) debugLine(ux, uy, tx, ty, {1.0f, 1.0f, 1.0f, 0.35f}, 1.5f, 5.0f);
        }

        // Etykieta
        const bool stalled = u.stallTime > 2.0f;
        if (detailed || u.selected || stalled) {
            char buf[96];
            if (stalled) {
                std::snprintf(buf, sizeof(buf), "#%zu %s STOI %.0fs", i, World::stateName(u.state), u.stallTime);
            } else {
                std::snprintf(buf, sizeof(buf), "#%zu %s", i, World::stateName(u.state));
            }
            const Color lc = stalled ? Color{1.0f, 0.25f, 0.2f, 1.0f} : sc;
            if (stalled) gui_->addOverlayFrame(ux - 9, uy - 30, 18, 32, 2.0f, lc, 0.947f);
            text(buf, ux + 8.0f, uy - 18.0f, 0.4f, lc);
        }
    }

    // --- Budynki: wejścia, pracownik, obszar ---
    for (size_t i = 0; i < w.buildings.size(); ++i) {
        const Building& b = w.buildings[i];
        if (!b.alive) continue;
        const float dx = b.pos.x - focus.x, dz = b.pos.z - focus.z;
        if (dx * dx + dz * dz > range * range) continue;
        const Vec3 door = b.door();
        const bool doorFree = w.nav().isWalkableWorld(door.x, door.z);
        float sx, sy;
        if (project(onGround(door, 0.5f), sx, sy)) {
            const Color dc = doorFree ? Color{0.2f, 1.0f, 0.3f, 1.0f} : Color{1.0f, 0.15f, 0.1f, 1.0f};
            gui_->addOverlayFrame(sx - 6, sy - 6, 12, 12, 2.0f, dc, 0.947f);
            if (detailed) text(doorFree ? "wejście" : "WEJŚCIE ZABL.", sx + 9, sy + 4, 0.38f, dc);
        }
        if (detailed && project(onGround(b.pos, buildingDef(b.type).height * 0.5f), sx, sy)) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "#%zu %s prac=%d obszar=%d%s%s", i, buildingDef(b.type).name, b.worker,
                          w.nav().regionAt(door.x, door.z), b.unreachable ? " BRAK DOJŚCIA" : "",
                          b.lacksResources ? " BRAK SUROWCA" : "");
            text(buf, sx - 60.0f, sy, 0.4f, {1.0f, 1.0f, 0.75f, 1.0f});
        }
    }

    // --- Surowce: rezerwacje i nieosiągalne ---
    if (detailed) {
        for (const auto& n : w.nodes) {
            if (!n.alive || (n.reserved == 0 && n.blockedUntil <= w.gameTime)) continue;
            const float dx = n.pos.x - focus.x, dz = n.pos.z - focus.z;
            if (dx * dx + dz * dz > range * range) continue;
            float sx, sy;
            if (!project(onGround(n.pos, 3.0f), sx, sy)) continue;
            if (n.blockedUntil > w.gameTime) {
                text("nieosiągalne", sx - 20, sy, 0.38f, {1.0f, 0.4f, 0.3f, 1.0f});
            } else {
                text("rez=" + std::to_string(n.reserved) + " zost=" + std::to_string(n.amount), sx - 20, sy, 0.38f,
                     {0.6f, 1.0f, 0.9f, 1.0f});
            }
        }
    }

    // --- Panel statystyk ---
    const float x = 14.0f;
    float y = kTopBarH + 26.0f;
    const float pw = 340.0f;
    gui_->addOverlayRect(x - 6, y - 20, pw, 258, {0.02f, 0.03f, 0.06f, 0.82f}, 0.958f);
    auto line = [&](const std::string& s, const Color& c = {0.85f, 0.95f, 1.0f, 1.0f}) {
        text(s, x, y, 0.42f, c);
        y += 18.0f;
    };
    line(std::string("DEBUG (F3: ") + (debugMode_ == 2 ? "+siatka kolizji" : "ścieżki/stany") + ", F9: zrzut)",
         {1.0f, 0.9f, 0.5f, 1.0f});
    char buf[160];
    for (int s = 0; s < kUnitStateCount; s += 2) {
        if (s + 1 < kUnitStateCount) {
            std::snprintf(buf, sizeof(buf), "%-11s %3d    %-11s %3d", World::stateName(static_cast<UnitState>(s)), counts[s],
                          World::stateName(static_cast<UnitState>(s + 1)), counts[s + 1]);
        } else {
            std::snprintf(buf, sizeof(buf), "%-11s %3d", World::stateName(static_cast<UnitState>(s)), counts[s]);
        }
        line(buf);
    }
    std::snprintf(buf, sizeof(buf), "Zawieszone teraz: %d   wykryte: %d   odblokowane: %d", stalledNow,
                  w.debugStats.stallsDetected, w.debugStats.stallsRecovered);
    line(buf, stalledNow > 0 ? Color{1.0f, 0.4f, 0.3f, 1.0f} : Color{0.6f, 1.0f, 0.6f, 1.0f});
    std::snprintf(buf, sizeof(buf), "A*: %d   ścieżki proste: %d   pociski: %zu", w.debugStats.pathRequests,
                  w.debugStats.directPaths, w.projectiles.size());
    line(buf);
    std::snprintf(buf, sizeof(buf), "Encje sceny: %zu   jednostki: %zu   budynki: %zu", scene_->getEntities().size(),
                  w.units.size(), w.buildings.size());
    line(buf);
    // Komórka pod kursorem
    Vec3 hit;
    if (w.pickGround(mouseRay(in), hit)) {
        int cx, cz;
        if (w.nav().worldToCell(hit.x, hit.z, cx, cz)) {
            const bool walk = w.nav().isWalkable(cx, cz);
            const bool stat = w.nav().isStaticBlocked(cx, cz);
            std::snprintf(buf, sizeof(buf), "Kursor: (%.0f, %.0f) kom=(%d,%d) %s obszar=%d h=%.1f", hit.x, hit.z, cx, cz,
                          walk ? "wolna" : (stat ? "TEREN" : "PRZESZKODA"), w.nav().regionAt(hit.x, hit.z), hit.y);
            line(buf, walk ? Color{0.7f, 1.0f, 0.7f, 1.0f} : Color{1.0f, 0.55f, 0.4f, 1.0f});
        }
    }
    line("Zaznacz jednostkę i wciśnij F9, aby zrzucić jej stan do konsoli", {0.7f, 0.7f, 0.7f, 1.0f});
}

// Siatka kolizji (tryb 2): półprzezroczyste kwadraty zablokowanych komórek wokół kamery.
void SettlersApp::updateDebugNavMesh(float dt) {
    if (!debugNavEntity_) return;
    if (debugMode_ != 2 || mode_ == Mode::Menu) {
        debugNavEntity_->setActive(false);
        return;
    }
    debugNavTimer_ -= dt;
    if (debugNavTimer_ > 0.0f) return;
    debugNavTimer_ = 0.4f;
    const auto& nav = world_->nav();
    const Vec3 f = cam_.focus();
    const float radius = std::clamp(cam_.distance() * 0.9f, 80.0f, 260.0f);
    int c0x, c0z, c1x, c1z;
    nav.worldToCell(f.x - radius, f.z - radius, c0x, c0z);
    nav.worldToCell(f.x + radius, f.z + radius, c1x, c1z);
    c0x = std::max(0, c0x); c0z = std::max(0, c0z);
    c1x = std::min(nav.width() - 1, c1x); c1z = std::min(nav.height() - 1, c1z);
    MeshBuilder mb;
    const float hs = nav.cellSize() * 0.45f;
    for (int cz = c0z; cz <= c1z; ++cz) {
        for (int cx = c0x; cx <= c1x; ++cx) {
            if (nav.isWalkable(cx, cz)) continue;
            const Vec3 c = nav.cellCenter(cx, cz);
            const bool stat = nav.isStaticBlocked(cx, cz);
            const Vec3 col = stat ? Vec3{0.95f, 0.15f, 0.1f} : Vec3{1.0f, 0.6f, 0.05f};
            auto pt = [&](float ox, float oz) {
                return Vec3{c.x + ox, world_->heightAt(c.x + ox, c.z + oz) + 0.6f, c.z + oz};
            };
            const Vec3 a = pt(-hs, -hs), b = pt(hs, -hs), d = pt(hs, hs), e = pt(-hs, hs);
            mb.polygon({a, b, d, e}, {c.x, a.y - 10.0f, c.z}, col);
        }
    }
    auto* mc = debugNavEntity_->getComponent<MeshComponent>();
    if (mb.data().vertices.empty()) {
        debugNavEntity_->setActive(false);
        return;
    }
    engine_.getMeshManager()->loadMesh("debug_nav_grid", mb.build());
    mc->meshName = "debug_nav_grid";
    debugNavEntity_->setActive(true);
}

// F9: wypisuje do konsoli pełny opis zaznaczonych jednostek, jednostek stojących w miejscu
// i zaznaczonego budynku - przydatne przy zgłaszaniu błędów.
void SettlersApp::dumpDebug() {
    const World& w = *world_;
    LOG_INFO("===== ZRZUT DEBUG t={:.1f}s (A*={}, zawieszenia wykryte={} odblokowane={}) =====", w.gameTime,
             w.debugStats.pathRequests, w.debugStats.stallsDetected, w.debugStats.stallsRecovered);
    int dumped = 0;
    for (int id : selectedUnits_) {
        LOG_INFO("[zaznaczona] {}", w.describeUnit(id));
        ++dumped;
    }
    for (size_t i = 0; i < w.units.size(); ++i) {
        if (w.units[i].alive && w.units[i].stallTime > 2.0f) {
            LOG_INFO("[stoi] {}", w.describeUnit(static_cast<int>(i)));
            ++dumped;
        }
    }
    if (selectedBuilding_ >= 0) {
        const Building& b = w.buildings[selectedBuilding_];
        LOG_INFO("[budynek] #{} {} poz=({:.1f},{:.1f}) hp={:.0f} prac={} brakDojscia={} brakSurowca={} wZasiegu={}",
                 selectedBuilding_, buildingDef(b.type).name, b.pos.x, b.pos.z, b.hp, b.worker, b.unreachable,
                 b.lacksResources, b.resourcesInRange);
        // Flaga budynku: towary, rezerwacje tragarzy, połączenie z magazynem; tragarze, którzy mają z nią coś wspólnego.
        if (b.flag >= 0) {
            const Flag& fl = w.flags[b.flag];
            LOG_INFO("[flaga] #{} towary={} wolne={} połączona={} doZamku={} drogi={} zablokowana produkcja={}", b.flag,
                     fl.goods.size(), w.flagAvailableGoods(fl), fl.connected, fl.castleLinked, fl.roads.size(), b.outputBlocked);
            for (size_t i = 0; i < w.units.size(); ++i) {
                const Unit& u = w.units[i];
                if (!u.alive || u.faction != b.faction) continue;
                if (u.jobFlag == b.flag || u.jobDest == selectedBuilding_) {
                    LOG_INFO("[tragarz] #{} {} zlecenie: odbiór z flagi {} / dostawa {} -> budynek {} niesie {}x{}", i,
                             World::stateName(u.state), u.jobFlag, u.jobOrder, u.jobDest, u.carryCount, goodName(u.carry));
                }
            }
        }
        if (!b.constructed || b.type == BuildingType::Barracks) {
            LOG_INFO("[dostawy] dowiezione: {}", stockSummary(b.delivered, 12));
        }
        if (storageCapacity(b.type) > 0) {
            LOG_INFO("[magazyn] zapas {}/{}: {}", b.stock.total(), storageCapacity(b.type), stockSummary(b.stock, 12));
        }
        ++dumped;
    }
    world_->pushMessage("Zrzut debug: " + std::to_string(dumped) + " wpisów w konsoli (F9)", {0.7f, 0.9f, 1.0f, 1.0f});
}

} // namespace settlers
