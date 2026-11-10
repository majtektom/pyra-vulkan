#pragma once

#include <array>
#include <string>
#include <vector>

#include "feature/gui/gui_pixel_icon.h"

namespace engine3d {

class GuiMgr;

/**
 * @brief Karta informacji (tooltip): tytuł z ikoną, opis, "żetony" (np. koszt), ostrzeżenie, stopka.
 *
 * Rysowana na wierzchu wszystkiego (także okien GUI). Rozmiar dopasowuje się do treści.
 *
 *   GuiCard card;
 *   card.title = "Dom";
 *   card.lines = {{"Zwiększa limit populacji o 6."}};
 *   card.chipsLabel = "Koszt:";
 *   card.chips = {{woodColor, "20 drewna", true}, {stoneColor, "10 kamienia", false}};   // false = na czerwono
 *   card.warning = "Brakuje: 5 kamienia";
 *   card.footer = "Klawisz 1   |   LPM: postaw";
 *   card.draw(gui, anchorX, bottomY);   // wyśrodkowana nad punktem (anchorX, bottomY)
 */
struct GuiCard {
    struct Line {
        std::string text;
        std::array<float, 4> color{0.9f, 0.9f, 0.88f, 1.0f};
    };
    struct Chip {
        std::array<float, 4> swatch{1, 1, 1, 1};   // kolorowy kwadracik przed tekstem
        std::string text;
        bool ok = true;                             // false = tekst na czerwono
    };

    std::string title;
    const PixelIcon* icon = nullptr;
    std::vector<Line> lines;
    std::string chipsLabel;
    std::vector<Chip> chips;
    std::string warning;
    std::string footer;

    // Rozmiar karty (zależny od treści i szerokości napisów).
    void measure(const GuiMgr& gui, float& w, float& h) const;
    // Rysuje kartę wyśrodkowaną poziomo nad punktem (anchorX, bottomY), przyciętą do ekranu.
    void draw(GuiMgr& gui, float anchorX, float bottomY) const;
};

/**
 * @brief Pasek narzędzi z ikonami (np. dolny pasek budowy w grze strategicznej).
 *
 * Przyciski są pogrupowane (nazwa grupy nad przyciskami, separator między grupami). Każdy ma ikonę
 * pixel-art, napis skrótu w rogu i kartę informacji pokazywaną po najechaniu kursorem.
 * Pasek jest rysowany w trybie immediate (overlay GuiMgr) - nie jest oknem GUI.
 *
 * Użycie (co klatkę):
 *   toolbar.layout(screenW, screenH);
 *   toolbar.item(ID_DOM)->enabled = staćNas;           // stan przycisków
 *   const int clicked = toolbar.update(mouseX, mouseY, lewyKlik);
 *   if (clicked == ID_DOM) ...;
 *   toolbar.draw(gui);
 * Kliknięcie w pasek nie powinno trafiać w świat gry: sprawdzaj toolbar.contains(mx, my).
 */
class GuiToolbar {
public:
    struct Item {
        int id = 0;                    // identyfikator nadany przez grę (>= 0)
        PixelIcon icon;
        std::string hotkey;            // napis w prawym dolnym rogu ("1", "T"...)
        GuiCard card;                  // karta po najechaniu (ikona karty = ikona przycisku)
        bool enabled = true;           // false: czerwonawy przycisk, przygaszona ikona (np. brak surowców)
        bool active = false;           // true: złota ramka (np. trwa stawianie tego budynku)
    };
    struct Group {
        std::string title;
        std::vector<Item> items;
    };
    struct Style {
        float slot = 58.0f;            // bok przycisku
        float gap = 6.0f;              // odstęp w grupie
        float groupGap = 24.0f;        // odstęp między grupami
        float pad = 12.0f;             // margines poziomy
        float labelH = 18.0f;          // wiersz z nazwami grup
        float bottomMargin = 8.0f;     // odstęp od dolnej krawędzi ekranu
        float areaLeft = 0.0f;         // pasek wyśrodkowany w obszarze [areaLeft, szerokość - areaRight]
        float areaRight = 0.0f;        //   (np. żeby nie zachodził na minimapę)
        float iconPixel = 4.0f;        // rozmiar "piksela" ikony
        std::array<float, 4> background{0.1f, 0.075f, 0.05f, 0.93f};
        std::array<float, 4> border{0.62f, 0.5f, 0.27f, 0.95f};
        std::array<float, 4> groupTitle{0.85f, 0.75f, 0.55f, 1.0f};
        std::array<float, 4> hotkeyText{1.0f, 0.88f, 0.55f, 1.0f};
    };
    struct Slot {
        float x = 0, y = 0, size = 0;
        int id = 0;
    };
    static constexpr int kNone = -1;

    void setGroups(std::vector<Group> groups);
    void setStyle(const Style& style) { style_ = style; }
    [[nodiscard]] const Style& style() const { return style_; }
    void setVisible(bool v) { visible_ = v; }
    [[nodiscard]] bool visible() const { return visible_; }

    // Przycisk o danym id (nullptr, gdy nie ma) - do zmiany stanu enabled/active/karty.
    Item* item(int id);
    [[nodiscard]] const Item* item(int id) const;

    // Układ: pasek wyśrodkowany przy dolnej krawędzi ekranu. Wołać co klatkę (zmiana rozdzielczości).
    void layout(float screenW, float screenH);
    // Najechanie i kliknięcie: zwraca id klikniętego przycisku albo kNone.
    int update(int mouseX, int mouseY, bool leftPressed);
    void draw(GuiMgr& gui) const;

    [[nodiscard]] bool contains(float x, float y) const;
    [[nodiscard]] int hoveredId() const { return hovered_; }
    [[nodiscard]] int itemAt(float x, float y) const;
    bool itemCenter(int id, float& x, float& y) const;
    // Prostokąt paska (np. żeby postawić nad nim inny panel).
    [[nodiscard]] float x() const { return x_; }
    [[nodiscard]] float y() const { return y_; }
    [[nodiscard]] float width() const { return w_; }
    [[nodiscard]] float height() const { return h_; }

private:
    std::vector<Group> groups_;
    std::vector<Slot> slots_;
    Style style_;
    bool visible_ = true;
    int hovered_ = kNone;
    float x_ = 0, y_ = 0, w_ = 0, h_ = 0, screenW_ = 0;
};

} // namespace engine3d
