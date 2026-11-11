#pragma once
/**
 * transport.h - przewóz towarów w Osadnikach (klasa Transport, implementacja: transport.cpp).
 *
 * Trzy części:
 *   - trasy po drogach: odległości Dijkstry od flagi (z pamięcią do zmiany sieci dróg) i trasa flaga -> flaga,
 *   - zlecenia: opłacenie dostawy towarem z magazynu albo z flagi (fundOrder / unfundOrder),
 *   - tragarze: nabór do docelowej liczby, przydział zleceń i automat stanów tragarza.
 * Dane (flagi, drogi, zlecenia, jednostki) należą do World - Transport czyta je i zmienia przez referencję
 * (jest "friend" World). Flagi, drogi i magazyny jako takie (budowa, usuwanie, zapasy) są w World (world_roads.cpp).
 */
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "core/math.h"
#include "settlers_defs.h"

namespace settlers {

using engine3d::Vec3;
class World;
struct Unit;
struct Flag;
struct TransportOrder;

class Transport {
public:
    explicit Transport(World& world) : w_(world) {}

    // --- Trasy po drogach ---
    // Odległości po drogach od flagi - z pamięci podręcznej (Dijkstra liczona raz na flagę startową, dopóki
    // sieć dróg się nie zmieni). Wcześniej przydział zleceń liczył Dijkstrę dla każdego tragarza i zlecenia.
    struct NetDistances {
        std::vector<float> dist;     // odległość od flagi startowej (>= 1e8 = brak połączenia)
        std::vector<int> prev;       // odcinek drogi, którym doszliśmy do flagi (do odtworzenia trasy)
        uint64_t version = 0;
    };
    const NetDistances& networkFrom(int fromFlag) const;
    // Dijkstra po sieci dróg od flagi: odległości do wszystkich flag i odcinek, którym do nich doszliśmy.
    void networkDistances(int fromFlag, std::vector<float>& dist, std::vector<int>& prevRoad) const;
    // Trasa po drogach od flagi do flagi (punkty dróg w kolejności marszu); false = brak połączenia.
    bool networkPath(int fromFlag, int toFlag, std::vector<Vec3>& out) const;
    // Zmiana flag lub dróg - pamięć odległości do przeliczenia.
    void roadGraphChanged() { ++roadGraphVersion_; }

    // --- Zlecenia ---
    // Opłaca zlecenie towarem z najbliższego magazynu albo leżącym na fladze (false = brak towaru).
    bool fundOrder(TransportOrder& o);
    // Cofa opłatę zlecenia: towar wraca do magazynu / zwalnia rezerwację na fladze.
    void unfundOrder(TransportOrder& o);

    // --- Tragarze ---
    // Nabór wolnych osadników do docelowej liczby tragarzy albo zwalnianie nadmiaru.
    void manageCarriers(int faction, int limit);
    // Automat tragarza (jeden krok symulacji).
    void updateCarrier(Unit& u, int id, float dt);
    // Tragarz przerywa zlecenie: zwalnia rezerwacje, niesiony towar zostawia na fladze.
    void releaseCarrierJob(Unit& u);

    // Przydział zleceń (odbiory i dostawy) wolnym tragarzom.
    void assignTransportJobs(int faction);

private:
    // Stany automatu tragarza: czeka / idzie bez ładunku / niesie towar.
    void carrierWait(Unit& u, float dt);
    void carrierGoToRoad(Unit& u, float dt);
    void carrierWalk(Unit& u, float dt);
    void carrierPickUpFromFlag(Unit& u);
    void carrierPickUpOrder(Unit& u);
    void finishCarrierJob(Unit& u);
    // Odkłada niesiony towar na fladze (gdy cel zniknął) - inny tragarz zaniesie go do magazynu.
    void dropCarriedGoods(Unit& u, int flag);
    // Zdejmuje z flagi do maxCount sztuk towaru danego typu; zwraca, ile zdjął.
    static int takeGoodsFromFlag(Flag& flag, CarryType type, int maxCount);

    World& w_;
    mutable std::unordered_map<int, NetDistances> netCache_;   // flaga startowa -> odległości po drogach
    uint64_t roadGraphVersion_ = 1;
};

} // namespace settlers
