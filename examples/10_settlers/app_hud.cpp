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

// Pasek (życie, postęp budowy): czarne tło + wypełnienie proporcjonalne do frac (0..1).
void SettlersApp::bar(float x, float y, float w, float h, float frac, const Color& fill) {
    gui_->addOverlayRect(x - 1, y - 1, w + 2, h + 2, {0.0f, 0.0f, 0.0f, 0.75f}, 0.951f);
    gui_->addOverlayRect(x, y, w * std::clamp(frac, 0.0f, 1.0f), h, fill, 0.95f);
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

    if (!hoverMissing_.empty() && mode_ == Mode::Game) {
        const float x = static_cast<float>(in.mouseX) + 18.0f, y = static_cast<float>(in.mouseY) + 6.0f;
        const float tw = engine_.measureText(hoverMissing_, 0.52f);
        gui_->addOverlayRect(x - 6.0f, y - 18.0f, tw + 12.0f, 26.0f, {0.1f, 0.05f, 0.04f, 0.92f}, 0.05f);
        text(hoverMissing_, x, y, 0.52f, {1.0f, 0.6f, 0.45f, 1.0f}, 0.04f);
    }

    if (showHelp_) drawHelp();
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
    item("Drewno:", std::to_string(fs.res.wood), {0.95f, 0.75f, 0.45f, 1.0f});
    item("Kamień:", std::to_string(fs.res.stone), {0.85f, 0.85f, 0.9f, 1.0f});
    item("Żywność:", std::to_string(fs.res.food), {0.95f, 0.9f, 0.4f, 1.0f});
    item("Ludność:", std::to_string(fs.population) + "/" + std::to_string(fs.popCap),
         fs.population >= fs.popCap ? Color{1.0f, 0.5f, 0.4f, 1.0f} : Color{0.8f, 1.0f, 0.8f, 1.0f});
    item("Wojsko:", std::to_string(soldiers), {0.7f, 0.85f, 1.0f, 1.0f});

    const int t = static_cast<int>(w.gameTime);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", t / 60, t % 60);
    static const char* diffNames[] = {"Łatwy", "Normalny", "Trudny"};
    std::string right = std::string("Poziom: ") + diffNames[static_cast<int>(w.difficulty)] + "   Czas: " + buf;
    if (gameSpeed_ != 1) right += " (x" + std::to_string(gameSpeed_) + ")";
    right += "   FPS: " + std::to_string(static_cast<int>(e.getCurrentFPS())) + "   F1: pomoc";
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
        if (!b.alive || b.faction != kPlayer || (!b.constructed && !b.unreachable)) continue;
        const BuildingDef& def = buildingDef(b.type);
        std::string label;
        Color color{1.0f, 0.55f, 0.3f, 1.0f};
        if (b.unreachable) {
            label = "! brak dojścia";
        } else if (b.lacksResources) {
            label = b.type == BuildingType::Woodcutter ? "! brak drzew" : "! brak kamienia";
        } else if (b.repairBlocked) {
            label = "! brak drewna na naprawę";
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
        if (!u.alive) continue;
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
    selPanelY_ = static_cast<int>(screenH_) - panelH - 12;
    if (!hasSelectionPanel()) return;
    const float px = static_cast<float>(selPanelX_), py = static_cast<float>(selPanelY_);
    gui_->addOverlayRect(px - 3, py - 3, selPanelW_ + 6.0f, panelH + 6.0f, {0.55f, 0.45f, 0.25f, 0.95f}, 0.97f);
    gui_->addOverlayRect(px, py, static_cast<float>(selPanelW_), static_cast<float>(panelH), {0.1f, 0.085f, 0.06f, 0.92f}, 0.965f);

    const Color title{1.0f, 0.92f, 0.65f, 1.0f};
    const Color body{0.9f, 0.9f, 0.88f, 1.0f};

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
            status = "W budowie: " + std::to_string(static_cast<int>(b.progress * 100.0f)) + "%";
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
            status = "Kolejka: " + std::to_string(b.trainQueue.size());
            if (!b.trainQueue.empty()) {
                const float total = unitDef(b.trainQueue.front()).trainTime;
                status += std::string("   Szkoli: ") + unitDef(b.trainQueue.front()).name + " " +
                          std::to_string(static_cast<int>(std::min(1.0f, b.trainTimer / total) * 100.0f)) + "%";
            }
        } else if (def.popBonus > 0) {
            status = "Mieszkańcy: +" + std::to_string(def.popBonus);
        } else if (def.attackRange > 0.0f) {
            status = "Zasięg strzału: " + std::to_string(static_cast<int>(def.attackRange));
        }
        if (b.constructed && b.hp < def.maxHp * 0.97f) {
            const int rep = w.repairersOf(selectedBuilding_);
            if (b.repairBlocked) status += "   Naprawa: brak drewna";
            else if (rep > 0) status += "   Naprawia: " + std::to_string(rep);
            else if (w.gameTime - b.lastDamagedTime < 8.0f) status += "   Naprawa po walce";
            else status += "   Czeka na naprawę";
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
            std::string job = "Osadnicy pracują samodzielnie - wznoszą budowle i obsługują warsztaty.";
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
    const float w = std::min(screenW_ - 40.0f, 1100.0f), h = 450.0f;
    const float x = screenW_ * 0.5f - w * 0.5f, y = std::max(kTopBarH + 10.0f, std::min(kTopBarH + 180.0f, screenH_ - h - 20.0f));
    gui_->addOverlayRect(x - 3, y - 3, w + 6, h + 6, {0.55f, 0.45f, 0.25f, 0.95f}, 0.94f);
    gui_->addOverlayRect(x, y, w, h, {0.08f, 0.07f, 0.05f, 0.94f}, 0.935f);
    const Color headerC{1.0f, 0.9f, 0.6f, 1.0f};
    const size_t total = std::size(kControls);
    const size_t split = 17;   // początek sekcji BUDOWA - druga kolumna
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
                "Domy zwiększają limit ludności. Wieże poszerzają terytorium.",
                "Osadnicy naprawiają budynki (1 drewno = 10% PŻ).",
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

} // namespace settlers
