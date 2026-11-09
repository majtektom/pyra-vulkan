#include "feature/gui/gui_pixel_icon.h"

#include <algorithm>

#include "feature/gui/gui.h"

namespace engine3d {

int PixelIcon::width() const {
    size_t w = 0;
    for (const auto& r : rows) w = std::max(w, r.size());
    return static_cast<int>(w);
}

const std::map<char, std::array<float, 4>>& defaultPixelPalette() {
    static const std::map<char, std::array<float, 4>> palette = {
        {'R', {0.78f, 0.27f, 0.17f, 1.0f}},   // dachówka
        {'W', {0.92f, 0.86f, 0.72f, 1.0f}},   // tynk
        {'B', {0.6f, 0.4f, 0.22f, 1.0f}},     // drewno
        {'D', {0.36f, 0.23f, 0.12f, 1.0f}},   // ciemne drewno
        {'K', {0.12f, 0.12f, 0.15f, 1.0f}},   // czerń / okno / otwór
        {'G', {0.36f, 0.68f, 0.28f, 1.0f}},   // zieleń
        {'g', {0.2f, 0.45f, 0.18f, 1.0f}},    // ciemna zieleń
        {'Y', {0.93f, 0.8f, 0.38f, 1.0f}},    // słoma / złoto
        {'S', {0.72f, 0.72f, 0.74f, 1.0f}},   // kamień / stal
        {'s', {0.45f, 0.45f, 0.48f, 1.0f}},   // ciemny kamień
        {'w', {0.97f, 0.97f, 1.0f, 1.0f}},    // śnieg / biel
        {'T', {0.72f, 0.6f, 0.4f, 1.0f}},     // ubita ziemia
        {'F', {0.25f, 0.45f, 1.0f, 1.0f}},    // niebieski (flaga)
        {'r', {0.9f, 0.15f, 0.12f, 1.0f}},    // czerwony
        {'O', {0.95f, 0.55f, 0.15f, 1.0f}},   // pomarańczowy
    };
    return palette;
}

void drawPixelIcon(GuiMgr& gui, const PixelIcon& icon, float x, float y, float px, float alpha, float depth) {
    const auto& defaults = defaultPixelPalette();
    for (size_t r = 0; r < icon.rows.size(); ++r) {
        const std::string& row = icon.rows[r];
        for (size_t c = 0; c < row.size(); ++c) {
            const char ch = row[c];
            if (ch == '.' || ch == ' ') continue;
            std::array<float, 4> color;
            if (auto it = icon.palette.find(ch); it != icon.palette.end()) {
                color = it->second;
            } else if (auto jt = defaults.find(ch); jt != defaults.end()) {
                color = jt->second;
            } else {
                continue;
            }
            color[3] *= alpha;
            // +0.3: sąsiednie kwadraty lekko na siebie zachodzą, żeby nie było szczelin przy skalowaniu
            gui.addOverlayRect(x + static_cast<float>(c) * px, y + static_cast<float>(r) * px, px + 0.3f, px + 0.3f, color, depth);
        }
    }
}

} // namespace engine3d
