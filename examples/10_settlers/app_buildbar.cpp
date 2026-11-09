/**
 * app_buildbar.cpp - dolny pasek budowy.
 *
 * Sam pasek (układ, rysowanie, karta informacji, najechanie/kliknięcie) to komponent silnika
 * engine3d::GuiToolbar (engine/feature/gui/gui_toolbar.h). Tutaj gra dostarcza tylko:
 *  - grupy i przyciski: ikony pixel-art budynków (engine3d::PixelIcon - znak = piksel w kolorze palety),
 *  - co klatkę stan przycisków (stać nas? trwa stawianie?) i kartę z kosztem,
 *  - reakcję na kliknięcie (tryb stawiania budynku / budowy drogi).
 */
#include "settlers_app.h"

namespace settlers {

namespace {

// Ikony 12x12 (paleta domyślna silnika: R dachówka, W tynk, B/D drewno, K czerń, G/g zieleń, Y słoma,
// S/s kamień, w śnieg, T ziemia, F niebieski).
PixelIcon iconFor(BuildingType type) {
    switch (type) {
    case BuildingType::House:
        return {{"............", ".....RR.....", "....RRRR....", "...RRRRRR...", "..RRRRRRRR..", ".RRRRRRRRRR.",
                 "..WWWWWWWW..", "..WKKWWBBW..", "..WKKWWBBW..", "..WWWWWBBW..", "..WWWWWBBW..", ".gggggggggg."}};
    case BuildingType::Woodcutter:
        return {{"............", "..SSS.......", ".SSSSD......", "..SS..D.....", ".......D....", "........D...",
                 "....BBBBBBBB", "...YBBBBBBBB", "...BBBBBBBB.", "..BBBBBBBB..", ".YBBBBBBBB..", "............"}};
    case BuildingType::Forester:
        return {{".....gg.....", "....gGGg....", "...gGGGGg...", "..gGGGGGGg..", "...gGGGGg...", "..gGGGGGGg..",
                 ".gGGGGGGGGg.", "..gggggggg..", ".....DD.....", ".....DD..G..", ".....DD.GG..", ".BBBBBBBBBB."}};
    case BuildingType::Quarry:
        return {{"............", ".......SSSD.", "......S..D..", "........D...", ".......D....", "..sss.D.....",
                 ".sSSSs......", "sSSSSSs.ss..", "sSSSSSSsSSs.", "sSSSSSSSSSs.", ".sssssssss..", "............"}};
    case BuildingType::Mine:
        return {{"............", ".....ww.....", "....wSSs....", "...sSSSSs...", "..sSSSSSSs..", "..sSDDDDSs..",
                 ".sSSDKKDSSs.", ".sSSDKKDSSs.", "sSSSDKKDSSSs", "sSSSDKKDSSSs", "TTTTTTTTTTTT", "............"}};
    case BuildingType::Farm:
        return {{"............", ".....Y......", "....YYY.....", "...YYDYY....", "....YDY.....", "....YDY.....",
                 "............", "YYGYYGYYGYYG", "GGGGGGGGGGGG", "YYGYYGYYGYYG", "GGGGGGGGGGGG", "............"}};
    case BuildingType::Barracks:
        return {{"............", ".S........S.", "..S......S..", "...S....S...", "....S..S....", ".....SS.....",
                 ".....SS.....", "....S..S....", "..DS....SD..", ".DD......DD.", "D..........D", "............"}};
    case BuildingType::Tower:
        return {{".....F......", ".....FF.....", ".....D......", "...SsSsS....", "...SSSSS....", "...SSKSS....",
                 "...SSSSS....", "...SSKSS....", "...SSSSS....", "...SSSSS....", "..SSSDDSS...", ".gggggggggg."}};
    case BuildingType::Warehouse:
        return {{"............", "....RRRR....", "..RRRRRRRR..", ".RRRRRRRRRR.", "..BBBBBBBB..", "..BBDDDDBB..",
                 "..BBDBBDBB..", "..BBDBBDBB..", "YBBBDBBDBBY.", "YYBBBBBBBYY.", ".gggggggggg.", "............"}};
    default:
        return {};
    }
}

PixelIcon roadIcon() {
    return {{".........FF.", ".........FFF", ".........D..", "...T.....D..", "....TT...D..", ".....TTT.D..",
             "......TTT...", ".......TTTT.", "........TTT.", "TT.......TT.", ".TTT........", "..TTTTT....."}};
}

// Kolory "żetonów" kosztu na karcie (drewno, kamień, żywność).
const std::array<float, 4> kGoodSwatch[] = {
    {0, 0, 0, 0}, {0.75f, 0.5f, 0.25f, 1.0f}, {0.72f, 0.72f, 0.75f, 1.0f}, {0.95f, 0.85f, 0.35f, 1.0f}};
const char* const kGoodGenitive[] = {"", "drewna", "kamienia", "żywności"};

} // namespace

// Grupy paska: Mieszkańcy, Surowce, Logistyka, Wojsko. Identyfikator = indeks w kBuildMenu (klawisz 1-9).
void SettlersApp::setupBuildBar() {
    auto building = [](int index) {
        const BuildingType type = kBuildMenu[index];
        GuiToolbar::Item it;
        it.id = index;
        it.icon = iconFor(type);
        it.hotkey = std::to_string(index + 1);
        it.card.title = buildingDef(type).name;
        it.card.lines = {{buildingDef(type).description}};
        it.card.chipsLabel = "Koszt:";
        it.card.footer = "Klawisz " + it.hotkey + "   |   LPM: postaw   R: obróć   PPM: anuluj";
        return it;
    };
    GuiToolbar::Item road;
    road.id = kBarRoad;
    road.icon = roadIcon();
    road.hotkey = "T";
    road.card.title = "Droga";
    road.card.lines = {{"Łączy budynki z magazynami - tragarze chodzą tylko po drogach, a po drodze idzie się szybciej."}};
    road.card.footer = "Klawisz T   |   LPM na fladze lub budynku, potem LPM w miejscu docelowym";

    buildBar_.setGroups({
        {"Mieszkańcy", {building(0)}},
        {"Surowce", {building(1), building(2), building(3), building(4), building(5)}},
        {"Logistyka", {building(8), road}},
        {"Wojsko", {building(6), building(7)}},
    });
}

// Stan przycisków i treść kart zależą od surowców w magazynach i bieżącego trybu (stawianie / droga).
void SettlersApp::refreshBuildBar() {
    buildBar_.setVisible(buildBarVisible());
    buildBar_.layout(screenW_, screenH_);
    if (!world_) return;
    const Resources& res = world_->factions[kPlayer].res;
    for (int i = 0; i < kBuildMenuSize; ++i) {
        GuiToolbar::Item* it = buildBar_.item(i);
        if (!it) continue;
        const Resources& cost = buildingDef(kBuildMenu[i]).cost;
        it->enabled = res.canAfford(cost);
        it->active = placing_ && placeType_ == kBuildMenu[i];
        it->card.chips.clear();
        for (int t = 1; t <= kGoodTypeCount; ++t) {
            const auto type = static_cast<CarryType>(t);
            const int need = resourceOf(cost, type);
            if (need > 0) {
                it->card.chips.push_back({kGoodSwatch[t], std::to_string(need) + " " + kGoodGenitive[t], resourceOf(res, type) >= need});
            }
        }
        it->card.warning = it->enabled ? "" : "Brakuje: " + missingText(cost, res);
    }
    if (GuiToolbar::Item* road = buildBar_.item(kBarRoad)) road->active = roadMode_;
}

bool SettlersApp::mouseOverBuildBar(int mx, int my) const {
    return buildBarVisible() && buildBar_.contains(static_cast<float>(mx), static_cast<float>(my));
}

bool SettlersApp::buildBarSlotCenter(int id, float& x, float& y) const {
    return buildBar_.itemCenter(id, x, y);
}

// LPM na przycisku: tryb stawiania budynku / budowy drogi (drugi klik - anulowanie). Kliknięcie w tło
// paska też jest "zjadane", żeby nie zaznaczało niczego w świecie pod paskiem.
bool SettlersApp::handleBuildBar(Engine& e, const InputSnapshot& in) {
    if (!mouseOverBuildBar(in.mouseX, in.mouseY) || !e.mousePressed(0)) return false;
    const int id = buildBar_.update(in.mouseX, in.mouseY, true);
    if (id == kBarRoad) {
        if (roadMode_) {
            cancelRoad();
        } else if (selectedFlag_ >= 0) {
            beginRoad(selectedFlag_);
        } else if (selectedBuilding_ >= 0 && world_->buildings[selectedBuilding_].faction == kPlayer) {
            beginRoad(world_->buildings[selectedBuilding_].flag);
        } else {
            beginRoad();
        }
        sfx("click", 0.6f);
    } else if (id >= 0 && id < kBuildMenuSize) {
        if (placing_ && placeType_ == kBuildMenu[id]) {
            cancelPlacement();
        } else {
            beginPlacement(kBuildMenu[id]);
        }
    }
    dragging_ = false;
    return true;
}

void SettlersApp::drawBuildBar() {
    if (buildBarVisible()) buildBar_.draw(*gui_);
}

} // namespace settlers
