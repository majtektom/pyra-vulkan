/**
 * 10_settlers - strategia ekonomiczno-wojenna w stylu klasycznych "Osadników".
 *
 * Gracz rozbudowuje osadę (drwal, kamieniarz, farma, domy, koszary, wieże),
 * osadnicy samodzielnie budują i pracują, a wojsko walczy z komputerowym przeciwnikiem.
 * Celem jest zniszczenie wrogiego zamku.
 *
 * MAPA PLIKÓW (od czego zacząć czytanie)
 * --------------------------------------
 *   settlers_defs.h/.cpp      - typy i statystyki: budynki, jednostki, surowce (balans gry)
 *   settlers_assets.h/.cpp    - proceduralne modele 3D (MeshBuilder) i materiały
 *   settlers_world.h          - WORLD: cała symulacja gry (opis w nagłówku)
 *     world_create.cpp        -   generowanie terenu, surowców i baz
 *     world_entities.cpp      -   tworzenie/usuwanie jednostek, budynków i encji 3D
 *     world_queries.cpp       -   zapytania: picking, gdzie można budować, wyszukiwanie celów
 *     world_movement.cpp      -   ścieżki, ruch, rozpychanie jednostek, wykrywanie zawieszeń
 *     world_update.cpp        -   krok symulacji: frakcje, przydział prac, budynki, efekty, wizualizacja
 *     world_settlers.cpp      -   zachowanie osadników (automat stanów: budowa, praca, transport)
 *     world_combat.cpp        -   walka: żołnierze, strzały, obrażenia, rozkazy wojskowe
 *     world_ai.cpp            -   komputerowy przeciwnik
 *   settlers_app.h            - SETTLERSAPP: silnik, sterowanie, interfejs, dźwięk (opis w nagłówku)
 *     app_*.cpp               -   poszczególne części aplikacji
 *
 * Elementy silnika użyte w przykładzie:
 *  - RtsCameraController (kamera strategiczna), screenPointToRay / worldToScreen (picking),
 *  - Terrain::raycast + Terrain::setHeightmap (proceduralny teren z warstwami tekstur),
 *  - NavGrid (A*, obszary dostępności), MeshBuilder (proceduralne modele low-poly),
 *  - GuiMgr::addOverlayRect (HUD, paski życia, minimapa, ramka zaznaczenia),
 *  - AudioSystem (efekty 3D, muzyka), SkydomeRenderer, OceanRenderer, cienie, PBR.
 */
#include "settlers_app.h"

int main() {
    settlers::SettlersApp app;
    if (!app.initialize()) {
        return 1;
    }
    app.run();
    return 0;
}
