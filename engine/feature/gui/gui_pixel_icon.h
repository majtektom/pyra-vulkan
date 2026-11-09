#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

namespace engine3d {

class GuiMgr;

/**
 * @brief Ikona pixel-art zapisana tekstem - każdy znak to jeden "piksel" w kolorze z palety.
 *
 * GUI silnika rysuje prostokąty, więc ikonę składamy z małych kwadratów (overlay HUD).
 * Ikonę rysuje się i edytuje bez plików graficznych:
 *
 *   PixelIcon house{{
 *       "....RR....",
 *       "...RRRR...",
 *       "..WWWWWW..",
 *       "..WKKWBW..",
 *   }};                                   // kropka = przezroczysty piksel
 *   drawPixelIcon(gui, house, x, y, 4.0f); // piksel 4x4 punkty ekranu
 *
 * Domyślna paleta (defaultPixelPalette) ma kolory typowe dla gier (dachówka, drewno, kamień, zieleń...);
 * ikona może mieć też własną paletę (pole palette - nadpisuje kolory domyślne).
 */
struct PixelIcon {
    std::vector<std::string> rows;
    std::map<char, std::array<float, 4>> palette;   // opcjonalne kolory własne

    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const { return static_cast<int>(rows.size()); }
    [[nodiscard]] bool empty() const { return rows.empty(); }
};

// Paleta domyślna:
//  R dachówka   W tynk      B drewno    D ciemne drewno  K czerń/okno   G zieleń   g ciemna zieleń
//  Y słoma/złoto S kamień/stal s ciemny kamień  w śnieg/biel  T ubita ziemia  F niebieski  r czerwony  O pomarańcz
const std::map<char, std::array<float, 4>>& defaultPixelPalette();

// Rysuje ikonę w punkcie (x, y) = lewy górny róg; px = rozmiar "piksela" na ekranie; alpha < 1 przygasza.
void drawPixelIcon(GuiMgr& gui, const PixelIcon& icon, float x, float y, float px, float alpha = 1.0f, float depth = 0.938f);

} // namespace engine3d
