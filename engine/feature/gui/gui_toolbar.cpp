#include "feature/gui/gui_toolbar.h"

#include <algorithm>
#include <cmath>

#include "feature/gui/gui.h"

namespace engine3d {

namespace {
// Głębokości (mniejsza = bliżej): pasek pod oknami GUI razem z HUD, karta na wierzchu wszystkiego.
constexpr float kBarBorderZ = 0.948f, kBarBgZ = 0.946f, kSeparatorZ = 0.944f, kSlotZ = 0.942f, kSlotFrameZ = 0.94f,
                kIconZ = 0.938f, kHotkeyBgZ = 0.936f, kTextZ = 0.93f;
constexpr float kCardBorderZ = 0.07f, kCardBgZ = 0.06f, kCardHeadZ = 0.055f, kCardIconZ = 0.05f, kCardSwatchZ = 0.045f,
                kCardTextZ = 0.04f;
const std::array<float, 4> kGold{1.0f, 0.88f, 0.55f, 1.0f};
}

// =====================================================================================
// GuiCard
// =====================================================================================

void GuiCard::measure(const GuiMgr& gui, float& w, float& h) const {
    w = 300.0f;
    w = std::max(w, gui.measureText(title, 0.62f) + 60.0f);
    for (const auto& l : lines) w = std::max(w, gui.measureText(l.text, 0.46f) + 28.0f);
    w = std::max(w, gui.measureText(footer, 0.42f) + 28.0f);
    w = std::max(w, gui.measureText(warning, 0.44f) + 28.0f);
    float chipsW = 28.0f + gui.measureText(chipsLabel, 0.46f) + 12.0f;
    for (const auto& c : chips) chipsW += 15.0f + gui.measureText(c.text, 0.46f) + 16.0f;
    if (!chips.empty()) w = std::max(w, chipsW);
    h = 30.0f + 12.0f + static_cast<float>(lines.size()) * 22.0f;
    if (!chips.empty()) h += 26.0f;
    if (!warning.empty()) h += 20.0f;
    if (!footer.empty()) h += 24.0f;
    h += 4.0f;
}

void GuiCard::draw(GuiMgr& gui, float anchorX, float bottomY) const {
    float w, h;
    measure(gui, w, h);
    const float sw = gui.screenWidth();
    const float x = std::clamp(anchorX - w * 0.5f, 8.0f, std::max(8.0f, sw - w - 8.0f));
    const float y = bottomY - h;
    gui.addOverlayRect(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, {0.62f, 0.5f, 0.27f, 0.95f}, kCardBorderZ);
    gui.addOverlayRect(x, y, w, h, {0.09f, 0.07f, 0.05f, 0.96f}, kCardBgZ);
    gui.addOverlayRect(x, y, w, 30.0f, {0.2f, 0.15f, 0.09f, 0.96f}, kCardHeadZ);
    float tx = x + 12.0f;
    if (icon && !icon->empty()) {
        const float px = 24.0f / static_cast<float>(std::max(icon->width(), icon->height()));
        drawPixelIcon(gui, *icon, x + 8.0f, y + 3.0f, px, 1.0f, kCardIconZ);
        tx = x + 40.0f;
    }
    gui.addOverlayText(title, tx, y + 22.0f, 0.62f, kGold, kCardTextZ);

    float ly = y + 30.0f + 22.0f;
    for (const auto& l : lines) {
        gui.addOverlayText(l.text, x + 14.0f, ly, 0.46f, l.color, kCardTextZ);
        ly += 22.0f;
    }
    if (!chips.empty()) {
        ly += 4.0f;
        float cx = x + 14.0f;
        if (!chipsLabel.empty()) {
            gui.addOverlayText(chipsLabel, cx, ly, 0.46f, {0.8f, 0.75f, 0.6f, 1.0f}, kCardTextZ);
            cx += gui.measureText(chipsLabel, 0.46f) + 12.0f;
        }
        for (const auto& c : chips) {
            gui.addOverlayRect(cx, ly - 11.0f, 11.0f, 11.0f, c.swatch, kCardSwatchZ);
            cx += 15.0f;
            gui.addOverlayText(c.text, cx, ly, 0.46f, c.ok ? std::array<float, 4>{0.9f, 0.9f, 0.88f, 1.0f}
                                                            : std::array<float, 4>{1.0f, 0.45f, 0.35f, 1.0f},
                               kCardTextZ);
            cx += gui.measureText(c.text, 0.46f) + 16.0f;
        }
        ly += 22.0f;
    }
    if (!warning.empty()) {
        gui.addOverlayText(warning, x + 14.0f, ly, 0.44f, {1.0f, 0.5f, 0.4f, 1.0f}, kCardTextZ);
        ly += 20.0f;
    }
    if (!footer.empty()) gui.addOverlayText(footer, x + 14.0f, y + h - 10.0f, 0.42f, {0.7f, 0.7f, 0.66f, 1.0f}, kCardTextZ);
}

// =====================================================================================
// GuiToolbar
// =====================================================================================

void GuiToolbar::setGroups(std::vector<Group> groups) {
    groups_ = std::move(groups);
    slots_.clear();
    hovered_ = kNone;
}

GuiToolbar::Item* GuiToolbar::item(int id) {
    for (auto& g : groups_) {
        for (auto& it : g.items) {
            if (it.id == id) return &it;
        }
    }
    return nullptr;
}

const GuiToolbar::Item* GuiToolbar::item(int id) const {
    return const_cast<GuiToolbar*>(this)->item(id);
}

// Szerokość = przyciski + odstępy w grupach + odstępy między grupami + marginesy.
void GuiToolbar::layout(float screenW, float screenH) {
    screenW_ = screenW;
    const Style& s = style_;
    w_ = s.pad * 2.0f;
    for (size_t g = 0; g < groups_.size(); ++g) {
        const float n = static_cast<float>(groups_[g].items.size());
        if (n <= 0.0f) continue;
        w_ += n * s.slot + (n - 1.0f) * s.gap + (g + 1 < groups_.size() ? s.groupGap : 0.0f);
    }
    h_ = 8.0f + s.labelH + s.slot + 10.0f;
    const float areaW = screenW - s.areaLeft - s.areaRight;
    x_ = std::floor(s.areaLeft + std::max(0.0f, areaW - w_) * 0.5f);
    y_ = screenH - h_ - s.bottomMargin;
    slots_.clear();
    float sx = x_ + s.pad;
    const float sy = y_ + 8.0f + s.labelH;
    for (const auto& g : groups_) {
        for (const auto& it : g.items) {
            slots_.push_back({sx, sy, s.slot, it.id});
            sx += s.slot + s.gap;
        }
        sx += s.groupGap - s.gap;
    }
}

bool GuiToolbar::contains(float x, float y) const {
    return visible_ && x >= x_ && x <= x_ + w_ && y >= y_ && y <= y_ + h_;
}

int GuiToolbar::itemAt(float x, float y) const {
    if (!visible_) return kNone;
    for (const auto& s : slots_) {
        if (x >= s.x && x <= s.x + s.size && y >= s.y && y <= s.y + s.size) return s.id;
    }
    return kNone;
}

bool GuiToolbar::itemCenter(int id, float& x, float& y) const {
    for (const auto& s : slots_) {
        if (s.id != id) continue;
        x = s.x + s.size * 0.5f;
        y = s.y + s.size * 0.5f;
        return true;
    }
    return false;
}

int GuiToolbar::update(int mouseX, int mouseY, bool leftPressed) {
    hovered_ = itemAt(static_cast<float>(mouseX), static_cast<float>(mouseY));
    return leftPressed ? hovered_ : kNone;
}

// Tło z ramką, nazwy grup i separatory, przyciski (tło wg stanu, ramka, ikona, skrót), karta najechanego przycisku.
void GuiToolbar::draw(GuiMgr& gui) const {
    if (!visible_ || slots_.empty()) return;
    const Style& s = style_;
    gui.addOverlayRect(x_ - 3.0f, y_ - 3.0f, w_ + 6.0f, h_ + 6.0f, s.border, kBarBorderZ);
    gui.addOverlayRect(x_, y_, w_, h_, s.background, kBarBgZ);

    size_t first = 0;
    for (const auto& g : groups_) {
        if (g.items.empty()) continue;
        const Slot& a = slots_[first];
        const Slot& b = slots_[first + g.items.size() - 1];
        gui.addOverlayTextCentered(g.title, (a.x + b.x + b.size) * 0.5f, y_ + 8.0f + s.labelH - 4.0f, 0.42f, s.groupTitle, kTextZ);
        if (first > 0) {
            gui.addOverlayRect(a.x - s.groupGap * 0.5f - 1.0f, y_ + 10.0f, 2.0f, h_ - 20.0f, {0.45f, 0.36f, 0.2f, 0.9f}, kSeparatorZ);
        }
        first += g.items.size();
    }

    const Slot* hoveredSlot = nullptr;
    for (const auto& slot : slots_) {
        const Item* it = item(slot.id);
        if (!it) continue;
        const bool hovered = hovered_ == slot.id;
        if (hovered) hoveredSlot = &slot;
        std::array<float, 4> bg = it->enabled ? std::array<float, 4>{0.2f, 0.16f, 0.11f, 1.0f} : std::array<float, 4>{0.3f, 0.12f, 0.1f, 1.0f};
        if (hovered) bg = it->enabled ? std::array<float, 4>{0.33f, 0.27f, 0.17f, 1.0f} : std::array<float, 4>{0.42f, 0.16f, 0.13f, 1.0f};
        const std::array<float, 4> frame = it->active ? std::array<float, 4>{1.0f, 0.82f, 0.3f, 1.0f}
                                          : hovered  ? std::array<float, 4>{0.85f, 0.7f, 0.4f, 1.0f}
                                                     : std::array<float, 4>{0.42f, 0.34f, 0.2f, 1.0f};
        gui.addOverlayRect(slot.x, slot.y, slot.size, slot.size, bg, kSlotZ);
        gui.addOverlayFrame(slot.x, slot.y, slot.size, slot.size, it->active ? 3.0f : 1.5f, frame, kSlotFrameZ);
        if (!it->icon.empty()) {
            const float iw = static_cast<float>(it->icon.width()) * s.iconPixel;
            const float ih = static_cast<float>(it->icon.height()) * s.iconPixel;
            drawPixelIcon(gui, it->icon, slot.x + (slot.size - iw) * 0.5f, slot.y + (slot.size - ih) * 0.5f - 2.0f, s.iconPixel,
                          it->enabled ? 1.0f : 0.45f, kIconZ);
        }
        if (!it->hotkey.empty()) {
            gui.addOverlayRect(slot.x + slot.size - 15.0f, slot.y + slot.size - 15.0f, 14.0f, 14.0f, {0.05f, 0.04f, 0.03f, 0.85f}, kHotkeyBgZ);
            gui.addOverlayTextCentered(it->hotkey, slot.x + slot.size - 8.0f, slot.y + slot.size - 3.0f, 0.38f, s.hotkeyText, kTextZ);
        }
    }

    if (hoveredSlot) {
        const Item* it = item(hoveredSlot->id);
        GuiCard card = it->card;
        if (!card.icon) card.icon = &it->icon;
        card.draw(gui, hoveredSlot->x + hoveredSlot->size * 0.5f, y_ - 10.0f);
    }
}

} // namespace engine3d
