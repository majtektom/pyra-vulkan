/**
 * app_hud.cpp - rysowanie interfejsu na ekranie (HUD).
 *
 * HUD jest rysowany w trybie "immediate": w każdej klatce od nowa dodajemy prostokąty
 * (GuiMgr::addOverlayRect) i napisy (Engine::setTextLabel). Nic nie trzeba usuwać - to, czego
 * w danej klatce nie narysujemy, po prostu znika.
 * Pozycje obiektów 3D na ekranie liczy worldToScreen (paski życia, minimapa, znaczniki).
 */
#include "settlers_app.h"

namespace settlers {

// Napis HUD w pikselach ekranu (y = linia bazowa tekstu).
void SettlersApp::text(const std::string& s, float x, float y, float scale, const Color& c, float depth) {
    engine_.setTextLabel(s, kFont, x, y, scale, c, depth);
}

// Napis wyśrodkowany względem cx (szerokość mierzy silnik: Engine::measureText).
void SettlersApp::textCentered(const std::string& s, float cx, float y, float scale, const Color& c, float depth) {
    text(s, cx - engine_.measureText(s, scale) * 0.5f, y, scale, c, depth);
}

// Pasek (życie, postęp budowy): GuiMgr::addOverlayBar z silnika.
void SettlersApp::bar(float x, float y, float w, float h, float frac, const Color& fill) {
    gui_->addOverlayBar(x, y, w, h, frac, fill);
}

// Rysuje cały interfejs dla bieżącego trybu. W menu tylko tytuł; w grze: paski życia,
// górny pasek, komunikaty, minimapa, panel zaznaczenia, ramka zaznaczania, podpowiedzi, pomoc.
void SettlersApp::drawHud(Engine& e, const InputSnapshot& in) {
    const World& w = *world_;
    if (mode_ == Mode::Menu) {
        textCentered("OSADNICY", screenW_ * 0.5f, screenH_ * 0.5f - 270.0f, 1.6f, {1.0f, 0.92f, 0.6f, 1.0f}, 0.92f);
        textCentered("strategia na silniku engine3d", screenW_ * 0.5f, screenH_ * 0.5f - 237.0f, 0.6f,
                     {0.9f, 0.9f, 0.95f, 1.0f}, 0.92f);
        text("FPS: " + std::to_string(static_cast<int>(e.getCurrentFPS())), 12.0f, screenH_ - 12.0f, 0.5f,
             {1.0f, 1.0f, 0.5f, 1.0f});
        return;
    }

    drawWorldBars();
    drawTopBar(e);
    drawMessages();
    drawMinimap();
    drawSelectionPanel();
    drawBuildBar();

    if (dragging_) {
        const float x0 = static_cast<float>(std::min(dragStartX_, in.mouseX));
        const float y0 = static_cast<float>(std::min(dragStartY_, in.mouseY));
        const float bw = static_cast<float>(std::abs(in.mouseX - dragStartX_));
        const float bh = static_cast<float>(std::abs(in.mouseY - dragStartY_));
        gui_->addOverlayRect(x0, y0, bw, bh, {0.3f, 1.0f, 0.4f, 0.12f}, 0.96f);
        gui_->addOverlayFrame(x0, y0, bw, bh, 1.5f, {0.4f, 1.0f, 0.5f, 0.9f}, 0.955f);
    }

    if (placing_ && mode_ == Mode::Game) {
        const BuildingDef& def = buildingDef(placeType_);
        const float x = static_cast<float>(in.mouseX) + 18.0f, y = static_cast<float>(in.mouseY) + 30.0f;
        text(std::string(def.name) + " (" + costText(def.cost) + ")", x, y, 0.55f, {1, 1, 1, 1});
        if (!placeReason_.empty()) text(placeReason_, x, y + 20.0f, 0.5f, {1.0f, 0.5f, 0.4f, 1.0f});
        text("LPM: postaw  PPM: anuluj  R: obróć  Shift: kilka", x, y + 40.0f, 0.42f, {0.85f, 0.85f, 0.85f, 1.0f});
    }

    if (roadMode_ && mode_ == Mode::Game) drawRoadPreview(in);

    // Zaznaczona flaga: ramka wokół jej położenia na ekranie.
    if (selectedFlag_ >= 0 && w.flags[selectedFlag_].alive) {
        float sx, sy;
        if (worldToScreen(*camera_, add(w.flags[selectedFlag_].pos, {0.0f, 2.5f, 0.0f}), screenW_, screenH_, sx, sy)) {
            gui_->addOverlayFrame(sx - 16.0f, sy - 26.0f, 32.0f, 44.0f, 2.0f, {0.4f, 1.0f, 0.5f, 0.95f}, 0.95f);
        }
    }


    if (showHelp_) drawHelp();
    if (showEconomy_) drawEconomy();
}

// Górny pasek: surowce, ludność, liczebność wojska; po prawej poziom, czas gry (z prędkością) i FPS.
void SettlersApp::drawTopBar(Engine& e) {
    const World& w = *world_;
    const FactionState& fs = w.factions[kPlayer];
    gui_->addOverlayRect(0, 0, screenW_, kTopBarH, {0.09f, 0.075f, 0.05f, 0.88f}, 0.96f);
    gui_->addOverlayRect(0, kTopBarH, screenW_, 2, {0.55f, 0.45f, 0.25f, 0.9f}, 0.96f);

    int soldiers = 0;
    for (const auto& u : w.units) {
        if (u.alive && u.faction == kPlayer && u.type != UnitType::Settler) ++soldiers;
    }
    float x = 14.0f;
    const float y = 24.0f;
    auto item = [&](const std::string& label, const std::string& value, const Color& c) {
        text(label, x, y, 0.55f, {0.85f, 0.8f, 0.65f, 1.0f});
        x += engine_.measureText(label, 0.55f) + 6.0f;
        text(value, x, y, 0.6f, c);
        x += engine_.measureText(value, 0.6f) + 26.0f;
    };
    // Najważniejsze towary (pełna lista: okno gospodarki - I): magazyny + wolne towary na flagach.
    const Resources have = world_->spendable(kPlayer);
    item("Deski:", std::to_string(have[CarryType::Planks]), {0.95f, 0.75f, 0.45f, 1.0f});
    item("Kamień:", std::to_string(have[CarryType::Stone]), {0.85f, 0.85f, 0.9f, 1.0f});
    item("Drewno:", std::to_string(have[CarryType::Wood]), {0.8f, 0.6f, 0.4f, 1.0f});
    item("Żywność:", std::to_string(have.food()), {0.95f, 0.9f, 0.4f, 1.0f});
    item("Broń:", std::to_string(have[CarryType::Weapons]), {0.85f, 0.9f, 1.0f, 1.0f});
    const int used = w.storageUsedOf(kPlayer), cap = w.storageCapacityOf(kPlayer);
    item("Magazyn:", std::to_string(used) + "/" + std::to_string(cap),
         used * 10 >= cap * 9 ? Color{1.0f, 0.5f, 0.4f, 1.0f} : Color{0.85f, 0.85f, 0.75f, 1.0f});
    item("Ludność:", std::to_string(fs.population) + "/" + std::to_string(fs.popCap) + (fs.popCap >= kMaxPopulation ? " (maks.)" : ""),
         fs.population >= fs.popCap ? Color{1.0f, 0.5f, 0.4f, 1.0f} : Color{0.8f, 1.0f, 0.8f, 1.0f});
    item("Wojsko:", std::to_string(soldiers), {0.7f, 0.85f, 1.0f, 1.0f});

    const int t = static_cast<int>(w.gameTime);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", t / 60, t % 60);
    static const char* diffNames[] = {"Łatwy", "Normalny", "Trudny"};
    std::string right = std::string("Poziom: ") + diffNames[static_cast<int>(w.difficulty)] + "   Czas: " + buf;
    if (gameSpeed_ != 1) right += " (x" + std::to_string(gameSpeed_) + ")";
    right += "   FPS: " + std::to_string(static_cast<int>(e.getCurrentFPS())) + "   I: gospodarka   F1: pomoc";
    if (spectator_) {
        const std::string tag = "POKAZ: AI kontra AI";
        const float tw = engine_.measureText(tag, 0.6f);
        gui_->addOverlayRect(screenW_ * 0.5f - tw * 0.5f - 10.0f, kTopBarH + 4.0f, tw + 20.0f, 26.0f, {0.35f, 0.1f, 0.08f, 0.85f}, 0.955f);
        textCentered(tag, screenW_ * 0.5f, kTopBarH + 23.0f, 0.6f, {1.0f, 0.9f, 0.6f, 1.0f});
    }
    text(right, screenW_ - engine_.measureText(right, 0.5f) - 14.0f, y, 0.5f, {0.85f, 0.85f, 0.85f, 1.0f});
}

// Ostatnie komunikaty gry na środku u góry; po kilku sekundach zanikają (alfa maleje).
void SettlersApp::drawMessages() {
    const World& w = *world_;
    float y = kTopBarH + (spectator_ ? 56.0f : 30.0f);
    for (const auto& m : w.messages) {
        const float age = w.gameTime - m.time;
        const float alpha = std::clamp(9.0f - age, 0.0f, 1.0f);
        Color c = m.color;
        c[3] *= alpha;
        textCentered(m.text, screenW_ * 0.5f, y, 0.58f, c);
        y += 24.0f;
    }
}

// Paski życia nad jednostkami i budynkami (tylko gdy coś się dzieje: zaznaczony, ranny, w budowie)
// oraz stałe znaczniki problemów nad budynkami gracza (brak drzew, brak dojścia, naprawa).
void SettlersApp::drawWorldBars() {
    const World& w = *world_;
    const float maxDist = 900.0f;
    const Vec3 eye = camera_->getOwner()->transform().position;
    for (const auto& u : w.units) {
        if (!u.alive) continue;
        const UnitDef& def = unitDef(u.type);
        const bool show = u.selected || u.hp < def.maxHp || w.gameTime - u.lastDamagedTime < 4.0f;
        if (!show) continue;
        if (length2(subtract(u.pos, eye)) > maxDist * maxDist) continue;
        float sx, sy;
        if (!worldToScreen(*camera_, add(u.pos, {0.0f, def.height + 1.6f, 0.0f}), screenW_, screenH_, sx, sy)) continue;
        const float bw = 26.0f;
        const Color fill = u.faction == kPlayer ? hpColor(u.hp / def.maxHp) : Color{0.95f, 0.25f, 0.2f, 1.0f};
        bar(sx - bw * 0.5f, sy, bw, 4.0f, u.hp / def.maxHp, fill);
    }
    for (const auto& b : w.buildings) {
        if (!b.alive) continue;
        const BuildingDef& def = buildingDef(b.type);
        const bool show = b.selected || !b.constructed || b.hp < def.maxHp * 0.999f || w.gameTime - b.lastDamagedTime < 4.0f;
        if (!show) continue;
        if (length2(subtract(b.pos, eye)) > maxDist * maxDist * 1.4f) continue;
        const float top = b.constructed ? def.height + 3.0f : def.height * 0.75f + 3.0f;
        float sx, sy;
        if (!worldToScreen(*camera_, add(b.pos, {0.0f, top, 0.0f}), screenW_, screenH_, sx, sy)) continue;
        const float bw = 54.0f;
        const Color fill = b.faction == kPlayer ? hpColor(b.hp / def.maxHp) : Color{0.95f, 0.25f, 0.2f, 1.0f};
        bar(sx - bw * 0.5f, sy, bw, 5.0f, b.hp / def.maxHp, fill);
        if (!b.constructed) {
            bar(sx - bw * 0.5f, sy + 8.0f, bw, 4.0f, b.progress, {0.35f, 0.65f, 1.0f, 1.0f});
        }
    }
    // Znaczniki problemów nad budynkami gracza (widoczne zawsze)
    for (const auto& b : w.buildings) {
        if (!b.alive || b.faction != kPlayer) continue;
        const BuildingDef& def = buildingDef(b.type);
        std::string label;
        Color color{1.0f, 0.55f, 0.3f, 1.0f};
        const bool needsRoad = producesGoods(b.type) || b.type == BuildingType::Warehouse;
        if (b.unreachable) {
            label = "! brak dojścia";
        } else if (!b.constructed) {
            if (def.cost.total() <= 0 || w.flagConnected(b.flag)) continue;
            label = "! brak drogi - brak materiałów";
        } else if (needsRoad && !w.buildingLinked(b)) {
            label = b.type == BuildingType::Warehouse ? "! brak drogi do zamku" : "! brak drogi do magazynu";
        } else if (b.outputBlocked) {
            switch (w.flagBlockReason(b.flag)) {
            case World::FlagBlock::StorageFull: label = "! pełna flaga - magazyny pełne"; break;
            case World::FlagBlock::StockLimit:
                label = "pełna flaga - limit zapasu";
                color = {0.85f, 0.85f, 0.7f, 1.0f};   // ustawienie gracza, nie problem
                break;
            default: label = "! pełna flaga - za mało tragarzy"; break;
            }
        } else if (b.lacksResources) {
            label = b.type == BuildingType::Woodcutter ? "! brak drzew" : "! brak kamienia";
        } else if (b.inputStarved && b.worker >= 0) {
            label = "! brak dostaw - praca stoi";
        } else if (b.repairBlocked) {
            label = "! brak desek na naprawę";
        } else if (w.needsRepairPublic(b) && w.gameTime - b.lastDamagedTime > 8.0f) {
            label = "naprawa";
            color = {0.6f, 0.85f, 1.0f, 1.0f};
        } else {
            continue;
        }
        float sx, sy;
        if (!worldToScreen(*camera_, add(b.pos, {0.0f, def.height + 7.0f, 0.0f}), screenW_, screenH_, sx, sy)) continue;
        const float tw = engine_.measureText(label, 0.5f);
        gui_->addOverlayRect(sx - tw * 0.5f - 5.0f, sy - 16.0f, tw + 10.0f, 21.0f, {0.08f, 0.05f, 0.03f, 0.8f}, 0.952f);
        text(label, sx - tw * 0.5f, sy, 0.5f, color);
    }
}

// Minimapa: kolory terenu (policzone raz przy tworzeniu świata), budynki i jednostki obu stron
// oraz prostokąt pokazujący obszar widziany przez kamerę.
void SettlersApp::drawMinimap() {
    const World& w = *world_;
    const MinimapRect r = minimapRect();
    gui_->addOverlayRect(r.x - 3, r.y - 3, r.size + 6, r.size + 6, {0.55f, 0.45f, 0.25f, 0.95f}, 0.97f);
    const int n = w.minimapSize();
    const float cell = r.size / static_cast<float>(n);
    const auto& colors = w.minimapColors();
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const Vec3& c = colors[static_cast<size_t>(y) * n + x];
            gui_->addOverlayRect(r.x + x * cell, r.y + y * cell, cell + 0.5f, cell + 0.5f, {c.x, c.y, c.z, 1.0f}, 0.965f);
        }
    }
    auto toMap = [&](const Vec3& p, float& mx, float& my) {
        mx = r.x + (p.x + kWorldHalf) / (kWorldHalf * 2.0f) * r.size;
        my = r.y + (p.z + kWorldHalf) / (kWorldHalf * 2.0f) * r.size;
    };
    for (const auto& b : w.buildings) {
        if (!b.alive) continue;
        float mx, my;
        toMap(b.pos, mx, my);
        const Vec3 fc = factionColor(b.faction);
        const float s = b.type == BuildingType::Castle ? 8.0f : 5.0f;
        gui_->addOverlayRect(mx - s * 0.5f - 1, my - s * 0.5f - 1, s + 2, s + 2, {0, 0, 0, 1}, 0.962f);
        gui_->addOverlayRect(mx - s * 0.5f, my - s * 0.5f, s, s, {fc.x, fc.y, fc.z, 1.0f}, 0.96f);
    }
    for (const auto& u : w.units) {
        if (!u.alive || u.state == UnitState::Garrison) continue;
        float mx, my;
        toMap(u.pos, mx, my);
        const Vec3 fc = u.faction == kPlayer ? Vec3{0.5f, 0.75f, 1.0f} : Vec3{1.0f, 0.45f, 0.4f};
        gui_->addOverlayRect(mx - 1.0f, my - 1.0f, 2.5f, 2.5f, {fc.x, fc.y, fc.z, 1.0f}, 0.958f);
    }
    // Widok kamery
    float cx, cy;
    toMap(cam_.focus(), cx, cy);
    const float vs = std::clamp(cam_.distance() / (kWorldHalf * 2.0f) * r.size * 1.4f, 10.0f, r.size * 0.8f);
    gui_->addOverlayFrame(cx - vs * 0.5f, cy - vs * 0.35f, vs, vs * 0.7f, 1.0f, {1, 1, 1, 0.9f}, 0.955f);
}

// Panel na dole ekranu: szczegóły zaznaczonego budynku, jednej jednostki albo podsumowanie grupy.
void SettlersApp::drawSelectionPanel() {
    const World& w = *world_;
    selPanelW_ = 470;
    selPanelX_ = static_cast<int>(screenW_ * 0.5f) - selPanelW_ / 2;
    const int panelH = 132;
    selPanelH_ = panelH;
    // Panel stoi nad paskiem budowy (albo przy dolnej krawędzi, gdy paska nie ma - pokaz AI).
    const float bottom = buildBarVisible() ? buildBar_.y() - 14.0f : screenH_ - 12.0f;
    selPanelY_ = static_cast<int>(bottom) - panelH;
    if (!hasSelectionPanel()) return;
    const float px = static_cast<float>(selPanelX_), py = static_cast<float>(selPanelY_);
    gui_->addOverlayRect(px - 3, py - 3, selPanelW_ + 6.0f, panelH + 6.0f, {0.55f, 0.45f, 0.25f, 0.95f}, 0.97f);
    gui_->addOverlayRect(px, py, static_cast<float>(selPanelW_), static_cast<float>(panelH), {0.1f, 0.085f, 0.06f, 0.92f}, 0.965f);

    const Color title{1.0f, 0.92f, 0.65f, 1.0f};
    const Color body{0.9f, 0.9f, 0.88f, 1.0f};

    if (selectedFlag_ >= 0) {
        const Flag& fl = w.flags[selectedFlag_];
        text(fl.building >= 0 ? std::string("Flaga: ") + buildingDef(w.buildings[fl.building].type).name : "Flaga", px + 14, py + 26,
             0.7f, title);
        int counts[kGoodTypeCount + 1] = {};
        for (CarryType g : fl.goods) counts[static_cast<int>(g)]++;
        std::string goods = "Towary: " + std::to_string(fl.goods.size()) + "/" + std::to_string(kFlagCapacity);
        for (int t = 1; t <= kGoodTypeCount; ++t) {
            if (counts[t] > 0) goods += "   " + std::string(goodName(static_cast<CarryType>(t))) + ": " + std::to_string(counts[t]);
        }
        const int reserved = static_cast<int>(fl.goods.size()) - w.flagAvailableGoods(fl);
        if (reserved > 0) goods += "   (tragarze idą po " + std::to_string(reserved) + ")";
        text(goods, px + 14, py + 54, 0.52f, body);
        text("Drogi: " + std::to_string(fl.roads.size()) + "   " +
                 (fl.connected ? "połączona z magazynem" : "BRAK POŁĄCZENIA Z MAGAZYNEM"),
             px + 14, py + 80, 0.5f, fl.connected ? body : Color{1.0f, 0.55f, 0.35f, 1.0f});
        text("T: droga od tej flagi   Del: usuń flagę i jej drogi", px + 14, py + 112, 0.45f, {0.75f, 0.75f, 0.7f, 1.0f});
        return;
    }

    if (selectedBuilding_ >= 0) {
        const Building& b = w.buildings[selectedBuilding_];
        const BuildingDef& def = buildingDef(b.type);
        text(std::string(def.name) + (b.faction == kPlayer ? "" : " (wróg)"), px + 14, py + 26, 0.7f, title);
        bar(px + 14, py + 38, 200, 8, b.hp / def.maxHp, hpColor(b.hp / def.maxHp));
        text(std::to_string(static_cast<int>(b.hp)) + " / " + std::to_string(static_cast<int>(def.maxHp)), px + 224, py + 47,
             0.45f, body);
        std::string status;
        std::string resLine;
        Color resColor{0.8f, 0.95f, 0.8f, 1.0f};
        if (!b.constructed) {
            status = "W budowie: " + std::to_string(static_cast<int>(b.progress * 100.0f)) + "%   Materiały: " +
                     std::to_string(b.delivered.total()) + "/" + std::to_string(def.cost.total());
            if (b.faction == kPlayer && def.cost.total() > 0) {
                if (!w.flagConnected(b.flag)) {
                    resLine = "Brak drogi do magazynu - tragarze nie dowiozą materiałów (T: droga)";
                    resColor = {1.0f, 0.5f, 0.35f, 1.0f};
                } else if (b.delivered.total() < def.cost.total()) {
                    resLine = "Tragarze donoszą materiały z magazynu";
                    resColor = {0.8f, 0.9f, 1.0f, 1.0f};
                }
            }
        } else if (def.needsWorker) {
            status = (b.worker >= 0 && w.units[b.worker].alive) ? "Pracownik: jest" : "Pracownik: brak (czeka na osadnika)";
            if (b.produced > 0) {
                status += (b.type == BuildingType::Forester ? "   Posadzono: " : "   Dostarczono: ") + std::to_string(b.produced);
            }
            if (b.type == BuildingType::Forester) {
                resLine = "Rosnące sadzonki w okolicy: " + std::to_string(w.saplingsNear(b.pos, def.workRadius)) +
                          " (rosną ok. " + std::to_string(static_cast<int>(kTreeGrowSeconds / 60.0f)) + " min)";
            }
            if (b.type == BuildingType::Woodcutter || b.type == BuildingType::Quarry) {
                const bool tree = b.type == BuildingType::Woodcutter;
                if (b.lacksResources) {
                    resLine = tree ? "BRAK DRZEW - postaw leśniczego obok albo drwala bliżej lasu"
                                   : "BRAK SKAŁ - postaw kamieniarza przy skałach albo kopalnię w górach";
                    resColor = {1.0f, 0.5f, 0.35f, 1.0f};
                } else if (b.resourcesInRange >= 0) {
                    resLine = std::string(tree ? "Drzewa" : "Skały") + " w zasięgu pracy: " + std::to_string(b.resourcesInRange);
                    if (tree) resLine += ", rośnie: " + std::to_string(w.saplingsNear(b.pos, def.workRadius));
                    if (b.resourcesInRange <= 2) resColor = {1.0f, 0.85f, 0.45f, 1.0f};
                }
            }
        } else if (b.type == BuildingType::Barracks) {
            status = "Kolejka: " + std::to_string(b.trainQueue.size()) +
                     (b.autoTrain ? "   Auto: " + std::to_string(b.swordShare) + "% mieczników, " +
                                        std::to_string(100 - b.swordShare) + "% łuczników"
                                  : "   Auto: wył.");
            if (!b.trainQueue.empty() && !b.trainPaid) {
                resLine = w.flagConnected(b.flag) ? "Czeka, aż tragarze dowiozą koszt szkolenia"
                                                  : "Brak drogi do magazynu - koszt szkolenia nie dotrze";
                resColor = {1.0f, 0.75f, 0.4f, 1.0f};
            }
            if (!b.trainQueue.empty()) {
                const float total = unitDef(b.trainQueue.front()).trainTime;
                status += std::string("   Szkoli: ") + unitDef(b.trainQueue.front()).name + " " +
                          std::to_string(static_cast<int>(std::min(1.0f, b.trainTimer / total) * 100.0f)) + "%";
            }
        } else if (def.popBonus > 0) {
            status = "Mieszkańcy: +" + std::to_string(def.popBonus);
        } else if (World::garrisonCapacity(b.type) > 0 && b.constructed) {
            const int g = w.garrisonOf(selectedBuilding_), cap = World::garrisonCapacity(b.type);
            status = "Załoga: " + std::to_string(g) + "/" + std::to_string(cap) + (g > 0 ? "   strzela" : "   PUSTA - nie strzela");
            if (b.faction == kPlayer) {
                resLine = g > 0 ? "PPM z zaznaczonym wojskiem: dołóż żołnierzy" : "Pusta wieża - wróg może ją zdobyć! Obsadź (PPM z wojskiem)";
                resColor = g > 0 ? Color{0.8f, 0.9f, 1.0f, 1.0f} : Color{1.0f, 0.55f, 0.35f, 1.0f};
            } else {
                resLine = g > 0 ? "Wroga załoga - szturmuj, obrońcy wyjdą do walki" : "Pusta wieża - żołnierz, który do niej wejdzie, zdobędzie ją";
                resColor = {1.0f, 0.85f, 0.5f, 1.0f};
            }
        } else if (def.attackRange > 0.0f) {
            status = "Zasięg strzału: " + std::to_string(static_cast<int>(def.attackRange));
        }
        if (b.constructed && b.hp < def.maxHp * 0.97f) {
            const int rep = w.repairersOf(selectedBuilding_);
            if (b.repairBlocked) status += "   Naprawa: brak desek";
            else if (rep > 0) status += "   Naprawia: " + std::to_string(rep);
            else if (w.gameTime - b.lastDamagedTime < 8.0f) status += "   Naprawa po walce";
            else status += "   Czeka na naprawę";
        }
        // Magazyn: zapasy; warsztat: towary na fladze i połączenie z magazynem.
        if (b.constructed && storageCapacity(b.type) > 0) {
            resLine = "Zapas " + std::to_string(b.stock.total()) + "/" + std::to_string(storageCapacity(b.type)) + ": " +
                      stockSummary(b.stock, 5);
            resColor = b.stock.total() >= storageCapacity(b.type) ? Color{1.0f, 0.55f, 0.35f, 1.0f} : Color{0.8f, 0.95f, 0.8f, 1.0f};
            if (b.type == BuildingType::Warehouse && !w.buildingLinked(b)) {
                resLine = "Brak drogi do zamku - połącz Magazyn z siecią dróg ('Połącz drogą' lub T)";
                resColor = {1.0f, 0.5f, 0.35f, 1.0f};
            }
        }
        // Warsztat z towarami wejściowymi: co ma u siebie i czego brakuje.
        if (b.constructed && b.faction == kPlayer) {
            std::string inputs;
            for (CarryType in : productionDef(b.type).inputs) {
                if (in == CarryType::None) continue;
                const int have = in == CarryType::AnyFood ? b.delivered.food() : b.delivered[in];
                inputs += (inputs.empty() ? "" : ", ") + std::string(goodName(in)) + " " + std::to_string(have);
            }
            if (!inputs.empty()) {
                resLine = "Dostarczone: " + inputs;
                resColor = {0.8f, 0.95f, 0.8f, 1.0f};
                if (b.inputStarved) {
                    resLine += "  - BRAK w magazynach, praca stoi";
                    resColor = {1.0f, 0.6f, 0.4f, 1.0f};
                }
            }
        }
        if (b.constructed && producesGoods(b.type) && b.flag >= 0) {
            const Flag& fl = w.flags[b.flag];
            status += "   Na fladze: " + std::to_string(fl.goods.size()) + "/" + std::to_string(kFlagCapacity);
            if (!fl.connected) {
                resLine = "Brak drogi do magazynu - towary nie dotrą (T: droga, albo 'Połącz drogą')";
                resColor = {1.0f, 0.5f, 0.35f, 1.0f};
            } else if (b.outputBlocked) {
                switch (w.flagBlockReason(b.flag)) {
                case World::FlagBlock::StorageFull:
                    resLine = "Flaga pełna - magazyny pełne: zbuduj Magazyn albo obniż limity zapasu (I)";
                    break;
                case World::FlagBlock::StockLimit:
                    resLine = "Flaga pełna - zapas osiągnął limit (I); towar idzie prosto do odbiorców";
                    break;
                default: resLine = "Flaga pełna - produkcja wstrzymana, czeka na tragarza (I: tragarze)"; break;
                }
                resColor = {1.0f, 0.75f, 0.4f, 1.0f};
            }
        }
        if (b.unreachable && b.faction == kPlayer) {
            resLine = "Osadnicy nie mogą tu dojść - udrożnij drogę lub zburz (Del)";
            resColor = {1.0f, 0.5f, 0.35f, 1.0f};
        }
        text(status, px + 14, py + 76, 0.52f, body);
        if (!resLine.empty()) {
            text(resLine, px + 14, py + 104, 0.5f, resColor);
        } else {
            text(def.description, px + 14, py + 104, 0.45f, {0.75f, 0.75f, 0.7f, 1.0f});
        }
        return;
    }

    if (selectedUnits_.size() == 1) {
        const Unit& u = w.units[selectedUnits_[0]];
        const UnitDef& def = unitDef(u.type);
        text(std::string(def.name) + (u.faction == kPlayer ? "" : " (wróg)"), px + 14, py + 26, 0.7f, title);
        bar(px + 14, py + 38, 200, 8, u.hp / def.maxHp, hpColor(u.hp / def.maxHp));
        text(std::to_string(static_cast<int>(u.hp)) + " / " + std::to_string(static_cast<int>(def.maxHp)), px + 224, py + 47,
             0.45f, body);
        std::string st = stateText(u, w);
        if (u.type == UnitType::Settler && u.building >= 0 && w.buildings[u.building].alive &&
            w.buildings[u.building].constructed && (u.state == UnitState::GoBuild || u.state == UnitState::Building)) {
            st = u.state == UnitState::Building ? "Naprawia budynek" : "Idzie naprawić budynek";
        }
        if (u.hp < def.maxHp && w.gameTime - u.lastDamagedTime >= 8.0f && u.state != UnitState::Attack) st += "  (leczy się)";
        text(st, px + 14, py + 76, 0.55f, body);
        if (u.type == UnitType::Settler) {
            std::string job = "Osadnicy pracują samodzielnie - budują, obsługują warsztaty i noszą towary.";
            if (u.building >= 0 && w.buildings[u.building].alive) {
                job = std::string("Miejsce: ") + buildingDef(w.buildings[u.building].type).name;
            }
            text(job, px + 14, py + 104, 0.45f, {0.75f, 0.75f, 0.7f, 1.0f});
        } else {
            text("Atak: " + std::to_string(static_cast<int>(def.damage)) + "   Zasięg: " +
                     std::to_string(static_cast<int>(def.range)) + "   PPM: ruch / atak, Ctrl+PPM: marsz bojowy, H: stój",
                 px + 14, py + 104, 0.45f, {0.75f, 0.75f, 0.7f, 1.0f});
        }
        return;
    }

    // Wiele jednostek
    std::map<std::string, int> counts;
    for (int id : selectedUnits_) counts[unitDef(w.units[id].type).name]++;
    std::string summary = "Zaznaczono: ";
    bool first = true;
    for (const auto& [name, c] : counts) {
        if (!first) summary += ", ";
        summary += std::to_string(c) + "x " + name;
        first = false;
    }
    text(summary, px + 14, py + 26, 0.62f, title);
    const int perRow = 18;
    for (size_t i = 0; i < selectedUnits_.size() && i < 54; ++i) {
        const Unit& u = w.units[selectedUnits_[i]];
        const UnitDef& def = unitDef(u.type);
        const float ix = px + 14 + static_cast<float>(i % perRow) * 24.0f;
        const float iy = py + 40 + static_cast<float>(i / perRow) * 22.0f;
        const Vec3 fc = u.type == UnitType::Settler ? Vec3{0.8f, 0.7f, 0.4f}
                                                     : (u.type == UnitType::Archer ? Vec3{0.4f, 0.7f, 0.3f} : Vec3{0.6f, 0.65f, 0.75f});
        gui_->addOverlayRect(ix, iy, 20, 14, {fc.x, fc.y, fc.z, 1.0f}, 0.96f);
        bar(ix, iy + 16, 20, 3, u.hp / def.maxHp, hpColor(u.hp / def.maxHp));
    }
    text("PPM: ruch / atak   Ctrl+PPM: marsz bojowy   H: stój   Tab: całe wojsko", px + 14, py + 120, 0.45f,
         {0.75f, 0.75f, 0.7f, 1.0f});
}

// Pomoc F1: lista sterowania w dwóch kolumnach (z kControls) i zasady gry.
void SettlersApp::drawHelp() {
    // Dwie kolumny sterowania (wspólna lista z oknem "Sterowanie") + zasady gry
    // Poniżej strefy komunikatów (do 6 linii pod górnym paskiem)
    const float w = std::min(screenW_ - 40.0f, 1100.0f), h = 640.0f;
    const float x = screenW_ * 0.5f - w * 0.5f, y = std::max(kTopBarH + 10.0f, std::min(kTopBarH + 180.0f, screenH_ - h - 20.0f));
    gui_->addOverlayRect(x - 3, y - 3, w + 6, h + 6, {0.55f, 0.45f, 0.25f, 0.95f}, 0.94f);
    gui_->addOverlayRect(x, y, w, h, {0.08f, 0.07f, 0.05f, 0.94f}, 0.935f);
    const Color headerC{1.0f, 0.9f, 0.6f, 1.0f};
    const size_t total = std::size(kControls);
    // Druga kolumna zaczyna się od sekcji BUDOWA (szukamy nagłówka, żeby dopisanie wiersza nie psuło podziału).
    size_t split = 0;
    for (size_t i = 0; i < total; ++i) {
        if (kControls[i].key[0] == '\0' && std::string(kControls[i].desc) == "BUDOWA") split = i;
    }
    const float colW = w * 0.5f;
    for (int col = 0; col < 2; ++col) {
        float ly = y + 30.0f;
        const size_t from = col == 0 ? 0 : split, to = col == 0 ? split : total;
        for (size_t i = from; i < to; ++i) {
            const auto& c = kControls[i];
            const float cx = x + 18.0f + col * colW;
            if (c.key[0] == '\0') {
                if (ly > y + 30.0f) ly += 6.0f;
                text(c.desc, cx, ly, 0.55f, headerC, 0.93f);
            } else {
                text(c.key, cx + 8.0f, ly, 0.46f, {0.75f, 0.9f, 1.0f, 1.0f}, 0.93f);
                text(c.desc, cx + 185.0f, ly, 0.46f, {0.9f, 0.9f, 0.9f, 1.0f}, 0.93f);
            }
            ly += 21.0f;
        }
        if (col == 1) {
            static const char* rules[] = {
                "Osadnicy sami budują i pracują. Zamek przyjmuje nowych za żywność.",
                "Warsztaty odkładają towary na fladze (max 8) - połącz je drogą z magazynem.",
                "Tragarze niosą towary całą trasą do magazynu i materiały na budowy (I: ilu).",
                "Po drogach chodzi się szybciej. Towar z flagi warsztatu idzie też prosto do odbiorcy.",
                "Zamek i Magazyn mieszczą po 200 towarów. Limity zapasu (I) chronią przed zapchaniem.",
                "Domy zwiększają limit ludności. Wieże poszerzają terytorium.",
                "Wieża strzela tylko z załogą; pustą wieżę wroga zdobywa żołnierz.",
                "Budowa i naprawy: deski z Tartaku (kłody od Drwala) i kamień.",
                "Kopalnie zjadają żywność; Huta + Kuźnia robią broń dla Koszar.",
                "Koszary: Auto-rekrutacja szkoli sama (proporcja mieczników -/+).",
                "Jednostki poza walką leczą się - najszybciej przy zamku i koszarach.",
                "Zniszcz wrogi zamek, by wygrać!",
            };
            ly += 8.0f;
            text("ROZGRYWKA", x + 18.0f + colW, ly, 0.55f, headerC, 0.93f);
            ly += 21.0f;
            for (const char* r : rules) {
                text(r, x + 26.0f + colW, ly, 0.44f, {0.9f, 0.9f, 0.9f, 1.0f}, 0.93f);
                ly += 20.0f;
            }
        }
    }
    text("F1 - zamknij pomoc   |   Menu (ESC) -> Sterowanie / Ustawienia", x + 18.0f, y + h - 12.0f, 0.44f,
         {0.7f, 0.7f, 0.7f, 1.0f}, 0.93f);
}

// Podgląd budowanej drogi: linia trasy (zielona = można, czerwona = nie) i podpowiedź przy kursorze.
void SettlersApp::drawRoadPreview(const InputSnapshot& in) {
    const World& w = *world_;
    const Color ok{0.45f, 1.0f, 0.45f, 0.9f}, bad{1.0f, 0.35f, 0.3f, 0.9f};
    if (roadStart_ >= 0) {
        float sx, sy;
        if (project(onGround(w.flags[roadStart_].pos, 2.0f), sx, sy)) {
            gui_->addOverlayFrame(sx - 14.0f, sy - 14.0f, 28.0f, 28.0f, 2.0f, ok, 0.95f);
        }
        float px = 0, py = 0;
        bool havePrev = false;
        for (size_t i = 0; i < roadPreview_.size(); ++i) {
            float x, y;
            const bool vis = project(onGround(roadPreview_[i], 0.6f), x, y);
            if (vis && havePrev) debugLine(px, py, x, y, roadPreviewOk_ ? ok : bad, 3.0f);
            havePrev = vis;
            px = x;
            py = y;
        }
    }
    const float x = static_cast<float>(in.mouseX) + 18.0f, y = static_cast<float>(in.mouseY) + 30.0f;
    text(roadStart_ < 0 ? "Droga: wskaż flagę lub budynek początkowy" : "Droga: LPM - poprowadź do tego miejsca", x, y, 0.55f,
         {1, 1, 1, 1});
    if (!roadReason_.empty()) text(roadReason_, x, y + 20.0f, 0.5f, {1.0f, 0.5f, 0.4f, 1.0f});
    text("PPM / ESC: koniec   Shift: buduj dalej od istniejącej flagi", x, y + 40.0f, 0.42f, {0.85f, 0.85f, 0.85f, 1.0f});
}

// Położenie panelu gospodarki (wspólne dla rysowania i okienka z przyciskami tragarzy).
void SettlersApp::economyPanelRect(float& x, float& y, float& w, float& h) const {
    w = 680.0f;
    h = 500.0f;
    x = screenW_ * 0.5f - w * 0.5f;
    y = std::max(kTopBarH + 10.0f, screenH_ * 0.5f - h * 0.5f - 40.0f);
}

// Okno gospodarki (klawisz I): gdzie są towary, zapełnienie magazynów, tragarze i przestoje warsztatów.
// Krótki opis zapasu: n towarów, których jest najwięcej ("Deski 40, Kamień 30, Chleb 12").
std::string SettlersApp::stockSummary(const Resources& r, int maxItems) {
    std::vector<std::pair<int, CarryType>> items;
    for (int t = 1; t <= kGoodTypeCount; ++t) {
        if (r.v[t] > 0) items.push_back({r.v[t], static_cast<CarryType>(t)});
    }
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::string out;
    for (size_t i = 0; i < items.size() && static_cast<int>(i) < maxItems; ++i) {
        out += (out.empty() ? "" : ", ") + std::string(goodName(items[i].second)) + " " + std::to_string(items[i].first);
    }
    if (static_cast<int>(items.size()) > maxItems) out += "...";
    return out.empty() ? "pusto" : out;
}

void SettlersApp::drawEconomy() {
    const World& w = *world_;
    float x, y, pw, ph;
    economyPanelRect(x, y, pw, ph);
    gui_->addOverlayRect(x - 3, y - 3, pw + 6, ph + 6, {0.55f, 0.45f, 0.25f, 0.95f}, 0.94f);
    gui_->addOverlayRect(x, y, pw, ph, {0.08f, 0.07f, 0.05f, 0.95f}, 0.935f);
    const Color head{1.0f, 0.9f, 0.6f, 1.0f}, body{0.9f, 0.9f, 0.9f, 1.0f}, dim{0.7f, 0.7f, 0.68f, 1.0f};
    const Color warn{1.0f, 0.6f, 0.4f, 1.0f};

    // Towary: w magazynach / na flagach / niesione (tragarze i pracownicy)
    Resources onFlags, carried;
    for (const auto& fl : w.flags) {
        if (!fl.alive || fl.faction != kPlayer) continue;
        for (CarryType g : fl.goods) resourceRef(onFlags, g) += 1;
    }
    for (const auto& u : w.units) {
        if (!u.alive || u.faction != kPlayer || u.carry == CarryType::None) continue;
        const bool carrier = u.state == UnitState::CarrierWalk;
        resourceRef(carried, u.carry) += carrier ? u.carryCount : kGoodsPerTrip;
    }
    const Resources& stored = w.factions[kPlayer].res;

    float ly = y + 32.0f;
    text("GOSPODARKA", x + 18.0f, ly, 0.65f, head, 0.93f);
    ly += 30.0f;
    // 12 towarów w dwóch kolumnach: nazwa, w magazynach, na flagach, niesione, limit zapasu (przyciski - / +).
    const FactionState& fsl = w.factions[kPlayer];
    const float mx = static_cast<float>(engine_.input().mouseX), my = static_cast<float>(engine_.input().mouseY);
    limitButtons_.clear();
    for (int col = 0; col < 2; ++col) {
        const float cx = x + 18.0f + col * 330.0f;
        text("Towar", cx, ly, 0.44f, dim, 0.93f);
        text("magazyn", cx + 92.0f, ly, 0.44f, dim, 0.93f);
        text("flagi", cx + 158.0f, ly, 0.44f, dim, 0.93f);
        text("w drodze", cx + 200.0f, ly, 0.44f, dim, 0.93f);
        text("limit", cx + 268.0f, ly, 0.44f, dim, 0.93f);
    }
    ly += 22.0f;
    const int rows = (kGoodTypeCount + 1) / 2;
    for (int t = 1; t <= kGoodTypeCount; ++t) {
        const auto g = static_cast<CarryType>(t);
        const int col = (t - 1) / rows, row = (t - 1) % rows;
        const float cx = x + 18.0f + col * 330.0f, ry = ly + row * 20.0f;
        gui_->addOverlayRect(cx, ry - 10.0f, 9.0f, 9.0f, goodColor(g), 0.932f);
        text(goodName(g), cx + 14.0f, ry, 0.48f, body, 0.93f);
        text(std::to_string(stored[g]), cx + 92.0f, ry, 0.48f, {0.7f, 1.0f, 0.7f, 1.0f}, 0.93f);
        text(std::to_string(onFlags[g]), cx + 158.0f, ry, 0.48f, body, 0.93f);
        text(std::to_string(carried[g]), cx + 200.0f, ry, 0.48f, body, 0.93f);
        // Limit: [-] wartość [+]; osiągnięty limit na żółto.
        const int lim = fsl.stockLimit[t];
        const bool reached = lim < kNoStockLimit && stored[g] >= lim;
        for (int k = 0; k < 2; ++k) {
            const float bx = cx + (k == 0 ? 248.0f : 300.0f), by = ry - 13.0f;
            const bool hover = mx >= bx && mx <= bx + 15.0f && my >= by && my <= by + 15.0f;
            gui_->addOverlayRect(bx, by, 15.0f, 15.0f, hover ? Color{0.45f, 0.38f, 0.22f, 1.0f} : Color{0.25f, 0.21f, 0.13f, 1.0f}, 0.932f);
            gui_->addOverlayTextCentered(k == 0 ? "-" : "+", bx + 7.5f, by + 12.0f, 0.46f, head, 0.93f);
            limitButtons_.push_back({bx, by, 15.0f, g, k == 0 ? -1 : +1});
        }
        gui_->addOverlayTextCentered(lim >= kNoStockLimit ? "bez" : std::to_string(lim), cx + 282.0f, ry, 0.46f,
                                     reached ? Color{1.0f, 0.85f, 0.4f, 1.0f} : body, 0.93f);
    }
    ly += rows * 20.0f;
    text("Wydawać można tylko towary z magazynów i flag. Powyżej limitu tragarze nie wnoszą towaru do magazynu -",
         x + 18.0f, ly + 4.0f, 0.42f, dim, 0.93f);
    text("zostaje na fladze warsztatu i idzie prosto do odbiorców (magazyny nie zapychają się jednym towarem).",
         x + 18.0f, ly + 20.0f, 0.42f, dim, 0.93f);
    ly += 48.0f;

    // Magazyny
    text("MAGAZYNY  " + std::to_string(w.storageUsedOf(kPlayer)) + " / " + std::to_string(w.storageCapacityOf(kPlayer)), x + 18.0f,
         ly, 0.55f, head, 0.93f);
    ly += 24.0f;
    int listed = 0;
    for (const auto& b : w.buildings) {
        if (!b.alive || b.faction != kPlayer || storageCapacity(b.type) <= 0) continue;
        if (listed >= 4) break;
        const int cap = storageCapacity(b.type);
        std::string line = std::string(buildingDef(b.type).name);
        if (!b.constructed) {
            line += " (w budowie)";
            text(line, x + 26.0f, ly, 0.48f, dim, 0.93f);
        } else {
            text(line, x + 26.0f, ly, 0.48f, body, 0.93f);
            // Pasek zapełnienia (głębokość mniejsza niż tło okna, żeby był na wierzchu)
            const float frac = std::clamp(static_cast<float>(b.stock.total()) / cap, 0.0f, 1.0f);
            gui_->addOverlayRect(x + 159.0f, ly - 11.0f, 202.0f, 11.0f, {0.0f, 0.0f, 0.0f, 0.8f}, 0.933f);
            gui_->addOverlayRect(x + 160.0f, ly - 10.0f, 200.0f * frac, 9.0f,
                                 frac >= 1.0f ? Color{1.0f, 0.4f, 0.3f, 1.0f} : Color{0.45f, 0.75f, 1.0f, 1.0f}, 0.932f);
            text(std::to_string(b.stock.total()) + "/" + std::to_string(cap) + "  " + stockSummary(b.stock, 3), x + 372.0f, ly,
                 0.42f, body, 0.93f);
        }
        ly += 21.0f;
        ++listed;
    }
    ly += 10.0f;

    // Tragarze (liczbę zmienia się przyciskami w okienku "Tragarze" pod panelem)
    const TransportStats ts = w.transportStats(kPlayer);
    const FactionState& fs = w.factions[kPlayer];
    int roadCount = 0;
    for (const auto& r : w.roads) roadCount += (r.alive && r.faction == kPlayer) ? 1 : 0;
    text("TRANSPORT", x + 18.0f, ly, 0.55f, head, 0.93f);
    ly += 24.0f;
    text("Tragarze: " + std::to_string(ts.carriers) + " (cel " + std::to_string(ts.target) + (fs.carrierAuto ? ", auto" : ", ręcznie") +
             ")   zajęci: " + std::to_string(ts.busy) + "   wolni: " + std::to_string(ts.carriers - ts.busy) +
             "   odcinki dróg: " + std::to_string(roadCount),
         x + 26.0f, ly, 0.48f, body, 0.93f);
    ly += 22.0f;
    text("Towary czekające na odbiór: " + std::to_string(ts.goodsWaiting) + "   dostawy w kolejce: " +
             std::to_string(ts.ordersWaiting) + "   brak towaru na dostawy: " + std::to_string(ts.ordersUnfunded),
         x + 26.0f, ly, 0.48f, body, 0.93f);
    ly += 22.0f;
    if (ts.carriers < ts.target) {
        text("Brakuje wolnych osadników na tragarzy - zbuduj Dom", x + 26.0f, ly, 0.46f, warn, 0.93f);
        ly += 22.0f;
    } else if (ts.busy == ts.carriers && ts.goodsWaiting + ts.ordersWaiting > ts.carriers) {
        text("Wszyscy tragarze zajęci, a towary czekają - dodaj tragarzy (+)", x + 26.0f, ly, 0.46f, warn, 0.93f);
        ly += 22.0f;
    }

    // Warsztaty
    int producers = 0, blocked = 0, unconnected = 0;
    std::string blockedNames;
    for (const auto& b : w.buildings) {
        if (!b.alive || b.faction != kPlayer || !b.constructed || !producesGoods(b.type)) continue;
        ++producers;
        if (!w.flagConnected(b.flag)) {
            ++unconnected;
        } else if (b.outputBlocked) {
            ++blocked;
            if (blockedNames.size() < 60) blockedNames += std::string(blockedNames.empty() ? "" : ", ") + buildingDef(b.type).name;
        }
    }
    ly += 6.0f;
    text("WARSZTATY  " + std::to_string(producers), x + 18.0f, ly, 0.55f, head, 0.93f);
    ly += 24.0f;
    text("bez drogi do magazynu: " + std::to_string(unconnected) + "   wstrzymane (pełna flaga): " + std::to_string(blocked),
         x + 26.0f, ly, 0.48f, (unconnected + blocked) > 0 ? warn : body, 0.93f);
    ly += 22.0f;
    if (!blockedNames.empty()) text("Wstrzymane: " + blockedNames, x + 26.0f, ly, 0.44f, warn, 0.93f);

    text("I - zamknij      Tragarze (- / + / Auto):", x + 18.0f, y + ph - 18.0f, 0.46f, dim, 0.93f);
}

} // namespace settlers
